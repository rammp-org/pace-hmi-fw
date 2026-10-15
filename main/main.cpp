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
#include <numeric>
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
#include "keypad_input.hpp"

#include "about_ui.hpp"
#include "alloc_guard/alloc_guard.hpp"
#include "app_instances.hpp"
#include "bench_hooks.hpp"
#include "bench_verbs.hpp"
#include "board/board.hpp"
#include "board/board_port.hpp"
#include "boot_logo.h"
#include "cues.hpp"
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
#include "selftest_spec.hpp"
#include "settings.hpp"
#include "settings_applied.hpp"
#include "storage.hpp"
#include "update_ui.hpp"

#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
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
#include "post_facts/post_facts.hpp"
#include "stick/mapping_selftest.hpp"
#include "stick_io.hpp"
#include "task_configs.hpp"
#include "ui_ports.hpp"

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

// The system clock's validity: set by hmi::housekeeping::SystemClock (app_main's
// system_clock), read by the TopBar. The time sync itself is the housekeeping
// component's.
static std::atomic<bool> clock_valid{false};

// The UI's loggers, made at start-up in this order (the three tags the UI logs under).
static espp::Logger logger_overdraw({.tag = "overdraw", .level = espp::Logger::Verbosity::INFO});
static espp::Logger logger_nav({.tag = "nav", .level = espp::Logger::Verbosity::INFO});
static espp::Logger logger_flip({.tag = "flip", .level = espp::Logger::Verbosity::INFO});

// The one UiApp: every view the UI task draws, wired (constinit: no global constructor).
constinit hmi::ui::UiApp ui_app{{
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
MainDriveAdapter drive_adapter{{.view = drive_port}};

// How long an updated image runs before it keeps itself (github_ota_boot_confirm).
static constexpr uint32_t kOtaConfirmAfterMs = 30'000;

// InternetScreen: Ethernet or WiFi, the network list and the password page.
void internet_screen_init() {
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
void update_screen_init() {
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

// AdcStickIo's bars (stick_io.hpp), on the stick task: the one place it touches LVGL.
inline void AdcStickIo::show(const hmi::stick::Position &mounted) {
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
  // (park_main_task); the cues (cues.cpp) reach it once attached.
  hmi::feedback::Feedback cues({.i2c = i2c, .boot_log = logger, .sound = click_sound_config()});
  cues_attach(cues);

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
  // the panel's frame buffers (CONFIG_M5STACK_TAB5_DPI_NUM_FBS=2; espp PR A)
  const bool direct_render = ui_app.display_flip().start_direct_render(
      tab5.frame_buffers(), espp::M5StackTab5::frame_buffer_size(), tab5.display_width(),
      tab5.display_height(), logger);

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
  hmi::housekeeping::Housekeeping housekeeping(housekeeping_config());

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
  static espp::Button gpio48_button(gpio48_button_config(gpio48_button_callback));

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
  SelfTestPlatform selftest_board = selftest_platform(&cues, found_addresses, direct_render);
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
  // C2's stick monitor: each change of its state, logged by the UI task (REQ-UI-27).
  espp::Logger stick_logger({.tag = "stick", .level = espp::Logger::Verbosity::INFO});
  ui_app.attach_stick_log(stick_logger);
  espp::Logger seat_logger({.tag = "seat", .level = espp::Logger::Verbosity::INFO});
  ui_app.attach_seat_log(seat_logger); // REQ-UI-29: a failed SeatCommand publish
  {
    hmi::post::I2cSet i2c_found;
    for (const uint8_t address : found_addresses) {
      i2c_found.add(address);
    }
    ui_app.post().attach(hmi::fw::reader(rest_windows), i2c_found, post_logger);
  }

  hmi::ui::UiIsland ui_island(ui_island_config(lvgl_cycle, kFpsInstrument ? &fps_meter : nullptr));
  if (!ui_island.start()) {
    logger.error("Failed to start LVGL task!");
    park_main_task();
  }

  // The click sound (click.wav, bundled in the image). Without it the HMI carries on silent:
  // the UI keeps running and the boot goes on (owner, T4, 2026-10-11); ClickSound plays nothing
  // before a successful load, and the speaker stays muted. No self-test or POST check covers it.
  size_t wav_size = 0;
  size_t wav_sample_rate = 0;
  if (load_audio(wav_size, wav_sample_rate)) {
    logger.info("Loaded {} bytes of audio", wav_size);
    board.start_speaker(wav_sample_rate);
  } else {
    logger.error("Failed to load the click sound (click.wav): carrying on without it");
  }

  // (brightness is the saved setting, applied when the BrightnessView adds its observer)

  logger.info("Starting data display task...");
  if (!housekeeping.start()) {
    // The boot goes on: the IMU, battery and RTC are then not read again after boot.
    logger.error("Failed to start the data display task!");
  }

  // guards the joystick range-mapping math (center/range deadbands, circular
  // clamp, and that twist stays independent of the X/Y gimbal). A regression
  // aborts the boot loudly, as the espp fork's joystick_selftest() asserts did
  // in every image (J2, components/stick; no image sets NDEBUG).
  if (!hmi::stick::mapping_selftest()) {
    logger.error("Stick mapping self test FAILED");
    abort();
  }

  logger.info("Starting continuous adc...");

  // The control island ("Read ADC"): the ADC drivers come up here (stick_io.cpp has the
  // channels and the task); the task starts once the calibration is loaded, below.
  MainStickIsland stick_island(stick_island_config());

  // The calibration in use (stick_io.cpp): the saved one, or the ideal divider's; the monitor's
  // limits follow it before the task starts. Without the task the stick sends nothing, which is
  // the safe direction: no XYTwist, no motion. The boot goes on so the failure is seen: this
  // line, and post_facts' stick_task_running().
  const JoystickCal joystick_cal = stick_calibration_at_boot(adc_state);
  // The stick pipeline (components/stick): the joystick mapping on this calibration, the key
  // trigger and the gate, owned by the island's task. A StickSlot is the StickPipeline itself, or
  // with CONFIG_HMI_BENCH_STICK_INJECT the bench stick injection in front of its reads.
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

  // Start-up is complete. From here the bench allocation guard (CONFIG_HMI_BENCH_ALLOC_GUARD,
  // TS-DET-08) counts every heap allocation of the self test's mem.alloc_* tasks.
  if constexpr (hmi::alloc_guard::ENABLED) {
    hmi::alloc_guard::start(selftest_spec::kAllocTasks);
  }
  park_main_task();
}
