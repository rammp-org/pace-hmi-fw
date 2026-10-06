// L1 characterisation of the joystick calibration as it is at dev_refactor fa3f13a
// (main/joystick_cal.cpp): the file codec, the plausibility check, the boot log line the bench
// greps for, and the calibration run. Every case goes through main/joystick_cal.hpp only, with
// LVGL and the storage partition replaced by the fakes in fakes/ (see the Makefile).
//
// These pin TODAY's behaviour, hazards included: a case whose name says HAZARD pins something
// the firmware should probably not do, kept as it is until a behaviour change is approved
// (plan §3.3, H3). Expected values come from the code and from board 2's boot log
// (tests/characterisation/baseline-e2047a4/boot-board2.log:129).
//
// The module under test keeps its state in file-scope statics, so each case starts by bringing
// the run back to idle (start_clean) and never relies on another case having run (TS-DET-03).

#include <unistd.h>

#include <array>
#include <cstdio>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <string_view>

#include "fake_storage.hpp"
#include "joystick_cal.hpp"
#include "lvgl.h"
#include "storage.hpp"
#include "test_case.hpp"

namespace {

// --- the record of board 2 (boot-board2.log:129) --------------------------------------------

const JoystickCal kBoard2{
    {{11.0f, 1507.0f, 2971.0f}, {6.0f, 1510.0f, 2962.0f}, {10.0f, 1477.0f, 2960.0f}}};
constexpr std::string_view kBoard2Text = "horizontal 11/1507/2971, vertical 6/1510/2962, "
                                         "twist 10/1477/2960 mV";
// What the run writes for it: the golden file.
constexpr std::string_view kBoard2File = "# joystick calibration, raw ADC mV: min center max\n"
                                         "version 1\n"
                                         "horizontal 11.0 1507.0 2971.0\n"
                                         "vertical 6.0 1510.0 2962.0\n"
                                         "twist 10.0 1477.0 2960.0\n";
const JoystickCal kDefaults{
    {{100.0f, 1600.0f, 3100.0f}, {101.0f, 1601.0f, 3101.0f}, {102.0f, 1602.0f, 3102.0f}}};
constexpr const char *kFileName = "joystick_cal.txt";
constexpr const char *kIdleText = "Hold CALIBRATE to calibrate";

// The run's constants as the code derives them: 1000 / 33 = 30 periods per second.
constexpr int kSettleTicks = 45;
constexpr int kHoldTicks = 30;
constexpr int kTimeoutTicks = 600;
constexpr int kResultTicks = 90;

bool same(const JoystickCal &a, const JoystickCal &b) {
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].min_mv != b[i].min_mv || a[i].center_mv != b[i].center_mv ||
        a[i].max_mv != b[i].max_mv) {
      return false;
    }
  }
  return true;
}

bool travel_at_least_1000(const JoystickCal &c) {
  for (const JoystickAxisCal &a : c) {
    if (!(a.center_mv - a.min_mv >= 1000.0f && a.max_mv - a.center_mv >= 1000.0f)) {
      return false;
    }
  }
  return true;
}

bool starts_with(const std::string &s, std::string_view prefix) { return s.rfind(prefix, 0) == 0; }

bool contains(const std::string &haystack, std::string_view needle) {
  return haystack.find(needle) != std::string::npos;
}

// Captures what the code prints (espp::Logger writes to stdout) from construction to stop().
class StdoutCapture {
public:
  StdoutCapture() {
    std::fflush(stdout);
    saved_ = ::dup(1);
    file_ = std::tmpfile();
    if (saved_ < 0 || file_ == nullptr || ::dup2(::fileno(file_), 1) < 0) {
      TEST_FAIL_MESSAGE("could not redirect stdout");
    }
  }
  StdoutCapture(const StdoutCapture &) = delete;
  StdoutCapture &operator=(const StdoutCapture &) = delete;
  ~StdoutCapture() { (void)stop(); } // the text is not wanted when stop() was not called

  std::string stop() {
    if (file_ == nullptr) {
      return text_;
    }
    std::fflush(stdout);
    (void)::dup2(saved_, 1); // restoring stdout: nothing better to do if it fails
    ::close(saved_);
    std::rewind(file_);
    std::array<char, 4096> buf{};
    std::size_t n = 0;
    while ((n = std::fread(buf.data(), 1, buf.size(), file_)) > 0) {
      text_.append(buf.data(), n);
    }
    std::fclose(file_);
    file_ = nullptr;
    return text_;
  }

private:
  int saved_ = -1;
  std::FILE *file_ = nullptr;
  std::string text_;
};

// --- the JoystickScreen widgets, bound once per process ------------------------------------

struct Widgets {
  lv_obj_t screen;
  lv_obj_t button;
  lv_obj_t button_label;
  lv_obj_t instructions;
  lv_subject_t blink;
  int feedback_count = 0;
  lv_timer_t *timer = nullptr;
};

Widgets &widgets() {
  static Widgets w;
  static bool bound = false;
  if (!bound) {
    bound = true;
    w.instructions.text = kIdleText;
    w.button_label.text = "CALIBRATE";
    lv_subject_init_int(&w.blink, 1);
    joystick_cal_init_ui({.screen = &w.screen,
                          .button = &w.button,
                          .button_label = &w.button_label,
                          .instructions = &w.instructions,
                          .blink = &w.blink,
                          .feedback = [] { ++w.feedback_count; }});
    w.timer = fake_lvgl::last_timer();
  }
  return w;
}

std::string shown() { return fake_lvgl::label_text(&widgets().instructions); }

std::string button_shows() { return fake_lvgl::label_text(&widgets().button_label); }

// One LVGL period, with the ADC task having noted (h, v, t) just before it.
bool tick(float h, float v, float t) {
  joystick_cal_note_raw(h, v, t);
  return fake_lvgl::tick(widgets().timer);
}

// `n` periods at one position; returns how many ran (a paused timer runs none).
int hold(float h, float v, float t, int n) {
  int ran = 0;
  for (int i = 0; i < n; ++i) {
    ran += tick(h, v, t) ? 1 : 0;
  }
  return ran;
}

// Back to idle with nothing pending, whatever an earlier case left.
void start_clean() {
  Widgets &w = widgets();
  if (joystick_cal_running()) {
    joystick_cal_toggle();
  }
  for (int i = 0; i < 1000 && fake_lvgl::tick(w.timer); ++i) {
  }
  TEST_ASSERT_TRUE_MESSAGE(w.timer->paused, "the run did not come back to idle");
  (void)joystick_cal_take_new(); // drop what an earlier case left
  lv_subject_set_int(&w.blink, 1);
  w.feedback_count = 0;
  fake_storage::set_write_fails(false);
  fake_storage::remove(kFileName);
}

// Rest at board 2's centres: taken on the 75th period.
void do_rest() { (void)hold(1507.0f, 1510.0f, 1477.0f, kSettleTicks + kHoldTicks); }

// Board 2's six ends, in the order the run asks for them.
constexpr std::array<std::array<float, 3>, 6> kEnds{{
    {11.0f, 1510.0f, 1477.0f},   // left: horizontal min
    {2971.0f, 1510.0f, 1477.0f}, // right: horizontal max
    {1507.0f, 6.0f, 1477.0f},    // forward: vertical min
    {1507.0f, 2962.0f, 1477.0f}, // back: vertical max
    {1507.0f, 1510.0f, 2960.0f}, // clockwise: twist max
    {1507.0f, 1510.0f, 10.0f},   // counter-clockwise: twist min
}};

void do_directions(int count = 6) {
  for (int d = 0; d < count; ++d) {
    const auto &e = kEnds[static_cast<std::size_t>(d)];
    (void)hold(e[0], e[1], e[2], kHoldTicks);
  }
}

void do_release() { (void)hold(1507.0f, 1510.0f, 1477.0f, kHoldTicks); }

// Loads `text` as the saved file; returns what is in use and, optionally, what was logged.
JoystickCal load_text(std::string_view text, std::string *log = nullptr) {
  fake_storage::put(kFileName, text);
  StdoutCapture cap;
  const JoystickCal cal = joystick_cal_load(kDefaults);
  const std::string out = cap.stop();
  if (log != nullptr) {
    *log = out;
  }
  return cal;
}

// Whether `text` loads as a saved calibration; a rejected one must leave the defaults in use.
bool accepted(std::string_view text) {
  const JoystickCal cal = load_text(text);
  const bool saved = joystick_cal_saved();
  TEST_ASSERT_TRUE_MESSAGE(saved || same(cal, kDefaults), "rejected, but not the defaults");
  return saved;
}

std::string golden_with(std::string_view horizontal, std::string_view vertical,
                        std::string_view twist) {
  return "version 1\nhorizontal " + std::string(horizontal) + "\nvertical " +
         std::string(vertical) + "\ntwist " + std::string(twist) + "\n";
}

} // namespace

// ============================================================================================
// The file and the boot log line
// ============================================================================================

TEST_CASE("CAL-001 a run at board 2's positions writes exactly the golden file", "[cal][codec]") {
  start_clean();
  const int writes = fake_storage::write_calls();
  joystick_cal_toggle();
  do_rest();
  do_directions();
  StdoutCapture cap;
  do_release();
  const std::string log = cap.stop();
  TEST_ASSERT_EQUAL_INT(writes + 1, fake_storage::write_calls());
  TEST_ASSERT_EQUAL_STRING(std::string(kBoard2File).c_str(), fake_storage::last_written().c_str());
  TEST_ASSERT_TRUE(contains(log, "[joy_cal/I]"));
  TEST_ASSERT_TRUE_MESSAGE(contains(log, "]: calibrated: " + std::string(kBoard2Text) + "\n"),
                           log.c_str());
}

TEST_CASE("CAL-002 board 2's file loads as its record and logs the boot line byte for byte",
          "[cal][codec]") {
  start_clean();
  std::string log;
  const JoystickCal cal = load_text(kBoard2File, &log);
  TEST_ASSERT_TRUE(same(cal, kBoard2));
  TEST_ASSERT_TRUE(joystick_cal_saved());
  TEST_ASSERT_TRUE(same(joystick_cal_current(), kBoard2));
  // boot-board2.log:129, with the fake's folder in place of /storage
  const std::string line =
      "]: loaded " + storage_path(kFileName) + ": " + std::string(kBoard2Text) + "\n";
  TEST_ASSERT_TRUE_MESSAGE(contains(log, line), log.c_str());
  TEST_ASSERT_TRUE(contains(log, "[joy_cal/I]"));
}

TEST_CASE("CAL-003 board 2's record round-trips: run, write, load give the same numbers",
          "[cal][codec]") {
  start_clean();
  joystick_cal_toggle();
  do_rest();
  do_directions();
  do_release();
  const std::string written = fake_storage::last_written();
  const JoystickCal cal = load_text(written);
  TEST_ASSERT_TRUE(same(cal, kBoard2));
  TEST_ASSERT_TRUE(joystick_cal_saved());
}

TEST_CASE("CAL-004 no saved file falls back to the defaults and says so", "[cal][codec]") {
  start_clean();
  StdoutCapture cap;
  const JoystickCal cal = joystick_cal_load(kDefaults);
  const std::string log = cap.stop();
  TEST_ASSERT_TRUE(same(cal, kDefaults));
  TEST_ASSERT_FALSE(joystick_cal_saved());
  TEST_ASSERT_TRUE(same(joystick_cal_current(), kDefaults));
  TEST_ASSERT_TRUE(contains(log, "[joy_cal/W]"));
  TEST_ASSERT_TRUE(
      contains(log, "]: no joystick calibration saved; using defaults until CALIBRATE is run\n"));
}

TEST_CASE("CAL-005 an empty file falls back to the defaults as not a version 1 file",
          "[cal][codec][hostile]") {
  start_clean();
  std::string log;
  const JoystickCal cal = load_text("", &log);
  TEST_ASSERT_TRUE(same(cal, kDefaults));
  TEST_ASSERT_FALSE(joystick_cal_saved());
  TEST_ASSERT_TRUE_MESSAGE(
      contains(log, "]: " + storage_path(kFileName) + ": not a version 1 calibration file\n"),
      log.c_str());
  TEST_ASSERT_TRUE(contains(log, "no joystick calibration saved"));
}

namespace {
struct HeaderCase {
  const char *text;
  bool accepted;
  const char *warning; // the warning a rejection logs, or nullptr
};
// What follows each header: board 2's three lines.
constexpr const char *kBody =
    "\nhorizontal 11 1507 2971\nvertical 6 1510 2962\ntwist 10 1477 2960\n";
constexpr std::array<HeaderCase, 12> kHeaders{{
    {"version 1", true, nullptr},
    {"version 01", true, nullptr}, // read as an int
    {"version +1", true, nullptr}, // read as an int
    {"# a comment\n# another\nversion 1", true, nullptr},
    {"#no space\nversion 1", true, nullptr},
    {"version 2", false, "not a version 1 calibration file"},
    {"version 0", false, "not a version 1 calibration file"},
    {"version", false, "not a version 1 calibration file"}, // "horizontal" is not an int
    {"VERSION 1", false, "not a version 1 calibration file"},
    {"versio 1", false, "not a version 1 calibration file"},
    {"version 1.0", false, "expected a 'horizontal min center max' line"}, // ".0" is a word
    {"", false, "not a version 1 calibration file"},                       // no header at all
}};
} // namespace

TEST_CASE("CAL-006 the header is the word version then the integer 1", "[cal][codec][hostile]") {
  start_clean();
  for (const HeaderCase &c : kHeaders) {
    std::string log;
    (void)load_text(std::string(c.text) + kBody, &log);
    TEST_ASSERT_EQUAL_MESSAGE(c.accepted, joystick_cal_saved(), c.text);
    if (c.warning != nullptr) {
      TEST_ASSERT_TRUE_MESSAGE(contains(log, c.warning), c.text);
    }
  }
}

TEST_CASE("CAL-007 every truncation of the golden file is rejected, except cuts inside the last "
          "number's fraction",
          "[cal][codec][hostile]") {
  start_clean();
  // "...twist 10.0 1477.0 2960.0\n" cut to "2960", "2960." or "2960.0" still reads 2960.
  const std::size_t full = kBoard2File.size();
  StdoutCapture quiet;
  for (std::size_t len = 0; len < full; ++len) {
    const bool ok = accepted(kBoard2File.substr(0, len));
    TEST_ASSERT_EQUAL_MESSAGE(len >= full - 3, ok, std::to_string(len).c_str());
    if (ok) {
      TEST_ASSERT_TRUE(same(joystick_cal_current(), kBoard2));
    }
  }
}

TEST_CASE("CAL-008 comments, blank lines, tabs, CRLF and one-line layouts are all accepted",
          "[cal][codec]") {
  start_clean();
  constexpr std::array<const char *, 5> kLayouts{{
      "version 1\n# c\nhorizontal 11 1507 2971\n\n# c\nvertical 6 1510 2962\ntwist 10 1477 2960",
      "version 1 horizontal 11 1507 2971 vertical 6 1510 2962 twist 10 1477 2960",
      "\tversion\t1\r\nhorizontal\t11\t1507\t2971\r\nvertical 6 1510 2962\r\ntwist 10 1477 "
      "2960\r\n",
      "version\n1\nhorizontal\n11\n1507\n2971\nvertical\n6\n1510\n2962\ntwist\n10\n1477\n2960",
      "version 1\nhorizontal 11 1507 2971 # a comment after the numbers\nvertical 6 1510 2962\n"
      "twist 10 1477 2960",
  }};
  for (const char *text : kLayouts) {
    TEST_ASSERT_TRUE_MESSAGE(accepted(text), text);
    TEST_ASSERT_TRUE(same(joystick_cal_current(), kBoard2));
  }
}

TEST_CASE("CAL-009 axis lines out of order or misnamed are rejected naming the expected axis",
          "[cal][codec][hostile]") {
  start_clean();
  struct Bad {
    const char *text;
    const char *warning;
  };
  constexpr std::array<Bad, 5> kBad{{
      {"version 1\nvertical 6 1510 2962\nhorizontal 11 1507 2971\ntwist 10 1477 2960",
       "expected a 'horizontal min center max' line"},
      {"version 1\nhorizontal 11 1507 2971\ntwist 10 1477 2960\nvertical 6 1510 2962",
       "expected a 'vertical min center max' line"},
      {"version 1\nhorizontal 11 1507 2971\nvertical 6 1510 2962\nTwist 10 1477 2960",
       "expected a 'twist min center max' line"},
      {"version 1\nhorizontal 11 1507 2971\nvertical 6 1510 2962\n",
       "expected a 'twist min center max' line"},
      {"version 1\nhorizontal 11 1507 2971\n# vertical 6 1510 2962\ntwist 10 1477 2960",
       "expected a 'vertical min center max' line"},
  }};
  for (const Bad &b : kBad) {
    std::string log;
    (void)load_text(b.text, &log);
    TEST_ASSERT_FALSE_MESSAGE(joystick_cal_saved(), b.text);
    TEST_ASSERT_TRUE_MESSAGE(contains(log, b.warning), b.text);
    TEST_ASSERT_TRUE(contains(log, "[joy_cal/W]"));
  }
}

TEST_CASE("CAL-010 numbers that are not floats are rejected: words, nan, inf, overflow, missing",
          "[cal][codec][hostile]") {
  start_clean();
  constexpr std::array<const char *, 10> kBadHorizontal{{
      "11 1507 x",
      "11 1507", // then "vertical" is not a number
      "eleven 1507 2971",
      "nan 1507 2971",
      "11 1507 inf",
      "11 1507 1e39", // above FLT_MAX
      "-1e39 1507 2971",
      "11 1507 0x2971", // reads 0, then "x2971" is not the word vertical
      "11, 1507, 2971", // the comma stops the number
      "",
  }};
  for (const char *h : kBadHorizontal) {
    std::string log;
    (void)load_text(golden_with(h, "6 1510 2962", "10 1477 2960"), &log);
    TEST_ASSERT_FALSE_MESSAGE(joystick_cal_saved(), h);
    TEST_ASSERT_TRUE_MESSAGE(contains(log, "expected a '"), h);
  }
}

TEST_CASE("CAL-011 the travel limit is 1000 mV each way from rest, on every axis, inclusive",
          "[cal][plausible]") {
  start_clean();
  // exactly 1000 each way: accepted
  TEST_ASSERT_TRUE(accepted(golden_with("500 1500 2500", "500 1500 2500", "500 1500 2500")));
  // 999.9 on one side of one axis: rejected, and logged with the limit and the record
  constexpr std::array<std::array<const char *, 3>, 6> kShort{{
      {"500.1 1500 2500", "500 1500 2500", "500 1500 2500"},
      {"500 1500 2499.9", "500 1500 2500", "500 1500 2500"},
      {"500 1500 2500", "500.1 1500 2500", "500 1500 2500"},
      {"500 1500 2500", "500 1500 2499.9", "500 1500 2500"},
      {"500 1500 2500", "500 1500 2500", "500.1 1500 2500"},
      {"500 1500 2500", "500 1500 2500", "500 1500 2499.9"},
  }};
  for (const auto &s : kShort) {
    std::string log;
    const JoystickCal cal = load_text(golden_with(s[0], s[1], s[2]), &log);
    TEST_ASSERT_FALSE_MESSAGE(joystick_cal_saved(), s[0]);
    TEST_ASSERT_TRUE(same(cal, kDefaults));
    TEST_ASSERT_TRUE_MESSAGE(contains(log, "]: " + storage_path(kFileName) +
                                               " ignored, travel under 1000 mV: horizontal "),
                             log.c_str());
  }
}

TEST_CASE("CAL-012 a reversed or collapsed axis is rejected", "[cal][plausible][hostile]") {
  start_clean();
  StdoutCapture quiet;
  TEST_ASSERT_FALSE(accepted(golden_with("2971 1507 11", "6 1510 2962", "10 1477 2960")));
  TEST_ASSERT_FALSE(accepted(golden_with("1507 1507 1507", "6 1510 2962", "10 1477 2960")));
  TEST_ASSERT_FALSE(accepted(golden_with("0 0 0", "0 0 0", "0 0 0")));
}

TEST_CASE("CAL-013 HAZARD (H3) a record far outside the ADC's range is accepted: no upper bound",
          "[cal][plausible][hazard]") {
  start_clean();
  // The ADC reads about 0..3300 mV; nothing checks a saved record against that.
  TEST_ASSERT_TRUE(accepted(golden_with("11 1507 9999", "6 1510 2962", "10 1477 2960")));
  TEST_ASSERT_TRUE(accepted(golden_with("-5000 1507 2971", "6 1510 2962", "10 1477 2960")));
  TEST_ASSERT_TRUE(accepted(golden_with("-1e30 0 1e30", "-1e30 0 1e30", "-1e30 0 1e30")));
  TEST_ASSERT_TRUE(accepted(golden_with("100000 101000 102000", "6 1510 2962", "10 1477 2960")));
  TEST_ASSERT_EQUAL_FLOAT(101000.0f, joystick_cal_current()[JOY_HORIZONTAL].center_mv);
}

TEST_CASE("CAL-014 anything after the twist line is ignored", "[cal][codec]") {
  start_clean();
  TEST_ASSERT_TRUE(accepted(std::string(kBoard2File) + "garbage \x01\x02\xff\n"));
  TEST_ASSERT_TRUE(accepted(std::string(kBoard2File) + "version 2\nhorizontal 0 0 0\n"));
  TEST_ASSERT_TRUE(same(joystick_cal_current(), kBoard2));
}

TEST_CASE("CAL-015 seeded random bytes load as the defaults or a plausible record, never crash",
          "[cal][codec][hostile]") {
  start_clean();
  std::mt19937 rng(20261006u);
  std::uniform_int_distribution<int> len_dist(0, 512);
  std::uniform_int_distribution<int> byte_dist(0, 255);
  StdoutCapture quiet; // 500 warnings
  for (int i = 0; i < 500; ++i) {
    std::string text(static_cast<std::size_t>(len_dist(rng)), '\0');
    for (char &ch : text) {
      ch = static_cast<char>(byte_dist(rng));
    }
    const JoystickCal cal = load_text(text);
    TEST_ASSERT_TRUE(joystick_cal_saved() ? travel_at_least_1000(cal) : same(cal, kDefaults));
  }
}

TEST_CASE("CAL-016 seeded byte changes in the golden file load as the defaults or a plausible "
          "record",
          "[cal][codec][hostile]") {
  start_clean();
  std::mt19937 rng(6102026u);
  std::uniform_int_distribution<std::size_t> pos_dist(0, kBoard2File.size() - 1);
  std::uniform_int_distribution<int> byte_dist(0, 255);
  std::uniform_int_distribution<int> count_dist(1, 3);
  StdoutCapture quiet;
  int kept = 0;
  for (int i = 0; i < 1000; ++i) {
    std::string text(kBoard2File);
    for (int n = count_dist(rng); n > 0; --n) {
      text[pos_dist(rng)] = static_cast<char>(byte_dist(rng));
    }
    const JoystickCal cal = load_text(text);
    TEST_ASSERT_TRUE(joystick_cal_saved() ? travel_at_least_1000(cal) : same(cal, kDefaults));
    kept += joystick_cal_saved() ? 1 : 0;
  }
  TEST_ASSERT_TRUE(kept > 0); // changes inside the comment line still load
}

TEST_CASE("CAL-017 HAZARD no length limit: a 1 MiB comment is skipped and a 1 MiB number is "
          "read whole, then rejected",
          "[cal][codec][hostile][hazard]") {
  start_clean();
  StdoutCapture quiet;
  const std::string mib(std::size_t{1} << 20, '7');
  TEST_ASSERT_TRUE(accepted("#" + mib + "\n" + std::string(kBoard2File)));
  TEST_ASSERT_FALSE(accepted(golden_with("11 1507 " + mib, "6 1510 2962", "10 1477 2960")));
  TEST_ASSERT_FALSE(accepted("version 1\n" + std::string(std::size_t{1} << 20, 'h')));
}

TEST_CASE("CAL-018 the file keeps 0.1 mV and the log rounds to whole mV", "[cal][codec]") {
  start_clean();
  std::string log;
  (void)load_text(golden_with("10.5 1506.5 2970.5", "5.25 1509.75 2961.45", "9.94 1477.06 2960.5"),
                  &log);
  TEST_ASSERT_TRUE(joystick_cal_saved());
  TEST_ASSERT_TRUE_MESSAGE(contains(log, ": horizontal 10/1506/2970, vertical 5/1510/2961, "
                                         "twist 10/1477/2960 mV\n"),
                           log.c_str());
  // A run at a position off the 0.1 mV grid writes it rounded to one decimal.
  start_clean();
  joystick_cal_toggle();
  (void)hold(1507.26f, 1510.0f, 1477.0f, kSettleTicks + kHoldTicks);
  do_directions();
  (void)hold(1507.26f, 1510.0f, 1477.0f, kHoldTicks);
  TEST_ASSERT_TRUE_MESSAGE(
      contains(fake_storage::last_written(), "\nhorizontal 11.0 1507.3 2971.0\n"),
      fake_storage::last_written().c_str());
}

// ============================================================================================
// The calibration run
// ============================================================================================

TEST_CASE("CAL-020 starting a run prompts step 1, reads CANCEL and owns the stick", "[cal][run]") {
  start_clean();
  Widgets &w = widgets();
  TEST_ASSERT_EQUAL_STRING(kIdleText, shown().c_str());
  TEST_ASSERT_EQUAL_STRING("CALIBRATE", button_shows().c_str());
  TEST_ASSERT_FALSE(joystick_cal_running());
  StdoutCapture cap;
  joystick_cal_toggle();
  const std::string log = cap.stop();
  TEST_ASSERT_TRUE(joystick_cal_running());
  TEST_ASSERT_FALSE(w.timer->paused);
  TEST_ASSERT_EQUAL_UINT32(33, w.timer->period_ms);
  TEST_ASSERT_EQUAL_STRING("Step 1 of 8\nLet go of the joystick and keep still", shown().c_str());
  TEST_ASSERT_EQUAL_STRING("CANCEL", button_shows().c_str());
  TEST_ASSERT_TRUE(contains(log, "]: calibration started\n"));
}

TEST_CASE("CAL-021 the rest position is taken on the 75th period: 45 to settle, then 30 held",
          "[cal][run]") {
  start_clean();
  joystick_cal_toggle();
  (void)hold(1507.0f, 1510.0f, 1477.0f, kSettleTicks + kHoldTicks - 1);
  TEST_ASSERT_EQUAL_STRING("Step 1 of 8\nLet go of the joystick and keep still", shown().c_str());
  TEST_ASSERT_EQUAL_INT(0, widgets().feedback_count);
  (void)tick(1507.0f, 1510.0f, 1477.0f);
  TEST_ASSERT_EQUAL_STRING("Step 2 of 8\nPush the joystick fully LEFT and hold it",
                           shown().c_str());
  TEST_ASSERT_EQUAL_INT(1, widgets().feedback_count);
}

TEST_CASE("CAL-022 rest is taken only when every axis is steady: 30 mV peak to peak, 60 on twist",
          "[cal][run]") {
  struct Wobble {
    float dh, dv, dt;
    bool taken;
  };
  constexpr std::array<Wobble, 6> kWobbles{{
      {30.0f, 0.0f, 0.0f, true},
      {31.0f, 0.0f, 0.0f, false},
      {0.0f, 30.0f, 0.0f, true},
      {0.0f, 31.0f, 0.0f, false},
      {0.0f, 0.0f, 60.0f, true},
      {0.0f, 0.0f, 61.0f, false},
  }};
  for (const Wobble &wb : kWobbles) {
    start_clean();
    joystick_cal_toggle();
    for (int i = 0; i < kSettleTicks + kHoldTicks; ++i) {
      const float k = (i % 2 == 0) ? 0.0f : 1.0f;
      (void)tick(1507.0f + k * wb.dh, 1510.0f + k * wb.dv, 1477.0f + k * wb.dt);
    }
    TEST_ASSERT_EQUAL(wb.taken, starts_with(shown(), "Step 2 of 8"));
  }
  // The window slides: one wobble holds the step back until it has left the window.
  start_clean();
  joystick_cal_toggle();
  (void)hold(1507.0f, 1510.0f, 1477.0f, kSettleTicks + kHoldTicks - 1);
  (void)tick(1600.0f, 1510.0f, 1477.0f); // period 75
  (void)hold(1507.0f, 1510.0f, 1477.0f, kHoldTicks - 1);
  TEST_ASSERT_TRUE(starts_with(shown(), "Step 1 of 8"));
  (void)tick(1507.0f, 1510.0f, 1477.0f); // period 105: the wobble has left the window
  TEST_ASSERT_TRUE(starts_with(shown(), "Step 2 of 8"));
}

TEST_CASE("CAL-023 a direction counts from 1000 mV past rest, inclusive, held for 30 periods",
          "[cal][run]") {
  start_clean();
  joystick_cal_toggle();
  do_rest();
  (void)hold(507.1f, 1510.0f, 1477.0f, 100); // 999.9 mV left of rest: never taken
  TEST_ASSERT_TRUE(starts_with(shown(), "Step 2 of 8"));
  (void)hold(507.0f, 1510.0f, 1477.0f, kHoldTicks - 1); // exactly 1000
  TEST_ASSERT_TRUE(starts_with(shown(), "Step 2 of 8"));
  (void)tick(507.0f, 1510.0f, 1477.0f);
  TEST_ASSERT_EQUAL_STRING("Step 3 of 8\nPush the joystick fully RIGHT and hold it",
                           shown().c_str());
}

TEST_CASE("CAL-024 easing off under 1000 mV restarts a hold, a wobble over 30 mV stalls it, the "
          "other axes are not checked",
          "[cal][run]") {
  start_clean();
  joystick_cal_toggle();
  do_rest();
  (void)hold(11.0f, 1510.0f, 1477.0f, kHoldTicks - 1);
  (void)tick(600.0f, 1510.0f, 1477.0f); // 907 mV from rest: the hold starts again
  (void)hold(11.0f, 1510.0f, 1477.0f, kHoldTicks - 1);
  TEST_ASSERT_TRUE(starts_with(shown(), "Step 2 of 8"));
  (void)tick(11.0f, 1510.0f, 1477.0f);
  TEST_ASSERT_TRUE(starts_with(shown(), "Step 3 of 8"));
  for (int i = 0; i < 100; ++i) { // right, wobbling 31 mV
    (void)tick(i % 2 == 0 ? 2971.0f : 2940.0f, 1510.0f, 1477.0f);
  }
  TEST_ASSERT_TRUE(starts_with(shown(), "Step 3 of 8"));
  (void)hold(2971.0f, 100.0f, 3000.0f, kHoldTicks); // vertical and twist far off: still taken
  TEST_ASSERT_TRUE(starts_with(shown(), "Step 4 of 8"));
}

TEST_CASE("CAL-025 the steps come in a fixed order, eight in all", "[cal][run]") {
  start_clean();
  joystick_cal_toggle();
  do_rest();
  constexpr std::array<const char *, 6> kNext{{
      "Step 3 of 8\nPush the joystick fully RIGHT and hold it",
      "Step 4 of 8\nPush the joystick fully FORWARD and hold it",
      "Step 5 of 8\nPull the joystick fully BACK and hold it",
      "Step 6 of 8\nTwist the joystick fully CLOCKWISE and hold it",
      "Step 7 of 8\nTwist the joystick fully COUNTER-CLOCKWISE and hold it",
      "Step 8 of 8\nLet go of the joystick",
  }};
  for (std::size_t i = 0; i < kEnds.size(); ++i) {
    // the next end does not count before its turn
    if (i + 1 < kEnds.size()) {
      const auto &n = kEnds[i + 1];
      (void)hold(n[0], n[1], n[2], kHoldTicks);
      TEST_ASSERT_EQUAL_INT(static_cast<int>(i) + 1, widgets().feedback_count);
    }
    (void)hold(kEnds[i][0], kEnds[i][1], kEnds[i][2], kHoldTicks);
    TEST_ASSERT_EQUAL_STRING(kNext[i], shown().c_str());
  }
  TEST_ASSERT_EQUAL_INT(7, widgets().feedback_count);
  TEST_ASSERT_TRUE(joystick_cal_running());
}

TEST_CASE("CAL-026 the release is taken within 150 mV of rest on every axis, inclusive, held 30",
          "[cal][run]") {
  start_clean();
  joystick_cal_toggle();
  do_rest();
  do_directions();
  (void)hold(1507.0f, 1510.0f, 1477.0f + 150.5f, 100);
  TEST_ASSERT_TRUE(joystick_cal_running());
  (void)hold(1507.0f - 150.0f, 1510.0f + 150.0f, 1477.0f + 150.0f, kHoldTicks - 1);
  TEST_ASSERT_TRUE(joystick_cal_running());
  (void)tick(1507.0f - 150.0f, 1510.0f + 150.0f, 1477.0f + 150.0f);
  TEST_ASSERT_FALSE(joystick_cal_running());
  TEST_ASSERT_EQUAL_STRING("Calibration saved", shown().c_str());
}

TEST_CASE("CAL-027 a finished run is in use at once and handed to the ADC task exactly once",
          "[cal][run]") {
  start_clean();
  joystick_cal_toggle();
  do_rest();
  do_directions();
  do_release();
  TEST_ASSERT_FALSE(joystick_cal_running());
  TEST_ASSERT_TRUE(joystick_cal_saved());
  TEST_ASSERT_TRUE(same(joystick_cal_current(), kBoard2));
  TEST_ASSERT_EQUAL_STRING("CALIBRATE", button_shows().c_str());
  const std::optional<JoystickCal> first = joystick_cal_take_new();
  TEST_ASSERT_TRUE(first.has_value());
  TEST_ASSERT_TRUE(same(*first, kBoard2));
  TEST_ASSERT_FALSE(joystick_cal_take_new().has_value());
  TEST_ASSERT_EQUAL_INT(7, widgets().feedback_count); // rest and six ends; not the release
}

TEST_CASE("CAL-028 a run that cannot save is still used, and says it will be lost", "[cal][run]") {
  start_clean();
  fake_storage::set_write_fails(true);
  joystick_cal_toggle();
  do_rest();
  do_directions();
  StdoutCapture cap;
  do_release();
  const std::string log = cap.stop();
  fake_storage::set_write_fails(false);
  TEST_ASSERT_EQUAL_STRING(
      "Calibrated, but could not save to flash.\nIt will be lost at power-off.", shown().c_str());
  TEST_ASSERT_FALSE(joystick_cal_saved());
  TEST_ASSERT_TRUE(same(joystick_cal_current(), kBoard2));
  TEST_ASSERT_TRUE(joystick_cal_take_new().has_value());
  TEST_ASSERT_TRUE_MESSAGE(
      contains(log, "]: calibrated (NOT saved): " + std::string(kBoard2Text) + "\n"), log.c_str());
}

TEST_CASE("CAL-029 each step times out on its 601st period and changes nothing", "[cal][run]") {
  struct Timeout {
    int ends_done; // -1: still at rest (step 1); 6: at the release (step 8)
    const char *step;
  };
  constexpr std::array<Timeout, 4> kTimeouts{
      {{-1, "step 1\n"}, {0, "step 2\n"}, {5, "step 7\n"}, {6, "step 8\n"}}};
  for (const Timeout &t : kTimeouts) {
    start_clean();
    const JoystickCal before = joystick_cal_current();
    const int writes = fake_storage::write_calls();
    joystick_cal_toggle();
    if (t.ends_done >= 0) {
      do_rest();
      do_directions(t.ends_done);
    }
    // A position the step never takes: wobbling at rest for step 1, at rest for a direction,
    // pushed left for the release.
    const auto never = [&t](int i) {
      if (t.ends_done < 0) {
        return i % 2 == 0 ? 1507.0f : 1600.0f;
      }
      return t.ends_done == 6 ? 11.0f : 1507.0f;
    };
    for (int i = 0; i < kTimeoutTicks; ++i) {
      (void)tick(never(i), 1510.0f, 1477.0f);
    }
    TEST_ASSERT_TRUE_MESSAGE(joystick_cal_running(), t.step);
    StdoutCapture cap;
    (void)tick(never(kTimeoutTicks), 1510.0f, 1477.0f);
    const std::string log = cap.stop();
    TEST_ASSERT_FALSE_MESSAGE(joystick_cal_running(), t.step);
    TEST_ASSERT_EQUAL_STRING("Timed out.\nNothing changed.", shown().c_str());
    TEST_ASSERT_TRUE_MESSAGE(contains(log, "]: calibration timed out at " + std::string(t.step)),
                             log.c_str());
    TEST_ASSERT_TRUE(same(joystick_cal_current(), before));
    TEST_ASSERT_FALSE(joystick_cal_take_new().has_value());
    TEST_ASSERT_EQUAL_INT(writes, fake_storage::write_calls());
  }
}

TEST_CASE("CAL-030 cancelling from the menu, the button or by leaving the screen changes nothing",
          "[cal][run]") {
  struct Cancel {
    int how; // 0: joystick_cal_toggle, 1: the button, 2: leaving the screen
    const char *why;
  };
  constexpr std::array<Cancel, 3> kCancels{
      {{0, "(menu)"}, {1, "(button)"}, {2, "(left the screen)"}}};
  for (const Cancel &c : kCancels) {
    start_clean();
    const JoystickCal before = joystick_cal_current();
    const int writes = fake_storage::write_calls();
    joystick_cal_toggle();
    do_rest();
    do_directions(1);
    StdoutCapture cap;
    if (c.how == 0) {
      joystick_cal_toggle();
    } else if (c.how == 1) {
      fake_lvgl::send_event(&widgets().button, LV_EVENT_CLICKED);
    } else {
      fake_lvgl::send_event(&widgets().screen, LV_EVENT_SCREEN_UNLOAD_START);
    }
    const std::string log = cap.stop();
    TEST_ASSERT_FALSE(joystick_cal_running());
    TEST_ASSERT_EQUAL_STRING("Calibration cancelled.\nNothing changed.", shown().c_str());
    TEST_ASSERT_EQUAL_STRING("CALIBRATE", button_shows().c_str());
    TEST_ASSERT_TRUE_MESSAGE(contains(log, "]: calibration cancelled " + std::string(c.why) + "\n"),
                             log.c_str());
    TEST_ASSERT_TRUE(same(joystick_cal_current(), before));
    TEST_ASSERT_FALSE(joystick_cal_take_new().has_value());
    TEST_ASSERT_EQUAL_INT(writes, fake_storage::write_calls());
  }
}

TEST_CASE("CAL-031 leaving the screen with no run going changes nothing; the button starts a run",
          "[cal][run]") {
  start_clean();
  fake_lvgl::send_event(&widgets().screen, LV_EVENT_SCREEN_UNLOAD_START);
  TEST_ASSERT_FALSE(joystick_cal_running());
  TEST_ASSERT_EQUAL_STRING(kIdleText, shown().c_str());
  fake_lvgl::send_event(&widgets().button, LV_EVENT_CLICKED);
  TEST_ASSERT_TRUE(joystick_cal_running());
  TEST_ASSERT_TRUE(starts_with(shown(), "Step 1 of 8"));
}

TEST_CASE("CAL-032 the result stays up for 90 periods, then the screen's own text comes back",
          "[cal][run]") {
  start_clean();
  joystick_cal_toggle();
  joystick_cal_toggle(); // cancelled: the result is up
  Widgets &w = widgets();
  TEST_ASSERT_EQUAL_INT(kResultTicks - 1, hold(1507.0f, 1510.0f, 1477.0f, kResultTicks - 1));
  TEST_ASSERT_EQUAL_STRING("Calibration cancelled.\nNothing changed.", shown().c_str());
  TEST_ASSERT_FALSE(w.timer->paused);
  (void)tick(1507.0f, 1510.0f, 1477.0f);
  TEST_ASSERT_EQUAL_STRING(kIdleText, shown().c_str());
  TEST_ASSERT_TRUE(w.timer->paused);
}

TEST_CASE("CAL-033 a new run can start while the last result is up, and counts from its start",
          "[cal][run]") {
  start_clean();
  joystick_cal_toggle();
  joystick_cal_toggle();
  (void)hold(1507.0f, 1510.0f, 1477.0f, 10);
  joystick_cal_toggle();
  TEST_ASSERT_TRUE(joystick_cal_running());
  TEST_ASSERT_EQUAL_STRING("Step 1 of 8\nLet go of the joystick and keep still", shown().c_str());
  (void)hold(1507.0f, 1510.0f, 1477.0f, kSettleTicks + kHoldTicks - 1);
  TEST_ASSERT_TRUE(starts_with(shown(), "Step 1 of 8"));
  (void)tick(1507.0f, 1510.0f, 1477.0f);
  TEST_ASSERT_TRUE(starts_with(shown(), "Step 2 of 8"));
}

TEST_CASE("CAL-034 a prompt blinks with the blink subject; idle and result text do not",
          "[cal][run]") {
  start_clean();
  Widgets &w = widgets();
  lv_subject_set_int(&w.blink, 0);
  TEST_ASSERT_EQUAL_UINT8(LV_OPA_COVER, w.instructions.text_opa);
  joystick_cal_toggle();
  TEST_ASSERT_EQUAL_UINT8(LV_OPA_TRANSP, w.instructions.text_opa);
  lv_subject_set_int(&w.blink, 1);
  TEST_ASSERT_EQUAL_UINT8(LV_OPA_COVER, w.instructions.text_opa);
  lv_subject_set_int(&w.blink, 0);
  TEST_ASSERT_EQUAL_UINT8(LV_OPA_TRANSP, w.instructions.text_opa);
  joystick_cal_toggle(); // the result
  TEST_ASSERT_EQUAL_UINT8(LV_OPA_COVER, w.instructions.text_opa);
  lv_subject_set_int(&w.blink, 1);
  lv_subject_set_int(&w.blink, 0);
  TEST_ASSERT_EQUAL_UINT8(LV_OPA_COVER, w.instructions.text_opa);
}

TEST_CASE("CAL-035 HAZARD the run reuses the last noted sample: periods with no fresh ADC value "
          "still count toward a hold",
          "[cal][run][hazard]") {
  start_clean();
  joystick_cal_toggle();
  joystick_cal_note_raw(1507.0f, 1510.0f, 1477.0f);
  for (int i = 0; i < kSettleTicks + kHoldTicks; ++i) {
    (void)fake_lvgl::tick(widgets().timer); // the ADC task noted nothing new
  }
  TEST_ASSERT_TRUE(starts_with(shown(), "Step 2 of 8"));
}

TEST_CASE("CAL-036 HAZARD a NaN sample passes every hold check; a NaN rest ends the run as "
          "travel too short, with nothing changed",
          "[cal][run][hazard]") {
  start_clean();
  const JoystickCal before = joystick_cal_current();
  const int writes = fake_storage::write_calls();
  const float nan = std::numeric_limits<float>::quiet_NaN();
  joystick_cal_toggle();
  (void)hold(nan, 1510.0f, 1477.0f, kSettleTicks + kHoldTicks); // rest "taken" with a NaN centre
  TEST_ASSERT_TRUE(starts_with(shown(), "Step 2 of 8"));
  (void)hold(1507.0f, 1510.0f, 1477.0f, kHoldTicks); // "fully left" while at rest: taken
  TEST_ASSERT_TRUE(starts_with(shown(), "Step 3 of 8"));
  do_directions(6); // the other ends (and left again, ignored by then)
  StdoutCapture cap;
  do_release();
  do_release();
  const std::string log = cap.stop();
  TEST_ASSERT_FALSE(joystick_cal_running());
  TEST_ASSERT_EQUAL_STRING("Calibration failed: travel too short.\nNothing changed.",
                           shown().c_str());
  TEST_ASSERT_TRUE_MESSAGE(contains(log, "[joy_cal/E]"), log.c_str());
  TEST_ASSERT_TRUE_MESSAGE(contains(log, "]: calibration rejected: horizontal "), log.c_str());
  TEST_ASSERT_TRUE(same(joystick_cal_current(), before));
  TEST_ASSERT_FALSE(joystick_cal_take_new().has_value());
  TEST_ASSERT_EQUAL_INT(writes, fake_storage::write_calls());
}
