// L1 host tests for the stick pipeline (step 11, refactor.md §3.2; CS-SAF-02 last bullet).
//
// STK-001..009  golden replays: the frozen table (golden_stick.inc) against the legacy harness
//               (a verbatim copy of today's ADC-task math) and against the extraction
// STK-010..029  the behaviours the table pins, hazards first, read off named golden rows. They
//               state what the code does TODAY, including what is unsafe (refactor.md §1 H3,
//               H9): a characterisation, not a specification. Fixing them is parked (§3.3).
// STK-030..     the extracted pure stages, one behaviour each

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>

#include "golden_stick.inc"
#include "legacy_adc_cycle.hpp"
#include "stick/stick_pipeline.hpp"
#include "stick_vectors.hpp"
#include "test_case.hpp"

namespace {

using stick_test::GOLDEN;
using stick_test::StickOutputs;
using stick_test::StickVector;

constexpr std::uint32_t bits(float f) { return std::bit_cast<std::uint32_t>(f); }

/// Compares one cycle's outputs with a golden row; on a mismatch names the row and the field.
bool same(std::size_t row, const StickVector &want, const StickOutputs &got) {
  struct Field {
    const char *name;
    long long want;
    long long got;
  };
  const Field fields[] = {
      {"published", want.published, got.published},
      {"cmd_x", want.cmd_x, got.cmd_x},
      {"cmd_y", want.cmd_y, got.cmd_y},
      {"cmd_twist", want.cmd_twist, got.cmd_twist},
      {"buttons", want.buttons, got.buttons},
      {"bars_set", want.bars_set, got.bars_set},
      {"bar_x", want.bar_x, got.bar_x},
      {"bar_y", want.bar_y, got.bar_y},
      {"bar_twist", want.bar_twist, got.bar_twist},
      {"joy_key", want.joy_key, got.joy_key},
      {"joy_flick", want.joy_flick, got.joy_flick},
  };
  bool ok = true;
  for (const Field &f : fields) {
    if (f.want != f.got) {
      std::printf("row %zu %s: golden 0x%llx, got 0x%llx\n", row, f.name,
                  static_cast<unsigned long long>(f.want), static_cast<unsigned long long>(f.got));
      ok = false;
    }
  }
  return ok;
}

// ---------------------------------------------------------------- the extraction under test

using hmi::stick::CalibrationMv;
using hmi::stick::StickPipeline;

constexpr CalibrationMv CAL_IDEAL_MV{
    {0.0f, 1650.0f, 3300.0f}, {0.0f, 1650.0f, 3300.0f}, {0.0f, 1650.0f, 3300.0f}};
constexpr CalibrationMv CAL_BOARD2_MV{
    {11.0f, 1507.0f, 2971.0f}, {6.0f, 1510.0f, 2962.0f}, {10.0f, 1477.0f, 2960.0f}};

std::optional<CalibrationMv> cal_of(std::uint8_t id) {
  switch (id) {
  case stick_test::CAL_IDEAL:
    return CAL_IDEAL_MV;
  case stick_test::CAL_BOARD2:
    return CAL_BOARD2_MV;
  default:
    return std::nullopt;
  }
}

/// The values main passes (frag_status_band.inc:154-158, LVGL's key codes).
StickPipeline::Config pipeline_config(std::uint8_t cal) {
  return {.calibration = *cal_of(cal),
          .center_deadzone_radius = 0.10f,
          .range_deadzone = 0.05f,
          .twist_center_deadband_mv = 60.0f,
          .twist_range_deadband_mv = 40.0f,
          .keys = {.up = stick_test::KEY_UP,
                   .down = stick_test::KEY_DOWN,
                   .right = stick_test::KEY_RIGHT,
                   .left = stick_test::KEY_LEFT}};
}

/// The order of Io calls, one letter each. Fixed size: a failing Unity assert longjmps out of
/// the case, and a heap string would then show up as a leak instead of the failure.
struct Trace {
  std::array<char, 40> text{};
  std::size_t len = 0;
  void add(char c) {
    if (len + 1 < text.size()) {
      text[len] = c;
      ++len;
    }
  }
  const char *c_str() const { return text.data(); }
};

/// A fake of the ADC task's surroundings, like the legacy harness's: the vector's inputs in,
/// the cycle's outputs recorded, joy_key kept across cycles, and `trace` the order of calls.
struct FakeIo {
  std::optional<CalibrationMv> pending_cal;
  const StickVector *in = nullptr;
  std::uint32_t key = 0;
  std::uint32_t flick = 0;
  StickOutputs out{};
  Trace trace;

  std::optional<CalibrationMv> take_new_calibration() {
    trace.add('T');
    const std::optional<CalibrationMv> cal = pending_cal;
    pending_cal.reset();
    return cal;
  }
  float smooth_twist_mv(float twist_mv) {
    trace.add('S');
    return twist_mv; // the filter stays in main; the vector's twist is its output
  }
  void note_raw_mv(float, float, float) { trace.add('N'); }
  bool calibrating() {
    trace.add('C');
    return in->calibrating;
  }
  bool swap() {
    trace.add('w');
    return in->swap;
  }
  bool invert_x() {
    trace.add('x');
    return in->invert_x;
  }
  bool invert_y() {
    trace.add('y');
    return in->invert_y;
  }
  int sensitivity() {
    trace.add('s');
    return in->sensitivity;
  }
  std::uint32_t joy_key() {
    trace.add('k');
    return key;
  }
  std::uint32_t remote_key() {
    trace.add('r');
    return in->remote_key;
  }
  void set_joy_key(std::uint32_t k) {
    trace.add('K');
    key = k;
  }
  void set_joy_flick(std::uint32_t f) {
    trace.add('F');
    flick = f;
  }
  void show(const hmi::stick::Position &p) {
    trace.add('B');
    out.bars_set = true;
    out.bar_x = static_cast<std::int32_t>(p.x * 100.0f);
    out.bar_y = static_cast<std::int32_t>(p.y * 100.0f);
    out.bar_twist = static_cast<std::int32_t>(p.twist * 100.0f);
  }
  int drive_speed() {
    trace.add('d');
    return in->drive_speed;
  }
  bool stick_drives() {
    trace.add('g');
    return in->drives;
  }
  bool button_pressed() {
    trace.add('b');
    return in->button;
  }
  bool publish(const hmi::stick::Command &c, bool button) {
    trace.add('P');
    out.published = true;
    out.cmd_x = std::bit_cast<std::uint32_t>(c.x);
    out.cmd_y = std::bit_cast<std::uint32_t>(c.y);
    out.cmd_twist = std::bit_cast<std::uint32_t>(c.twist);
    out.buttons = button ? 1U : 0U; // rammp::Buttons::JOYSTICK : NONE
    return true;
  }
};

/// Runs vectors through a StickPipeline the way the legacy harness runs them through today's
/// code: power-on state on `reset`, a pending calibration until a valid cycle takes it, and
/// joy_flick cleared before each cycle (the keypad read).
struct PipelineRunner {
  std::optional<StickPipeline> pipeline;
  FakeIo io;

  StickOutputs cycle(const StickVector &v) {
    if (v.reset || !pipeline) {
      pipeline.reset();
      pipeline.emplace(pipeline_config(v.power_on_cal));
      io = FakeIo{};
    }
    if (v.new_cal != stick_test::CAL_NONE) {
      io.pending_cal = cal_of(v.new_cal);
    }
    io.in = &v;
    io.out = StickOutputs{};
    io.flick = 0;
    io.trace = Trace{};
    const hmi::stick::RawReadsMv raw{
        .horizontal_mv = v.horiz_ok ? std::optional<float>{v.horiz_mv} : std::nullopt,
        .vertical_mv = v.vert_ok ? std::optional<float>{v.vert_mv} : std::nullopt,
        .twist_mv = v.twist_ok ? std::optional<float>{v.twist_mv} : std::nullopt};
    const bool published = pipeline->cycle(io, raw);
    StickOutputs out = io.out;
    out.published = out.published && published;
    out.joy_key = io.key;
    out.joy_flick = io.flick;
    return out;
  }
};

} // namespace

// ---------------------------------------------------------------- golden replays

TEST_CASE("STK-001 the legacy harness reproduces every frozen golden row bit-exactly",
          "[stick][golden][safety]") {
  std::size_t failures = 0;
  for (std::size_t i = 0; i < GOLDEN.size(); ++i) {
    if (!same(i, GOLDEN[i], stick_test::legacy::cycle(GOLDEN[i]))) {
      ++failures;
    }
  }
  TEST_ASSERT_EQUAL_size_t(0, failures);
}

TEST_CASE("STK-002 StickPipeline reproduces every frozen golden row bit-exactly",
          "[stick][golden][safety]") {
  PipelineRunner runner;
  std::size_t failures = 0;
  for (std::size_t i = 0; i < GOLDEN.size(); ++i) {
    if (!same(i, GOLDEN[i], runner.cycle(GOLDEN[i]))) {
      ++failures;
    }
  }
  TEST_ASSERT_EQUAL_size_t(0, failures);
}

TEST_CASE("STK-003 a valid cycle calls its Io in today's order", "[stick][order][safety]") {
  // T take_new_calibration, S smooth_twist, N note_raw, C calibrating, w/x/y swap/invert,
  // s sensitivity, k joy_key, r remote_key, K set_joy_key, F set_joy_flick, B show (bars),
  // d drive_speed, g stick_drives, b button, P publish: the order of main.cpp:1481-1601
  PipelineRunner runner;
  StickVector v = GOLDEN[stick_test::ROW_REMOTE_OVERRIDES]; // remote key set: read twice
  v.reset = true;
  (void)runner.cycle(v);
  TEST_ASSERT_EQUAL_STRING("TSNCwxyskrrKkFBdgbP", runner.io.trace.c_str());
  v.reset = false;
  v.remote_key = 0; // the stick's own key: RIGHT, a change from LEFT, so it flicks
  (void)runner.cycle(v);
  TEST_ASSERT_EQUAL_STRING("TSNCwxyskrKkFBdgbP", runner.io.trace.c_str());
  (void)runner.cycle(v); // RIGHT held: no flick
  TEST_ASSERT_EQUAL_STRING("TSNCwxyskrKkBdgbP", runner.io.trace.c_str());
}

TEST_CASE("STK-004 while calibrating the gate's stick_drives is not read", "[stick][order]") {
  PipelineRunner runner;
  StickVector v = GOLDEN[stick_test::ROW_CALIBRATING_FULL_RIGHT];
  v.reset = true;
  (void)runner.cycle(v);
  TEST_ASSERT_EQUAL_STRING("TSNCwxyskrKkBdbP", runner.io.trace.c_str());
}

TEST_CASE("STK-005 an invalid cycle calls nothing on its Io", "[stick][order][safety]") {
  PipelineRunner runner;
  for (int m = 0; m < 7; ++m) { // every combination with at least one read missing
    StickVector v = GOLDEN[stick_test::ROW_OPEN_HORIZ];
    v.reset = m == 0;
    v.horiz_ok = (m & 1) != 0;
    v.vert_ok = (m & 2) != 0;
    v.twist_ok = (m & 4) != 0;
    const StickOutputs out = runner.cycle(v);
    TEST_ASSERT_FALSE(out.published);
    TEST_ASSERT_EQUAL_STRING("", runner.io.trace.c_str());
  }
}

// ---------------------------------------------------------------- hazards pinned (today)

TEST_CASE("STK-010 today an open-circuit horizontal pot (0 mV) commands full left",
          "[stick][hazard][safety]") {
  const StickVector &r = GOLDEN[stick_test::ROW_OPEN_HORIZ];
  TEST_ASSERT_TRUE(r.published);
  TEST_ASSERT_EQUAL_HEX32(bits(-1.0f), r.cmd_x);
  TEST_ASSERT_EQUAL_HEX32(bits(0.0f), r.cmd_y);
}

TEST_CASE("STK-011 today an open-circuit vertical pot (0 mV) commands full forward",
          "[stick][hazard][safety]") {
  const StickVector &r = GOLDEN[stick_test::ROW_OPEN_VERT];
  TEST_ASSERT_TRUE(r.published);
  TEST_ASSERT_EQUAL_HEX32(bits(0.0f), r.cmd_x);
  TEST_ASSERT_EQUAL_HEX32(bits(1.0f), r.cmd_y);
}

TEST_CASE("STK-012 today an open-circuit twist pot (0 mV) commands full counter-clockwise",
          "[stick][hazard][safety]") {
  const StickVector &r = GOLDEN[stick_test::ROW_OPEN_TWIST];
  TEST_ASSERT_TRUE(r.published);
  TEST_ASSERT_EQUAL_HEX32(bits(-1.0f), r.cmd_twist);
}

TEST_CASE("STK-013 today a pot shorted to 3300 mV commands full right, reverse or clockwise",
          "[stick][hazard][safety]") {
  TEST_ASSERT_EQUAL_HEX32(bits(1.0f), GOLDEN[stick_test::ROW_SHORT_HORIZ].cmd_x);
  TEST_ASSERT_EQUAL_HEX32(bits(-1.0f), GOLDEN[stick_test::ROW_SHORT_VERT].cmd_y);
  TEST_ASSERT_EQUAL_HEX32(bits(1.0f), GOLDEN[stick_test::ROW_SHORT_TWIST].cmd_twist);
}

TEST_CASE("STK-014 today all three pots open command full left-forward and full twist",
          "[stick][hazard][safety]") {
  const StickVector &r = GOLDEN[stick_test::ROW_OPEN_ALL];
  TEST_ASSERT_TRUE(r.published);
  TEST_ASSERT_EQUAL_HEX32(0xbf3504f3U, r.cmd_x); // -0.70710677: normalised onto the unit circle
  TEST_ASSERT_EQUAL_HEX32(0x3f3504f3U, r.cmd_y);
  TEST_ASSERT_EQUAL_HEX32(bits(-1.0f), r.cmd_twist);
}

TEST_CASE("STK-015 today an invalid ADC cycle publishes nothing and keeps the last key (H9)",
          "[stick][hazard][safety]") {
  const StickVector &r = GOLDEN[stick_test::ROW_INVALID_HORIZ_MISSING];
  TEST_ASSERT_FALSE(r.published);
  TEST_ASSERT_FALSE(r.bars_set);
  TEST_ASSERT_EQUAL_UINT32(stick_test::KEY_RIGHT, r.joy_key);
}

TEST_CASE("STK-016 the closed gate is a multiply: a negative deflection is sent as -0.0",
          "[stick][gate][safety]") {
  const StickVector &r = GOLDEN[stick_test::ROW_GATE_CLOSED_NEGATIVE_BUTTON];
  TEST_ASSERT_FALSE(r.drives);
  TEST_ASSERT_TRUE(r.published);
  TEST_ASSERT_EQUAL_HEX32(bits(-0.0f), r.cmd_x);
  TEST_ASSERT_EQUAL_HEX32(bits(-0.0f), r.cmd_y);
}

TEST_CASE("STK-017 the stick button reaches the MCB with the gate closed",
          "[stick][gate][safety]") {
  const StickVector &r = GOLDEN[stick_test::ROW_GATE_CLOSED_NEGATIVE_BUTTON];
  TEST_ASSERT_FALSE(r.drives);
  TEST_ASSERT_EQUAL_UINT32(1U, r.buttons); // rammp::Buttons::JOYSTICK
}

TEST_CASE("STK-018 a calibration run zeroes the command with the gate open, bars still move",
          "[stick][gate][safety]") {
  const StickVector &r = GOLDEN[stick_test::ROW_CALIBRATING_FULL_RIGHT];
  TEST_ASSERT_TRUE(r.drives);
  TEST_ASSERT_TRUE(r.calibrating);
  TEST_ASSERT_EQUAL_HEX32(bits(0.0f), r.cmd_x);
  TEST_ASSERT_EQUAL_INT32(100, r.bar_x);
}

TEST_CASE("STK-019 a calibration run releases the key; the held stick re-engages after it",
          "[stick][keys]") {
  TEST_ASSERT_EQUAL_UINT32(0U, GOLDEN[stick_test::ROW_CALIBRATING_RELEASES_KEY].joy_key);
  TEST_ASSERT_EQUAL_UINT32(stick_test::KEY_RIGHT, GOLDEN[stick_test::ROW_CAL_OVER_REFLICK].joy_key);
  TEST_ASSERT_EQUAL_UINT32(stick_test::KEY_RIGHT,
                           GOLDEN[stick_test::ROW_CAL_OVER_REFLICK].joy_flick);
}

TEST_CASE("STK-020 a calibration that arrives on an invalid cycle applies on the next valid one",
          "[stick][calibration]") {
  // 2971 mV is board 2's end of travel; on the ideal calibration it is short of it
  TEST_ASSERT_EQUAL_HEX32(0x3f532c7eU, GOLDEN[stick_test::ROW_PENDING_CAL_APPLIED].cmd_x);
}

TEST_CASE("STK-021 a new calibration applies on the cycle that takes it", "[stick][calibration]") {
  TEST_ASSERT_EQUAL_HEX32(0x3f53018eU, GOLDEN[stick_test::ROW_IDEAL_2971].cmd_x);
  TEST_ASSERT_EQUAL_HEX32(0x3f7ed17bU, GOLDEN[stick_test::ROW_BOARD2_TAKEN_2971].cmd_x);
}

TEST_CASE("STK-022 the remote key overrides the stick's key and flicks", "[stick][keys]") {
  const StickVector &r = GOLDEN[stick_test::ROW_REMOTE_OVERRIDES];
  TEST_ASSERT_EQUAL_UINT32(stick_test::KEY_LEFT, r.joy_key);
  TEST_ASSERT_EQUAL_UINT32(stick_test::KEY_LEFT, r.joy_flick);
}

TEST_CASE("STK-023 a held key does not flick again; a re-aim does", "[stick][keys]") {
  TEST_ASSERT_EQUAL_UINT32(0U, GOLDEN[stick_test::ROW_HELD_NO_FLICK].joy_flick);
  TEST_ASSERT_EQUAL_UINT32(stick_test::KEY_UP, GOLDEN[stick_test::ROW_REAIM_FLICK].joy_key);
  TEST_ASSERT_EQUAL_UINT32(stick_test::KEY_UP, GOLDEN[stick_test::ROW_REAIM_FLICK].joy_flick);
}

// ---------------------------------------------------------------- the pure stages

TEST_CASE("STK-030 key thresholds clamp the sensitivity to 1..10", "[stick][keys]") {
  const hmi::stick::KeyThresholds lo = hmi::stick::key_thresholds(1);
  const hmi::stick::KeyThresholds hi = hmi::stick::key_thresholds(10);
  TEST_ASSERT_EQUAL_HEX32(bits(0.30f), bits(lo.engage));
  TEST_ASSERT_EQUAL_HEX32(bits(0.15f), bits(lo.release));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.01f, hi.engage);
  TEST_ASSERT_EQUAL_HEX32(bits(lo.engage), bits(hmi::stick::key_thresholds(0).engage));
  TEST_ASSERT_EQUAL_HEX32(bits(lo.engage),
                          bits(hmi::stick::key_thresholds(std::numeric_limits<int>::min()).engage));
  TEST_ASSERT_EQUAL_HEX32(bits(hi.engage), bits(hmi::stick::key_thresholds(11).engage));
}

TEST_CASE("STK-036 every sensitivity level keeps today's key threshold bits", "[stick][keys]") {
  // Frozen once from main.cpp:1534-1535's expression on the host (refactor.md §3.2): the
  // golden replay compares outputs, and no vector sits on a threshold to the ulp.
  constexpr std::array<std::array<std::uint32_t, 2>, 10> ENGAGE_RELEASE{{
      {0x3e99999aU, 0x3e19999aU}, // 1: 0.300000012 / 0.150000006
      {0x3e891a2cU, 0x3e091a2cU}, // 2: 0.267777801 / 0.1338889
      {0x3e71357aU, 0x3df1357aU}, // 3: 0.235555559 / 0.11777778
      {0x3e50369eU, 0x3dd0369eU}, // 4: 0.203333348 / 0.101666674
      {0x3e2f37c1U, 0x3daf37c1U}, // 5: 0.171111122 / 0.0855555609
      {0x3e0e38e4U, 0x3d8e38e4U}, // 6: 0.138888896 / 0.0694444478
      {0x3dda7410U, 0x3d5a7410U}, // 7: 0.106666684 / 0.0533333421
      {0x3d987656U, 0x3d187656U}, // 8: 0.0744444579 / 0.0372222289
      {0x3d2cf138U, 0x3cacf138U}, // 9: 0.0422222316 / 0.0211111158
      {0x3c23d720U, 0x3ba3d720U}, // 10: 0.0100000203 / 0.00500001013
  }};
  for (int level = 1; level <= 10; ++level) {
    const hmi::stick::KeyThresholds t = hmi::stick::key_thresholds(level);
    const auto &want = ENGAGE_RELEASE[static_cast<std::size_t>(level - 1)];
    TEST_ASSERT_EQUAL_HEX32(want[0], bits(t.engage));
    TEST_ASSERT_EQUAL_HEX32(want[1], bits(t.release));
  }
}

TEST_CASE("STK-031 the key trigger holds its state between release and engage", "[stick][keys]") {
  const hmi::stick::KeyThresholds t{.engage = 0.2f, .release = 0.1f};
  TEST_ASSERT_TRUE(hmi::stick::key_engaged(false, false, 0.21f, 0.0f, t));
  TEST_ASSERT_TRUE(hmi::stick::key_engaged(false, false, 0.0f, -0.21f, t)); // the larger axis
  TEST_ASSERT_FALSE(hmi::stick::key_engaged(false, false, 0.15f, 0.0f, t));
  TEST_ASSERT_TRUE(hmi::stick::key_engaged(true, false, 0.15f, 0.0f, t));
  TEST_ASSERT_FALSE(hmi::stick::key_engaged(true, false, 0.09f, 0.0f, t));
  TEST_ASSERT_FALSE(hmi::stick::key_engaged(true, true, 1.0f, 0.0f, t)); // calibrating
}

TEST_CASE("STK-032 the key direction follows the larger axis and a tie goes to x",
          "[stick][keys]") {
  const hmi::stick::KeyCodes c{.up = 1, .down = 2, .right = 3, .left = 4};
  TEST_ASSERT_EQUAL_UINT32(0U, hmi::stick::key_direction(false, 1.0f, 0.0f, c));
  TEST_ASSERT_EQUAL_UINT32(3U, hmi::stick::key_direction(true, 0.5f, 0.5f, c));
  TEST_ASSERT_EQUAL_UINT32(4U, hmi::stick::key_direction(true, -0.5f, 0.4f, c));
  TEST_ASSERT_EQUAL_UINT32(1U, hmi::stick::key_direction(true, 0.1f, 0.4f, c));
  TEST_ASSERT_EQUAL_UINT32(2U, hmi::stick::key_direction(true, 0.1f, -0.4f, c));
}

TEST_CASE("STK-033 the drive speed scale clamps to 0.1..1.0", "[stick][scale]") {
  TEST_ASSERT_EQUAL_HEX32(bits(1.0f), bits(hmi::stick::drive_speed_scale(10)));
  TEST_ASSERT_EQUAL_HEX32(bits(1.0f), bits(hmi::stick::drive_speed_scale(1000)));
  TEST_ASSERT_EQUAL_HEX32(bits(1.0f / 10.0f), bits(hmi::stick::drive_speed_scale(1)));
  TEST_ASSERT_EQUAL_HEX32(bits(1.0f / 10.0f), bits(hmi::stick::drive_speed_scale(-5)));
}

TEST_CASE("STK-034 mount swaps first, then mirrors, and leaves twist alone", "[stick][mount]") {
  const hmi::stick::Position p{.x = 0.25f, .y = -0.5f, .twist = 0.75f};
  const hmi::stick::Position m = hmi::stick::mount(p, true, true, false);
  TEST_ASSERT_EQUAL_HEX32(bits(0.5f), bits(m.x)); // y moved to x, then mirrored
  TEST_ASSERT_EQUAL_HEX32(bits(0.25f), bits(m.y));
  TEST_ASSERT_EQUAL_HEX32(bits(0.75f), bits(m.twist));
  const hmi::stick::Position n = hmi::stick::mount(p, false, false, true);
  TEST_ASSERT_EQUAL_HEX32(bits(0.25f), bits(n.x));
  TEST_ASSERT_EQUAL_HEX32(bits(0.5f), bits(n.y));
}

TEST_CASE("STK-035 the command is a multiply: a NaN passes a closed gate as NaN",
          "[stick][gate][safety]") {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const hmi::stick::Command c = hmi::stick::command({.x = nan, .y = -0.5f, .twist = 0.5f}, 0.0f);
  TEST_ASSERT_TRUE(std::isnan(c.x));
  TEST_ASSERT_EQUAL_HEX32(bits(-0.0f), bits(c.y));
  TEST_ASSERT_EQUAL_HEX32(bits(0.0f), bits(c.twist));
}
