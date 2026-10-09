#pragma once
// The quick POST on the UI task (hazard-c3-spec.md §2.3-2.9): the POST runner, the rest windows
// the ADC task sends it, the POST gate it writes, and what the persistent indicator shows.
//
// Interim (decision C6, spec-deviation(CS-SAF-04) until the islands work moves POST to
// `control`): the runner is safety logic on the UI task. A stalled UI task leaves the gate
// PENDING or NOT_RUN, which holds the stick.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "logger.hpp"

#include "fw_core/atomic_value.hpp"
#include "fw_core/mailbox.hpp"
#include "post/indicator.hpp"
#include "post/rest_window.hpp"
#include "post/runner.hpp"
#include "stick/permit_types.hpp"

namespace hmi::ui {

/// What the POST gathers from the board: main's ESP-IDF calls (C3 §2.2). Each runs on the UI
/// task, inside the runner's tick, except where it says otherwise.
struct PostPort {
  std::uint32_t (*now_ms)();                         ///< the UI side's ms clock
  hmi::post::ResetReason (*reset_reason)();          ///< esp_reset_reason(); any task
  hmi::post::ImageFacts (*image)();                  ///< the running image
  hmi::post::Calibration (*calibration)();           ///< the calibration in use
  std::optional<hmi::post::MemoryFacts> (*memory)(); ///< heap headroom; nothing: unmeasured
  hmi::post::StackFacts (*stacks)();                 ///< stack headroom of Read ADC, lv_task
  bool (*calibration_saved_now)();                   ///< a calibration is saved now; any task
  bool (*stick_task_running)(); ///< Read ADC exists; asked only at the budget (UI task)
};

/// The one-slot channel of rest windows: Read ADC writes, the runner (UI task) reads.
using RestWindowMailbox = hmi::fw::Mailbox<hmi::post::RestWindowMsg>;

/// The indicator's text, built in a fixed buffer.
struct PostText {
  std::array<char, 112> text{}; ///< NUL-terminated
};

/// One instance (UiApp's, constant initialised). Ticked by the UI poll before the drive
/// adapter's tick; the indicator is read after it (REQ-UI-23).
class PostStage {
public:
  struct Config {
    const PostPort *port;                             ///< main's IDF calls
    hmi::fw::AtomicValue<hmi::stick::PostGate> *gate; ///< the POST gate: this is its writer
  };

  /// What the indicator shows, as one value any task can load (the bench's STATE).
  struct Shown {
    hmi::post::IndicatorKind kind = hmi::post::IndicatorKind::CHECKING;
    std::optional<hmi::post::Id> check; ///< the blocking check, if the gate names one
    bool timed_out = false;
    bool stick_task_missing = false; ///< timed out with no Read ADC task: the cause
  };

  constexpr explicit PostStage(const Config &config) noexcept
      : config_(config)
      , runner_(RunnerPort{.stage = this}) {}
  PostStage(const PostStage &) = delete;
  PostStage &operator=(const PostStage &) = delete;
  PostStage(PostStage &&) = delete;
  PostStage &operator=(PostStage &&) = delete;
  ~PostStage() = default;

  /// @brief Hands over the rest windows' read end, the boot I2C scan and the "post" logger
  ///        (the TS-POST-05 lines). Until then the runner sees no window and logs nothing.
  /// app_main, before lv_task starts; @p log must outlive the firmware (app_main's).
  void attach(hmi::fw::Reader<RestWindowMailbox> windows, const hmi::post::I2cSet &i2c,
              espp::Logger &log);

  /// @brief One UI tick: a bench request, then the runner on this tick's window (if any), then
  ///        the indicator's state. UI task, before the drive adapter's tick.
  void tick();

  /// @return the indicator as of the last tick. Any task.
  [[nodiscard]] Shown shown() const;

  /// @brief The indicator's words for @p shown (hmi_rtps_spec's texts). Any task.
  /// @param shown the indicator
  /// @param reset the last reset's reason (named in the unclean-reset text)
  /// @param calibration_saved_now a calibration is saved now (the "Restart" text, C3 §2.12)
  /// @return the text; empty for NONE
  [[nodiscard]] static PostText text(const Shown &shown, hmi::post::ResetReason reset,
                                     bool calibration_saved_now);

  /// @brief The indicator's text now. Any task.
  [[nodiscard]] PostText text_now() const;

  /// Bench only (C3 REQ-RUI-06): POST RERUN and PERMIT POST, applied by the runner on its next
  /// tick. Any task.
  void request_rerun();
  void request_gate(hmi::stick::PostGate gate);

private:
  /// The runner's port: main's calls, the gate's write end and the log.
  struct RunnerPort {
    PostStage *stage;
    hmi::post::ResetReason reset_reason() { return stage->config_.port->reset_reason(); }
    hmi::post::ImageFacts image() { return stage->config_.port->image(); }
    hmi::post::Calibration calibration() { return stage->config_.port->calibration(); }
    std::optional<hmi::post::MemoryFacts> memory() { return stage->config_.port->memory(); }
    hmi::post::StackFacts stacks() { return stage->config_.port->stacks(); }
    [[nodiscard]] hmi::post::I2cSet i2c() const { return stage->i2c_; }
    bool stick_task_running() { return stage->config_.port->stick_task_running(); }
    void store_gate(hmi::stick::PostGate gate) { stage->config_.gate->write(gate); }
    void print(std::string_view line) {
      if (stage->log_ != nullptr) {
        stage->log_->info("{}", line);
      }
    }
  };

  void apply_request();
  void publish_shown(std::uint32_t now_ms);

  Config config_;
  hmi::post::I2cSet i2c_{};
  espp::Logger *log_ = nullptr;
  hmi::post::PostRunner<RunnerPort> runner_;
  std::optional<hmi::fw::Reader<RestWindowMailbox>> windows_;
  bool not_run_seen_ = false;          ///< the gate was NOT_RUN at the last tick
  std::uint32_t not_run_since_ms_ = 0; ///< since when
  /// The indicator packed: kind | check << 8 (0xFF: none) | timed_out << 16
  /// | stick_task_missing << 17.
  hmi::fw::AtomicValue<std::uint32_t> shown_{{.initial = 0xFF01U}}; // CHECKING, no check
  /// A bench request, written by the remote UI's task only: sequence << 8 | what (1 rerun,
  /// 2 + PostGate a forced gate). The runner applies each sequence number once.
  hmi::fw::AtomicValue<std::uint32_t> request_{{.initial = 0}};
  std::uint32_t request_seen_ = 0; ///< UI task: the last sequence number applied
};

} // namespace hmi::ui
