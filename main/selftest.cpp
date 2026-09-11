#include "selftest.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_clk.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "log_capture.hpp"
#include "logger.hpp"
#include "rtps_comms.hpp"
#include "selftest_spec.h"

namespace {

/////////////////////////////////////////////////////////////////////////////
// The spec, as C++
/////////////////////////////////////////////////////////////////////////////

enum Id : uint8_t {
#define ST_ENUM(id, name, unit, lo, hi, need, desc) ST_##id,
  SELFTEST_TABLE(ST_ENUM)
#undef ST_ENUM
      ST_COUNT
};

struct Spec {
  const char *name;
  const char *unit;
  int32_t lo;
  int32_t hi;
  int need;
  const char *desc;
};

constexpr Spec kSpec[] = {
#define ST_ROW(id, name, unit, lo, hi, need, desc) {name, unit, lo, hi, need, desc},
    SELFTEST_TABLE(ST_ROW)
#undef ST_ROW
};
static_assert(std::size(kSpec) == ST_COUNT);
static_assert(ST_COUNT < 256, "the report carries the check index as a uint8_t");

constexpr bool spec_fits_the_wire() {
  return std::all_of(std::begin(kSpec), std::end(kSpec), [](const Spec &s) {
    return std::string_view(s.name).size() < RAMMP_SELFTEST_NAME_LEN &&
           std::string_view(s.unit).size() < RAMMP_SELFTEST_UNIT_LEN && s.lo <= s.hi;
  });
}
static_assert(spec_fits_the_wire(),
              "a selftest_spec.h name or unit is too long for the report, or has lo > hi");

bool is_yes_no(const Spec &s) { return s.lo == 1 && s.hi == 1 && s.unit[0] == '\0'; }

/// "182 KB", "yes", "0.7%": how a value reads on screen and in the log.
std::string format_value(const Spec &s, int32_t value) {
  if (is_yes_no(s)) {
    return value ? "yes" : "no";
  }
  if (std::string_view(s.unit) == "0.1%") {
    return fmt::format("{}.{}%", value / 10, std::abs(value % 10));
  }
  return s.unit[0] ? fmt::format("{} {}", value, s.unit) : fmt::format("{}", value);
}

/// format_value, shortened for the overlay's narrow columns: microseconds of a
/// millisecond or more as ms, and byte or KB counts of ten thousand or more a
/// unit up, each to one decimal. Only the screen uses it; the serial log and
/// the report keep the spec's own units. (At full length the widest row was
/// 361 px against a 340 px column on the 720 px screen.)
std::string compact_value(const Spec &s, int32_t value) {
  // one decimal only while it still matters: "4.9 ms" but "88 ms". With the
  // decimal kept everywhere the widest row fitted its column by 1 px, so any
  // longer reading on a failing run would have been cut off again.
  const auto scaled = [](float v, const char *unit) {
    return std::abs(v) < 10.0f ? fmt::format("{:.1f} {}", v, unit)
                               : fmt::format("{:.0f} {}", v, unit);
  };
  const std::string_view unit(s.unit);
  if (unit == "us" && std::abs(value) >= 1000) {
    return scaled(value / 1000.0f, "ms");
  }
  if (unit == "B" && value >= 10000) {
    return scaled(value / 1024.0f, "KB");
  }
  if (unit == "KB" && value >= 10000) {
    return scaled(value / 1024.0f, "MB");
  }
  return format_value(s, value);
}

std::string format_limits(const Spec &s) {
  if (s.lo == s.hi) {
    return is_yes_no(s) ? "yes" : fmt::format("= {}", format_value(s, s.lo));
  }
  if (s.lo == ST_ANY_LO) {
    return fmt::format("<= {}", format_value(s, s.hi));
  }
  if (s.hi == ST_ANY_HI) {
    return fmt::format(">= {}", format_value(s, s.lo));
  }
  return fmt::format("{} .. {}", format_value(s, s.lo), format_value(s, s.hi));
}

/////////////////////////////////////////////////////////////////////////////
// Shared state
/////////////////////////////////////////////////////////////////////////////

espp::Logger logger({.tag = "selftest", .level = espp::Logger::Verbosity::INFO});

SelfTestPlatform platform;

std::atomic<bool> running{false};
std::atomic<uint8_t> pending_run_id{0};
std::atomic<bool> pending_remote{false};
// -1 = none yet; a remote command resent with the same id is ignored
std::atomic<int> last_remote_run_id{-1};

// Stack for the run's task, in PSRAM. Internal RAM is the one resource this
// board is short of: with the stack there, a 16 KB run left the DMA-capable
// pool at 4 KB - the pool the W5500's SPI bounce buffers come from, whose
// exhaustion has boot-looped this board before - so the test was endangering
// what it measures. Nothing on this task disables the cache, which is the one
// thing a PSRAM stack rules out (CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY).
//
// Sized from measurement: a full run peaks at ~6 KB (logged at the end of
// each run). It is deep because an RTPS publish goes all the way down through
// lwIP to the W5500 driver on the caller's stack, and LVGL relays out a label
// inside lv_subject_copy_string.
constexpr uint32_t kTaskStackBytes = 12 * 1024;
constexpr UBaseType_t kTaskPriority = 3;

/////////////////////////////////////////////////////////////////////////////
// Overlay. Everything firmware-driven reaches it through subjects (CLAUDE.md);
// the run's task only ever calls lv_subject_* under the LVGL lock.
/////////////////////////////////////////////////////////////////////////////

constexpr int kMaxColumns = 3;
constexpr size_t kTitleLen = 128;
constexpr size_t kStatusLen = 256;
constexpr size_t kColumnLen = 64 * ST_COUNT; // PSRAM; room for every row in one column
// The rows fill as many columns as the display is wide enough for: two on the
// Tab5's 720 px portrait canvas, three at 1100 px or more. Measured from the
// display rather than assumed - the first version assumed 1280 px landscape and
// put the second column half off the screen and the third wholly off it.
int ui_columns = 2;
int ui_rows_per_column = (ST_COUNT + 1) / 2;

constexpr uint32_t kColourPass = 0x2ECC71;
constexpr uint32_t kColourFail = 0xFF5050;
constexpr uint32_t kColourSkip = 0xE0A000;
constexpr uint32_t kColourPending = 0x707070;

lv_subject_t ui_visible_subject; // 0/1, bound to the overlay's HIDDEN flag
lv_subject_t ui_title_subject;
lv_subject_t ui_status_subject;
lv_subject_t ui_column_subject[kMaxColumns];
bool ui_ready = false;
// The panel's widgets exist only while it is up: built when a run starts and
// deleted on dismiss, so ~2 KB of LVGL objects do not sit in internal RAM - the
// DMA-capable pool the W5500 depends on - for a panel that is up a few times a
// day. The subjects and their PSRAM text outlive them. LVGL lock held.
lv_obj_t *overlay_panel = nullptr;
// Stands in for the panel while the render check has it hidden: a blinking
// "Timing screen redraws" strip, so the vanishing panel does not look like a
// crash. Built and deleted with the panel. 0/1 in ui_banner_subject.
lv_obj_t *overlay_banner = nullptr;
lv_subject_t ui_banner_subject;
void create_overlay_locked();
// mirrors of ui state readable from any task without the LVGL lock
std::atomic<bool> ui_shown{false};
std::atomic<bool> ui_finished{true};

/////////////////////////////////////////////////////////////////////////////
// Probes fed from other tasks while a run is capturing
/////////////////////////////////////////////////////////////////////////////

// Render timing, from the display's own render events (LVGL task).
std::atomic<uint32_t> render_frames{0};
std::atomic<uint32_t> render_last_us{0};
int64_t render_start_us = 0; // LVGL task only

void render_start_cb(lv_event_t *) { render_start_us = esp_timer_get_time(); }

void render_ready_cb(lv_event_t *) {
  if (render_start_us == 0) {
    return;
  }
  render_last_us = static_cast<uint32_t>(esp_timer_get_time() - render_start_us);
  render_frames++;
}

// Joystick / ADC, from the ADC task.
struct AdcCapture {
  uint32_t cycles = 0;
  uint32_t valid = 0;
  uint32_t published = 0;
  bool button_seen = false;
  std::array<float, 3> min{};
  std::array<float, 3> max{};
  std::array<double, 3> sum{};
  int64_t last_us = 0;
  int64_t period_sum_us = 0;
  int64_t period_max_us = 0;
  uint32_t periods = 0;
};
std::mutex adc_mutex;
AdcCapture adc_capture;
std::atomic<bool> adc_armed{false};

// RTPS ping/pong, from the RTPS receive task.
constexpr uint32_t kMaxPings = 256;
static_assert(ST_WINDOW_MS / ST_PING_PERIOD_MS < kMaxPings, "ping bookkeeping is too small");
// Round-trip bookkeeping, alive only while a window is open. On the heap (PSRAM,
// at this size) rather than in .bss, which is internal RAM - the DMA-capable
// pool the W5500 depends on, and the one thing this board is short of.
struct PingLog {
  std::array<int64_t, kMaxPings> sent_us{}; // 0 = not sent
  std::array<bool, kMaxPings> seen{};
  std::vector<uint32_t> rtt_us;
  uint32_t peer_rx = 0;
  bool peer_rx_known = true; // false once any pong was a plain echo
};
std::mutex ping_mutex;
std::unique_ptr<PingLog> ping_log; // non-null only during a window

void on_pong(uint16_t seq, int peer_rx) {
  const int64_t now = esp_timer_get_time();
  std::lock_guard<std::mutex> lock(ping_mutex);
  PingLog *pings = ping_log.get();
  if (pings == nullptr || seq >= kMaxPings || pings->sent_us[seq] == 0 || pings->seen[seq]) {
    return; // no window open, never sent, or a duplicate
  }
  pings->seen[seq] = true;
  pings->rtt_us.push_back(static_cast<uint32_t>(now - pings->sent_us[seq]));
  if (peer_rx < 0) {
    pings->peer_rx_known = false;
  } else {
    pings->peer_rx = std::max(pings->peer_rx, static_cast<uint32_t>(peer_rx));
  }
}

const char *reset_reason_name(esp_reset_reason_t reason) {
  switch (reason) {
  case ESP_RST_POWERON:
    return "power-on";
  case ESP_RST_EXT:
    return "external pin";
  case ESP_RST_SW:
    return "software";
  case ESP_RST_PANIC:
    return "PANIC";
  case ESP_RST_INT_WDT:
    return "INTERRUPT WDT";
  case ESP_RST_TASK_WDT:
    return "TASK WDT";
  case ESP_RST_WDT:
    return "WDT";
  case ESP_RST_DEEPSLEEP:
    return "deep sleep";
  case ESP_RST_BROWNOUT:
    return "BROWNOUT";
  case ESP_RST_USB:
    return "USB";
  case ESP_RST_JTAG:
    return "JTAG";
  case ESP_RST_CPU_LOCKUP:
    return "CPU LOCKUP";
  case ESP_RST_PWR_GLITCH:
    return "POWER GLITCH";
  default:
    return "other";
  }
}

void copy_field(char *dst, size_t size, std::string_view src) {
  const size_t n = std::min(src.size(), size - 1);
  std::memcpy(dst, src.data(), n);
  std::memset(dst + n, 0, size - n);
}

/////////////////////////////////////////////////////////////////////////////
// One run
/////////////////////////////////////////////////////////////////////////////

struct Outcome {
  bool done = false;     // reached by the runner at all
  bool measured = false; // false = could not be measured, see detail
  int32_t value = 0;
  uint8_t result = RAMMP_SELFTEST_RESULT_SKIP;
  std::string detail;
};

class Run {
public:
  Run(uint8_t run_id, bool remote)
      : run_id_(run_id)
      , remote_(remote) {}

  void execute() {
    // Low-water marks as they stood before this run touched anything, so the
    // memory checks can tell the firmware's own worst case from the test's.
    int_min_before_ = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    log_count_before_ = log_capture_count();
    dma_min_before_ = heap_caps_get_minimum_free_size(MALLOC_CAP_DMA);
    const int64_t start_us = esp_timer_get_time();
    version_ = esp_app_get_description()->version;
    logger.info("run {} ({}) starting on firmware {}", run_id_, remote_ ? "remote" : "local",
                version_);
    ui_begin();
    publish_marker(RAMMP_SELFTEST_KIND_STARTED, 0, 0, 0, version_);

    status("Checking the board");
    check_system();
    check_board();
    check_haptic_presence();
    check_display();
    check_network();
    ui_refresh();

    status("Hands off the joystick - measuring it at rest in a moment");
    vTaskDelay(pdMS_TO_TICKS(ST_SETTLE_MS));

    status(fmt::format("Observing for {} s: joystick, ADC, RTPS, UI responsiveness",
                       ST_WINDOW_MS / 1000));
    observe_window();
    ui_refresh();

    status("Timing full-screen redraws of the screen behind this panel");
    check_render();
    ui_refresh();

    status("Haptic check - the motor clicks once");
    check_haptic_play();

    check_log();
    // last, so the low-water mark and the stacks cover everything above
    check_memory();

    for (uint8_t i = 0; i < ST_COUNT; ++i) {
      if (!out_[i].done) {
        // a spec row with nothing behind it must not pass by omission
        out_[i] = Outcome{.done = true,
                          .measured = false,
                          .result = RAMMP_SELFTEST_RESULT_FAIL,
                          .detail = "not measured by this firmware"};
        publish_result(i);
      }
    }

    const int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
    finish(elapsed_ms);
  }

private:
  // --- recording ----------------------------------------------------------

  void record(Id id, int32_t value, std::string detail = {}) {
    const Spec &s = kSpec[id];
    Outcome &o = out_[id];
    o.done = true;
    o.measured = true;
    o.value = value;
    o.detail = std::move(detail);
    o.result =
        (value >= s.lo && value <= s.hi) ? RAMMP_SELFTEST_RESULT_PASS : RAMMP_SELFTEST_RESULT_FAIL;
    publish_result(id);
  }

  void unmeasurable(Id id, std::string reason) {
    const Spec &s = kSpec[id];
    Outcome &o = out_[id];
    o.done = true;
    o.measured = false;
    o.detail = std::move(reason);
    const bool required = s.need == ST_REQUIRED || (s.need == ST_REMOTE && remote_);
    o.result = required ? RAMMP_SELFTEST_RESULT_FAIL : RAMMP_SELFTEST_RESULT_SKIP;
    publish_result(id);
  }

  void publish_result(uint8_t id) const {
    const Spec &s = kSpec[id];
    const Outcome &o = out_[id];
    rammp_selftest_report_t report{};
    report.run_id = run_id_;
    report.kind = RAMMP_SELFTEST_KIND_RESULT;
    report.index = id;
    report.count = ST_COUNT;
    report.result = o.result;
    report.value = o.value;
    report.lo = s.lo;
    report.hi = s.hi;
    copy_field(report.name, sizeof(report.name), s.name);
    copy_field(report.unit, sizeof(report.unit), s.unit);
    copy_field(report.detail, sizeof(report.detail), o.detail);
    send_report(report);
  }

  // espp/rtps queues a best-effort writer's samples two deep and a pool thread
  // sends them, so samples published back to back can overwrite each other
  // before they go out - the bench lost the first check that way, twice. A
  // short gap after each one keeps the queue from ever holding a third.
  static void send_report(const rammp_selftest_report_t &report) {
    if (rtps_comms_publish_selftest_report(report)) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }

  void publish_marker(uint8_t kind, int32_t value, int32_t lo, int32_t hi,
                      std::string_view detail) const {
    rammp_selftest_report_t report{};
    report.run_id = run_id_;
    report.kind = kind;
    report.count = ST_COUNT;
    report.value = value;
    report.lo = lo;
    report.hi = hi;
    copy_field(report.name, sizeof(report.name),
               kind == RAMMP_SELFTEST_KIND_STARTED ? "started" : "summary");
    copy_field(report.detail, sizeof(report.detail), detail);
    send_report(report);
  }

  // --- checks ---------------------------------------------------------------

  void check_system() {
    const esp_reset_reason_t reason = esp_reset_reason();
    const bool clean = reason != ESP_RST_PANIC && reason != ESP_RST_INT_WDT &&
                       reason != ESP_RST_TASK_WDT && reason != ESP_RST_WDT &&
                       reason != ESP_RST_BROWNOUT && reason != ESP_RST_CPU_LOCKUP &&
                       reason != ESP_RST_PWR_GLITCH;
    record(ST_SYS_RESET, clean ? 1 : 0, fmt::format("reset reason: {}", reset_reason_name(reason)));
    record(ST_SYS_CPU, esp_clk_cpu_freq() / 1000000);
    record(ST_SYS_UPTIME, static_cast<int32_t>(esp_timer_get_time() / 1000000));
  }

  // The run prints its own progress, so by now a working capture has grown; one
  // that is stuck, or that something has routed stdout around, has not.
  void check_log() {
    const uint32_t now = log_capture_count();
    record(ST_LOG_CAPTURE, log_capture_active() && now > log_count_before_ ? 1 : 0,
           fmt::format("{} lines since boot, {} this run", now, now - log_count_before_));
  }

  void check_board() {
    if (!platform.i2c_probe) {
      unmeasurable(ST_I2C_MISSING, "no I2C probe");
      unmeasurable(ST_I2C_COUNT, "no I2C probe");
    } else {
      // Scan the whole bus again rather than re-probing the boot list, so the
      // count stands on its own: one boot on the bench found nothing, and a
      // re-probe of an empty list passes by doing nothing.
      std::vector<uint8_t> now;
      std::string found;
      for (uint8_t address = 0x08; address <= 0x77; ++address) { // non-reserved, as at boot
        if (platform.i2c_probe(address)) {
          now.push_back(address);
          found += fmt::format(" {:02x}", address);
        }
      }
      record(ST_I2C_COUNT, static_cast<int32_t>(now.size()), found.empty() ? "none" : "at" + found);
      if (platform.boot_i2c_devices.empty()) {
        unmeasurable(ST_I2C_MISSING, "the boot-time scan found no devices");
      } else {
        std::string missing;
        int32_t lost = 0;
        for (uint8_t address : platform.boot_i2c_devices) {
          if (std::find(now.begin(), now.end(), address) == now.end()) {
            lost++;
            missing += fmt::format(" 0x{:02x}", address);
          }
        }
        record(ST_I2C_MISSING, lost, missing.empty() ? std::string() : "gone:" + missing);
      }
    }

    const auto accel = platform.imu_accel_mg ? platform.imu_accel_mg() : std::nullopt;
    if (accel) {
      record(ST_IMU_ACCEL, *accel);
    } else {
      unmeasurable(ST_IMU_ACCEL, "no accelerometer reading");
    }

    const auto vbat = platform.battery_mv ? platform.battery_mv() : std::nullopt;
    if (!vbat) {
      unmeasurable(ST_PWR_VBAT, "battery monitor did not answer");
    } else if (*vbat < 5000) {
      // no 2S pack reads this low (pwr.vbat in selftest_spec.h); the raw
      // reading goes in the detail, because the bench unit flips between this
      // and a full pack on one boot
      unmeasurable(ST_PWR_VBAT, fmt::format("reads {} mV: taken as no pack fitted", *vbat));
    } else {
      record(ST_PWR_VBAT, *vbat);
    }
  }

  void check_haptic_presence() {
    const auto status = platform.drv2605_status ? platform.drv2605_status() : std::nullopt;
    if (status) {
      record(ST_HAP_DRV_ID, *status >> 5);
      // OVER_TEMP (bit 1) and OC_DETECT (bit 0) latch until read
      record(ST_HAP_DRV_FAULT, (*status & 0x03) == 0 ? 1 : 0,
             fmt::format("STATUS 0x{:02x}", *status));
    } else {
      unmeasurable(ST_HAP_DRV_ID, "DRV2605 did not answer");
      unmeasurable(ST_HAP_DRV_FAULT, "DRV2605 did not answer");
    }
    if (platform.da7280_found) {
      record(ST_HAP_DA7280, 1);
    } else {
      unmeasurable(ST_HAP_DA7280, "not fitted (no ACK at 0x4a at boot)");
    }
  }

  void check_haptic_play() {
    if (!platform.drv2605_play) {
      unmeasurable(ST_HAP_DRV_PLAY, "no DRV2605");
      return;
    }
    std::string detail;
    const auto ok = platform.drv2605_play(detail);
    if (ok) {
      record(ST_HAP_DRV_PLAY, *ok ? 1 : 0, detail);
    } else {
      unmeasurable(ST_HAP_DRV_PLAY, detail);
    }
  }

  void check_display() {
    record(ST_DISP_DIRECT, platform.direct_render ? 1 : 0);
    if (platform.backlight_percent) {
      record(ST_DISP_BACKLIGHT, platform.backlight_percent());
    } else {
      unmeasurable(ST_DISP_BACKLIGHT, "no backlight probe");
    }
  }

  void check_network() {
    const RtpsLinkState state = rtps_comms_link_state();
    const auto rank = static_cast<int>(state);
    const std::string detail = rtps_comms_link_state_meaning(state);
    // ranks: ETH_FAILED < LINK_DOWN < NO_IP < NO_PEER < CONNECTED
    // the cause goes on the row that fails, not on every row: "McbStatus
    // arriving" beside a passing Ethernet link only muddies what it proves
    const bool link = rank >= static_cast<int>(RtpsLinkState::NO_IP);
    const bool lease = rank >= static_cast<int>(RtpsLinkState::NO_PEER);
    record(ST_NET_LINK, link ? 1 : 0, link ? std::string() : detail);
    record(ST_NET_IP, lease ? 1 : 0, lease ? std::string() : detail);
    if (state == RtpsLinkState::CONNECTED) {
      record(ST_RTPS_LINK, 1);
    } else {
      unmeasurable(ST_RTPS_LINK, detail);
    }
  }

  // Joystick, ADC cadence, McbStatus, pings, RTC and UI stalls, all measured
  // across the same window so they describe the same stretch of time.
  void observe_window() {
    {
      std::lock_guard<std::mutex> lock(adc_mutex);
      adc_capture = AdcCapture{};
    }
    {
      auto fresh = std::make_unique<PingLog>();
      fresh->rtt_us.reserve(ST_WINDOW_MS / ST_PING_PERIOD_MS + 1);
      std::lock_guard<std::mutex> lock(ping_mutex);
      ping_log = std::move(fresh); // opens the window: pongs count from here
    }
    rtps_comms_mcb_stats_reset();
    const auto rtc_start = platform.rtc_seconds ? platform.rtc_seconds() : std::nullopt;
    adc_armed = true;

    const int64_t start_us = esp_timer_get_time();
    const int64_t end_us = start_us + ST_WINDOW_MS * 1000LL;
    int64_t next_ping_us = start_us;
    uint32_t pings_sent = 0;
    uint32_t ping_seq = 0;
    int64_t stall_max_us = 0;
    for (int64_t now = start_us; now < end_us; now = esp_timer_get_time()) {
      if (now >= next_ping_us && ping_seq < kMaxPings) {
        {
          // stamped before the send, so even the fastest pong finds it
          std::lock_guard<std::mutex> lock(ping_mutex);
          ping_log->sent_us[ping_seq] = now;
        }
        if (rtps_comms_publish_selftest_ping(static_cast<uint16_t>(ping_seq))) {
          pings_sent++;
        } else {
          std::lock_guard<std::mutex> lock(ping_mutex);
          ping_log->sent_us[ping_seq] = 0; // never went out: a stray pong must not count
        }
        ping_seq++;
        next_ping_us += ST_PING_PERIOD_MS * 1000LL;
      }
      // How long the UI lock is held against a waiter: the worst case is how
      // long a producer task (ADC, RTPS) can be stuck behind a redraw.
      if (platform.lvgl_mutex) {
        const int64_t before = esp_timer_get_time();
        platform.lvgl_mutex->lock();
        stall_max_us = std::max(stall_max_us, esp_timer_get_time() - before);
        platform.lvgl_mutex->unlock();
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    adc_armed = false;
    const auto rtc_end = platform.rtc_seconds ? platform.rtc_seconds() : std::nullopt;
    const RtpsMcbStats mcb = rtps_comms_mcb_stats();
    // late pongs still count: give the last few a moment to come back
    vTaskDelay(pdMS_TO_TICKS(300));
    std::unique_ptr<PingLog> pings;
    {
      std::lock_guard<std::mutex> lock(ping_mutex);
      pings = std::move(ping_log); // closes the window: later pongs are ignored
    }

    if (platform.lvgl_mutex) {
      record(ST_TIME_UI_STALL, static_cast<int32_t>(stall_max_us));
    } else {
      unmeasurable(ST_TIME_UI_STALL, "no LVGL lock");
    }

    if (rtc_start && rtc_end) {
      record(ST_RTC_TICK, static_cast<int32_t>(*rtc_end - *rtc_start),
             fmt::format("over a {} ms window", ST_WINDOW_MS));
    } else {
      unmeasurable(ST_RTC_TICK, "RTC read failed");
    }

    judge_adc();
    judge_mcb(mcb);
    judge_pings(pings_sent, *pings);
  }

  void judge_adc() {
    const auto cal_centers =
        platform.joystick_cal_centers_mv ? platform.joystick_cal_centers_mv() : std::nullopt;
    record(ST_JOY_CAL, cal_centers ? 1 : 0, cal_centers ? "" : "defaults in use: press CALIBRATE");
    AdcCapture c;
    {
      std::lock_guard<std::mutex> lock(adc_mutex);
      c = adc_capture;
    }
    if (c.cycles == 0) {
      for (Id id : {ST_TIME_ADC_AVG, ST_TIME_ADC_MAX, ST_JOY_VALID, ST_JOY_X, ST_JOY_Y,
                    ST_JOY_TWIST, ST_JOY_X_NOISE, ST_JOY_Y_NOISE, ST_JOY_TWIST_NOISE, ST_JOY_X_CAL,
                    ST_JOY_Y_CAL, ST_JOY_TWIST_CAL, ST_JOY_BUTTON, ST_RTPS_ADC_HZ}) {
        unmeasurable(id, "ADC task not running");
      }
      return;
    }
    if (c.periods > 0) {
      record(ST_TIME_ADC_AVG, static_cast<int32_t>(c.period_sum_us / c.periods),
             fmt::format("{} cycles", c.cycles));
      record(ST_TIME_ADC_MAX, static_cast<int32_t>(c.period_max_us));
    } else {
      unmeasurable(ST_TIME_ADC_AVG, "one cycle only");
      unmeasurable(ST_TIME_ADC_MAX, "one cycle only");
    }
    record(ST_JOY_VALID, static_cast<int32_t>(c.valid * 100 / c.cycles),
           fmt::format("{} of {} cycles", c.valid, c.cycles));
    const Id rest[3] = {ST_JOY_X, ST_JOY_Y, ST_JOY_TWIST};
    const Id noise[3] = {ST_JOY_X_NOISE, ST_JOY_Y_NOISE, ST_JOY_TWIST_NOISE};
    const Id cal_off[3] = {ST_JOY_X_CAL, ST_JOY_Y_CAL, ST_JOY_TWIST_CAL};
    for (int axis = 0; axis < 3; ++axis) {
      if (c.valid == 0) {
        unmeasurable(rest[axis], "no valid sample");
        unmeasurable(noise[axis], "no valid sample");
        unmeasurable(cal_off[axis], "no valid sample");
        continue;
      }
      const double mean = c.sum[axis] / c.valid;
      record(rest[axis], static_cast<int32_t>(mean),
             fmt::format("{:.0f}..{:.0f} mV", c.min[axis], c.max[axis]));
      record(noise[axis], static_cast<int32_t>(c.max[axis] - c.min[axis]));
      if (cal_centers) {
        const float center = (*cal_centers)[axis];
        record(cal_off[axis], static_cast<int32_t>(std::lround(std::fabs(mean - center))),
               fmt::format("rest {:.0f}, calibrated {:.0f} mV", mean, center));
      } else {
        unmeasurable(cal_off[axis], "no saved calibration");
      }
    }
    record(ST_JOY_BUTTON, c.button_seen ? 0 : 1,
           c.button_seen ? "pressed during the at-rest capture" : "");
    if (c.published == 0) {
      unmeasurable(ST_RTPS_ADC_HZ, "no subscriber for the joystick topic");
    } else {
      record(ST_RTPS_ADC_HZ, static_cast<int32_t>(c.published * 1000 / ST_WINDOW_MS),
             fmt::format("{} samples in {} ms", c.published, ST_WINDOW_MS));
    }
  }

  void judge_mcb(const RtpsMcbStats &mcb) {
    if (mcb.samples < 2) {
      const std::string why = fmt::format("{} McbStatus in the window", mcb.samples);
      unmeasurable(ST_RTPS_MCB_PERIOD, why);
      unmeasurable(ST_RTPS_MCB_GAP, why);
      unmeasurable(ST_RTPS_MCB_LOSS, why);
      return;
    }
    const int64_t mean_us = (mcb.last_us - mcb.first_us) / (mcb.samples - 1);
    record(ST_RTPS_MCB_PERIOD, static_cast<int32_t>(mean_us / 1000),
           fmt::format("{} samples", mcb.samples));
    record(ST_RTPS_MCB_GAP, static_cast<int32_t>(mcb.max_gap_us / 1000));
    record(ST_RTPS_MCB_LOSS, static_cast<int32_t>(mcb.lost * 1000 / (mcb.samples + mcb.lost)),
           fmt::format("{} lost of {}", mcb.lost, mcb.samples + mcb.lost));
  }

  // `pings` is this window's log, already detached from the RTPS task.
  void judge_pings(uint32_t sent, PingLog &pings) {
    std::vector<uint32_t> &rtts = pings.rtt_us;
    uint32_t peer_rx = pings.peer_rx;
    const bool peer_rx_known = pings.peer_rx_known;
    if (sent == 0 || rtts.empty()) {
      const std::string why =
          sent == 0 ? "RTPS not up, or nothing subscribed to pings" : "no pong: no self-test peer";
      unmeasurable(ST_RTPS_RTT_P50, why);
      unmeasurable(ST_RTPS_RTT_P99, why);
      unmeasurable(ST_RTPS_PING_LOSS, why);
      return;
    }
    std::sort(rtts.begin(), rtts.end());
    const auto percentile = [&rtts](size_t pct) {
      return rtts[std::min(rtts.size() - 1, rtts.size() * pct / 100)];
    };
    const auto received = static_cast<uint32_t>(rtts.size());
    // the peer counts what reached it, so the two directions separate; a
    // peer_rx beyond what was sent is a count left over from an earlier run
    peer_rx = std::min(peer_rx, sent);
    const uint32_t lost_up = sent - peer_rx;
    const uint32_t lost_down = peer_rx > received ? peer_rx - received : 0;
    record(ST_RTPS_RTT_P50, static_cast<int32_t>(percentile(50)),
           fmt::format("min {} us, max {} us", rtts.front(), rtts.back()));
    record(ST_RTPS_RTT_P99, static_cast<int32_t>(percentile(99)));
    record(ST_RTPS_PING_LOSS, static_cast<int32_t>((sent - received) * 1000 / sent),
           peer_rx_known
               ? fmt::format("{} sent, {} lost out, {} lost back", sent, lost_up, lost_down)
               : fmt::format("{} sent, {} back (plain echo: no direction)", sent, received));
  }

  // Forces full-screen redraws and times each one between the display's
  // RENDER_START and RENDER_READY events - the CPU cost of drawing, without
  // the vsync wait. The frame right after an invalidation is the full one:
  // the invalidation is made under the LVGL lock, so no render is in flight.
  void check_render() {
    if (!platform.lvgl_mutex) {
      unmeasurable(ST_TIME_RENDER_AVG, "no LVGL lock");
      unmeasurable(ST_TIME_RENDER_MAX, "no LVGL lock");
      return;
    }
    // Measured with the overlay hidden: it is a full-screen opaque panel of
    // recoloured text, and LVGL still draws the screen beneath it, so timing
    // with it up measures this test rather than the UI.
    render_banner(true);
    uint64_t total_us = 0;
    uint32_t max_us = 0;
    uint32_t frames = 0;
    for (int i = 0; i < ST_RENDER_FRAMES; ++i) {
      uint32_t before = 0;
      {
        std::lock_guard<std::recursive_mutex> lock(*platform.lvgl_mutex);
        before = render_frames.load();
        lv_obj_invalidate(lv_screen_active());
        lv_obj_invalidate(lv_layer_top());
      }
      const int64_t deadline = esp_timer_get_time() + 500 * 1000;
      while (render_frames.load() == before && esp_timer_get_time() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(2));
      }
      if (render_frames.load() == before) {
        continue; // no frame came; counted by what is missing from `frames`
      }
      const uint32_t us = render_last_us.load();
      total_us += us;
      max_us = std::max(max_us, us);
      frames++;
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    render_banner(false);
    if (frames == 0) {
      unmeasurable(ST_TIME_RENDER_AVG, "no frame rendered");
      unmeasurable(ST_TIME_RENDER_MAX, "no frame rendered");
      return;
    }
    record(ST_TIME_RENDER_AVG, static_cast<int32_t>(total_us / frames),
           fmt::format("{} of {} frames", frames, ST_RENDER_FRAMES));
    record(ST_TIME_RENDER_MAX, static_cast<int32_t>(max_us));
  }

  void check_memory() {
    constexpr uint32_t kInternal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const auto kb = [](size_t bytes) { return static_cast<int32_t>(bytes / 1024); };
    record(ST_MEM_INT_FREE, kb(heap_caps_get_free_size(kInternal)));
    record(ST_MEM_INT_MIN, kb(heap_caps_get_minimum_free_size(kInternal)),
           fmt::format("{} KB before this run", kb(int_min_before_)));
    record(ST_MEM_INT_BLOCK, kb(heap_caps_get_largest_free_block(kInternal)));
    record(ST_MEM_DMA_FREE, kb(heap_caps_get_free_size(MALLOC_CAP_DMA)));
    // In bytes, not KB: the question is whether one Ethernet frame's bounce
    // buffer would still fit, and rounding down to KB would fail a block that
    // is big enough.
    record(ST_MEM_DMA_MIN, static_cast<int32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_DMA)),
           fmt::format("{} B before this run", dma_min_before_));
    record(ST_MEM_DMA_BLOCK,
           static_cast<int32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
    record(ST_MEM_PSRAM_FREE, kb(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    record(ST_MEM_HEAP_OK, heap_caps_check_integrity_all(true) ? 1 : 0);
    // (No LVGL pool check: this build's LVGL allocates from the system heap,
    // so it is already inside the figures above, and lv_mem_monitor reports
    // nothing.)

    check_stack(ST_MEM_STK_LVGL, "lv_task");
    check_stack(ST_MEM_STK_ADC, "Read ADC");
    check_stack(ST_MEM_STK_RTPS, "rtps_pub");
  }

  void check_stack(Id id, const char *task_name) {
    TaskHandle_t task = xTaskGetHandle(task_name);
    if (task == nullptr) {
      unmeasurable(id, fmt::format("no task named '{}'", task_name));
      return;
    }
    // ESP-IDF reports the high-water mark in bytes, not words
    record(id, static_cast<int32_t>(uxTaskGetStackHighWaterMark(task)));
  }

  // --- output -----------------------------------------------------------------

  void finish(int64_t elapsed_ms) {
    int32_t pass = 0;
    int32_t fail = 0;
    int32_t skip = 0;
    for (const Outcome &o : out_) {
      pass += o.result == RAMMP_SELFTEST_RESULT_PASS;
      fail += o.result == RAMMP_SELFTEST_RESULT_FAIL;
      skip += o.result == RAMMP_SELFTEST_RESULT_SKIP;
    }
    const bool passed = fail == 0;

    fmt::print("\n[SELFTEST] ===== run {} ({}) - firmware {} =====\n", run_id_,
               remote_ ? "remote" : "local", version_);
    fmt::print("[SELFTEST] {:<4}  {:<18} {:>14}  {:<20} {}\n", "", "check", "measured", "limit",
               "detail");
    for (uint8_t i = 0; i < ST_COUNT; ++i) {
      const Spec &s = kSpec[i];
      const Outcome &o = out_[i];
      fmt::print("[SELFTEST] {:<4}  {:<18} {:>14}  {:<20} {}\n", result_tag(o.result), s.name,
                 o.measured ? format_value(s, o.value) : "-", format_limits(s),
                 o.detail.empty() ? s.desc : o.detail);
    }
    fmt::print("[SELFTEST] ===== {} - {} pass, {} fail, {} skip in {}.{} s =====\n",
               passed ? "PASS" : "FAIL", pass, fail, skip, elapsed_ms / 1000,
               (elapsed_ms % 1000) / 100);
    // one machine-readable line, for anything scraping the serial log
    fmt::print("SELFTEST_SUMMARY run={} trigger={} result={} pass={} fail={} skip={} ms={} fw={}\n",
               run_id_, remote_ ? "remote" : "local", passed ? "PASS" : "FAIL", pass, fail, skip,
               elapsed_ms, version_);

    const std::string timing = fmt::format("{} ms", elapsed_ms);
    publish_marker(RAMMP_SELFTEST_KIND_FINISHED, pass, fail, skip, timing);
    // The report topic is best-effort: send it all once more so a reader that
    // dropped a sample can complete the table (it de-duplicates on index).
    for (uint8_t i = 0; i < ST_COUNT; ++i) {
      publish_result(i);
    }
    publish_marker(RAMMP_SELFTEST_KIND_FINISHED, pass, fail, skip, timing);

    status(fmt::format("#{:06x} {}#  {} pass, {} fail, {} skip ({}.{} s)\n"
                       "Tap, or press the stick button, to close",
                       passed ? kColourPass : kColourFail, passed ? "ALL PASSED" : "FAILED", pass,
                       fail, skip, elapsed_ms / 1000, (elapsed_ms % 1000) / 100));
    ui_refresh();
    log_overlay_layout();
    ui_finished = true;
    logger.info("run {} finished: {} ({} pass, {} fail, {} skip)", run_id_,
                passed ? "PASS" : "FAIL", pass, fail, skip);
  }

  static const char *result_tag(uint8_t result) {
    switch (result) {
    case RAMMP_SELFTEST_RESULT_PASS:
      return "PASS";
    case RAMMP_SELFTEST_RESULT_FAIL:
      return "FAIL";
    default:
      return "SKIP";
    }
  }

  // A symbol rather than the word: on a two-column 720 px screen each column is
  // ~340 px, and "PASS " cost a row a sixth of that.
  static const char *result_symbol(uint8_t result) {
    switch (result) {
    case RAMMP_SELFTEST_RESULT_PASS:
      return LV_SYMBOL_OK;
    case RAMMP_SELFTEST_RESULT_FAIL:
      return LV_SYMBOL_CLOSE;
    default:
      return LV_SYMBOL_MINUS;
    }
  }

  std::string row_text(uint8_t i) const {
    const Spec &s = kSpec[i];
    const Outcome &o = out_[i];
    if (!o.done) {
      return fmt::format("#{:06x} .  {}#", kColourPending, s.name);
    }
    const uint32_t colour = o.result == RAMMP_SELFTEST_RESULT_PASS   ? kColourPass
                            : o.result == RAMMP_SELFTEST_RESULT_FAIL ? kColourFail
                                                                     : kColourSkip;
    return fmt::format("#{:06x} {}# {} {}", colour, result_symbol(o.result), s.name,
                       o.measured ? compact_value(s, o.value) : "-");
  }

  // --- overlay ----------------------------------------------------------------

  void ui_begin() {
    if (!ui_ready || !platform.lvgl_mutex) {
      return;
    }
    ui_finished = false;
    std::lock_guard<std::recursive_mutex> lock(*platform.lvgl_mutex);
    create_overlay_locked();
    lv_subject_copy_string(&ui_title_subject,
                           fmt::format("R&D SELF TEST  -  run {} ({})\n{}", run_id_,
                                       remote_ ? "remote" : "local", version_)
                               .c_str());
    refresh_columns_locked();
    lv_subject_set_int(&ui_visible_subject, 1);
    ui_shown = true;
  }

  // The render check's swap: the panel hides so the redraws timed are the UI's
  // own, and the blinking banner shows in its place so that does not look like
  // a crash. ui_shown stays set throughout, so the joystick stays with the test.
  static void render_banner(bool on) {
    if (!ui_ready || !platform.lvgl_mutex) {
      return;
    }
    std::lock_guard<std::recursive_mutex> lock(*platform.lvgl_mutex);
    lv_subject_set_int(&ui_visible_subject, on ? 0 : 1);
    lv_subject_set_int(&ui_banner_subject, on ? 1 : 0);
  }

  void status(const std::string &text) {
    logger.info("{}", text);
    if (!ui_ready || !platform.lvgl_mutex) {
      return;
    }
    std::lock_guard<std::recursive_mutex> lock(*platform.lvgl_mutex);
    lv_subject_copy_string(&ui_status_subject, text.c_str());
  }

  void ui_refresh() const {
    if (!ui_ready || !platform.lvgl_mutex) {
      return;
    }
    std::lock_guard<std::recursive_mutex> lock(*platform.lvgl_mutex);
    refresh_columns_locked();
  }

  // The overlay's measured geometry, to the serial log: nobody watching the
  // log can see the screen, and a clipped or off-screen layout is otherwise
  // invisible from here.
  static void log_overlay_layout() {
    if (!ui_ready || !platform.lvgl_mutex) {
      return;
    }
    std::lock_guard<std::recursive_mutex> lock(*platform.lvgl_mutex);
    if (overlay_panel == nullptr) {
      return;
    }
    lv_obj_update_layout(overlay_panel);
    lv_display_t *display = lv_display_get_default();
    const int32_t width = lv_display_get_horizontal_resolution(display);
    const int32_t height = lv_display_get_vertical_resolution(display);
    lv_obj_t *rows = lv_obj_get_child(overlay_panel, -1);
    int32_t column_width = 0;
    int32_t widest = 0;
    int32_t bottom = 0;
    for (uint32_t i = 0; i < lv_obj_get_child_count(rows); ++i) {
      lv_obj_t *column = lv_obj_get_child(rows, static_cast<int32_t>(i));
      lv_obj_t *label = lv_obj_get_child(column, 0);
      lv_area_t area;
      lv_obj_get_coords(label, &area);
      column_width = lv_obj_get_width(column);
      widest = std::max(widest, lv_obj_get_width(label));
      bottom = std::max(bottom, area.y2);
    }
    logger.info("overlay: {}x{} display, {} columns of {} px, widest rows {} px{}, last row ends "
                "at y={}{}",
                width, height, ui_columns, column_width, widest,
                widest > column_width ? " (CLIPPED)" : "", bottom,
                bottom >= height ? " (OFF SCREEN)" : "");
  }

  void refresh_columns_locked() const {
    for (int column = 0; column < ui_columns; ++column) {
      std::string text;
      for (int row = 0; row < ui_rows_per_column; ++row) {
        const int i = column * ui_rows_per_column + row;
        if (i >= ST_COUNT) {
          break;
        }
        text += row_text(static_cast<uint8_t>(i));
        text += '\n';
      }
      lv_subject_copy_string(&ui_column_subject[column], text.c_str());
    }
  }

  uint8_t run_id_;
  bool remote_;
  const char *version_ = "";
  size_t int_min_before_ = 0;
  size_t dma_min_before_ = 0;
  uint32_t log_count_before_ = 0;
  std::array<Outcome, ST_COUNT> out_{};
};

void selftest_task(void *) {
  {
    // on the heap rather than this task's stack: the results table alone is a
    // couple of KB, and it is big enough to land in PSRAM
    auto run = std::make_unique<Run>(pending_run_id.load(), pending_remote.load());
    run->execute();
  }
  logger.info("self-test task stack headroom: {} of {} bytes unused",
              static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr)), kTaskStackBytes);
  running = false;
  // WithCaps task: vTaskDelete would leak the PSRAM stack
  vTaskDeleteWithCaps(nullptr);
}

/////////////////////////////////////////////////////////////////////////////
// Overlay construction
/////////////////////////////////////////////////////////////////////////////

char *alloc_text(size_t size) {
  // PSRAM: this text only exists for a screen that is up a few times a day
  auto *buf = static_cast<char *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM));
  if (buf != nullptr) {
    buf[0] = '\0';
  }
  return buf;
}

lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, int32_t width) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_width(label, width);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_label_set_recolor(label, true);
  return label;
}

// A plain layout box: no style, and neither clickable nor scrollable, so a tap
// anywhere on the overlay falls through to the panel's dismiss handler.
lv_obj_t *make_box(lv_obj_t *parent, int32_t width) {
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_set_size(box, width, LV_SIZE_CONTENT);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  return box;
}

void overlay_clicked_cb(lv_event_t *) { selftest_ui_dismiss(); }

// The overlay's subjects and their text buffers, once at boot. The widgets
// that bind to them come and go with each run (create_overlay_locked).
bool prepare_overlay() {
  const int32_t width = lv_display_get_horizontal_resolution(lv_display_get_default());
  ui_columns = width >= 1100 ? 3 : 2;
  ui_rows_per_column = (ST_COUNT + ui_columns - 1) / ui_columns;
  char *title = alloc_text(kTitleLen);
  char *status = alloc_text(kStatusLen);
  std::array<char *, kMaxColumns> columns{};
  std::generate(columns.begin(), columns.end(), [] { return alloc_text(kColumnLen); });
  if (!title || !status ||
      std::any_of(columns.begin(), columns.end(), [](char *c) { return !c; })) {
    logger.error("no PSRAM for the overlay text; results go to serial and RTPS only");
    return false;
  }
  lv_subject_init_int(&ui_visible_subject, 0);
  lv_subject_init_int(&ui_banner_subject, 0);
  lv_subject_init_string(&ui_title_subject, title, nullptr, kTitleLen, "");
  lv_subject_init_string(&ui_status_subject, status, nullptr, kStatusLen, "");
  for (int i = 0; i < kMaxColumns; ++i) {
    lv_subject_init_string(&ui_column_subject[i], columns[i], nullptr, kColumnLen, "");
  }

  return true;
}

void banner_blink_exec(void *var, int32_t value) {
  lv_obj_set_style_opa(static_cast<lv_obj_t *>(var), static_cast<lv_opa_t>(value), LV_PART_MAIN);
}

// Blinks the banner's text while the banner is up. A hard 1 Hz on/off rather
// than a fade: a fade redraws the strip every frame, and those frames would
// land in the very redraw timings the banner is announcing. The animation's
// var is the label, so LVGL deletes it with the label.
void banner_blink_observer(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *label = lv_observer_get_target_obj(observer);
  lv_anim_delete(label, banner_blink_exec);
  lv_obj_set_style_opa(label, LV_OPA_COVER, LV_PART_MAIN);
  if (lv_subject_get_int(subject) == 0) {
    return;
  }
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, label);
  lv_anim_set_exec_cb(&a, banner_blink_exec);
  lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_duration(&a, 500);
  lv_anim_set_reverse_duration(&a, 500);
  lv_anim_set_path_cb(&a, lv_anim_path_step);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
}

void create_overlay_locked() {
  if (overlay_panel != nullptr) {
    return;
  }
  // On the top layer, so it covers whichever screen is up - a remote run can
  // arrive on any of them.
  lv_obj_t *panel = lv_obj_create(lv_layer_top());
  overlay_panel = panel;
  lv_obj_remove_style_all(panel);
  lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x0B0F14), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE); // also swallows touches meant for below
  lv_obj_add_event_cb(panel, overlay_clicked_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_bind_flag_if_eq(panel, &ui_visible_subject, LV_OBJ_FLAG_HIDDEN, 0);

  // A column of title, status and rows, so a title or status that wraps
  // pushes the rows down instead of drawing over them.
  constexpr int32_t kMargin = 16;
  lv_obj_set_style_pad_all(panel, kMargin, LV_PART_MAIN);
  lv_obj_set_style_pad_row(panel, 12, LV_PART_MAIN);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_label_bind_text(make_label(panel, &lv_font_montserrat_34, LV_PCT(100)), &ui_title_subject,
                     nullptr);
  lv_label_bind_text(make_label(panel, &lv_font_montserrat_24, LV_PCT(100)), &ui_status_subject,
                     nullptr);

  // One box per column, each exactly its share of the width. The label inside
  // sizes to its text and never wraps; the box clips it. A row too long for
  // its column is therefore cut short instead of wrapping onto a second line,
  // which would push every row beneath it out of line with the other column.
  const int32_t width = lv_display_get_horizontal_resolution(lv_display_get_default());
  const int32_t column_width = (width - 2 * kMargin) / ui_columns;
  lv_obj_t *rows = make_box(panel, LV_PCT(100));
  lv_obj_set_flex_flow(rows, LV_FLEX_FLOW_ROW);
  for (int i = 0; i < ui_columns; ++i) {
    lv_obj_t *column = make_box(rows, column_width);
    lv_label_bind_text(make_label(column, &lv_font_montserrat_24, LV_SIZE_CONTENT),
                       &ui_column_subject[i], nullptr);
  }

  // The render-check banner. A transparent full-screen layer that swallows
  // touches - the real screen shows through while the panel is hidden, but is
  // not to be used mid-test - with a strip across the top. Transparent costs
  // nothing to draw and the strip is small, so the redraws being timed are
  // still essentially the UI's own.
  lv_obj_t *guard = lv_obj_create(lv_layer_top());
  overlay_banner = guard;
  lv_obj_remove_style_all(guard);
  lv_obj_set_size(guard, LV_PCT(100), LV_PCT(100));
  lv_obj_add_flag(guard, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(guard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_bind_flag_if_eq(guard, &ui_banner_subject, LV_OBJ_FLAG_HIDDEN, 0);
  lv_obj_t *strip = make_box(guard, LV_PCT(100));
  lv_obj_set_style_bg_color(strip, lv_color_hex(0x0B0F14), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_all(strip, kMargin, LV_PART_MAIN);
  lv_obj_t *banner = make_label(strip, &lv_font_montserrat_34, LV_PCT(100));
  lv_obj_set_style_text_color(banner, lv_color_hex(kColourSkip), LV_PART_MAIN);
  lv_obj_set_style_text_align(banner, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_text(banner, "Timing screen redraws...");
  lv_subject_add_observer_obj(&ui_banner_subject, banner_blink_observer, banner, nullptr);
}

} // namespace

void selftest_init(SelfTestPlatform config) {
  platform = std::move(config);
  ui_ready = prepare_overlay();
  lv_display_t *display = lv_display_get_default();
  lv_display_add_event_cb(display, render_start_cb, LV_EVENT_RENDER_START, nullptr);
  lv_display_add_event_cb(display, render_ready_cb, LV_EVENT_RENDER_READY, nullptr);
  rtps_comms_on_selftest_pong(on_pong);
  rtps_comms_on_selftest_run([](uint8_t run_id) {
    if (run_id != 0) {
      selftest_request(SelfTestTrigger::REMOTE, run_id);
    }
  });
  logger.info("ready: {} checks (selftest_spec.h)", static_cast<int>(ST_COUNT));
}

bool selftest_request(SelfTestTrigger trigger, uint8_t run_id) {
  const bool remote = trigger == SelfTestTrigger::REMOTE;
  if (remote && last_remote_run_id.load() == run_id) {
    return false; // a resend of a command already acted on
  }
  bool expected = false;
  if (!running.compare_exchange_strong(expected, true)) {
    logger.warn("run {} ignored: a self test is already running", run_id);
    return false;
  }
  if (remote) {
    last_remote_run_id = run_id;
  }
  pending_run_id = remote ? run_id : 0;
  pending_remote = remote;
  // core 0: the LVGL task has core 1, and the render check needs it free
  if (xTaskCreatePinnedToCoreWithCaps(selftest_task, "selftest", kTaskStackBytes, nullptr,
                                      kTaskPriority, nullptr, 0, MALLOC_CAP_SPIRAM) != pdPASS) {
    logger.error("could not create the self-test task");
    running = false;
    return false;
  }
  return true;
}

void selftest_note_adc(bool valid, float x_mv, float y_mv, float twist_mv, bool published,
                       bool button_pressed) {
  if (!adc_armed.load(std::memory_order_relaxed)) {
    return;
  }
  const int64_t now = esp_timer_get_time();
  std::lock_guard<std::mutex> lock(adc_mutex);
  AdcCapture &c = adc_capture;
  if (c.last_us != 0) {
    const int64_t period = now - c.last_us;
    c.period_sum_us += period;
    c.period_max_us = std::max(c.period_max_us, period);
    c.periods++;
  }
  c.last_us = now;
  c.cycles++;
  c.published += published ? 1 : 0;
  c.button_seen = c.button_seen || button_pressed;
  if (!valid) {
    return;
  }
  const float mv[3] = {x_mv, y_mv, twist_mv};
  for (int axis = 0; axis < 3; ++axis) {
    if (c.valid == 0) {
      c.min[axis] = mv[axis];
      c.max[axis] = mv[axis];
    }
    c.min[axis] = std::min(c.min[axis], mv[axis]);
    c.max[axis] = std::max(c.max[axis], mv[axis]);
    c.sum[axis] += mv[axis];
  }
  c.valid++;
}

bool selftest_ui_visible() { return ui_shown.load(); }

void selftest_ui_dismiss() {
  if (!ui_ready || !ui_finished.load()) {
    return;
  }
  lv_subject_set_int(&ui_visible_subject, 0);
  lv_subject_set_int(&ui_banner_subject, 0);
  ui_shown = false;
  // async: this is often called from the panel's own click handler
  if (overlay_panel != nullptr) {
    lv_obj_delete_async(overlay_panel);
    overlay_panel = nullptr;
  }
  if (overlay_banner != nullptr) {
    lv_obj_delete_async(overlay_banner);
    overlay_banner = nullptr;
  }
}
