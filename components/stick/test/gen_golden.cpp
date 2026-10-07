// Generates golden_stick.inc ONCE from the legacy harness (the verbatim copy of today's ADC
// task math). After that the table is frozen: it is the declaration the extraction is measured
// against (CORE never-list: never edit a golden to silence a check). `make golden` rebuilds
// this tool and writes the table to $(BUILD_DIR); copying it over the committed table is a
// deliberate, reviewed act.
//
// Scenarios (each starts from power-on): single-axis sweeps over [0, 3300] mV incl. the
// board-2 calibration ends and beyond, an X/Y grid, noise around rest, every stick
// sensitivity and drive speed (and out-of-range values), the 8 swap/invert combinations, the
// gate open/closed x calibrating on/off, invalid ADC cycles, a calibration switch, the remote
// key, the button, and the open/short-circuit hazards.

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "legacy_adc_cycle.hpp"
#include "stick_vectors.hpp"

using stick_test::CalId;
using stick_test::StickOutputs;
using stick_test::StickVector;

namespace {

struct AxisMv {
  float min_mv;
  float center_mv;
  float max_mv;
};
struct CalMv {
  AxisMv horizontal;
  AxisMv vertical;
  AxisMv twist;
};
constexpr CalMv IDEAL{{0, 1650, 3300}, {0, 1650, 3300}, {0, 1650, 3300}};
constexpr CalMv BOARD2{{11, 1507, 2971}, {6, 1510, 2962}, {10, 1477, 2960}};

const CalMv &cal_mv(CalId id) { return id == stick_test::CAL_IDEAL ? IDEAL : BOARD2; }

struct Row {
  StickVector v;
  std::string comment;
  std::string name; // non-empty: emitted as ROW_<name>
};

std::vector<Row> g_rows;

/// Inputs of a quiet cycle on `cal`: stick centred, Settings defaults (main frag_state.inc:
/// sensitivity 9, speed 10), gate open, no button, no remote key.
StickVector rest(CalId cal) {
  const CalMv &c = cal_mv(cal);
  StickVector v{};
  v.power_on_cal = cal;
  v.horiz_ok = v.vert_ok = v.twist_ok = true;
  v.horiz_mv = c.horizontal.center_mv;
  v.vert_mv = c.vertical.center_mv;
  v.twist_mv = c.twist.center_mv;
  v.sensitivity = 9;
  v.drive_speed = 10;
  v.drives = true;
  return v;
}

void add(StickVector v, const std::string &comment, const std::string &name = {}) {
  g_rows.push_back({v, comment, name});
}

/// Starts a scenario: its first vector resets to power-on.
void start(StickVector v, const std::string &comment, const std::string &name = {}) {
  v.reset = true;
  add(v, comment, name);
}

std::uint32_t g_lcg = 12345U;
float noise_mv(float amplitude_mv) { // deterministic, in [-amplitude, +amplitude]
  g_lcg = g_lcg * 1664525U + 1013904223U;
  const float unit = static_cast<float>(g_lcg >> 8U) / 16777215.0f; // [0, 1]
  return (unit * 2.0f - 1.0f) * amplitude_mv;
}

void sweeps() {
  const float specials[] = {1, 5, 6, 10, 11, 12, 1477, 1507, 1510, 2960, 2962, 2971, 2972, 3000};
  for (const CalId cal : {stick_test::CAL_IDEAL, stick_test::CAL_BOARD2}) {
    for (int axis = 0; axis < 3; ++axis) {
      const char *axis_name = axis == 0 ? "horizontal" : axis == 1 ? "vertical" : "twist";
      std::vector<float> values;
      for (int mv = 0; mv <= 3300; mv += 50) {
        values.push_back(static_cast<float>(mv));
      }
      values.insert(values.end(), std::begin(specials), std::end(specials));
      bool first = true;
      for (const float mv : values) {
        StickVector v = rest(cal);
        (axis == 0 ? v.horiz_mv : axis == 1 ? v.vert_mv : v.twist_mv) = mv;
        const std::string c = std::string("sweep ") + axis_name + " cal" + std::to_string(cal);
        if (first) {
          start(v, c);
          first = false;
        } else {
          add(v, c);
        }
      }
    }
  }
}

void grid() {
  for (const CalId cal : {stick_test::CAL_IDEAL, stick_test::CAL_BOARD2}) {
    bool first = true;
    for (int h = 0; h <= 3300; h += 300) {
      for (int vv = 0; vv <= 3300; vv += 300) {
        StickVector v = rest(cal);
        v.horiz_mv = static_cast<float>(h);
        v.vert_mv = static_cast<float>(vv);
        const std::string c = "grid cal" + std::to_string(cal);
        if (first) {
          start(v, c);
          first = false;
        } else {
          add(v, c);
        }
      }
    }
  }
}

void noise() {
  // inside the dead zones (X/Y +-40 mV, twist +-70 mV straddles its 60 mV dead band), then
  // around the edge of the circular dead zone (~146 mV on board 2)
  for (const float xy_amp : {40.0f, 170.0f}) {
    bool first = true;
    for (int i = 0; i < 60; ++i) {
      StickVector v = rest(stick_test::CAL_BOARD2);
      v.horiz_mv += noise_mv(xy_amp);
      v.vert_mv += noise_mv(xy_amp);
      v.twist_mv += noise_mv(70.0f);
      const std::string c = "noise +-" + std::to_string(static_cast<int>(xy_amp)) + " mV";
      if (first) {
        start(v, c);
        first = false;
      } else {
        add(v, c);
      }
    }
  }
}

/// A ramp out to the end of travel and back on one board-2 axis (0 = right, 1 = up).
void ramp(const StickVector &base, int dir, const std::string &c, bool first) {
  const CalMv &cal = BOARD2;
  for (int i = 0; i <= 24; ++i) {
    const int k = i <= 12 ? i : 24 - i;
    StickVector v = base;
    if (dir == 0) {
      v.horiz_mv = cal.horizontal.center_mv + (cal.horizontal.max_mv - cal.horizontal.center_mv) *
                                                  static_cast<float>(k) / 12.0f;
    } else { // up = toward the vertical minimum (the axis is inverted)
      v.vert_mv = cal.vertical.center_mv -
                  (cal.vertical.center_mv - cal.vertical.min_mv) * static_cast<float>(k) / 12.0f;
    }
    if (first && i == 0) {
      start(v, c);
    } else {
      add(v, c);
    }
  }
}

void sensitivities() {
  for (const int level : {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 100}) {
    StickVector v = rest(stick_test::CAL_BOARD2);
    v.sensitivity = level;
    const std::string c = "sensitivity " + std::to_string(level);
    ramp(v, 0, c + " ramp right", true);
    ramp(v, 1, c + " ramp up", false);
  }
}

/// Fixed board-2 positions used by several scenarios.
struct Pose {
  const char *name;
  float h, v, t;
};
constexpr Pose POSES[] = {
    {"rest", 1507, 1510, 1477},          {"full right", 2971, 1510, 1477},
    {"full left", 11, 1510, 1477},       {"full up", 1507, 6, 1477},
    {"full down", 1507, 2962, 1477},     {"half up-right", 2100, 1000, 1477},
    {"half down-left", 900, 2100, 1477}, {"twist cw", 1507, 1510, 2960},
    {"twist half ccw", 1507, 1510, 700},
};

StickVector posed(const Pose &p) {
  StickVector v = rest(stick_test::CAL_BOARD2);
  v.horiz_mv = p.h;
  v.vert_mv = p.v;
  v.twist_mv = p.t;
  return v;
}

void speeds() {
  for (const int speed : {-3, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 50}) {
    bool first = true;
    for (const Pose &p : POSES) {
      StickVector v = posed(p);
      v.drive_speed = speed;
      const std::string c = "drive speed " + std::to_string(speed) + " " + p.name;
      if (first) {
        start(v, c);
        first = false;
      } else {
        add(v, c);
      }
    }
  }
}

void mounts() {
  for (int m = 0; m < 8; ++m) {
    bool first = true;
    for (const Pose &p : POSES) {
      StickVector v = posed(p);
      v.swap = (m & 1) != 0;
      v.invert_x = (m & 2) != 0;
      v.invert_y = (m & 4) != 0;
      const std::string c = std::string("mount swap=") + (v.swap ? "1" : "0") +
                            " invert_x=" + (v.invert_x ? "1" : "0") +
                            " invert_y=" + (v.invert_y ? "1" : "0") + " " + p.name;
      if (first) {
        start(v, c);
        first = false;
      } else {
        add(v, c);
      }
    }
  }
}

void gates() {
  for (int g = 0; g < 4; ++g) {
    const bool drives = (g & 1) != 0;
    const bool calibrating = (g & 2) != 0;
    bool first = true;
    for (const Pose &p : POSES) {
      for (const bool button : {false, true}) {
        StickVector v = posed(p);
        v.drives = drives;
        v.calibrating = calibrating;
        v.button = button;
        const std::string c = std::string("gate drives=") + (drives ? "1" : "0") +
                              " calibrating=" + (calibrating ? "1" : "0") + " " + p.name +
                              (button ? " button" : "");
        std::string name;
        if (!drives && !calibrating && button && std::string(p.name) == "half down-left") {
          name = "GATE_CLOSED_NEGATIVE_BUTTON";
        }
        if (drives && calibrating && !button && std::string(p.name) == "full right") {
          name = "CALIBRATING_FULL_RIGHT";
        }
        if (first) {
          start(v, c, name);
          first = false;
        } else {
          add(v, c, name);
        }
      }
    }
  }
}

void calibrating_keys() {
  StickVector v = posed(POSES[1]); // full right
  start(v, "calibrating keys: right engages");
  v.calibrating = true;
  add(v, "calibrating keys: calibrating releases the key", "CALIBRATING_RELEASES_KEY");
  add(v, "calibrating keys: still calibrating");
  v.calibrating = false;
  add(v, "calibrating keys: run over, the held stick re-engages and flicks", "CAL_OVER_REFLICK");
}

void invalid_cycles() {
  StickVector v = posed(POSES[1]); // full right: engaged, key RIGHT
  start(v, "invalid: valid full right first");
  for (int m = 0; m < 7; ++m) {        // every non-valid combination of the three reads
    StickVector bad = posed(POSES[2]); // values that would read full left
    bad.horiz_ok = (m & 1) != 0;
    bad.vert_ok = (m & 2) != 0;
    bad.twist_ok = (m & 4) != 0;
    add(bad,
        "invalid: horiz_ok=" + std::to_string(bad.horiz_ok) +
            " vert_ok=" + std::to_string(bad.vert_ok) + " twist_ok=" + std::to_string(bad.twist_ok),
        m == 6 ? "INVALID_HORIZ_MISSING" : "");
  }
  add(v, "invalid: valid again");
  // a calibration that arrives on an invalid cycle waits for the next valid one
  StickVector bad = rest(stick_test::CAL_BOARD2);
  bad.twist_ok = false;
  bad.new_cal = stick_test::CAL_IDEAL;
  add(bad, "invalid: new calibration (ideal) arrives on an invalid cycle");
  StickVector right = posed(POSES[1]);
  add(right, "invalid: next valid cycle applies it (2971 mV is no longer full right)",
      "PENDING_CAL_APPLIED");
}

void new_calibration() {
  StickVector v = rest(stick_test::CAL_IDEAL);
  v.horiz_mv = 2971;
  start(v, "new cal: ideal power-on, 2971 mV", "IDEAL_2971");
  v.new_cal = stick_test::CAL_BOARD2;
  add(v, "new cal: board 2 taken this cycle, 2971 mV is full right", "BOARD2_TAKEN_2971");
  v.new_cal = stick_test::CAL_NONE;
  add(v, "new cal: board 2 stays");
  v.new_cal = stick_test::CAL_IDEAL;
  add(v, "new cal: back to ideal");
}

void remote_and_flicks() {
  StickVector v = posed(POSES[1]); // full right
  start(v, "remote: stick right");
  v.remote_key = stick_test::KEY_LEFT;
  add(v, "remote: remote LEFT overrides the stick", "REMOTE_OVERRIDES");
  v = posed(POSES[0]);
  v.remote_key = stick_test::KEY_UP;
  add(v, "remote: remote UP at rest");
  v.remote_key = 0;
  add(v, "remote: released, stick at rest");
  // roll from right to up without passing through the centre: re-aim flicks
  add(posed(POSES[1]), "flick: right");
  add(posed(POSES[1]), "flick: right held (no new flick)", "HELD_NO_FLICK");
  StickVector roll = posed(POSES[1]);
  roll.vert_mv = 400; // up and right, up the larger
  roll.horiz_mv = 2300;
  add(roll, "flick: rolled to up-right, up larger: re-aim flicks UP", "REAIM_FLICK");
  add(posed(POSES[0]), "flick: back to rest");
}

void hazards() {
  struct H {
    const char *what;
    const char *name;
    float h, v, t;
  };
  const H hs[] = {
      {"open-circuit horizontal (0 mV)", "OPEN_HORIZ", 0, 1510, 1477},
      {"open-circuit vertical (0 mV)", "OPEN_VERT", 1507, 0, 1477},
      {"open-circuit twist (0 mV)", "OPEN_TWIST", 1507, 1510, 0},
      {"horizontal shorted to 3300 mV", "SHORT_HORIZ", 3300, 1510, 1477},
      {"vertical shorted to 3300 mV", "SHORT_VERT", 1507, 3300, 1477},
      {"twist shorted to 3300 mV", "SHORT_TWIST", 1507, 1510, 3300},
      {"all three open (0 mV)", "OPEN_ALL", 0, 0, 0},
  };
  for (const H &h : hs) {
    StickVector v = rest(stick_test::CAL_BOARD2);
    v.horiz_mv = h.h;
    v.vert_mv = h.v;
    v.twist_mv = h.t;
    start(v, std::string("hazard: ") + h.what, h.name);
  }
}

std::string fmt_float(float f) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.9g", static_cast<double>(f));
  std::string s = buf;
  if (s.find_first_of(".en") == std::string::npos) {
    s += ".0";
  }
  return s + "f";
}

const char *b(bool x) { return x ? "true" : "false"; }

} // namespace

int main() {
  sweeps();
  grid();
  noise();
  sensitivities();
  speeds();
  mounts();
  gates();
  calibrating_keys();
  invalid_cycles();
  new_calibration();
  remote_and_flicks();
  hazards();

  std::printf(
      "// GENERATED ONCE by components/stick/test/gen_golden.cpp from the legacy harness\n"
      "// (legacy_adc_cycle.cpp, a verbatim copy of the ADC task's stick math at dev_refactor\n"
      "// 7ea7592), then FROZEN. Never edit it to make a test pass (CORE never-list).\n"
      "// Row: inputs {reset, power_on_cal, horiz_ok, vert_ok, twist_ok, horiz_mv, vert_mv,\n"
      "// twist_mv, new_cal, calibrating, swap, invert_x, invert_y, sensitivity, remote_key,\n"
      "// drive_speed, drives, button} then outputs {published, cmd_x, cmd_y, cmd_twist (float\n"
      "// bits), buttons, bars_set, bar_x, bar_y, bar_twist, joy_key, joy_flick}.\n"
      "// clang-format off\n"
      "#pragma once\n\n#include <array>\n#include <cstddef>\n\n#include \"stick_vectors.hpp\"\n\n"
      "namespace stick_test {\n\n");
  for (std::size_t i = 0; i < g_rows.size(); ++i) {
    if (!g_rows[i].name.empty()) {
      std::printf("inline constexpr std::size_t ROW_%s = %zu;\n", g_rows[i].name.c_str(), i);
    }
  }
  std::printf("\ninline constexpr std::array<StickVector, %zu> GOLDEN{{\n", g_rows.size());
  for (std::size_t i = 0; i < g_rows.size(); ++i) {
    const StickVector &v = g_rows[i].v;
    const StickOutputs o = stick_test::legacy::cycle(v);
    std::printf("  /* %4zu %s */\n", i, g_rows[i].comment.c_str());
    std::printf(
        "  {%s, %u, %s, %s, %s, %s, %s, %s, %u, %s, %s, %s, %s, %d, %" PRIu32 "u, %d, %s, %s,\n",
        b(v.reset), v.power_on_cal, b(v.horiz_ok), b(v.vert_ok), b(v.twist_ok),
        fmt_float(v.horiz_mv).c_str(), fmt_float(v.vert_mv).c_str(), fmt_float(v.twist_mv).c_str(),
        v.new_cal, b(v.calibrating), b(v.swap), b(v.invert_x), b(v.invert_y), v.sensitivity,
        v.remote_key, v.drive_speed, b(v.drives), b(v.button));
    std::printf("   %s, 0x%08" PRIx32 "u, 0x%08" PRIx32 "u, 0x%08" PRIx32 "u, %" PRIu32
                "u, %s, %" PRId32 ", %" PRId32 ", %" PRId32 ", %" PRIu32 "u, %" PRIu32 "u},\n",
                b(o.published), o.cmd_x, o.cmd_y, o.cmd_twist, o.buttons, b(o.bars_set), o.bar_x,
                o.bar_y, o.bar_twist, o.joy_key, o.joy_flick);
  }
  std::printf("}};\n\n} // namespace stick_test\n// clang-format on\n");
  return 0;
}
