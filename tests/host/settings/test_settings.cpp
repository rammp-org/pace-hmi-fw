// Characterisation tests for components/settings/src/settings.cpp: the settings.txt codec, the
// clamps and the save-on-change, pinned as they behave TODAY (dev_refactor fa3f13a, plan step 8).
//
// These are golden expectations of current behaviour, not approval of it. A case whose name
// says "today" pins behaviour that is questionable (plan §1 G8, CS-CFG-02, TS-UNIT-07): a bad
// token stops parsing silently, out-of-range values are clamped without a report, the file
// has no version. Changing any of them is a behaviour change, made on purpose in its own
// commit, which then updates the case that pins it.
//
// The real settings.cpp is built against fake_storage.cpp. settings.cpp keeps its values in
// one process-wide table, so every case starts from reset_to_defaults() (TS-DET-03), and the
// values before any load are captured during static initialisation (SET-003).

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <ios>
#include <iterator>
#include <random>
#include <string>
#include <string_view>

#include <unistd.h>

#include "fake_storage.hpp"
#include "settings.hpp"
#include "storage.hpp"
#include "test_case.hpp"

namespace {

constexpr int N = SETTINGS_PARAM_COUNT;
using Values = std::array<int, N>;

// --- golden: today's table, as settings.cpp stores it ---

constexpr std::array<std::string_view, N> KEYS{
    "brightness",        "theme",       "menu_slide",     "flip",
    "stick_sensitivity", "drive_speed", "stick_invert_x", "stick_invert_y",
    "stick_swap",        "sounds",      "network"};
constexpr Values DEFAULTS{75, 0, 0, 0, 9, 10, 0, 0, 0, 1, 0};
constexpr Values MINS{5, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0};
constexpr Values MAXS{100, 1, 1, 1, 10, 10, 1, 1, 1, 1, 1};

constexpr std::string_view DEFAULT_FILE = "brightness 75\n"
                                          "theme 0\n"
                                          "menu_slide 0\n"
                                          "flip 0\n"
                                          "stick_sensitivity 9\n"
                                          "drive_speed 10\n"
                                          "stick_invert_x 0\n"
                                          "stick_invert_y 0\n"
                                          "stick_swap 0\n"
                                          "sounds 1\n"
                                          "network 0\n";

// Board 2's settings.txt, rebuilt from save_locked()'s format and the values its baseline boot
// logged (tests/characterisation/baseline-e2047a4/boot-board2.log line 98).
constexpr std::string_view BOARD2_FILE = "brightness 80\n"
                                         "theme 1\n"
                                         "menu_slide 0\n"
                                         "flip 0\n"
                                         "stick_sensitivity 9\n"
                                         "drive_speed 10\n"
                                         "stick_invert_x 0\n"
                                         "stick_invert_y 0\n"
                                         "stick_swap 0\n"
                                         "sounds 1\n"
                                         "network 1\n";
constexpr Values BOARD2_VALUES{80, 1, 0, 0, 9, 10, 0, 0, 0, 1, 1};
// The message of that boot log line, verbatim.
constexpr std::string_view BOARD2_LOADED_LINE =
    "loaded: brightness 80, theme 1, menu_slide 0, flip 0, stick_sensitivity 9, drive_speed 10, "
    "stick_invert_x 0, stick_invert_y 0, stick_swap 0, sounds 1, network 1";

// SET-054's outcome, recorded from today's code (dev_refactor fa3f13a).
constexpr int SET_054_GOLDEN_CHANGED = 64;
constexpr std::uint32_t SET_054_GOLDEN_HASH = 0xCD74428Cu;

// --- helpers ---

Values current() {
  Values v{};
  for (int i = 0; i < N; i++) {
    v[static_cast<std::size_t>(i)] = settings_get(i);
  }
  return v;
}

// The values before any load or set, read during static initialisation (settings.cpp's table
// is constant-initialised, so it is ready before any dynamic initialiser runs).
const Values g_values_at_start = current();

void assert_values(const Values &expected, const Values &actual, const char *what) {
  TEST_ASSERT_EQUAL_INT_ARRAY_MESSAGE(expected.data(), actual.data(), N, what);
}

std::string file_path() { return storage_path("settings.txt"); }

void write_file(std::string_view text) {
  std::ofstream out(file_path(), std::ios::binary | std::ios::trunc);
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  out.flush();
  TEST_ASSERT_TRUE_MESSAGE(static_cast<bool>(out), "could not write the test's settings.txt");
}

void remove_file() { (void)std::remove(file_path().c_str()); } // absent already is fine

// Everything written to stdout (where espp::Logger prints) while fn runs.
template <typename Fn> std::string capture_stdout(Fn &&fn) {
  TEST_ASSERT_EQUAL_INT(0, std::fflush(stdout));
  std::FILE *tmp = std::tmpfile();
  if (tmp == nullptr) {
    TEST_FAIL_MESSAGE("tmpfile() failed");
    return {};
  }
  const int saved = dup(STDOUT_FILENO);
  TEST_ASSERT_TRUE(saved >= 0);
  TEST_ASSERT_TRUE(dup2(fileno(tmp), STDOUT_FILENO) >= 0);
  fn();
  const int flushed = std::fflush(stdout);
  TEST_ASSERT_TRUE(dup2(saved, STDOUT_FILENO) >= 0);
  TEST_ASSERT_EQUAL_INT(0, close(saved));
  TEST_ASSERT_EQUAL_INT(0, flushed);
  std::rewind(tmp);
  std::string out;
  std::array<char, 4096> buf{};
  for (std::size_t got = 0; (got = std::fread(buf.data(), 1, buf.size(), tmp)) > 0;) {
    out.append(buf.data(), got);
  }
  TEST_ASSERT_EQUAL_INT(0, std::fclose(tmp));
  return out;
}

// True if the captured output has an INFO line from the settings logger with exactly `msg`.
bool logged_info(const std::string &out, std::string_view msg) {
  if (out.find("[settings/I]") == std::string::npos) {
    return false;
  }
  return out.find("]: " + std::string(msg) + "\n") != std::string::npos;
}

bool logged_warning_or_error(const std::string &out) {
  return out.find("/W]") != std::string::npos || out.find("/E]") != std::string::npos;
}

// "brightness 75, theme 0, ..." for the given values, in table order.
std::string describe(const Values &v) {
  std::string out;
  for (std::size_t i = 0; i < KEYS.size(); i++) {
    out += (i == 0 ? "" : ", ") + std::string(KEYS[i]) + " " + std::to_string(v[i]);
  }
  return out;
}

// Write `text` as settings.txt and load it; returns what the load logged.
std::string load(std::string_view text) {
  write_file(text);
  return capture_stdout([] { settings_load(); });
}

// Every case starts here: every value at its default and the fake's record cleared.
void reset_to_defaults() {
  (void)load(DEFAULT_FILE);
  assert_values(DEFAULTS, current(), "reset_to_defaults");
  fake_storage::reset();
}

// DEFAULTS with one value changed.
Values defaults_with(int param, int value) {
  Values v = DEFAULTS;
  v[static_cast<std::size_t>(param)] = value;
  return v;
}

// A small FNV-1a over the values, to pin many outcomes in one golden number.
std::uint32_t fnv1a(std::uint32_t h, const Values &v) {
  for (const int x : v) {
    const auto u = static_cast<std::uint32_t>(x);
    for (unsigned shift = 0; shift < 32; shift += 8) {
      h ^= (u >> shift) & 0xFFu;
      h *= 16777619u;
    }
  }
  return h;
}

void assert_in_range(const Values &v) {
  for (std::size_t i = 0; i < v.size(); i++) {
    TEST_ASSERT_TRUE_MESSAGE(v[i] >= MINS[i] && v[i] <= MAXS[i], std::string(KEYS[i]).c_str());
  }
}

} // namespace

// ----------------------------------------------------------------------- the public table

TEST_CASE("SET-001 the parameter and page indices keep their order", "[settings]") {
  TEST_ASSERT_EQUAL_INT(11, SETTINGS_PARAM_COUNT);
  TEST_ASSERT_EQUAL_INT(0, SETTINGS_PARAM_BRIGHTNESS);
  TEST_ASSERT_EQUAL_INT(1, SETTINGS_PARAM_THEME);
  TEST_ASSERT_EQUAL_INT(2, SETTINGS_PARAM_MENU_SLIDE);
  TEST_ASSERT_EQUAL_INT(3, SETTINGS_PARAM_FLIP);
  TEST_ASSERT_EQUAL_INT(4, SETTINGS_PARAM_STICK_SENSITIVITY);
  TEST_ASSERT_EQUAL_INT(5, SETTINGS_PARAM_DRIVE_SPEED);
  TEST_ASSERT_EQUAL_INT(6, SETTINGS_PARAM_STICK_INVERT_X);
  TEST_ASSERT_EQUAL_INT(7, SETTINGS_PARAM_STICK_INVERT_Y);
  TEST_ASSERT_EQUAL_INT(8, SETTINGS_PARAM_STICK_SWAP);
  TEST_ASSERT_EQUAL_INT(9, SETTINGS_PARAM_SOUNDS);
  TEST_ASSERT_EQUAL_INT(10, SETTINGS_PARAM_NETWORK);
  TEST_ASSERT_EQUAL_INT(3, SETTINGS_PAGE_COUNT);
  TEST_ASSERT_EQUAL_INT(0, SETTINGS_PAGE_DISPLAY);
  TEST_ASSERT_EQUAL_INT(1, SETTINGS_PAGE_STICK);
  TEST_ASSERT_EQUAL_INT(2, SETTINGS_PAGE_NET);
}

TEST_CASE("SET-002 the range constants used outside settings.cpp keep their values", "[settings]") {
  TEST_ASSERT_EQUAL_INT(5, SETTINGS_BRIGHTNESS_MIN);
  TEST_ASSERT_EQUAL_INT(100, SETTINGS_BRIGHTNESS_MAX);
  TEST_ASSERT_EQUAL_INT(1, SETTINGS_STICK_SENSITIVITY_MIN);
  TEST_ASSERT_EQUAL_INT(10, SETTINGS_STICK_SENSITIVITY_MAX);
  TEST_ASSERT_EQUAL_INT(1, SETTINGS_DRIVE_SPEED_MIN);
  TEST_ASSERT_EQUAL_INT(10, SETTINGS_DRIVE_SPEED_MAX);
  TEST_ASSERT_EQUAL_INT(5, kBrightnessMinPercent);
  TEST_ASSERT_EQUAL_INT(100, kBrightnessMaxPercent);
}

TEST_CASE("SET-003 before any load every parameter holds its table default", "[settings]") {
  assert_values(DEFAULTS, g_values_at_start, "values at start-up");
}

TEST_CASE("SET-004 a setter clamps every parameter to its table range", "[settings]") {
  reset_to_defaults();
  for (int i = 0; i < N; i++) {
    const auto u = static_cast<std::size_t>(i);
    settings_set(i, INT_MIN);
    TEST_ASSERT_EQUAL_INT_MESSAGE(MINS[u], settings_get(i), std::string(KEYS[u]).c_str());
    settings_set(i, INT_MAX);
    TEST_ASSERT_EQUAL_INT_MESSAGE(MAXS[u], settings_get(i), std::string(KEYS[u]).c_str());
    settings_set(i, MINS[u] - 1);
    TEST_ASSERT_EQUAL_INT_MESSAGE(MINS[u], settings_get(i), std::string(KEYS[u]).c_str());
    settings_set(i, MAXS[u] + 1);
    TEST_ASSERT_EQUAL_INT_MESSAGE(MAXS[u], settings_get(i), std::string(KEYS[u]).c_str());
  }
}

TEST_CASE("SET-005 a getter with an index outside the table returns 0", "[settings]") {
  reset_to_defaults();
  TEST_ASSERT_EQUAL_INT(0, settings_get(-1));
  TEST_ASSERT_EQUAL_INT(0, settings_get(N));
  TEST_ASSERT_EQUAL_INT(0, settings_get(INT_MIN));
  TEST_ASSERT_EQUAL_INT(0, settings_get(INT_MAX));
}

TEST_CASE("SET-006 a setter with an index outside the table changes and writes nothing",
          "[settings]") {
  reset_to_defaults();
  settings_set(-1, 50);
  settings_set(N, 50);
  settings_set(INT_MAX, 50);
  settings_set(INT_MIN, 50);
  assert_values(DEFAULTS, current(), "after out-of-table sets");
  TEST_ASSERT_EQUAL_INT(0, fake_storage::state().write_calls);
}

TEST_CASE("SET-007 the theme and brightness helpers read and write their rows", "[settings]") {
  reset_to_defaults();
  TEST_ASSERT_EQUAL_UINT8(0, settings_theme());
  TEST_ASSERT_EQUAL_INT(75, settings_brightness());
  settings_set_theme(1);
  settings_set_brightness(40);
  TEST_ASSERT_EQUAL_UINT8(1, settings_theme());
  TEST_ASSERT_EQUAL_INT(1, settings_get(SETTINGS_PARAM_THEME));
  TEST_ASSERT_EQUAL_INT(40, settings_get(SETTINGS_PARAM_BRIGHTNESS));
  settings_set_theme(255); // clamped to the THEME row's max
  TEST_ASSERT_EQUAL_UINT8(1, settings_theme());
  settings_set_brightness(0); // never dark: clamped to the minimum
  TEST_ASSERT_EQUAL_INT(5, settings_brightness());
}

// ----------------------------------------------------------------------- saving

TEST_CASE("SET-010 a change writes the whole file once, every key in table order", "[settings]") {
  reset_to_defaults();
  const std::string out = capture_stdout([] { settings_set(SETTINGS_PARAM_BRIGHTNESS, 80); });
  const fake_storage::State &s = fake_storage::state();
  TEST_ASSERT_EQUAL_INT(1, s.write_calls);
  TEST_ASSERT_EQUAL_STRING("settings.txt", s.last_name.c_str());
  TEST_ASSERT_EQUAL_STRING("brightness 80\n"
                           "theme 0\n"
                           "menu_slide 0\n"
                           "flip 0\n"
                           "stick_sensitivity 9\n"
                           "drive_speed 10\n"
                           "stick_invert_x 0\n"
                           "stick_invert_y 0\n"
                           "stick_swap 0\n"
                           "sounds 1\n"
                           "network 0\n",
                           s.last_contents.c_str());
  TEST_ASSERT_TRUE(logged_info(out, "saved: " + describe(defaults_with(0, 80))));
}

TEST_CASE("SET-011 setting the value a parameter already has writes nothing", "[settings]") {
  reset_to_defaults();
  for (int i = 0; i < N; i++) {
    settings_set(i, DEFAULTS[static_cast<std::size_t>(i)]);
  }
  TEST_ASSERT_EQUAL_INT(0, fake_storage::state().write_calls);
}

TEST_CASE("SET-012 a value that clamps to the current one writes nothing", "[settings]") {
  reset_to_defaults();
  settings_set(SETTINGS_PARAM_BRIGHTNESS, 100);
  TEST_ASSERT_EQUAL_INT(1, fake_storage::state().write_calls);
  settings_set(SETTINGS_PARAM_BRIGHTNESS, 150);
  settings_set(SETTINGS_PARAM_BRIGHTNESS, INT_MAX);
  TEST_ASSERT_EQUAL_INT(1, fake_storage::state().write_calls);
}

TEST_CASE("SET-013 today a failed save keeps the new value in RAM, logs no saved line and an "
          "equal set does not retry it",
          "[settings]") {
  reset_to_defaults();
  fake_storage::state().fail_writes = true;
  const std::string out = capture_stdout([] { settings_set(SETTINGS_PARAM_THEME, 1); });
  TEST_ASSERT_EQUAL_INT(1, fake_storage::state().write_calls);
  TEST_ASSERT_EQUAL_INT(1, settings_get(SETTINGS_PARAM_THEME));
  TEST_ASSERT_EQUAL_size_t(std::string::npos, out.find("saved:"));
  fake_storage::state().fail_writes = false;
  settings_set(SETTINGS_PARAM_THEME, 1);
  TEST_ASSERT_EQUAL_INT(1, fake_storage::state().write_calls);
}

TEST_CASE("SET-014 each change writes the file again, holding every value so far", "[settings]") {
  reset_to_defaults();
  settings_set(SETTINGS_PARAM_SOUNDS, 0);
  settings_set(SETTINGS_PARAM_NETWORK, 1);
  TEST_ASSERT_EQUAL_INT(2, fake_storage::state().write_calls);
  Values expected = defaults_with(SETTINGS_PARAM_SOUNDS, 0);
  expected[SETTINGS_PARAM_NETWORK] = 1;
  std::string text;
  for (std::size_t i = 0; i < KEYS.size(); i++) {
    text += std::string(KEYS[i]) + " " + std::to_string(expected[i]) + "\n";
  }
  TEST_ASSERT_EQUAL_STRING(text.c_str(), fake_storage::state().last_contents.c_str());
}

// ----------------------------------------------------------------------- loading good files

TEST_CASE("SET-020 a missing file keeps the current values and logs no settings.txt yet",
          "[settings]") {
  reset_to_defaults();
  settings_set(SETTINGS_PARAM_FLIP, 1);
  fake_storage::reset();
  remove_file();
  const std::string out = capture_stdout([] { settings_load(); });
  assert_values(defaults_with(SETTINGS_PARAM_FLIP, 1), current(), "after a missing file");
  TEST_ASSERT_TRUE(
      logged_info(out, "no settings.txt yet: " + describe(defaults_with(SETTINGS_PARAM_FLIP, 1))));
  TEST_ASSERT_EQUAL_INT(0, fake_storage::state().write_calls);
}

TEST_CASE("SET-021 board 2's file loads to its values and logs the baseline loaded line",
          "[settings][board2]") {
  reset_to_defaults();
  const std::string out = load(BOARD2_FILE);
  assert_values(BOARD2_VALUES, current(), "board 2");
  TEST_ASSERT_TRUE(logged_info(out, BOARD2_LOADED_LINE));
  TEST_ASSERT_FALSE(logged_warning_or_error(out));
}

TEST_CASE("SET-022 board 2's file round-trips byte for byte through a save", "[settings][board2]") {
  reset_to_defaults();
  (void)load(BOARD2_FILE);
  settings_set(SETTINGS_PARAM_BRIGHTNESS, 81);
  settings_set(SETTINGS_PARAM_BRIGHTNESS, 80);
  TEST_ASSERT_EQUAL_INT(2, fake_storage::state().write_calls);
  TEST_ASSERT_EQUAL_STRING(std::string(BOARD2_FILE).c_str(),
                           fake_storage::state().last_contents.c_str());
  // and what was saved loads back to the same values
  (void)load(fake_storage::state().last_contents);
  assert_values(BOARD2_VALUES, current(), "board 2 reloaded");
}

TEST_CASE("SET-023 today a load never writes the file, even when it clamped a value",
          "[settings]") {
  reset_to_defaults();
  (void)load("brightness 500\ntheme 7\n");
  TEST_ASSERT_EQUAL_INT(100, settings_get(SETTINGS_PARAM_BRIGHTNESS));
  TEST_ASSERT_EQUAL_INT(1, settings_get(SETTINGS_PARAM_THEME));
  TEST_ASSERT_EQUAL_INT(0, fake_storage::state().write_calls);
}

TEST_CASE("SET-024 every value saved at its maximum and at its minimum loads back", "[settings]") {
  reset_to_defaults();
  std::string hi;
  std::string lo;
  for (std::size_t i = 0; i < KEYS.size(); i++) {
    hi += std::string(KEYS[i]) + " " + std::to_string(MAXS[i]) + "\n";
    lo += std::string(KEYS[i]) + " " + std::to_string(MINS[i]) + "\n";
  }
  (void)load(hi);
  assert_values(MAXS, current(), "all at max");
  (void)load(lo);
  assert_values(MINS, current(), "all at min");
}

TEST_CASE("SET-025 keys load in any order, on one line, with any whitespace and CRLF",
          "[settings]") {
  reset_to_defaults();
  (void)load("network 1 sounds 0\r\n\tbrightness\t\t40\r\n\r\n  theme\n1   ");
  Values expected = defaults_with(SETTINGS_PARAM_NETWORK, 1);
  expected[SETTINGS_PARAM_SOUNDS] = 0;
  expected[SETTINGS_PARAM_BRIGHTNESS] = 40;
  expected[SETTINGS_PARAM_THEME] = 1;
  assert_values(expected, current(), "free-form whitespace");
}

TEST_CASE("SET-026 today a load keeps the in-RAM value, not the default, for a key the file "
          "lacks",
          "[settings]") {
  reset_to_defaults();
  settings_set(SETTINGS_PARAM_STICK_SWAP, 1);
  (void)load("brightness 30\n");
  Values expected = defaults_with(SETTINGS_PARAM_STICK_SWAP, 1);
  expected[SETTINGS_PARAM_BRIGHTNESS] = 30;
  assert_values(expected, current(), "partial file");
}

TEST_CASE("SET-027 a key given twice takes the last value", "[settings]") {
  reset_to_defaults();
  (void)load("brightness 30\ntheme 1\nbrightness 60\n");
  Values expected = defaults_with(SETTINGS_PARAM_BRIGHTNESS, 60);
  expected[SETTINGS_PARAM_THEME] = 1;
  assert_values(expected, current(), "duplicate key");
}

TEST_CASE("SET-028 a value with a plus sign or leading zeros loads as its number", "[settings]") {
  reset_to_defaults();
  (void)load("brightness +50\nstick_sensitivity 0007\n");
  Values expected = defaults_with(SETTINGS_PARAM_BRIGHTNESS, 50);
  expected[SETTINGS_PARAM_STICK_SENSITIVITY] = 7;
  assert_values(expected, current(), "+50 and 0007");
}

// ----------------------------------------------------------------------- hostile input
// (TS-UNIT-07)

TEST_CASE("SET-030 today an empty file keeps the current values and logs loaded, not an error",
          "[settings][hostile]") {
  reset_to_defaults();
  const std::string out = load("");
  assert_values(DEFAULTS, current(), "empty file");
  TEST_ASSERT_TRUE(logged_info(out, "loaded: " + describe(DEFAULTS)));
  TEST_ASSERT_FALSE(logged_warning_or_error(out));
}

TEST_CASE("SET-031 a file of only whitespace keeps the current values", "[settings][hostile]") {
  reset_to_defaults();
  (void)load(" \n\r\n\t\t\n   ");
  assert_values(DEFAULTS, current(), "whitespace only");
}

TEST_CASE("SET-032 a file that ends after a key keeps the values read before it",
          "[settings][hostile]") {
  reset_to_defaults();
  (void)load("brightness 80\ntheme");
  assert_values(defaults_with(SETTINGS_PARAM_BRIGHTNESS, 80), current(), "key without value");
}

TEST_CASE("SET-033 today a file cut mid-number loads the cut number", "[settings][hostile]") {
  reset_to_defaults();
  (void)load("brightness 8"); // "brightness 80" cut one byte short
  assert_values(defaults_with(SETTINGS_PARAM_BRIGHTNESS, 8), current(), "cut mid-number");
}

TEST_CASE("SET-034 a file cut mid-key keeps the values read before it", "[settings][hostile]") {
  reset_to_defaults();
  (void)load("brightness 80\nthe");
  assert_values(defaults_with(SETTINGS_PARAM_BRIGHTNESS, 80), current(), "cut mid-key");
}

TEST_CASE("SET-035 every prefix of board 2's file loads without error and in range",
          "[settings][hostile][board2]") {
  for (std::size_t len = 0; len <= BOARD2_FILE.size(); len++) {
    reset_to_defaults();
    (void)load(BOARD2_FILE.substr(0, len));
    assert_in_range(current());
  }
  TEST_ASSERT_EQUAL_INT(0, fake_storage::state().write_calls);
}

TEST_CASE("SET-036 today a bad token stops parsing silently and later keys keep their values",
          "[settings][hostile]") {
  reset_to_defaults();
  const std::string out = load("brightness 80\ntheme x\nflip 1\nsounds 0\n");
  assert_values(defaults_with(SETTINGS_PARAM_BRIGHTNESS, 80), current(), "bad token");
  TEST_ASSERT_TRUE(logged_info(out, "loaded: " + describe(defaults_with(0, 80))));
  TEST_ASSERT_FALSE(logged_warning_or_error(out));
}

TEST_CASE("SET-037 today a comment line stops parsing at the comment", "[settings][hostile]") {
  reset_to_defaults();
  (void)load("# settings\nbrightness 80\n");
  assert_values(DEFAULTS, current(), "comment first");
}

TEST_CASE("SET-038 an unknown key with a number is skipped and parsing goes on",
          "[settings][hostile]") {
  reset_to_defaults();
  (void)load("volume 3\nbrightness 80\nfuture_key -12\ntheme 1\n");
  Values expected = defaults_with(SETTINGS_PARAM_BRIGHTNESS, 80);
  expected[SETTINGS_PARAM_THEME] = 1;
  assert_values(expected, current(), "unknown numeric keys");
}

TEST_CASE("SET-039 today an unknown key with a non-number value stops parsing",
          "[settings][hostile]") {
  reset_to_defaults();
  (void)load("wifi_ssid home\nbrightness 80\n");
  assert_values(DEFAULTS, current(), "unknown key, text value");
}

TEST_CASE("SET-040 keys are case-sensitive: an upper-case key is an unknown key",
          "[settings][hostile]") {
  reset_to_defaults();
  (void)load("BRIGHTNESS 80\nTheme 1\nflip 1\n");
  assert_values(defaults_with(SETTINGS_PARAM_FLIP, 1), current(), "case");
}

TEST_CASE("SET-041 today a UTF-8 byte-order mark hides the first key", "[settings][hostile]") {
  reset_to_defaults();
  (void)load("\xEF\xBB\xBF"
             "brightness 80\ntheme 1\n");
  assert_values(defaults_with(SETTINGS_PARAM_THEME, 1), current(), "BOM");
}

TEST_CASE("SET-042 values at the ends of each range load as they are", "[settings][hostile]") {
  reset_to_defaults();
  (void)load("brightness 5\nstick_sensitivity 10\ndrive_speed 1\n");
  Values expected = defaults_with(SETTINGS_PARAM_BRIGHTNESS, 5);
  expected[SETTINGS_PARAM_STICK_SENSITIVITY] = 10;
  expected[SETTINGS_PARAM_DRIVE_SPEED] = 1;
  assert_values(expected, current(), "range ends");
  (void)load("brightness 100\nstick_sensitivity 1\ndrive_speed 10\n");
  expected[SETTINGS_PARAM_BRIGHTNESS] = 100;
  expected[SETTINGS_PARAM_STICK_SENSITIVITY] = 1;
  expected[SETTINGS_PARAM_DRIVE_SPEED] = 10;
  assert_values(expected, current(), "range ends, other way");
}

TEST_CASE("SET-043 today values just outside a range are clamped without a report",
          "[settings][hostile]") {
  reset_to_defaults();
  std::string out = load("brightness 4\ntheme 2\nstick_sensitivity 0\ndrive_speed 11\n"
                         "sounds -1\n");
  Values expected = defaults_with(SETTINGS_PARAM_BRIGHTNESS, 5);
  expected[SETTINGS_PARAM_THEME] = 1;
  expected[SETTINGS_PARAM_STICK_SENSITIVITY] = 1;
  expected[SETTINGS_PARAM_DRIVE_SPEED] = 10;
  expected[SETTINGS_PARAM_SOUNDS] = 0;
  assert_values(expected, current(), "just outside");
  TEST_ASSERT_FALSE(logged_warning_or_error(out));
  out = load("brightness 101\n");
  TEST_ASSERT_EQUAL_INT(100, settings_get(SETTINGS_PARAM_BRIGHTNESS));
  TEST_ASSERT_FALSE(logged_warning_or_error(out));
}

TEST_CASE("SET-044 values at the ends of int are clamped into range", "[settings][hostile]") {
  reset_to_defaults();
  (void)load("brightness 2147483647\ntheme -2147483648\n");
  Values expected = defaults_with(SETTINGS_PARAM_BRIGHTNESS, 100);
  expected[SETTINGS_PARAM_THEME] = 0;
  assert_values(expected, current(), "int limits");
  (void)load("brightness -2147483648\ntheme 2147483647\n");
  expected[SETTINGS_PARAM_BRIGHTNESS] = 5;
  expected[SETTINGS_PARAM_THEME] = 1;
  assert_values(expected, current(), "int limits, other way");
}

TEST_CASE("SET-045 today a number beyond int stops parsing and its key keeps its value",
          "[settings][hostile]") {
  constexpr std::array<std::string_view, 4> TOO_BIG{
      "brightness 2147483648\ntheme 1\n", "brightness -2147483649\ntheme 1\n",
      "brightness 99999999999999999999\ntheme 1\n",
      "brightness 184467440737095516160000\ntheme 1\n"};
  for (const std::string_view text : TOO_BIG) {
    reset_to_defaults();
    (void)load(text);
    assert_values(DEFAULTS, current(), std::string(text).c_str());
  }
}

TEST_CASE("SET-046 today a fractional value loads its integer part, then parsing stops",
          "[settings][hostile]") {
  reset_to_defaults();
  (void)load("brightness 50.5\nflip 1\n");
  assert_values(defaults_with(SETTINGS_PARAM_BRIGHTNESS, 50), current(), "50.5");
}

TEST_CASE("SET-047 today a hex value loads as 0, clamped, then parsing stops",
          "[settings][hostile]") {
  reset_to_defaults();
  (void)load("brightness 0x50\nflip 1\n");
  assert_values(defaults_with(SETTINGS_PARAM_BRIGHTNESS, 5), current(), "0x50");
}

TEST_CASE("SET-048 a value glued to its key makes an unknown key and stops parsing",
          "[settings][hostile]") {
  reset_to_defaults();
  (void)load("brightness80\ntheme 1\n");
  assert_values(DEFAULTS, current(), "glued");
}

TEST_CASE("SET-049 a 64 KiB key with a number is skipped and parsing goes on",
          "[settings][hostile]") {
  reset_to_defaults();
  (void)load(std::string(65536, 'k') + " 3\nbrightness 80\n");
  assert_values(defaults_with(SETTINGS_PARAM_BRIGHTNESS, 80), current(), "long key");
}

TEST_CASE("SET-050 a 64 KiB run of leading zeros still loads its number", "[settings][hostile]") {
  reset_to_defaults();
  (void)load("brightness " + std::string(65536, '0') + "80\ntheme 1\n");
  Values expected = defaults_with(SETTINGS_PARAM_BRIGHTNESS, 80);
  expected[SETTINGS_PARAM_THEME] = 1;
  assert_values(expected, current(), "long zeros");
}

TEST_CASE("SET-051 today a 64 KiB run of nines stops parsing and its key keeps its value",
          "[settings][hostile]") {
  reset_to_defaults();
  (void)load("brightness " + std::string(65536, '9') + "\ntheme 1\n");
  assert_values(DEFAULTS, current(), "long nines");
}

TEST_CASE("SET-052 a NUL byte inside a key makes an unknown key and parsing goes on",
          "[settings][hostile]") {
  reset_to_defaults();
  (void)load(std::string("bright\0ness 80\ntheme 1\n", 23));
  assert_values(defaults_with(SETTINGS_PARAM_THEME, 1), current(), "NUL in key");
}

TEST_CASE("SET-053 seeded random bytes never crash or leave a value out of range",
          "[settings][hostile]") {
  std::mt19937 rng(0x5E7701u); // fixed seed (TS-DET-02)
  std::uint32_t hash = 2166136261u;
  for (int file = 0; file < 300; file++) {
    reset_to_defaults();
    const std::size_t len = rng() % 513u;
    std::string text(len, '\0');
    std::generate(text.begin(), text.end(), [&rng] { return static_cast<char>(rng() & 0xFFu); });
    (void)load(text);
    const Values v = current();
    assert_in_range(v);
    hash = fnv1a(hash, v);
  }
  // Today random bytes never name a key, so every file leaves the defaults. A change here
  // means the codec changed what it does with garbage.
  std::uint32_t defaults_hash = 2166136261u;
  for (int file = 0; file < 300; file++) {
    defaults_hash = fnv1a(defaults_hash, DEFAULTS);
  }
  TEST_ASSERT_EQUAL_HEX32(defaults_hash, hash);
}

TEST_CASE("SET-054 seeded random streams of keys, numbers and junk match today's golden outcome",
          "[settings][hostile]") {
  constexpr std::array<std::string_view, 8> JUNK{"x", "#", "1.5",  "0x7",
                                                 "+", "-", "\r\n", "BRIGHTNESS"};
  constexpr std::array<std::string_view, 6> SEPS{" ", "\n", "\t", "\r\n", "  ", "\n\n"};
  std::mt19937 rng(0x5E7702u); // fixed seed (TS-DET-02)
  std::uint32_t hash = 2166136261u;
  int changed = 0;
  for (int file = 0; file < 400; file++) {
    reset_to_defaults();
    std::string text;
    const auto tokens = static_cast<unsigned>(rng() % 24u);
    for (unsigned t = 0; t < tokens; t++) {
      const auto kind = static_cast<unsigned>(rng() % 10u);
      if (kind < 4) {
        text += KEYS[rng() % KEYS.size()];
      } else if (kind < 8) {
        text += std::to_string(static_cast<int>(rng() % 241u) - 120);
      } else {
        text += JUNK[rng() % JUNK.size()];
      }
      text += SEPS[rng() % SEPS.size()];
    }
    (void)load(text);
    const Values v = current();
    assert_in_range(v);
    changed += v == DEFAULTS ? 0 : 1;
    hash = fnv1a(hash, v);
  }
  TEST_ASSERT_EQUAL_INT(SET_054_GOLDEN_CHANGED, changed);
  TEST_ASSERT_EQUAL_HEX32(SET_054_GOLDEN_HASH, hash);
}
