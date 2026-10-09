/**
 * @file main.cpp
 * @brief The HMI's app_main: brings up the Tab5 board, builds the components from their
 *        Configs and starts them (CS-LAY-01). The UI island is hmi_ui's UiApp; the ports it
 *        reaches the rest of the firmware through (rtps_comms, the drive adapter, the cues, the
 *        self test, the board, the screens main keeps) are filled here.
 */

#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "m5stack-tab5.hpp"

#include "drv2605.hpp"
#include "feedback/da7280_bench.hpp"
#include "feedback/feedback.hpp"
#include "housekeeping/housekeeping.hpp"
#include "housekeeping/system_clock.hpp"

#include "simple_lowpass_filter.hpp"

#include "ui.h"

#include "button.hpp"
#include "joystick.hpp"
#include "keypad_input.hpp"

#include "about_ui.hpp"
#include "bench_verbs.hpp"
#include "board/board.hpp"
#include "board/board_port.hpp"
#include "boot_logo.h"
#include "drive_adapter.hpp"
#include "drive_session.hpp"
#include "fw_info.hpp"
#include "github_ota.hpp"
#include "internet_ui.hpp"
#include "joystick_cal.hpp"
#include "log_capture.hpp"
#include "log_view.hpp"
#include "remote_ui.hpp"
#include "rtps_comms.hpp"
#include "selftest.hpp"
#include "selftest_platform.hpp"
#include "settings.hpp"
#include "settings_applied.hpp"
#include "storage.hpp"
#include "update_ui.hpp"

#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "drive_ui/drive_port.hpp"
#include "hmi_format/topbar.hpp"
#include "hmi_ui/app_state.hpp"
#include "hmi_ui/fps_meter.hpp"
#include "hmi_ui/post_stage.hpp"
#include "hmi_ui/ui_app.hpp"
#include "hmi_ui/ui_build.hpp"
#include "hmi_ui/ui_island.hpp"

using namespace std::chrono_literals;

static std::recursive_mutex lvgl_mutex;

// ---------------------------------------------------------------------------
// Frame-rate instrumentation: a debug feature, off unless CONFIG_HMI_DEBUG_FPS
// (CS-LAY-09), and never in sdkconfig.defaults, which ships.
//
// Reports over serial rather than the LVGL perf overlay, so throughput can be
// measured without eyes on the panel. RENDER_START/RENDER_READY fire only when
// LVGL actually rasterizes, so these numbers are real frame cost -- REFR_*
// would tick even on an idle screen and read as a meaningless "infinite fps".
//
// kFpsStress forces a full-screen invalidation every LVGL cycle. Without it a
// static SquareLine screen invalidates nothing and renders nothing, which
// measures the redraw path not at all. With it we get sustained worst case
// (CONFIG_HMI_DEBUG_FPS_STRESS).
//
// The once-a-second report goes through its own espp::Logger at debug level,
// tag "fps" (CS-LOG-01/03): developer detail, and only in this debug build.
// The counters are hmi::ui::FpsMeter's members.
// ---------------------------------------------------------------------------
static constexpr bool kFpsInstrument = CONFIG_HMI_DEBUG_FPS_AS_INT != 0;
static constexpr bool kFpsStress = CONFIG_HMI_DEBUG_FPS_STRESS_AS_INT != 0;

// The one meter. Attached and reported only when kFpsInstrument.
static constinit hmi::ui::FpsMeter fps_meter;

// rtps_comms' RtpsLinkState is what the UI's link subject carries, as hmi::ui::LinkState.
static constexpr bool same_link_state(RtpsLinkState a, hmi::ui::LinkState b) {
  return static_cast<int32_t>(a) == static_cast<int32_t>(b);
}
static_assert(same_link_state(RtpsLinkState::NET_FAILED, hmi::ui::LinkState::NET_FAILED));
static_assert(same_link_state(RtpsLinkState::LINK_DOWN, hmi::ui::LinkState::LINK_DOWN));
static_assert(same_link_state(RtpsLinkState::NO_IP, hmi::ui::LinkState::NO_IP));
static_assert(same_link_state(RtpsLinkState::NO_PEER, hmi::ui::LinkState::NO_PEER));
static_assert(same_link_state(RtpsLinkState::CONNECTED, hmi::ui::LinkState::CONNECTED));

// The user's haptic and sound cues (hmi::feedback::Feedback). app_main builds
// them (app-main-shrink V6) and points this at them before any task starts; the
// rest of the unit cues through haptic_play below, and through play_click,
// play_refusal and refusal_feedback (after app_main, with load_audio).
static hmi::feedback::Feedback *feedback = nullptr;
using hmi::feedback::kHapticBuzzSlots;
// The self test reports whether the DA7280 answered the boot scan.
using hmi::feedback::kDa7280Address;

// HAPTIC TEST settings row -> kHapticBuzzDuration of vibration; the unlock and
// hold clicks; a refusal's double click. Returns false (having logged) if there
// is no motor or the I2C burst fails, so callers can leave their own UI alone.
static bool haptic_play(espp::Drv2605::Waveform w, uint8_t slots) {
  return feedback != nullptr && feedback->haptics().play(w, slots);
}

// How the click sound reaches the speaker and the LVGL task, for app_main's Feedback.
static hmi::feedback::ClickSound::Config click_sound_config() {
  return {
      .sounds_on = applied_settings.sounds_on(),
      .play =
          [](std::span<const uint8_t> samples) { espp::M5StackTab5::get().play_audio(samples); },
      .now_ms = [] { return lv_tick_get(); },
      // The second click of a refusal: a one-shot LVGL timer, so it runs on the
      // LVGL task like the first.
      .click_later =
          [](uint32_t delay_ms, hmi::feedback::ClickSound &sound) {
            lv_timer_t *second = lv_timer_create(
                [](lv_timer_t *timer) {
                  static_cast<hmi::feedback::ClickSound *>(lv_timer_get_user_data(timer))->click();
                },
                delay_ms, &sound);
            lv_timer_set_repeat_count(second, 1);
          },
  };
}

static bool load_audio(size_t &out_size, size_t &out_sample_rate);
static void play_click(espp::M5StackTab5 &tab5);
// "Can't do that", heard (play_refusal) or heard and felt (refusal_feedback);
// defined with play_click. A warning or error sounds even with Sounds off.
static void play_refusal(bool warning = false);
static void refusal_feedback();

// --
// AXIS WIRING: the gimbal pots are cross-wired relative to the channel names.
// ADC1_CH1 (GPIO17) is the HORIZONTAL axis and reads higher to the right, so
// it feeds the joystick's X with no inversion. ADC1_CH0 (GPIO16) is the
// VERTICAL axis and reads *lower* moving up, so it feeds Y with invert_output
// — after which +Y is up, as the rest of the code assumes. Twist reads higher
// clockwise. Fixed in hmi::stick (components/stick: horizontal_config,
// vertical_config, twist_config) so every consumer (UI bars, the keypad, RTPS)
// sees correct axes without compensating itself; joystick_cal.cpp's step
// prompts rely on the same directions.
//
// The stick math itself (mapping, mount, keys, gate, speed scale) is
// hmi::stick::StickPipeline. The ADC task runs one cycle of it per sample and
// AdcStickIo below is everything that cycle reads and writes here, in the order
// the code ran inline before it was extracted. stick_inject.hpp includes it, and
// adds StickSlot: the pipeline, or with CONFIG_HMI_BENCH_STICK_INJECT the bench
// stick injection in front of it.
#include "control/stick_island.hpp"
#include "post/rest_window.hpp"
#include "stick/output_permit.hpp"
#include "stick_inject.hpp"

static_assert(hmi::stick::SENSITIVITY_MIN == SETTINGS_STICK_SENSITIVITY_MIN &&
                  hmi::stick::SENSITIVITY_MAX == SETTINGS_STICK_SENSITIVITY_MAX,
              "hmi::stick clamps the stick sensitivity to the Settings range");
static_assert(hmi::stick::DRIVE_SPEED_MIN == SETTINGS_DRIVE_SPEED_MIN &&
                  hmi::stick::DRIVE_SPEED_MAX == SETTINGS_DRIVE_SPEED_MAX,
              "hmi::stick clamps the drive speed to the Settings range");

static hmi::stick::CalibrationMv stick_cal_mv(const JoystickCal &cal) {
  const auto axis = [](const JoystickAxisCal &a) {
    return hmi::stick::AxisCalMv{.min_mv = a.min_mv, .center_mv = a.center_mv, .max_mv = a.max_mv};
  };
  return {.horizontal = axis(cal[JOY_HORIZONTAL]),
          .vertical = axis(cal[JOY_VERTICAL]),
          .twist = axis(cal[JOY_TWIST])};
}

// How far each axis travels is measured per unit (joystick_cal.cpp, the CALIBRATE button on
// the JoystickTest screen); the dead zones are the stick's (hmi::stick::pipeline_config).
static hmi::stick::StickPipeline::Config stick_pipeline_config(const JoystickCal &cal) {
  return hmi::stick::pipeline_config(
      stick_cal_mv(cal),
      {.up = LV_KEY_UP, .down = LV_KEY_DOWN, .right = LV_KEY_RIGHT, .left = LV_KEY_LEFT});
}

// The ADC side's clock (hazard-fixes.md §10 item 22): one uint32 ms count, from this one
// function, for the output permit, C4's motion guard and its writers (the UI heartbeat, the
// MibStatus stamp), and later C2's monitor. Durations on it are taken modulo 2^32.
static uint32_t adc_clock_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// The ADC task's side of StickPipeline::cycle. Runs on the ADC task only; every
// member is what the stick block of adc_task_fn did inline before.
struct AdcStickIo {
  // What the ADC task keeps across cycles beside the pipeline: app_main's, handed to the
  // island at start.
  struct State {
    hmi::stick::OutputPermit permit; // the output permit and its neutral latch (C1 §3)
    hmi::post::RestWindow rest;      // the POST's rest window (C3 §2.2)
    hmi::fw::Writer<hmi::ui::RestWindowMailbox> windows; // to the POST runner (UI task)
    hmi::post::RestWindowMsg window{}; // the window being sent: here, not on the task's stack
    hmi::stick::PostGate post_seen = hmi::stick::PostGate::NOT_RUN; // the gate, last cycle
  };

  espp::SimpleLowpassFilter &twist_lowpass;
  State &state;

  // Every cycle, valid or not, before the pipeline: the reads it gets (after the bench
  // injection). An invalid cycle restarts the permit's neutral wait (REQ-STK-12).
  void note_reads(const hmi::stick::RawReadsMv &reads) {
    if (!(reads.horizontal_mv && reads.vertical_mv && reads.twist_mv)) {
      state.permit.invalid_cycle();
    }
    // A failed read goes over as NaN, which the rest window counts as a failed read: three
    // floats in registers, so the task's frame holds no copy of the reads.
    constexpr float FAILED = std::numeric_limits<float>::quiet_NaN();
    feed_rest_window(state, reads.horizontal_mv.value_or(FAILED),
                     reads.vertical_mv.value_or(FAILED), reads.twist_mv.value_or(FAILED),
                     joy_button_pressed.load());
  }
  // Defined after the UiApp, which owns the POST gate.
  [[gnu::noinline]] static inline void feed_rest_window(State &state, float horizontal_mv,
                                                        float vertical_mv, float twist_mv,
                                                        bool button);

  // A calibration run just finished: the pipeline switches to it between two
  // samples, on the task that owns the stick.
  std::optional<hmi::stick::CalibrationMv> take_new_calibration() {
    if (auto cal = joystick_cal_take_new()) {
      return stick_cal_mv(*cal);
    }
    return std::nullopt;
  }
  float smooth_twist_mv(float twist_mv) { return twist_lowpass(twist_mv); }
  void note_raw_mv(float horizontal_mv, float vertical_mv, float twist_mv) {
    joystick_cal_note_raw(horizontal_mv, vertical_mv, twist_mv);
  }
  // While a calibration run owns the stick nothing downstream may act on it:
  // the user is being told to push it to every end in turn.
  bool calibrating() { return joystick_cal_running(); }
  bool swap() { return applied_settings.stick_swap(); }
  bool invert_x() { return applied_settings.stick_invert_x(); }
  bool invert_y() { return applied_settings.stick_invert_y(); }
  int sensitivity() { return applied_settings.stick_sensitivity(); }
  uint32_t joy_key() { return ::joy_key.load(); }
  uint32_t remote_key() { return ::remote_key.load(); }
  void set_joy_key(uint32_t key) { ::joy_key.store(key); }
  void set_joy_flick(uint32_t key) { joy_flick.store(key); }
  void show(const hmi::stick::Position &mounted) {
    // lv_subject_set_int runs the bar's observer callback synchronously,
    // which touches the widget, so it needs the LVGL lock. Tried, not
    // waited for: the UI holds it for a whole frame (a full redraw is
    // ~100 ms, more with Flip screen), and the stick's path to the MCB
    // must not queue behind a render. A busy UI just gets the bars
    // one cycle later.
    std::unique_lock<std::recursive_mutex> lock(lvgl_mutex, std::try_to_lock);
    if (lock.owns_lock()) {
      lv_subject_set_int(joystick_view.x(), static_cast<int32_t>(mounted.x * 100.0f));
      lv_subject_set_int(joystick_view.y(), static_cast<int32_t>(mounted.y * 100.0f));
      lv_subject_set_int(joystick_view.twist(), static_cast<int32_t>(mounted.twist * 100.0f));
    }
  }
  int drive_speed() { return applied_settings.drive_speed(); }
  // The output permit (hazard-c1-spec.md §3): the stick drives only with every condition
  // met, in the hold-reason order, else the MCB gets a literal 0 and XYTwist keeps flowing.
  // 1 the gate: the stick may drive only from Drive with the menu shut (`stick_drives`); a
  //   push meant for the next row must never move the chair. The bars keep moving.
  // 2 C4's motion guard: not fitted until C4, so OK (decision D2).
  // 3 no calibration run (the user is pushing it to every end in turn); 4 a measured
  //   calibration (G5); 5 POST passed (G3, C3); 6, 7 stick health (C2; NOT_MONITORED passes
  //   until then, D2); 8 the stick centred for kNeutralHold since the last hold (G1).
  // The first failing one goes to the Drive notice (hold_reason). Lock-free reads only.
  // Defined after the UiApp, which owns the permit's channels.
  hmi::stick::Permit output_permit(const hmi::stick::Position &mounted);
  bool button_pressed() { return joy_button_pressed.load(); }
  // The MCB gets the same calibrated -1..+1 values the bars show (+Y forward,
  // deadzones applied), scaled by Settings "Speed sensitivity" (0 when held), so
  // it needs no calibration of its own. Quiet no-op until RTPS is up and a
  // subscriber is discovered.
  bool publish(const hmi::stick::Command &command, bool button) {
    return rtps_comms_publish_adc(command.x, command.y, command.twist,
                                  button ? rammp::Buttons::JOYSTICK : rammp::Buttons::NONE);
  }
  // After every cycle, valid or not: the self test measures the loop's cadence and
  // how often a read fails, as well as the values. A no-op unless a run is
  // capturing.
  void note_cycle(bool valid, float horizontal_mv, float vertical_mv, float twist_mv,
                  bool published) {
    selftest_note_adc(valid, horizontal_mv, vertical_mv, twist_mv, published,
                      joy_button_pressed.load());
  }
};

// Every change of the link state goes to the serial log, and so to the
// LogScreen: the TopBar shows where the link is now, the log keeps when it
// changed. Warnings on the way down, info on the way up.
static void log_link_change(RtpsLinkState state) {
  static espp::Logger link_logger({.tag = "rtps_link", .level = espp::Logger::Verbosity::INFO});
  static std::optional<RtpsLinkState> last;
  if (last == state) {
    return;
  }
  const std::string meaning = rtps_comms_link_state_meaning(state);
  if (!last) {
    link_logger.info("{} ({})", rtps_comms_link_state_name(state), meaning);
  } else if (state > *last) {
    link_logger.info("{} -> {} ({})", rtps_comms_link_state_name(*last),
                     rtps_comms_link_state_name(state), meaning);
  } else {
    link_logger.warn("{} -> {} ({})", rtps_comms_link_state_name(*last),
                     rtps_comms_link_state_name(state), meaning);
  }
  last = state;
}

// The TopBar's link label: what RTPS runs over. The design's "BT · WI-FI" is only
// a placeholder; it is set from the setting before rtps_comms_start, then from the
// link it brought up (WiFi with no network known runs Ethernet).
// NetLink -> hmi_format's Link (REQ-FMT-07): anything but WiFi reads Ethernet.
static const char *link_text(NetLink link) {
  return hmi::format::link_text(link == NetLink::WIFI ? hmi::format::Link::WIFI
                                                      : hmi::format::Link::ETHERNET);
}

// The Restart HMI tile.
static void action_restart_hmi() {
  static espp::Logger action_logger({.tag = "actions", .level = espp::Logger::Verbosity::INFO});
  action_logger.warn("restart requested from the actions screen");
  esp_restart();
}

// The system clock's validity: set by hmi::housekeeping::SystemClock (app_main's
// system_clock), read by the TopBar. The time sync itself is the housekeeping
// component's.
static std::atomic<bool> clock_valid{false};

// The UI's loggers, made at start-up in this order (the three tags the UI logs under).
static espp::Logger logger_overdraw({.tag = "overdraw", .level = espp::Logger::Verbosity::INFO});
static espp::Logger logger_nav({.tag = "nav", .level = espp::Logger::Verbosity::INFO});
static espp::Logger logger_flip({.tag = "flip", .level = espp::Logger::Verbosity::INFO});

// ---------------------------------------------------------------------------
// The UI island (components/hmi_ui UiApp) and the ports it reaches the rest of
// the firmware through (hmi_ui/app_ports.hpp): each port's functions delegate to
// what main owns. Declared here, defined after the one UiApp: the drive inputs
// (the DriveAdapter is made over the UiApp's DriveUi) and the screens whose
// adapters stay in main (they hand their groups to the UiApp's NavView).
// ---------------------------------------------------------------------------
static bool drive_session_input(hmi::drive_session::Input input);
static void drive_wait_poll();
static void drive_unlock_hold_done();
static void drive_exit_hold_done();
static void drive_refresh_notice(hmi::stick::HoldReason hold);
static void log_screen_init();
static void internet_screen_init();
static void update_screen_init();
static void calibration_screen_init();

static constexpr hmi::ui::LinkPort kLinkPort{
    .state = [] { return static_cast<hmi::ui::LinkState>(rtps_comms_link_state()); },
    .seen = [](hmi::ui::LinkState state) { log_link_change(static_cast<RtpsLinkState>(state)); },
    .wifi = [] { return rtps_comms_net_link() == NetLink::WIFI; },
    .setting_text =
        [] { return link_text(static_cast<NetLink>(settings_get(SETTINGS_PARAM_NETWORK))); },
    .diag_stats =
        [](int64_t *last_us, int32_t *rate_tenths_hz) {
          const RtpsDiagStats stats = rtps_comms_diag_stats();
          *last_us = stats.last_us;
          *rate_tenths_hz = stats.rate_tenths_hz;
        },
    .publish_drive = rtps_comms_publish_drive,
    .publish_seat = rtps_comms_publish_seat,
};

// The drive session's inputs: the one DriveAdapter below (CS-SAF-08: these are its only
// targets).
static constexpr hmi::ui::DriveInputs kDriveInputs{
    .input = drive_session_input,
    .tick = drive_wait_poll,
    .unlock_hold_done = drive_unlock_hold_done,
    .exit_hold_done = drive_exit_hold_done,
    .refresh_notice = drive_refresh_notice,
};

static constexpr hmi::ui::CuesPort kCuesPort{
    .click = [] { play_click(espp::M5StackTab5::get()); },
    .refusal = [](bool warning) { play_refusal(warning); },
    .refused = refusal_feedback,
    .haptic_click = [] { (void)haptic_play(espp::Drv2605::Waveform::STRONG_CLICK, 1); },
    .haptic_test =
        [] { (void)haptic_play(espp::Drv2605::Waveform::ALERT_1000MS, kHapticBuzzSlots); },
};

static constexpr hmi::ui::SelfTestPort kSelfTestPort{
    .overlay_visible = selftest_ui_visible,
    .dismiss = selftest_ui_dismiss,
    .run = [] { (void)selftest_request(SelfTestTrigger::LOCAL, 0); },
};

static constexpr hmi::ui::BoardPort kBoardPort{
    .backlight = hmi::board::backlight,
    .present = hmi::board::present,
    .restart = action_restart_hmi,
};

static constexpr hmi::ui::MainScreens kMainScreens{
    .log_init = log_screen_init,
    .log_group = log_view_group,
    .log_on_load = log_view_on_load,
    .internet_init = internet_screen_init,
    .internet_on_load = internet_ui_on_load,
    .about_init = about_ui_init,
    .about_on_load = about_ui_on_load,
    .update_init = update_screen_init,
    .update_on_load = update_ui_on_load,
    .calibration_init = calibration_screen_init,
    .calibration_toggle = joystick_cal_toggle,
    .calibrating = joystick_cal_running,
};

// The quick POST's facts from ESP-IDF (hazard-c3-spec.md §2.2, REQ-POST-19), for the POST
// runner on the UI task. post/facts.hpp mirrors the IDF enums value for value: the
// static_asserts here are POST-050, built against the IDF this firmware uses.
static_assert(static_cast<int>(hmi::post::ResetReason::UNKNOWN) == ESP_RST_UNKNOWN &&
                  static_cast<int>(hmi::post::ResetReason::POWERON) == ESP_RST_POWERON &&
                  static_cast<int>(hmi::post::ResetReason::EXT) == ESP_RST_EXT &&
                  static_cast<int>(hmi::post::ResetReason::SW) == ESP_RST_SW &&
                  static_cast<int>(hmi::post::ResetReason::PANIC) == ESP_RST_PANIC &&
                  static_cast<int>(hmi::post::ResetReason::INT_WDT) == ESP_RST_INT_WDT &&
                  static_cast<int>(hmi::post::ResetReason::TASK_WDT) == ESP_RST_TASK_WDT &&
                  static_cast<int>(hmi::post::ResetReason::WDT) == ESP_RST_WDT &&
                  static_cast<int>(hmi::post::ResetReason::DEEPSLEEP) == ESP_RST_DEEPSLEEP &&
                  static_cast<int>(hmi::post::ResetReason::BROWNOUT) == ESP_RST_BROWNOUT &&
                  static_cast<int>(hmi::post::ResetReason::SDIO) == ESP_RST_SDIO &&
                  static_cast<int>(hmi::post::ResetReason::USB) == ESP_RST_USB &&
                  static_cast<int>(hmi::post::ResetReason::JTAG) == ESP_RST_JTAG &&
                  static_cast<int>(hmi::post::ResetReason::EFUSE) == ESP_RST_EFUSE &&
                  static_cast<int>(hmi::post::ResetReason::PWR_GLITCH) == ESP_RST_PWR_GLITCH &&
                  static_cast<int>(hmi::post::ResetReason::CPU_LOCKUP) == ESP_RST_CPU_LOCKUP,
              "post::ResetReason must mirror esp_reset_reason_t (POST-050)");
static_assert(static_cast<uint32_t>(hmi::post::OtaImageState::NEW) == ESP_OTA_IMG_NEW &&
                  static_cast<uint32_t>(hmi::post::OtaImageState::PENDING_VERIFY) ==
                      ESP_OTA_IMG_PENDING_VERIFY &&
                  static_cast<uint32_t>(hmi::post::OtaImageState::VALID) == ESP_OTA_IMG_VALID &&
                  static_cast<uint32_t>(hmi::post::OtaImageState::INVALID) == ESP_OTA_IMG_INVALID &&
                  static_cast<uint32_t>(hmi::post::OtaImageState::ABORTED) == ESP_OTA_IMG_ABORTED &&
                  static_cast<uint32_t>(hmi::post::OtaImageState::UNDEFINED) ==
                      ESP_OTA_IMG_UNDEFINED,
              "post::OtaImageState must mirror esp_ota_img_states_t (POST-050)");
// img.ok's "verified" is the bootloader's own validation of the image (decision C7 a): true
// only because no BOOTLOADER_SKIP_VALIDATE_* option is set, which this proves.
static_assert(CONFIG_HMI_BOOTLOADER_SKIPS_VALIDATE_AS_INT == 0,
              "the bootloader must validate the app image: img.ok relies on it (C3 Q10)");

// A byte count as the POST's int32 facts carry it, saturated (REQ-POST-19).
static int32_t saturated_b(size_t bytes) {
  return bytes > static_cast<size_t>(INT32_MAX) ? INT32_MAX : static_cast<int32_t>(bytes);
}

static hmi::post::ImageFacts post_image() {
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running == nullptr || esp_ota_get_state_partition(running, &state) != ESP_OK) {
    state = ESP_OTA_IMG_UNDEFINED; // a factory image or no OTA data: no OTA state
  }
  return {.verified = CONFIG_HMI_BOOTLOADER_SKIPS_VALIDATE_AS_INT == 0,
          .state = static_cast<hmi::post::OtaImageState>(state)};
}

static hmi::post::Calibration post_calibration() {
  const JoystickCal cal = joystick_cal_current();
  const auto axis = [](const JoystickAxisCal &a) {
    return hmi::post::AxisCal{.min_mv = static_cast<int32_t>(std::lround(a.min_mv)),
                              .centre_mv = static_cast<int32_t>(std::lround(a.center_mv)),
                              .max_mv = static_cast<int32_t>(std::lround(a.max_mv))};
  };
  return {.saved = joystick_cal_saved(),
          .x = axis(cal[JOY_HORIZONTAL]),
          .y = axis(cal[JOY_VERTICAL]),
          .twist = axis(cal[JOY_TWIST])};
}

// Heap headroom (C3 §2.2): read-only heap_caps_get_* queries (they allocate nothing; the
// ratchet exempts them, owner 2026-10-08).
static std::optional<hmi::post::MemoryFacts> post_memory() {
  constexpr uint32_t kInternal8Bit = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  return hmi::post::MemoryFacts{
      .internal_free_min_b = saturated_b(heap_caps_get_minimum_free_size(kInternal8Bit)),
      .internal_largest_b = saturated_b(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
      .dma_free_min_b = saturated_b(heap_caps_get_minimum_free_size(MALLOC_CAP_DMA)),
      .psram_free_b = saturated_b(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
  };
}

static hmi::post::StackFacts post_stacks() {
  TaskHandle_t adc = xTaskGetHandle("Read ADC");
  return {.adc_free_b = adc == nullptr ? 0 : saturated_b(uxTaskGetStackHighWaterMark(adc)),
          // the runner's own task: lv_task
          .ui_free_b = saturated_b(uxTaskGetStackHighWaterMark(nullptr))};
}

// Whether the stick task (StickIsland's "Read ADC", app_main) exists. Until it does nothing
// publishes XYTwist, so the chair cannot move; a failed start is logged by app_main and stays
// false until reset. FreeRTOS's task list is the record, so there is no flag to keep in step.
// Any task, not an ISR. For POST (hazard-c3-spec.md): kPostPort, read only at the budget. The name
// is the task's in app_main's StickIsland config and in tools/guards/baselines/tasks.json: a rename
// reads as "not running", the safe direction.
static bool stick_task_running() { return xTaskGetHandle("Read ADC") != nullptr; }

static constexpr hmi::ui::PostPort kPostPort{
    .now_ms = [] { return static_cast<uint32_t>(esp_timer_get_time() / 1000); },
    .reset_reason = [] { return static_cast<hmi::post::ResetReason>(esp_reset_reason()); },
    .image = post_image,
    .calibration = post_calibration,
    .memory = post_memory,
    .stacks = post_stacks,
    .calibration_saved_now = joystick_cal_saved,
    .stick_task_running = stick_task_running,
};

// The one UiApp: every view the UI task draws, wired (constinit: no global constructor).
static constinit hmi::ui::UiApp ui_app{{
    .link = &kLinkPort,
    .drive = &kDriveInputs,
    .cues = &kCuesPort,
    .selftest = &kSelfTestPort,
    .board = &kBoardPort,
    .screens = &kMainScreens,
    .clock_valid = &clock_valid,
    .nav_log = &logger_nav,
    .flip_log = &logger_flip,
    .overdraw_log = &logger_overdraw,
    .post = &kPostPort,
}};

// The drive session: lock, ask, unlock, drive, ask to stop. The decisions live in
// components/drive_session (DriveSession, checked against the AS-IS table in
// drive_session_table.hpp and its TABLE.md); sampling, the deadlines and performing the
// actions live in components/drive_adapter (DriveAdapter); each action's LVGL and RTPS call is
// drive_ui's DrivePort (drive_port.hpp), over the drive UI (DriveUi: the padlock and the advance
// to Drive). Every input arrives on the LVGL task: the 250 ms poll, the hold completions, the
// nav callbacks and the unlock timer.
//
// The drive adapter's port: stateless over the UiApp's DriveUi.
static constexpr hmi::ui::DrivePort<hmi::ui::DriveUi> drive_port{ui_app.drive_ui()};
static_assert(hmi::drive_adapter::DrivePort<hmi::ui::DrivePort<hmi::ui::DriveUi>>);

// The drive session's adapter: the session, its deadlines and latches, and the logger for a
// corrupted state, made at start-up with the rest (CS-SAF-04). Its windows are the table's
// kDriveAnswer and kDriveWait (pinned to the RTPS spec in hmi_ui's ui_app.cpp).
static hmi::drive_adapter::DriveAdapter<hmi::ui::DrivePort<hmi::ui::DriveUi>> drive_adapter{
    {.view = drive_port}};

// AdcStickIo's output permit (its comment above): ADC task, lock-free reads of the UiApp's
// permit channels only.
inline hmi::stick::Permit AdcStickIo::output_permit(const hmi::stick::Position &mounted) {
  hmi::stick::PermitHooks &hooks = ui_app.permit_hooks();
  const hmi::stick::PermitInputs in{
      .gate_open = ::stick_drives.load(),
      .motion_guard_ok = true,
      .calibrating = joystick_cal_running(),
      .calibration_measured = joystick_cal_measured(),
      .post = hooks.post_gate.read(),
      .health = hooks.stick_health.read(),
  };
  const hmi::stick::Permit permit = state.permit.cycle(in, mounted, adc_clock_ms());
  hooks.hold_reason.write(permit.reason);
  return permit;
}

// The POST's rest window on the ADC task (hazard-c3-spec.md §2.2, §2.4, REQ-POST-20): every
// cycle, valid or not, the reads the pipeline gets (a failed one as NaN); a window to the runner
// once per WINDOW_MIN_SAMPLES cycles, never waiting (a one-slot mailbox). It stops once the gate is
// PASS or FAIL; a gate that leaves PASS or FAIL, or comes back to NOT_RUN (bench builds:
// POST RERUN), drops any partial window and waits again for a first all-valid cycle. Allocates
// nothing, logs nothing, no lock.
void AdcStickIo::feed_rest_window(State &state, float horizontal_mv, float vertical_mv,
                                  float twist_mv, bool button) {
  using hmi::stick::PostGate;
  const PostGate gate = ui_app.permit_hooks().post_gate.read();
  const bool rerun = hmi::post::rest_window_restarts(state.post_seen, gate);
  state.post_seen = gate;
  if (rerun) {
    state.rest.reset();
  }
  if (!hmi::post::rest_window_feeds(gate)) {
    return;
  }
  if (state.rest.add_into(horizontal_mv, vertical_mv, twist_mv, button, state.window.window)) {
    state.windows.write(state.window);
  }
}

static bool drive_session_input(hmi::drive_session::Input input) {
  return drive_adapter.input(input);
}
static void drive_wait_poll() { drive_adapter.tick(); }
static void drive_unlock_hold_done() { drive_adapter.unlock_hold_done(); }
// Ask only. The session's relock (TICK_FOLLOW) closes the screen once the MIB actually stops
// driving.
static void drive_exit_hold_done() { drive_adapter.exit_hold_done(); }
// The hold poll: the stick's hold reason between two ticks, for the Drive notice (C1 §2.7).
static void drive_refresh_notice(hmi::stick::HoldReason hold) {
  drive_adapter.refresh_notice(hold);
}

// How long an updated image runs before it keeps itself (github_ota_boot_confirm).
static constexpr uint32_t kOtaConfirmAfterMs = 30'000;

// The Log screen: TextArea1 shows what log_capture has kept. Down at the newest line leaves the
// log for the burger key.
static void log_screen_init() {
  log_view_init();
  log_view_set_escape([] { ui_app.nav().to_key(); });
}

// InternetScreen: Ethernet or WiFi, the network list and the password page.
static void internet_screen_init() {
  internet_ui_init({
      .lvgl_mutex = &lvgl_mutex,
      .connection = setting_subjects.value(SETTINGS_PARAM_NETWORK),
      .use_group = [](lv_group_t *group) { ui_app.nav().use_group(group, ui_InternetScreen); },
      .focus_ring = hmi::ui::NavView::focus_ring,
      .mirror_states = hmi::ui::NavView::mirror_states,
      .claim_clicks = hmi::ui::NavView::claim_clicks,
      .refuse = refusal_feedback,
  });
}

// UpdateScreen: the GitHub releases, one release, and an install running; then the OTA
// image's confirm.
static void update_screen_init() {
  update_ui_init({
      .lvgl_mutex = &lvgl_mutex,
      .use_group = [](lv_group_t *group) { ui_app.nav().use_group(group, ui_UpdateScreen); },
      .focus_ring = hmi::ui::NavView::focus_ring,
      .mirror_states = hmi::ui::NavView::mirror_states,
      .claim_clicks = hmi::ui::NavView::claim_clicks,
      .refuse = refusal_feedback,
      // Never under a driving chair: the restart waits for the MIB to stop.
      .may_restart =
          [] {
            return static_cast<MIB::MibSystemState>(lv_subject_get_int(ui_app.mib_state())) !=
                   MIB::MibSystemState::ENABLED;
          },
  });
  // An updated image boots unconfirmed, and the bootloader rolls back to the
  // one before if it resets first. Confirmed once the UI has run this long:
  // the LVGL task is up and nothing has crashed it (github_ota.hpp).
  lv_timer_set_repeat_count(
      lv_timer_create([](lv_timer_t *) { github_ota_boot_confirm(); }, kOtaConfirmAfterMs, nullptr),
      1);
}

// The Joystick screen's calibration (joystick_cal): its prompts blink on the RTPS indicator's
// phase, and each captured step clicks.
static void calibration_screen_init() {
  joystick_cal_init_ui({.screen = ui_JoystickScreen,
                        .button_label = ui_CalibrateButtonLabel,
                        .instructions = ui_JoystickHint,
                        .blink = &rtps_blink_subject,
                        .feedback = [] {
                          haptic_play(espp::Drv2605::Waveform::STRONG_CLICK, 1);
                          play_click(espp::M5StackTab5::get());
                        }});
}

// ---------------------------------------------------------------------------
// Entry points from other tasks: each takes the LVGL lock around the view it
// reaches (the views take none, CS-OWN-08).
// ---------------------------------------------------------------------------

// Any task. Clamped, so nothing can turn the screen fully off.
static void brightness_set(int percent) {
  std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
  ui_app.brightness().set(percent);
}

// The Tab5's side button: the next of 25/50/75/100 % above the current level.
static void brightness_step() {
  std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
  ui_app.brightness().step();
}

// One edge of the stick button, from the GPIO or from the remote UI channel --
// one path, so what a script presses is the real handling (hmi::ui::StickButton).
static void stick_button_edge(bool active) {
  // lv_subject_set_int runs the observers synchronously on this task, and they
  // touch widgets, so this needs the LVGL lock
  std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
  ui_app.stick_button().edge(active);
}

// Runs on the Button's interrupt task (not in ISR context).
static void gpio48_button_callback(const espp::Interrupt::Event &event) {
  stick_button_edge(event.active);
}

// One LVGL cycle, for the UI island: lv_task_handler under the LVGL lock (CS-UI: only the
// UI task touches LVGL; RTPS handlers and the side button take the same lock).
static void lvgl_cycle() {
  std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
  if constexpr (kFpsStress) {
    lv_obj_invalidate(lv_screen_active());
  }
  lv_task_handler();
}

// ---------------------------------------------------------------------------
// Adapters: the callbacks the board's own tasks run (app-main-shrink §3). Each
// stays on the task that calls it today; app_main builds and registers them. The
// touch click and the side button are components/board's (hmi::board).
// ---------------------------------------------------------------------------

// The joystick's LVGL keypad read, on the LVGL task (espp's KeypadInput calls it): UiApp's.
static void joystick_keypad_read(bool *up, bool *down, bool *left, bool *right, bool *enter,
                                 bool *escape) {
  ui_app.keypad_read(up, down, left, right, enter, escape);
}

// ---------------------------------------------------------------------------
// Board and service wiring: app_main's Configs and bring-up steps, by concern.
// ---------------------------------------------------------------------------

// app_main: the remote UI's hooks into the same latches the ADC task and the GPIO48 callback
// write (remote_ui.cpp).
static RemoteUiConfig remote_ui_config() {
  return {
      .lvgl_mutex = &lvgl_mutex,
      .set_key =
          [](uint32_t key) {
            remote_key.store(key);
            joy_key.store(key);
          },
      .press_select = [] { select_key.store(true); },
      .set_button = [](bool down) { stick_button_edge(down); },
      .screen_name = [] { return hmi::ui::NavView::screen_name(lv_screen_active()); },
  };
}

// The hazard bench verbs' hooks (components/remote_ui/include/bench_verbs.hpp), the fields of
// the stick's output permit (C1) and the quick POST (C3). Bench inject builds only: app_main
// hands them over inside `if constexpr (BENCH_STICK_INJECT)`. Each runs on the remote UI's
// task: atomics only, no LVGL lock (the calibration record's copy takes joystick_cal's short
// lock, as the ADC task's take_new does).
static const char *post_indicator_name(hmi::post::IndicatorKind kind) {
  switch (kind) {
  case hmi::post::IndicatorKind::NONE:
    return "NONE";
  case hmi::post::IndicatorKind::CHECKING:
    return "CHECKING";
  case hmi::post::IndicatorKind::WAITING:
    return "WAITING";
  case hmi::post::IndicatorKind::FAILED:
    return "FAILED";
  case hmi::post::IndicatorKind::NOT_RUN:
    return "NOT_RUN";
  }
  return "?";
}

static hmi::bench_verbs::CalRecord bench_cal_record() {
  const JoystickCal cal = joystick_cal_current();
  hmi::bench_verbs::CalRecord out{};
  for (size_t i = 0; i < out.size(); ++i) {
    out[i] = {.min_mv = static_cast<int32_t>(std::lround(cal[i].min_mv)),
              .centre_mv = static_cast<int32_t>(std::lround(cal[i].center_mv)),
              .max_mv = static_cast<int32_t>(std::lround(cal[i].max_mv))};
  }
  return out;
}

// The drive table's side of the bench STATE line (L1, hazard-c1-spec.md §6): the session's
// phase, the Drive notice, the menu and the refusal banner, each from an atomic mirror the UI
// task writes (no LVGL lock on the remote-UI task). The notice is §2.7's combination made at the
// read: the stop notice as of the last input or tick first, else the stick's hold reason now, so
// it agrees with the hold reason the same line reports.
static void add_drive_hooks(hmi::bench_verbs::Hooks &h) {
  h.phase = [] {
    return std::string(hmi::drive_session::to_string(drive_adapter.published_phase()));
  };
  h.notice = [] {
    const hmi::stick::HoldReason hold = ui_app.permit_hooks().hold_reason.read();
    return std::string(hmi::drive_adapter::to_string(drive_adapter.notice_for(hold)));
  };
  h.menu_open = [] { return ui_app.menu_open_any_task(); };
  h.banner = [] { return std::string(hmi::ui::refused_name(ui_app.refused_any_task())); };
}

static hmi::bench_verbs::Hooks bench_verb_hooks() {
  using hmi::bench_verbs::PostGateName;
  using hmi::bench_verbs::StickHealthName;
  hmi::bench_verbs::Hooks h;
  add_drive_hooks(h);
  h.hold_reason = [] {
    return std::string(hmi::stick::to_string(ui_app.permit_hooks().hold_reason.read()));
  };
  h.calibrating = [] { return joystick_cal_running(); };
  h.cal = bench_cal_record;
  h.post = [] {
    return std::string(hmi::stick::to_string(ui_app.permit_hooks().post_gate.read()));
  };
  h.post_check = []() -> std::optional<std::string> {
    const hmi::ui::PostStage::Shown shown = ui_app.post().shown();
    if (!shown.check) {
      return std::nullopt;
    }
    return std::string(hmi::post::check(*shown.check).name);
  };
  h.indicator = [] { return std::string(post_indicator_name(ui_app.post().shown().kind)); };
  h.indicator_text = [] { return std::string(ui_app.post().text_now().text.data()); };
  h.reset_reason = [] {
    return std::string(
        hmi::post::reset_reason_id(static_cast<hmi::post::ResetReason>(esp_reset_reason())));
  };
  // PERMIT POST: applied by the POST runner on its next tick (C3 §2.4), so the gate keeps one
  // writer. PostGateName is PostGate's order (static_asserted below).
  h.permit_post = [](PostGateName gate) {
    ui_app.post().request_gate(static_cast<hmi::stick::PostGate>(gate));
    return true;
  };
  // PERMIT STICK: C1 §3.2's other writer of stick health, bench builds only (gone with C2).
  h.permit_stick = [](StickHealthName health) {
    using hmi::stick::StickHealth;
    const StickHealth value = health == StickHealthName::OK      ? StickHealth::OK
                              : health == StickHealthName::FAULT ? StickHealth::FAULT
                                                                 : StickHealth::NOT_MONITORED;
    ui_app.permit_hooks().stick_health.write(value);
    return true;
  };
  // CAL UNSAVED: RAM only, until reboot: joystick_cal_measured() and the POST's "saved" fact
  // read false (C1 §6, REQ-RUI-06).
  h.cal_unsaved = [] {
    joystick_cal_forget_measured();
    return true;
  };
  // POST RERUN: the runner restarts from NOT_RUN on its next tick; Read ADC sees the gate back
  // at NOT_RUN and drops its partial window (C3 §2.4).
  h.post_rerun = [] {
    ui_app.post().request_rerun();
    return true;
  };
  return h;
}
static_assert(static_cast<int>(hmi::bench_verbs::PostGateName::NOT_RUN) ==
                      static_cast<int>(hmi::stick::PostGate::NOT_RUN) &&
                  static_cast<int>(hmi::bench_verbs::PostGateName::PENDING) ==
                      static_cast<int>(hmi::stick::PostGate::PENDING) &&
                  static_cast<int>(hmi::bench_verbs::PostGateName::PASS) ==
                      static_cast<int>(hmi::stick::PostGate::PASS) &&
                  static_cast<int>(hmi::bench_verbs::PostGateName::FAIL) ==
                      static_cast<int>(hmi::stick::PostGate::FAIL),
              "PERMIT POST's names are PostGate's values");

// app_main: LVGL in DIRECT render mode over the DSI panel's two frame buffers (see the
// comment at the call). @return whether it came up; if not, the BSP's flush stays.
static bool start_direct_render(espp::M5StackTab5 &tab5, espp::Logger &logger) {
  void *fb0 = nullptr;
  void *fb1 = nullptr;
  esp_err_t fb_err = esp_lcd_dpi_panel_get_frame_buffer(tab5.lcd_panel_handle(), 2, &fb0, &fb1);
  const size_t fb_bytes = tab5.display_width() * tab5.display_height() * sizeof(uint16_t);
  // DIRECT mode renders into screen-sized frame buffers and direct_flush_cb
  // assumes the panel's native orientation, so it is incompatible with LVGL
  // software rotation. Assert here rather than depend on the
  // lv_display_set_rotation(ROTATION_0) call much further down.
  assert(lv_display_get_rotation(lv_display_get_default()) == LV_DISPLAY_ROTATION_0 &&
         "DIRECT render mode requires rotation 0");
  if (fb_err == ESP_OK && fb0 && fb1) {
    ui_app.display_flip().use_panel_buffers(lv_display_get_default(), fb0, fb1, fb_bytes,
                                            tab5.display_width(), tab5.display_height());
    logger.info("LVGL rendering directly into the DSI frame buffers (DIRECT mode)");
    return true;
  }
  logger.error("Could not get DPI frame buffers ({}); leaving BSP flush in place",
               esp_err_to_name(fb_err));
  return false;
}

// app_main's end, and where a boot that stops early ends: app_main never returns. Its task
// sleeps here for good, which keeps every app_main local alive for the tasks that use them.
// From the side button on (phase 3), the BSP's tasks call into them: the side button
// app_main's logger, the touch task the Board's TouchClick and through it the cues, and the
// BSP has no call that stops either. A return would leave them calling destroyed objects.
[[noreturn]] static void park_main_task() {
  while (true) {
    std::this_thread::sleep_for(1s);
  }
}

extern "C" void app_main(void) {
  // First, so the LogScreen has everything printed from here on - including
  // what the tasks started below print.
  log_capture_start();
  espp::Logger logger({.tag = "main", .level = espp::Logger::Verbosity::INFO});
  logger.info("Starting the HMI");
  // Before anything reads /storage: the first boot of the two-slot layout brings the
  // calibration and settings over from where the old partition table kept them.
  storage_migrate_legacy();
  github_ota_boot_report();

  espp::M5StackTab5 &tab5 = espp::M5StackTab5::get();
  logger.info("Running on M5Stack Tab5");

  // The board's bring-up on the BSP (components/board): each step logs to `logger` as it
  // did here, and a step that returns false has logged why. Lives as long as app_main.
  hmi::board::Board board({
      .tab5 = tab5,
      .log = logger,
      .brightness_step = brightness_step,
      .click = kCuesPort.click,
  });
  board.probe_internal_i2c();
  auto &i2c = tab5.internal_i2c();
  const std::vector<uint8_t> &found_addresses = board.i2c_devices();

  // The haptic and sound cues: the DRV2605 comes up here (the HAPTIC TEST slot,
  // the unlock and hold clicks, a refusal); the click's samples load after the
  // LVGL task starts. Lives as long as app_main, which never returns
  // (park_main_task); the unit reaches it through `feedback`.
  hmi::feedback::Feedback cues({.i2c = i2c, .boot_log = logger, .sound = click_sound_config()});
  feedback = &cues;

  // DA7280 bring-up test (raw register read) and driver functional test (Da7280
  // driver class, DRO mode): bench only, CONFIG_HMI_BENCH_DA7280_TEST.
  hmi::feedback::run_da7280_bench(logger, i2c, found_addresses);

  if (!board.start_io_expanders() || !board.start_display()) {
    park_main_task();
  }

  // Switch LVGL to DIRECT render mode over the panel's own frame buffers. The
  // BSP already handed those buffers to lv_display_set_buffers, but espp's
  // Display hardcodes RENDER_MODE_PARTIAL; DIRECT is what lets LVGL treat them
  // as real frame buffers (tracking dirty areas across both) so a flush is a
  // vsync-gated flip (see direct_flush_cb) rather than a copy. DIRECT requires
  // rotation 0, which is what this panel runs at. The self test reports whether it
  // came up.
  const bool direct_render = start_direct_render(tab5, logger);

  // run the LVGL refresh timer at 60 fps — the espp lv_conf.h compiles in a
  // 33 ms (30 fps) default period; the lv_task loop below already calls
  // lv_task_handler every 8 ms so it can keep up
  lv_display_t *const display = lv_display_get_default();
  lv_timer_set_period(lv_display_get_refr_timer(display), 16);

  if constexpr (kFpsInstrument) {
    fps_meter.attach(display);
    logger.info("FPS instrumentation enabled (stress={})", kFpsStress);
  }

  // The housekeeping island (IMU, battery, RTC). Built here because the IMU takes its
  // orientation filter; its task starts after the click sound is loaded, below.
  hmi::housekeeping::Housekeeping housekeeping({
      .task =
          {
              .name = "Data Display Task",
              .stack_size_bytes = 6 * 1024,
              .priority = 10,
              .core_id = 1,
          },
      .period = 20ms,
      .angle_noise = 0.001f,
      .rate_noise = 0.1f,
  });

  if (!board.start_imu(housekeeping.orientation_filter())) {
    park_main_task();
  }
  board.start_sdcard();

  // The system clock and the RTC, kept to the MCB's time (housekeeping). Lives as long
  // as app_main, which never returns (park_main_task).
  hmi::housekeeping::SystemClock system_clock({.valid = &clock_valid, .max_drift_s = 2});

  if (!board.start_rtc(system_clock) || !board.start_battery() || !board.start_audio()) {
    park_main_task();
  }
  board.start_side_button();

  logger.info("Setting up LVGL UI...");
  // Load the SquareLine Studio UI. This creates every screen and makes
  // ui_BootScreen the active one; the default screen LVGL started on stays
  // behind it, empty.
  logger.info("Loading SquareLine UI...");
  hmi::ui::build_screens(&boot_logo, kFpsInstrument);

  // The settings, the subjects, every screen's chrome, the banners, the poll and the
  // Joystick screen (UiApp::build lists the order).
  ui_app.build();

  // GPIO48, pulled up and shorted to ground on press (board-wide convention),
  // so active LOW. Constructed after the subjects are initialized, because the
  // interrupt task starts here and its callback writes them. The internal
  // pull-up is redundant against the external one but harmless.
  logger.info("Initializing GPIO48 test button...");
  static espp::Button gpio48_button({
      .name = "GPIO48 Button",
      .interrupt_config =
          {
              .gpio_num = 48,
              .callback = gpio48_button_callback,
              .active_level = espp::Button::ActiveLevel::LOW,
              .interrupt_type = espp::Button::InterruptType::ANY_EDGE,
              .pullup_enabled = true,
          },
      .task_config = {.name = "Button", .stack_size_bytes = 4 * 1024, .priority = 5},
  });

  // Joystick as an LVGL keypad input device, moving the cursor through each
  // screen's focus group. The read function runs on the LVGL task and drains the
  // latch the ADC task fills, so one flick of the stick = one PRESSED cycle =
  // one LV_EVENT_KEY. Touch keeps working; indevs coexist.
  logger.info("Adding joystick keypad input device...");
  static espp::KeypadInput joystick_keypad({.read = joystick_keypad_read});
  // The joystick's input, the holds, the remaining screens and the build's finish
  // (UiApp::build_input lists the order).
  ui_app.build_input(joystick_keypad.get_input_device());

  // The self test's checks and their limits are in selftest_spec.hpp;
  // selftest.cpp measures them. Started from the "Self test" Skunk Works slot,
  // or by a PC over RTPS (scripts/rtps_selftest.py) — which is why this comes
  // before rtps_comms_start: selftest_init registers the self-test RTPS
  // handlers.
  SelfTestPlatform selftest_board = selftest_platform(feedback, found_addresses, direct_render);
  selftest_board.lvgl_mutex = &lvgl_mutex;
  selftest_init(selftest_board);

  ui_app.build_on_demand_parts();

  if (!board.start_touch()) {
    park_main_task();
  }
  if (auto touchpad = tab5.touchpad_input()) {
    std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
    ui_app.display_flip().wrap_touch(touchpad->get_touchpad_input_device());
  }

  // The UI island: lv_task_handler every 8ms -- the refresh timer runs at 16ms
  // (60 fps), polling at twice that rate keeps its firing jitter well under a frame.
  logger.info("Starting LVGL task...");
  // The POST's rest windows (hazard-c3-spec.md §2.4): the ADC task writes, the POST runner on
  // the UI task reads (bound before lv_task starts). The ADC task's own state holds the write
  // end and the output permit. Both live as long as app_main, which never returns.
  hmi::ui::RestWindowMailbox rest_windows{hmi::ui::RestWindowMailbox::Config{}};
  AdcStickIo::State adc_state{
      .permit = {}, .rest = {}, .windows = hmi::fw::writer(rest_windows), .window = {}};
  espp::Logger post_logger({.tag = "post", .level = espp::Logger::Verbosity::INFO});
  {
    hmi::post::I2cSet i2c_found;
    for (const uint8_t address : found_addresses) {
      i2c_found.add(address);
    }
    ui_app.post().attach(hmi::fw::reader(rest_windows), i2c_found, post_logger);
  }

  hmi::ui::UiIsland ui_island({
      .task =
          {
              .name = "lv_task",
              // Measured peak ~6 KB (self test mem.stk_lvgl: 26964 B of 32 KB
              // never used). The stack is internal DMA-capable RAM, which RTPS
              // start-up runs dry on: at 32 KB the W5500 driver's bounce buffer
              // failed to allocate and the board boot-looped. mem.stk_lvgl
              // guards the headroom.
              .stack_size_bytes = 16 * 1024,
              .priority = 20,
              .core_id = 1,
          },
      .cycle = lvgl_cycle,
      .period = 8ms,
      .fps_meter = kFpsInstrument ? &fps_meter : nullptr,
      // The motion guard's UI heartbeat (hazard-c4-spec.md §3.1), on the ADC side's clock.
      .heartbeat = [] { ui_app.guard_sources().note_ui_cycle(adc_clock_ms()); },
  });
  if (!ui_island.start()) {
    logger.error("Failed to start LVGL task!");
    park_main_task();
  }

  // load the audio file (wav file bundled in memory)
  size_t wav_size = 0;
  size_t wav_sample_rate = 0;
  if (!load_audio(wav_size, wav_sample_rate)) {
    logger.error("Failed to load audio file!");
    // The boot stops here with the UI stopped, as it always has (the return destroyed the
    // UiIsland); the tasks already started keep running, on live objects.
    (void)ui_island.stop();
    park_main_task();
  }
  logger.info("Loaded {} bytes of audio", wav_size);

  board.start_speaker(wav_sample_rate);

  // (brightness is the saved setting, applied when the BrightnessView adds its observer)

  logger.info("Starting data display task...");
  if (!housekeeping.start()) {
    // The boot goes on: the IMU, battery and RTC are then not read again after boot.
    logger.error("Failed to start the data display task!");
  }

  // guards the joystick range-mapping math (center/range deadbands, circular
  // clamp, and that twist stays independent of the X/Y gimbal). Asserts, so it
  // aborts loudly on a regression; compiles to nothing under NDEBUG.
  espp::joystick_selftest();

  logger.info("Starting continuous adc...");

  // X/Y: ADC1_CH0/CH1 = GPIO16/GPIO17 on the M5-Bus header.
  // Twist: ADC2_CH3 = GPIO52 on the M5-Bus (the W5500 INT moved to GPIO4 to
  // free it — analog inputs can't be re-routed through the GPIO matrix).
  // The twist channel is sampled oneshot rather than through the continuous
  // driver: mixing both units via ADC_CONV_BOTH_UNIT produced a stream of
  // invalid DMA frames on the P4 (log spam that starved LVGL's first frame).
  // The control island ("Read ADC"): the ADC drivers come up here, the task starts
  // once the calibration is loaded, below.
  static constexpr hmi::control::Cycle kStickCycle{
      // 30 Hz: ADC read, LVGL bars, RTPS publish
      .period_ms = 33,
      .twist_channel = {.unit = ADC_UNIT_2,
                        .channel = ADC_CHANNEL_3,
                        .attenuation = ADC_ATTEN_DB_12},
      // Twist is the noisy axis (~50 mV peak-to-peak at rest, where X/Y read ~1
      // mV): each cycle averages this many oneshot reads of it, which costs no
      // lag, then lowpasses the result to iron out what is left. 80 ms is short
      // enough that the chair does not feel late to turn.
      .twist_oversample = 8,
  };
  hmi::control::StickIsland<StickSlot, AdcStickIo, kStickCycle> stick_island({
      .task =
          {
              .name = "Read ADC",
              // espp's defaults, written out. Priority 0 runs at IDF's pthread default
              // (5), and unpinned the task is pinned by its first FPU use (core 0 on the
              // board): H11 is the fix, not this.
              .stack_size_bytes = 4096,
              .priority = 0,
              .core_id = -1,
          },
      .task_log_level = espp::Logger::Verbosity::INFO,
      // this initailizes the DMA and filter task for the continuous adc
      .adc = {.sample_rate_hz = 1 * 1000,
              .channels =
                  {{.unit = ADC_UNIT_1, .channel = ADC_CHANNEL_0, .attenuation = ADC_ATTEN_DB_12},
                   {.unit = ADC_UNIT_1, .channel = ADC_CHANNEL_1, .attenuation = ADC_ATTEN_DB_12}},
              .convert_mode = ADC_CONV_SINGLE_UNIT_1,
              .window_size_bytes = 1024,
              .log_level = espp::Logger::Verbosity::WARN},
      .twist_lowpass = {.time_constant = 0.08f},
  });

  // Joystick calibration: where each axis rests and the two ends of its travel,
  // in raw mV. Measured per unit by CALIBRATE on the JoystickTest screen and
  // kept in flash (joystick_cal.cpp); these ideal-divider values are only what
  // a unit that was never calibrated runs on, and WILL be off on real hardware.
  // How the travel is mapped (deadzones, axis wiring) is under "AXIS WIRING"
  // above AdcStickIo.
  static constexpr JoystickAxisCal kIdealAxis{
      .min_mv = 0.0f, .center_mv = 1650.0f, .max_mv = 3300.0f};
  const JoystickCal joystick_cal = joystick_cal_load({kIdealAxis, kIdealAxis, kIdealAxis});
  // The stick pipeline (components/stick): the joystick mapping on this
  // calibration, the key trigger and the gate, owned by the island's task. A
  // StickSlot is the StickPipeline itself, or with CONFIG_HMI_BENCH_STICK_INJECT
  // the bench stick injection in front of its reads (stick_inject.hpp).
  // Without the task the stick sends nothing, which is the safe direction: no XYTwist, no
  // motion. The boot goes on so the failure is seen: this line, and stick_task_running().
  if (!stick_island.start(stick_pipeline_config(joystick_cal), adc_state)) {
    logger.error("Failed to start the Read ADC task: no stick output (XYTwist) until reset!");
  }

  // bring up W5500 Ethernet + RTPS last so a missing cable / module can't
  // delay the HMI; on failure the UI keeps running without comms
  logger.info("Starting RTPS comms...");
  // remote LCD brightness (kHmiBrightness); floor at 5% so a remote command
  // can't turn the screen fully off. brightness_set takes the LVGL lock and
  // sets the brightness subject, so it is safe from the RTPS receive task.
  rtps_comms_on_brightness(
      [](float percent) { brightness_set(static_cast<int>(std::lround(percent))); });
  // RTPS handlers run on the RTPS receive task: subjects only (UiApp's RtpsUiBridge), under
  // the LVGL lock, because lv_subject_set_int runs the observers synchronously on this task
  // and they touch widgets.
  rtps_comms_on_mib_status([&system_clock](const MIB::MibStatus &status) {
    // First, before any lock: the motion guard's state, then its stamp (C4 §3.1, REQ-CTL-04).
    ui_app.guard_sources().note_mib_status(static_cast<uint8_t>(status.systemState),
                                           adc_clock_ms());
    system_clock.note_mcb_time(status); // no LVGL: sets the system clock and the RTC
    std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
    ui_app.rtps_bridge().apply_mib_status(status);
  });
  rtps_comms_on_diagnostics([](const rammp::Diagnostics &diag) {
    std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
    ui_app.rtps_bridge().apply_diagnostics(diag);
  });
  if (!rtps_comms_start(static_cast<NetLink>(settings_get(SETTINGS_PARAM_NETWORK)))) {
    logger.warn("RTPS comms not started (network bring-up failed)");
  }
  {
    std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
    ui_app.topbar().set_link(link_text(rtps_comms_net_link()));
  }
  // The firmware's SHA-256 for the About screen: ~4 MB of flash read on a
  // low-priority thread. Only once rtps_comms_start has set the W5500 up: run
  // across that, it left the chip with no TX buffer on most boots (no
  // link, or no DHCP lease).
  fw_info_start();

  // The remote UI debug channel (CONFIG_HMI_REMOTE_UI, off by default). Last,
  // because it drives everything above it: input goes into the same latches the
  // ADC task and the GPIO48 callback write, so what a script exercises is the
  // real handling and not a parallel path.
  if constexpr (BENCH_STICK_INJECT) {
    remote_ui_attach_bench_verbs(bench_verb_hooks()); // the one call, before the server starts
  }
  remote_ui_start(remote_ui_config());

  park_main_task();
}

// The click sound's samples: click.wav, embedded by main/CMakeLists.txt
// (EMBED_TXTFILES). The sound itself is hmi::feedback::ClickSound.
static bool load_audio(size_t &out_size, size_t &out_sample_rate) {
  extern const uint8_t click_wav_start[] asm("_binary_click_wav_start");
  extern const uint8_t click_wav_end[] asm("_binary_click_wav_end");
  return feedback->sound().load({click_wav_start, click_wav_end}, out_size, out_sample_rate);
}

// The click: a touch landing, the stick button selecting, a hold completing.
// Silent with Settings "Sounds" off. Every caller passes the one Tab5, which is
// the speaker the Feedback plays on.
static void play_click(espp::M5StackTab5 & /*tab5*/) { feedback->sound().play_click(); }

// "Can't do that", heard. `warning` is a banner coming up: it sounds even with
// Sounds off. LVGL task, or under lvgl_mutex.
static void play_refusal(bool warning) { feedback->sound().play_refusal(warning); }

// A refused press: the DRV2605's double click, which says the same by touch.
static void refusal_feedback() { feedback->refusal(); }
