// L1 host app: the drive goldens (app-main-shrink.md S1 and V5; hazard-c1-spec.md §5.2).
//
// The hazard fix C1 retired GLD-001, GLD-002 and GLD-004 (owner decision G1, hazard-c1-spec.md
// E1): they pinned the drive code's port and boundary calls before C1. Their frozen logs,
// golden_port.txt and golden_raw.txt, stay in this folder unchanged as history; they are never
// re-recorded. GLD-003 (every row taken) is replaced by GLD-115.
//
// What runs here:
//   GLD-101..114  C1's hand-written goldens (goldens_c1.cpp): the firmware's drive code (drive_ui's
//                 DrivePort over the recording shims, and the one DriveAdapter) against the
//                 filtered log the spec writes for each step;
//   GLD-115       the hand-written scenarios and the seeded walks (scripts.cpp) take every row of
//                 the drive table; no log is compared;
//   GLD-116       DrivePort's sample reads the link live, not from the rtps_link subject;
//   GLD-117..124  C3's hand-written goldens (the boot DISABLE, the refusals before POST pass);
//   GLD-125       DrivePort's sample reads the POST gate live.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "goldens.hpp"
#include "probe.hpp"
#include "scripts.hpp"
#include "stick/permit_types.hpp"
#include "test_case.hpp"
#include "world.hpp"

namespace golden {
Target main_unit_target();
bool port_sample_link_connected();
bool port_sample_post_ok();
} // namespace golden

namespace {

// One golden by its ID, against the firmware's drive code. Unity's asserts longjmp: the work
// that allocates is done before them.
void expect_golden(const char *id) {
  std::size_t steps = 0;
  std::size_t differences = 0;
  {
    const golden::Golden g = golden::golden_by_id(id);
    steps = g.steps.size();
    differences = steps == 0 ? 0 : golden::run_golden(g, golden::main_unit_target());
  }
  TEST_ASSERT_TRUE_MESSAGE(steps > 0, "no golden with this ID");
  TEST_ASSERT_EQUAL_UINT64(0, differences);
}

} // namespace

TEST_CASE("GLD-101 the MCB enables unasked: the entry, no DriveCommand", "[drive_golden]") {
  expect_golden("GLD-101");
}

TEST_CASE("GLD-102 the MCB stops on its own: relock, one DISABLE, stopped banner",
          "[drive_golden]") {
  expect_golden("GLD-102");
}

TEST_CASE("GLD-103 the link goes: relock, one DISABLE, lost banner; back ENABLED: the entry",
          "[drive_golden]") {
  expect_golden("GLD-103");
}

TEST_CASE("GLD-104 the exit hold or the burger key, the MCB obeys: Stopping, re-sent, relock, "
          "the menu after the key (GLD-104b)",
          "[drive_golden]") {
  expect_golden("GLD-104");
  expect_golden("GLD-104b");
}

TEST_CASE("GLD-105 the MCB ignores the stop: DISABLE every 250 ms, the fault at 5 s, then 1 Hz",
          "[drive_golden]") {
  expect_golden("GLD-105");
}

TEST_CASE("GLD-106 the burger key during an ignored stop keeps the fault at 5 s from the first "
          "stop",
          "[drive_golden]") {
  expect_golden("GLD-106");
}

TEST_CASE("GLD-107 an ignored stop ends when the MCB stops: relock, one DISABLE, notice cleared",
          "[drive_golden]") {
  expect_golden("GLD-107");
}

TEST_CASE("GLD-108 no entry while a calibration runs", "[drive_golden]") {
  expect_golden("GLD-108");
}

TEST_CASE("GLD-109 no entry behind the Boot screen", "[drive_golden]") { expect_golden("GLD-109"); }

TEST_CASE("GLD-110 a profile tap sends ENABLE only while the MCB is ENABLED", "[drive_golden]") {
  expect_golden("GLD-110");
}

TEST_CASE("GLD-111 the exit hold while locked sends one DISABLE and nothing else",
          "[drive_golden]") {
  expect_golden("GLD-111");
}

TEST_CASE("GLD-112 the exit hold while asking withdraws the ask", "[drive_golden]") {
  expect_golden("GLD-112");
}

TEST_CASE("GLD-113 a link loss ends the user's stop; back ENABLED, the entry again",
          "[drive_golden]") {
  expect_golden("GLD-113");
}

TEST_CASE("GLD-114 a corrupted input during a stop gives the safe state and reports it",
          "[drive_golden]") {
  expect_golden("GLD-114");
}

TEST_CASE("GLD-115 the scenarios and the seeded walks take every row of the drive table (C1 and "
          "C3)",
          "[drive_golden]") {
  golden::run_all(golden::main_unit_target());
  const auto &hits = golden::row_hits();
  unsigned covered = 0;
  for (std::size_t i = 0; i < hmi::drive_session::kTransitionCount; ++i) {
    if (hits[i] == 0) {
      std::printf("row %zu is not taken by any scenario\n", i + 1);
    } else {
      ++covered;
    }
  }
  std::printf("%zu scenarios, rows taken %u/%zu, steps with no row %u\n",
              golden::scenarios().size(), covered, hmi::drive_session::kTransitionCount,
              hits[hmi::drive_session::kTransitionCount]);
  TEST_ASSERT_EQUAL_UINT(hmi::drive_session::kTransitionCount, covered);
}

TEST_CASE("GLD-116 DrivePort's sample reads the link live: the subject CONNECTED, the link "
          "NO_PEER reads not connected",
          "[drive_golden][REQ-UI-18]") {
  golden::reset_world();
  golden::world().link = true; // the rtps_link subject: CONNECTED
  golden::world().live_link = false;
  const bool stale_subject = golden::port_sample_link_connected();
  golden::world().link = false;
  golden::world().live_link = true;
  const bool live = golden::port_sample_link_connected();
  TEST_ASSERT_FALSE(stale_subject);
  TEST_ASSERT_TRUE(live);
}

// ---- The hazard fix C3 (hazard-c3-spec.md §6.2) --------------------------------------------

TEST_CASE("GLD-117 boot with the link down, then up: one DISABLE on the first CONNECTED tick",
          "[drive_golden]") {
  expect_golden("GLD-117");
}

TEST_CASE("GLD-118 boot with the MCB ENABLED: the boot DISABLE, the entry a tick later, no ENABLE",
          "[drive_golden]") {
  expect_golden("GLD-118");
}

TEST_CASE("GLD-119 boot with the MCB ENABLED, IDLE before the next tick: the boot DISABLE only",
          "[drive_golden]") {
  expect_golden("GLD-119");
}

TEST_CASE("GLD-120 the unlock hold before POST pass is refused with REFUSED_POST",
          "[drive_golden]") {
  expect_golden("GLD-120");
}

TEST_CASE("GLD-121 the MCB ENABLED before POST pass enters Drive; a profile tap waits for PASS",
          "[drive_golden]") {
  expect_golden("GLD-121");
}

TEST_CASE("GLD-122 with POST failed, the push and the DRIVE row are refused with REFUSED_POST",
          "[drive_golden]") {
  expect_golden("GLD-122");
}

TEST_CASE("GLD-123 the boot DISABLE ignores the Boot screen; no entry behind it",
          "[drive_golden]") {
  expect_golden("GLD-123");
}

TEST_CASE("GLD-124 the unlock hold before the first tick: no boot DISABLE", "[drive_golden]") {
  expect_golden("GLD-124");
}

TEST_CASE("GLD-125 DrivePort's sample reads the POST gate live: PENDING not passed, PASS passed",
          "[drive_golden]") {
  golden::reset_world();
  golden::world().post_gate = static_cast<std::uint8_t>(hmi::stick::PostGate::PENDING);
  const bool pending = golden::port_sample_post_ok();
  golden::world().post_gate = static_cast<std::uint8_t>(hmi::stick::PostGate::PASS);
  const bool pass = golden::port_sample_post_ok();
  TEST_ASSERT_FALSE(pending);
  TEST_ASSERT_TRUE(pass);
}
