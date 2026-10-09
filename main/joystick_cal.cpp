#include "joystick_cal.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>

#include "cal_record.hpp"
#include "cal_run.hpp"
#include "logger.hpp"
#include "storage.hpp"

namespace {

espp::Logger logger({.tag = "joy_cal", .level = espp::Logger::Verbosity::INFO});

// The record, its file and its check: components/joystick_cal (cal_record.hpp).
using hmi::cal::describe;
using hmi::cal::kFileName;
using hmi::cal::kFullTravelMv;
using hmi::cal::plausible;

// The run itself - its steps, timing and limits - is the model in
// components/joystick_cal (cal_run.hpp); this file is its view.
using hmi::cal::Action;
using hmi::cal::Input;
using hmi::cal::kTickMs;

// Shared between tasks.
std::mutex cal_mutex;
JoystickCal current{}; // in use
// What `current` is: the compiled-in defaults, a calibration measured this boot but not saved,
// or the one the file holds. Atomic, so the ADC task reads it every cycle without a lock
// (joystick_cal_measured, the output permit's condition 4).
enum class InUse : uint8_t { DEFAULTS, MEASURED, SAVED };
std::atomic<InUse> in_use{InUse::DEFAULTS};
std::optional<JoystickCal> pending; // a finished run, for the ADC task to apply
std::atomic<bool> running{false};
std::atomic<float> latest_mv[3];

std::optional<JoystickCal> read_file(const std::string &path) {
  std::ifstream in(path);
  if (!in) {
    return std::nullopt;
  }
  JoystickCal cal{};
  hmi::cal::DecodeError error = hmi::cal::DecodeError::NONE;
  if (!hmi::cal::decode(in, cal, error)) {
    logger.warn("{}: {}", path, hmi::cal::message(error));
    return std::nullopt;
  }
  return cal;
}

bool write_file(const JoystickCal &cal) { return storage_write(kFileName, hmi::cal::encode(cal)); }

/////////////////////////////////////////////////////////////////////////////
// The run's view. Everything below is the LVGL task's: the model, the timer,
// the button and the subjects all run under the lock lv_timer_handler() is
// called with.
/////////////////////////////////////////////////////////////////////////////

constinit hmi::cal::CalibrationRun run(hmi::cal::CalibrationRun::Config{});

JoystickCalUi ui;
lv_timer_t *timer = nullptr;
// The instructions label's text. Starts as the export's own text, which comes
// back after every run.
lv_subject_t text_subject;
char text_buf[160];
char text_prev_buf[160];
std::string idle_text;
// 1 while a prompt is up: the label blinks on ui.blink.
lv_subject_t prompting_subject;
// 1 while a run is going: the button reads CANCEL.
lv_subject_t running_subject;
std::string button_text;

void feedback() {
  if (ui.feedback) {
    ui.feedback();
  }
}

void show_prompt() {
  const std::string prompt = run.prompt();
  lv_subject_copy_string(&text_subject, prompt.c_str());
  lv_subject_set_int(&prompting_subject, 1);
}

void finish(const char *message) {
  running = false;
  lv_subject_set_int(&prompting_subject, 0);
  lv_subject_set_int(&running_subject, 0);
  lv_subject_copy_string(&text_subject, message);
}

void complete() {
  const JoystickCal &cal = run.record();
  const bool saved = write_file(cal);
  {
    std::lock_guard<std::mutex> lock(cal_mutex);
    current = cal;
    in_use = saved ? InUse::SAVED : InUse::MEASURED;
    pending = cal;
  }
  logger.info("calibrated{}: {}", saved ? "" : " (NOT saved)", describe(cal));
  finish(saved ? "Calibration saved"
               : "Calibrated, but could not save to flash.\nIt will be lost at power-off.");
}

// Carries out what the model asked for. `why` names who cancelled, for CANCEL.
void act(Action action, const char *why) {
  switch (action) {
  case Action::NONE:
    return;
  case Action::BEGIN:
    running = true;
    lv_subject_set_int(&running_subject, 1);
    show_prompt();
    lv_timer_resume(timer);
    logger.info("calibration started");
    return;
  case Action::NEXT:
    feedback();
    show_prompt();
    return;
  case Action::COMPLETE:
    complete();
    return;
  case Action::REJECT: // every end was taken past kFullTravelMv, so not expected
    logger.error("calibration rejected: {}", describe(run.record()));
    finish("Calibration failed: travel too short.\nNothing changed.");
    return;
  case Action::TIME_OUT:
    logger.warn("calibration timed out at step {}", run.from_step());
    finish("Timed out.\nNothing changed.");
    return;
  case Action::CANCEL:
    logger.info("calibration cancelled ({})", why);
    finish("Calibration cancelled.\nNothing changed.");
    return;
  case Action::SHOW_IDLE:
    lv_subject_copy_string(&text_subject, idle_text.c_str());
    lv_timer_pause(timer);
    return;
  case Action::PAUSE:
    lv_timer_pause(timer);
    return;
  }
}

void start() { act(run.handle(Input::START), nullptr); }

void tick_cb(lv_timer_t *) {
  const hmi::cal::Sample mv = {latest_mv[0].load(), latest_mv[1].load(), latest_mv[2].load()};
  act(run.handle(Input::TICK, mv), nullptr);
}

void cancel(const char *why) {
  if (running) {
    act(run.handle(Input::CANCEL), why);
  }
}

void button_cb(lv_event_t *) {
  if (running) {
    cancel("button");
  } else {
    start();
  }
}

void screen_unload_cb(lv_event_t *) { cancel("left the screen"); }

// Blinks on opacity rather than colour, like the TopBar's RTPS label: the text
// keeps the theme's colour and the label never reflows.
void instructions_observer(lv_observer_t *observer, lv_subject_t *) {
  const bool visible = lv_subject_get_int(&prompting_subject) == 0 || lv_subject_get_int(ui.blink);
  lv_obj_set_style_text_opa(lv_observer_get_target_obj(observer),
                            visible ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
}

void button_label_observer(lv_observer_t *observer, lv_subject_t *subject) {
  lv_label_set_text(lv_observer_get_target_obj(observer),
                    lv_subject_get_int(subject) ? "CANCEL" : button_text.c_str());
}

} // namespace

JoystickCal joystick_cal_load(const JoystickCal &defaults) {
  const std::string path = storage_path(kFileName);
  const auto saved = read_file(path);
  std::lock_guard<std::mutex> lock(cal_mutex);
  if (saved && plausible(*saved)) {
    current = *saved;
    in_use = InUse::SAVED;
    logger.info("loaded {}: {}", path, describe(current));
  } else {
    current = defaults;
    in_use = InUse::DEFAULTS;
    if (saved) {
      logger.warn("{} ignored, travel under {:.0f} mV: {}", path, kFullTravelMv, describe(*saved));
    } else {
      logger.warn("no joystick calibration saved; using defaults until CALIBRATE is run");
    }
  }
  return current;
}

bool joystick_cal_saved() { return in_use.load() == InUse::SAVED; }

bool joystick_cal_measured() { return in_use.load() != InUse::DEFAULTS; }

void joystick_cal_forget_measured() { in_use = InUse::DEFAULTS; }

JoystickCal joystick_cal_current() {
  std::lock_guard<std::mutex> lock(cal_mutex);
  return current;
}

void joystick_cal_init_ui(const JoystickCalUi &config) {
  if (!config.screen || !config.instructions || !config.blink) {
    logger.error("JoystickScreen widgets missing from the UI export; CALIBRATE is disabled");
    return;
  }
  ui = config;
  idle_text = lv_label_get_text(ui.instructions);
  button_text = ui.button_label != nullptr ? lv_label_get_text(ui.button_label) : "CALIBRATE";
  lv_subject_init_string(&text_subject, text_buf, text_prev_buf, sizeof(text_buf),
                         idle_text.c_str());
  lv_subject_init_int(&prompting_subject, 0);
  lv_subject_init_int(&running_subject, 0);
  lv_label_bind_text(ui.instructions, &text_subject, nullptr);
  lv_subject_add_observer_obj(&prompting_subject, instructions_observer, ui.instructions, nullptr);
  lv_subject_add_observer_obj(ui.blink, instructions_observer, ui.instructions, nullptr);
  // Optional since spec V2: the menu row starts the run instead.
  if (ui.button_label != nullptr) {
    lv_subject_add_observer_obj(&running_subject, button_label_observer, ui.button_label, nullptr);
  }
  if (ui.button != nullptr) {
    lv_obj_add_event_cb(ui.button, button_cb, LV_EVENT_CLICKED, nullptr);
  }
  lv_obj_add_event_cb(ui.screen, screen_unload_cb, LV_EVENT_SCREEN_UNLOAD_START, nullptr);
  timer = lv_timer_create(tick_cb, kTickMs, nullptr);
  lv_timer_pause(timer);
}

void joystick_cal_toggle() {
  if (ui.screen == nullptr) {
    return; // no UI bound: nothing to prompt with
  }
  if (running) {
    cancel("menu");
  } else {
    start();
  }
}

void joystick_cal_note_raw(float horizontal_mv, float vertical_mv, float twist_mv) {
  latest_mv[JOY_HORIZONTAL].store(horizontal_mv, std::memory_order_relaxed);
  latest_mv[JOY_VERTICAL].store(vertical_mv, std::memory_order_relaxed);
  latest_mv[JOY_TWIST].store(twist_mv, std::memory_order_relaxed);
}

bool joystick_cal_running() { return running.load(); }

std::optional<JoystickCal> joystick_cal_take_new() {
  std::lock_guard<std::mutex> lock(cal_mutex);
  return std::exchange(pending, std::nullopt);
}
