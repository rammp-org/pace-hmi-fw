#include "joystick_cal.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <utility>

#include "logger.hpp"
#include "storage.hpp"

namespace {

espp::Logger logger({.tag = "joy_cal", .level = espp::Logger::Verbosity::INFO});

constexpr int kFileVersion = 1;
constexpr const char *kFileName = "joystick_cal.txt";
constexpr const char *kAxisNames[3] = {"horizontal", "vertical", "twist"};

// The run ticks at the ADC task's 33 ms, so every tick sees a fresh sample.
constexpr uint32_t kTickMs = 33;
constexpr int kTicksPerSecond = 1000 / kTickMs;
// Time to read the first prompt and take a hand off the stick before the rest
// position is sampled.
constexpr int kSettleTicks = kTicksPerSecond * 3 / 2;
// A position is taken once the axis has held it this long...
constexpr int kHoldTicks = kTicksPerSecond;
// ...moving no more than this, peak to peak. Twist is the noisy pot.
constexpr std::array<float, 3> kSteadyMv = {30.0f, 30.0f, 60.0f};
// How far past rest counts as pushed "fully". The bench stick travels ~1470 mV
// each way from rest, so this needs a clear push to the gate - a stick held
// half-way is not taken for the end - while leaving room for a unit with a
// shorter throw. Also the least travel a saved calibration may claim.
constexpr float kFullTravelMv = 1000.0f;
// Within this of rest counts as let go, for the last step.
constexpr float kReleasedMv = 150.0f;
constexpr int kStepTimeoutTicks = kTicksPerSecond * 20;
// How long the result stays up before the screen's own text comes back.
constexpr int kResultTicks = kTicksPerSecond * 3;
constexpr int kStepCount = 8; // let go, six directions, let go

struct Direction {
  JoystickAxis axis;
  float sign; // +1: this end reads above rest
  const char *prompt;
};
// Signs follow main.cpp's AXIS WIRING note: horizontal reads higher to the
// right, vertical reads LOWER pushed forward, twist reads higher clockwise.
constexpr Direction kDirections[] = {
    {JOY_HORIZONTAL, -1.0f, "Push the joystick fully LEFT and hold it"},
    {JOY_HORIZONTAL, +1.0f, "Push the joystick fully RIGHT and hold it"},
    {JOY_VERTICAL, -1.0f, "Push the joystick fully FORWARD and hold it"},
    {JOY_VERTICAL, +1.0f, "Pull the joystick fully BACK and hold it"},
    {JOY_TWIST, +1.0f, "Twist the joystick fully CLOCKWISE and hold it"},
    {JOY_TWIST, -1.0f, "Twist the joystick fully COUNTER-CLOCKWISE and hold it"},
};
constexpr int kDirectionCount = static_cast<int>(sizeof(kDirections) / sizeof(kDirections[0]));

// Shared between tasks.
std::mutex cal_mutex;
JoystickCal current{};              // in use
bool current_saved = false;         // `current` is what the file holds
std::optional<JoystickCal> pending; // a finished run, for the ADC task to apply
std::atomic<bool> running{false};
std::atomic<float> latest_mv[3];

std::string describe(const JoystickCal &cal) {
  std::string out;
  for (int i = 0; i < 3; ++i) {
    out += fmt::format("{}{} {:.0f}/{:.0f}/{:.0f}", i ? ", " : "", kAxisNames[i], cal[i].min_mv,
                       cal[i].center_mv, cal[i].max_mv);
  }
  return out + " mV";
}

bool plausible(const JoystickCal &cal) {
  return std::all_of(cal.begin(), cal.end(), [](const JoystickAxisCal &a) {
    return a.center_mv - a.min_mv >= kFullTravelMv && a.max_mv - a.center_mv >= kFullTravelMv;
  });
}

// The next word that is not part of a # comment.
bool next_word(std::istream &in, std::string &word) {
  while (in >> word) {
    if (word[0] != '#') {
      return true;
    }
    in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  }
  return false;
}

std::optional<JoystickCal> read_file(const std::string &path) {
  std::ifstream in(path);
  if (!in) {
    return std::nullopt;
  }
  std::string word;
  int version = 0;
  if (!next_word(in, word) || word != "version" || !(in >> version) || version != kFileVersion) {
    logger.warn("{}: not a version {} calibration file", path, kFileVersion);
    return std::nullopt;
  }
  JoystickCal cal{};
  for (int i = 0; i < 3; ++i) {
    if (!next_word(in, word) || word != kAxisNames[i] ||
        !(in >> cal[i].min_mv >> cal[i].center_mv >> cal[i].max_mv)) {
      logger.warn("{}: expected a '{} min center max' line", path, kAxisNames[i]);
      return std::nullopt;
    }
  }
  return cal;
}

bool write_file(const JoystickCal &cal) {
  std::string text = fmt::format("# joystick calibration, raw ADC mV: min center max\n"
                                 "version {}\n",
                                 kFileVersion);
  for (int i = 0; i < 3; ++i) {
    text += fmt::format("{} {:.1f} {:.1f} {:.1f}\n", kAxisNames[i], cal[i].min_mv, cal[i].center_mv,
                        cal[i].max_mv);
  }
  return storage_write(kFileName, text);
}

/////////////////////////////////////////////////////////////////////////////
// The run. Everything below is the LVGL task's: the timer, the button and the
// subjects all run under the lock lv_timer_handler() is called with.
/////////////////////////////////////////////////////////////////////////////

enum class Phase { IDLE, REST, DIRECTION, RELEASE, RESULT };

// The last kHoldTicks samples of all three axes.
struct Window {
  std::array<std::array<float, 3>, kHoldTicks> samples{};
  int count = 0;
  int next = 0;

  void clear() { count = next = 0; }
  void push(const std::array<float, 3> &mv) {
    samples[next] = mv;
    next = (next + 1) % kHoldTicks;
    count = std::min(count + 1, kHoldTicks);
  }
  bool full() const { return count == kHoldTicks; }
  float spread(int axis) const {
    float lo = samples[0][axis], hi = lo;
    for (int i = 1; i < count; ++i) {
      lo = std::min(lo, samples[i][axis]);
      hi = std::max(hi, samples[i][axis]);
    }
    return hi - lo;
  }
  float mean(int axis) const {
    float sum = 0.0f;
    for (int i = 0; i < count; ++i) {
      sum += samples[i][axis];
    }
    return sum / static_cast<float>(count);
  }
};

struct Run {
  Phase phase = Phase::IDLE;
  int direction = 0; // index into kDirections, in the DIRECTION phase
  int ticks = 0;     // since the current step began
  Window window;
  JoystickCal cal{};
};
Run run;

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

void begin_step(Phase phase) {
  run.phase = phase;
  run.ticks = 0;
  run.window.clear();
}

void show_prompt(int step, const char *text) {
  const std::string prompt = fmt::format("Step {} of {}\n{}", step, kStepCount, text);
  lv_subject_copy_string(&text_subject, prompt.c_str());
  lv_subject_set_int(&prompting_subject, 1);
}

void finish(const char *message) {
  running = false;
  begin_step(Phase::RESULT);
  lv_subject_set_int(&prompting_subject, 0);
  lv_subject_set_int(&running_subject, 0);
  lv_subject_copy_string(&text_subject, message);
}

void start() {
  run = Run{};
  begin_step(Phase::REST);
  running = true;
  lv_subject_set_int(&running_subject, 1);
  show_prompt(1, "Let go of the joystick and keep still");
  lv_timer_resume(timer);
  logger.info("calibration started");
}

void next_direction(int index) {
  run.direction = index;
  begin_step(Phase::DIRECTION);
  show_prompt(index + 2, kDirections[index].prompt);
}

void complete() {
  const JoystickCal &cal = run.cal;
  if (!plausible(cal)) { // every end was taken past kFullTravelMv, so not expected
    logger.error("calibration rejected: {}", describe(cal));
    finish("Calibration failed: travel too short.\nNothing changed.");
    return;
  }
  const bool saved = write_file(cal);
  {
    std::lock_guard<std::mutex> lock(cal_mutex);
    current = cal;
    current_saved = saved;
    pending = cal;
  }
  logger.info("calibrated{}: {}", saved ? "" : " (NOT saved)", describe(cal));
  finish(saved ? "Calibration saved"
               : "Calibrated, but could not save to flash.\nIt will be lost at power-off.");
}

void tick_rest(const std::array<float, 3> &mv) {
  if (run.ticks <= kSettleTicks) {
    return;
  }
  run.window.push(mv);
  if (!run.window.full()) {
    return;
  }
  for (int axis = 0; axis < 3; ++axis) {
    if (run.window.spread(axis) > kSteadyMv[axis]) {
      return; // still being touched; the window slides on
    }
  }
  for (int axis = 0; axis < 3; ++axis) {
    run.cal[axis].center_mv = run.window.mean(axis);
  }
  feedback();
  next_direction(0);
}

void tick_direction(const std::array<float, 3> &mv) {
  const Direction &d = kDirections[run.direction];
  if ((mv[d.axis] - run.cal[d.axis].center_mv) * d.sign < kFullTravelMv) {
    run.window.clear(); // not there yet, or let go early: start the hold again
    return;
  }
  run.window.push(mv);
  if (!run.window.full() || run.window.spread(d.axis) > kSteadyMv[d.axis]) {
    return;
  }
  (d.sign > 0 ? run.cal[d.axis].max_mv : run.cal[d.axis].min_mv) = run.window.mean(d.axis);
  feedback();
  if (run.direction + 1 < kDirectionCount) {
    next_direction(run.direction + 1);
  } else {
    begin_step(Phase::RELEASE);
    show_prompt(kStepCount, "Let go of the joystick");
  }
}

void tick_release(const std::array<float, 3> &mv) {
  for (int axis = 0; axis < 3; ++axis) {
    if (std::fabs(mv[axis] - run.cal[axis].center_mv) > kReleasedMv) {
      run.window.clear();
      return;
    }
  }
  run.window.push(mv);
  if (run.window.full()) {
    complete();
  }
}

void tick_cb(lv_timer_t *) {
  const std::array<float, 3> mv = {latest_mv[0].load(), latest_mv[1].load(), latest_mv[2].load()};
  run.ticks++;
  switch (run.phase) {
  case Phase::IDLE:
    lv_timer_pause(timer);
    return;
  case Phase::RESULT:
    if (run.ticks >= kResultTicks) {
      lv_subject_copy_string(&text_subject, idle_text.c_str());
      run.phase = Phase::IDLE;
      lv_timer_pause(timer);
    }
    return;
  case Phase::REST:
    tick_rest(mv);
    break;
  case Phase::DIRECTION:
    tick_direction(mv);
    break;
  case Phase::RELEASE:
    tick_release(mv);
    break;
  }
  if (run.phase != Phase::RESULT && run.ticks > kStepTimeoutTicks) {
    logger.warn("calibration timed out at step {}", run.phase == Phase::DIRECTION
                                                        ? run.direction + 2
                                                        : (run.phase == Phase::REST ? 1 : 8));
    finish("Timed out.\nNothing changed.");
  }
}

void cancel(const char *why) {
  if (running) {
    logger.info("calibration cancelled ({})", why);
    finish("Calibration cancelled.\nNothing changed.");
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
    current_saved = true;
    logger.info("loaded {}: {}", path, describe(current));
  } else {
    current = defaults;
    current_saved = false;
    if (saved) {
      logger.warn("{} ignored, travel under {:.0f} mV: {}", path, kFullTravelMv, describe(*saved));
    } else {
      logger.warn("no joystick calibration saved; using defaults until CALIBRATE is run");
    }
  }
  return current;
}

bool joystick_cal_saved() {
  std::lock_guard<std::mutex> lock(cal_mutex);
  return current_saved;
}

JoystickCal joystick_cal_current() {
  std::lock_guard<std::mutex> lock(cal_mutex);
  return current;
}

void joystick_cal_init_ui(const JoystickCalUi &config) {
  if (!config.screen || !config.button || !config.button_label || !config.instructions ||
      !config.blink) {
    logger.error("JoystickTest widgets missing from the UI export; CALIBRATE is disabled");
    return;
  }
  ui = config;
  idle_text = lv_label_get_text(ui.instructions);
  button_text = lv_label_get_text(ui.button_label);
  lv_subject_init_string(&text_subject, text_buf, text_prev_buf, sizeof(text_buf),
                         idle_text.c_str());
  lv_subject_init_int(&prompting_subject, 0);
  lv_subject_init_int(&running_subject, 0);
  lv_label_bind_text(ui.instructions, &text_subject, nullptr);
  lv_subject_add_observer_obj(&prompting_subject, instructions_observer, ui.instructions, nullptr);
  lv_subject_add_observer_obj(ui.blink, instructions_observer, ui.instructions, nullptr);
  lv_subject_add_observer_obj(&running_subject, button_label_observer, ui.button_label, nullptr);
  lv_obj_add_event_cb(ui.button, button_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(ui.screen, screen_unload_cb, LV_EVENT_SCREEN_UNLOAD_START, nullptr);
  timer = lv_timer_create(tick_cb, kTickMs, nullptr);
  lv_timer_pause(timer);
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
