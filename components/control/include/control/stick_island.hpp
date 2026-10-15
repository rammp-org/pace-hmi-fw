#pragma once
// The control island (app-main-shrink §3). Moved from main.cpp's app_main (the Read ADC
// task). Header-only: StickIsland is a template on main's stick and its Io, instantiated
// in main's unit, so the ADC path's calls stay in one translation unit (app-main-shrink
// §6, inlining and timing).

#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <vector>

#include "continuous_adc.hpp"
#include "control/cycle.hpp"
#include "logger.hpp"
#include "oneshot_adc.hpp"
#include "simple_lowpass_filter.hpp"
#include "stick/stick_monitor.hpp"
#include "stick/stick_pipeline.hpp"
#include "task.hpp"

namespace hmi::control {

/// @brief What the control island's cycle reads and how often: compile-time constants (a
/// template argument of StickIsland), so they stay constants in the task's code, as they were
/// when the task was app_main's lambda. Held at run time, the period alone grew the task's
/// frame by 16 B, and the twist read lost its constant-propagated clone (another 16 B).
struct Cycle {
  /// The wait after each cycle, in ms.
  int period_ms;
  /// The twist pot's channel, read oneshot.
  espp::AdcConfig twist_channel;
  /// How many oneshot reads of the twist each cycle averages.
  int twist_oversample;
};

/// @brief The control island: the "Read ADC" task. Every `period` it reads the joystick's
/// three pots (X/Y on the continuous ADC, the twist oneshot and oversampled) and runs one
/// cycle of the stick pipeline on them: calibration, mapping, keys, gate and the XYTwist
/// publish, all on this task (app-main-shrink V10). Built once, an app_main local (V6): the
/// constructor brings the ADC drivers up, start() builds the stick on its calibration and
/// starts the task.
/// @tparam Stick The stick pipeline (main's StickSlot): `bool cycle(Io &, const RawReadsMv &)`,
///         and optionally `RawReadsMv reads(const RawReadsMv &)`, the reads its cycle will use
///         (the bench injection's; without it the ADC's reads are used as they are).
/// @tparam Io What one cycle talks to (main's AdcStickIo): built every cycle on the twist's
///         lowpass and the task's own `Io::State`, handed the reads the pipeline gets before
///         the cycle (note_reads()), and told about every cycle, valid or not, through
///         note_cycle().
/// @tparam kCycle The cycle's period and twist read (Cycle).
/// @tparam Ports The board's side of each cycle, as static functions (direct calls, no
///         indirection on the ADC path): `std::uint32_t now_ms()` (the ADC side's clock),
///         `bool watchdog_subscribe()` and `void watchdog_reset()` (the task watchdog; hazard
///         fix C4, §4.3), and `void after_cycle()` (after the watchdog reset, before the
///         wait: the bench's STALL ADC, a no-op otherwise). The cycle itself is ControlCycle
///         (cycle.hpp): the motion guard, the stick, note_cycle, the watchdog reset.
template <typename Stick, typename Io, Cycle kCycle, typename Ports> class StickIsland {
public:
  // C2's P6 (twist all reads good) counts the island's oneshot reads (hazard-c2-spec.md §4).
  static_assert(kCycle.twist_oversample == hmi::stick::TWIST_OVERSAMPLE,
                "the monitor's TWIST_OVERSAMPLE is the island's twist_oversample");

  /// @brief The motion guard's channels (hazard fix C4, §3.1); every one outlives the task.
  struct Guard {
    const GuardSources *sources; ///< the UI heartbeat, the MibStatus state and stamp
    GuardFlags flags;            ///< the link's flags and the UI's gate
    GuardTelemetry *telemetry;   ///< where the guard's observability goes
  };

  struct Config {
    /// The task, as a literal copy of today's, espp's defaults written out.
    espp::Task::BaseConfig task;
    /// The task's log level.
    espp::Logger::Verbosity task_log_level;
    /// The continuous ADC: channels[0] is the vertical axis, channels[1] the horizontal one.
    espp::ContinuousAdc::Config adc;
    /// The twist's lowpass, after the average.
    espp::SimpleLowpassFilter::Config twist_lowpass;
    /// The motion guard's channels.
    Guard guard;
  };

  /// @brief Brings the continuous ADC up and starts it, then the twist's oneshot ADC; starts
  /// no task of its own.
  /// @param config See Config.
  explicit StickIsland(const Config &config)
      : xy_channels_{config.adc.channels.at(0), config.adc.channels.at(1)}
      , twist_lowpass_config_(config.twist_lowpass)
      , adc_(config.adc)
      , control_(watchdog_, PortClock{}, *config.guard.sources, config.guard.flags,
                 *config.guard.telemetry)
      , task_({.callback = [this](std::mutex &m, std::condition_variable &cv) -> bool {
                 // The raw sample (hazard fix C2, §2.1, REQ-CTL-15): both X/Y windows in one
                 // snapshot (mean, max, sequence; see the AXIS WIRING note at the
                 // calibrations: CH1 is horizontal, CH0 vertical), then the twist pot on ADC2
                 // (GPIO52), sampled oneshot (mean, max, reads).
                 read_windows(sample_);
                 read_twist(*twist_adc_, sample_);

                 // raw mV -> calibrated stick -> the keypad key, the bars and XYTwist:
                 // hmi::stick::StickPipeline (components/stick), fed through Io; a cycle with a
                 // read missing is the pipeline's to handle (H9). The Io is handed the sample
                 // the stick gets (after the bench injection) first, valid or not.
                 // ControlCycle runs, every cycle, valid or not: the motion guard (its verdict
                 // to the Io: the permit's condition 2), this cycle, note_cycle on the ADC's
                 // reads, the watchdog reset (hazard fix C4).
                 Io stick_io{*twist_lowpass_, *io_state_};
                 Step step{this, &sample_};
                 static_cast<void>(control_.run(step, stick_io, means_of(sample_)));
                 Ports::after_cycle();
                 if constexpr (requires { Ports::cadc_stall_ms(); }) {
                   step_cadc_stall(Ports::cadc_stall_ms(), Ports::now_ms()); // bench builds
                 }

                 // NOTE: sleeping in this way allows the sleep to exit early when the
                 // task is being stopped / destroyed
                 {
                   std::unique_lock<std::mutex> lk(m);
                   cv.wait_for(lk, std::chrono::milliseconds(kCycle.period_ms));
                 }
                 // don't want to stop the task
                 return false;
               },
               .task_config = config.task,
               .log_level = config.task_log_level}) {
    adc_.start();
    twist_adc_.emplace(espp::OneshotAdc::Config{.unit = kCycle.twist_channel.unit,
                                                .channels = {kCycle.twist_channel}});
  }
  StickIsland(const StickIsland &) = delete;
  StickIsland &operator=(const StickIsland &) = delete;
  StickIsland(StickIsland &&) = delete;
  StickIsland &operator=(StickIsland &&) = delete;
  ~StickIsland() = default;

  /// @brief Builds the stick on @p stick_config (its calibration), then the twist's lowpass,
  /// and starts the task (app_main, after the calibration is loaded).
  /// @param stick_config The stick pipeline's configuration.
  /// @param io_state The Io's state across cycles; it must outlive the task (app_main's).
  /// @return Whether the task started.
  bool start(const typename Stick::Config &stick_config, typename Io::State &io_state) {
    io_state_ = &io_state;
    stick_.emplace(stick_config);
    twist_lowpass_.emplace(twist_lowpass_config_);
    return task_.start();
  }

private:
  // The Ports as ControlCycle's watchdog and clock. A failed subscription is logged once, at
  // the task's first cycle (start-up, CS-SAF-04); the guard then holds WDT_MISSING.
  struct PortWatchdog {
    espp::Logger *logger;
    bool subscribe() {
      const bool ok = Ports::watchdog_subscribe();
      if (!ok) {
        logger->error("Read ADC: task watchdog subscription failed: stick held (WDT_MISSING)");
      }
      return ok;
    }
    void reset() { Ports::watchdog_reset(); }
  };
  struct PortClock {
    std::uint32_t operator()() const { return Ports::now_ms(); }
  };
  // The stick as ControlCycle sees it: one cycle_on on this cycle's raw sample.
  struct Step {
    StickIsland *island;
    hmi::stick::RawSample *sample;
    [[gnu::always_inline]] bool cycle(Io &stick_io, const hmi::stick::RawReadsMv &means) {
      return island->cycle_on(stick_io, *sample, means);
    }
  };

  // Both X/Y windows into @p out, from one snapshot (see the AXIS WIRING note at the
  // calibrations: CH1 is horizontal, CH0 vertical). Out of line (noinline): the snapshot's
  // arrays stay out of the task's own frame (hazard-c2-spec.md §10.3).
  // espp's ContinuousAdc::get_windows() (espp PR 1) reads them under its own lock, the one
  // its reader task publishes them under: both from the same window.
  [[gnu::noinline]] void read_windows(hmi::stick::RawSample &out) {
    const auto xy = adc_.get_windows(xy_channels_);
    out.horizontal = window_of(xy[1]); // ADC1_CH1
    out.vertical = window_of(xy[0]);   // ADC1_CH0
  }
  // A window as the raw sample carries it; a missing read carries no max and no sequence.
  [[gnu::always_inline]] static hmi::stick::XyWindowMv
  window_of(const std::optional<espp::ContinuousAdc::Window> &w) {
    if (!w) {
      return {.mean_mv = std::nullopt, .max_mv = 0.0f, .sequence = 0};
    }
    return {.mean_mv = w->mean_mv, .max_mv = w->max_mv, .sequence = w->sequence};
  }
  // The three means: what the pipeline and note_cycle get.
  [[gnu::always_inline]] static hmi::stick::RawReadsMv means_of(const hmi::stick::RawSample &s) {
    return {.horizontal_mv = s.horizontal.mean_mv,
            .vertical_mv = s.vertical.mean_mv,
            .twist_mv = s.twist_mean_mv};
  }

  // One cycle of the stick on the ADC's raw sample @p sample (whose means are @p means), through
  // @p stick_io. The Stick's own injection (bench builds: inject()) replaces the sample in place
  // when it has one. The Io is handed the sample (note_sample: the monitor's input, the one
  // `calibrating` of the cycle) and its means (note_reads) before the pipeline runs on them.
  [[gnu::always_inline]] bool cycle_on(Io &stick_io, hmi::stick::RawSample &sample,
                                       const hmi::stick::RawReadsMv &means) {
    if constexpr (requires { stick_->inject(sample); }) {
      stick_->inject(sample);
      const hmi::stick::RawReadsMv injected = means_of(sample);
      return cycle_with(stick_io, sample, injected);
    } else {
      return cycle_with(stick_io, sample, means);
    }
  }
  [[gnu::always_inline]] bool cycle_with(Io &stick_io, const hmi::stick::RawSample &sample,
                                         const hmi::stick::RawReadsMv &reads) {
    stick_io.note_sample(sample);
    stick_io.note_reads(reads);
    return stick_->cycle(stick_io, reads);
  }

  // The twist pot's reading for the Read ADC task: kCycle.twist_oversample oneshot reads
  // on ADC2, into @p out: their mean (nullopt if none succeeded), their largest, and how many
  // succeeded (hazard fix C2, §2.1).
  //
  // Out of line on purpose (noinline). OneshotAdc::read_mv brings espp Logger's
  // error paths with it, and once GCC also inlined Logger::get_time there (an fmt
  // buffer of ~500 B) the ADC task's own frame grew from 256 to 736 B, and every
  // call it makes (RTPS publish, the bars' observers) sat that much deeper: the
  // bench's mem.stk_adc fell ~500 B (B3, image a6ff6de, 2026-10-07). Here that cold
  // path holds stack only while the twist is read. Which way the inliner goes
  // depends on the rest of the unit, so this pins it instead of relying on it. The
  // channel and the count are kCycle's constants, as they were the arguments' (GCC
  // cloned the free function for them; with them as runtime values the frame was 16 B
  // larger).
  [[gnu::noinline]] static void read_twist(espp::OneshotAdc &twist_adc,
                                           hmi::stick::RawSample &out) {
    float sum = 0.0f;
    float max = 0.0f;
    std::uint8_t reads = 0;
    for (int i = 0; i < kCycle.twist_oversample; ++i) {
      if (auto mv = twist_adc.read_mv(kCycle.twist_channel)) {
        const float v = static_cast<float>(*mv);
        sum += v;
        max = reads == 0 || v > max ? v : max;
        ++reads;
      }
    }
    out.twist_reads = reads;
    out.twist_max_mv = max;
    out.twist_mean_mv =
        reads > 0 ? std::optional<float>{sum / static_cast<float>(reads)} : std::nullopt;
  }

  // Bench builds (C2 O14, STALL CADC): espp's ContinuousAdc task suspended by Read ADC from a
  // request until its time is up, so X/Y go stale as a starved producer's do (C2-16). Read ADC
  // only. A request while one is running is dropped.
  static constexpr const char *kCadcTaskName = "ContinuousAdc T";
  struct CadcStall {
    TaskHandle_t task = nullptr; ///< suspended, while not null
    std::uint32_t until_ms = 0;
  };
  void step_cadc_stall(std::uint32_t request_ms, std::uint32_t now_ms) noexcept {
    if (request_ms != 0 && cadc_stall_.task == nullptr) {
      // the reader task by its FreeRTOS name ("ContinuousAdc Task", cut to
      // configMAX_TASK_NAME_LEN); espp's ContinuousAdc keeps its handle to itself
      if (TaskHandle_t task = xTaskGetHandle(kCadcTaskName); task != nullptr) {
        vTaskSuspend(task);
        cadc_stall_ = {.task = task, .until_ms = now_ms + request_ms};
      }
    }
    if (cadc_stall_.task != nullptr &&
        static_cast<std::int32_t>(now_ms - cadc_stall_.until_ms) >= 0) {
      vTaskResume(cadc_stall_.task);
      cadc_stall_.task = nullptr;
    }
  }
  CadcStall cadc_stall_{};

  std::array<espp::AdcConfig, 2> xy_channels_; ///< vertical (CH0), horizontal (CH1)
  // This cycle's raw sample (C2 §2.1): every field written each cycle by read_windows and
  // read_twist, then (bench builds) the injection. A member, not a local, so the Read ADC
  // task's own frame holds none of it (hazard-c2-spec.md §10.3). Read ADC only.
  hmi::stick::RawSample sample_{};
  espp::SimpleLowpassFilter::Config twist_lowpass_config_;
  espp::ContinuousAdc adc_;
  std::optional<espp::OneshotAdc> twist_adc_;
  std::optional<Stick> stick_;
  typename Io::State *io_state_ = nullptr;
  std::optional<espp::SimpleLowpassFilter> twist_lowpass_;
  espp::Logger logger_{{.tag = "stick_island", .level = espp::Logger::Verbosity::WARN}};
  PortWatchdog watchdog_{&logger_};
  ControlCycle<PortWatchdog, PortClock> control_;
  espp::Task task_;
};

} // namespace hmi::control
