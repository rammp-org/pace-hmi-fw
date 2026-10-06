// L1 host tests for the stick pipeline (step 11, refactor.md §3.2; CS-SAF-02 last bullet).
//
// STK-001..009  golden replays: the frozen table (golden_stick.inc) against the legacy harness
//               (a verbatim copy of today's ADC-task math) and against the extraction
// STK-010..029  the behaviours the table pins, hazards first, read off named golden rows. They
//               state what the code does TODAY, including what is unsafe (refactor.md §1 H3,
//               H9): a characterisation, not a specification. Fixing them is parked (§3.3).

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "golden_stick.inc"
#include "legacy_adc_cycle.hpp"
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
