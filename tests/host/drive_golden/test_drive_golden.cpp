// L1 host app: the drive goldens (app-main-shrink.md S1 and V5; hazard-fixes.md Phase A).
//
// Two frozen logs pin what the drive code does today, so that moving it out of the main.cpp
// unit (components/drive_adapter) is proven to change nothing:
//   golden_port.txt  golden 1, at the DrivePort: each port method the drive code calls, in
//                    order, with where it samples the screen, link, MIB and menu and where
//                    it reads the clock; the world is stateful (world.hpp), so a sample
//                    taken before or after a screen change reads differently.
//   golden_raw.txt   golden 2, at the stub boundary: every lv_*, rtps_comms_publish_*,
//                    esp_timer and main-unit call the drive code makes, with its arguments.
// Both were recorded once from main/frag_drive.inc as it was before the move (6c431e4, the
// owner-reviewed table 9b8574f) and are never regenerated from the code under test: a
// mismatch is a behaviour change, never a reason to re-record (CORE never-list).
// On a mismatch the actual logs are written to $GOLDEN_ACTUAL_DIR for a diff.

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "probe.hpp"
#include "scripts.hpp"
#include "test_case.hpp"
#include "world.hpp"

namespace golden {
Target main_unit_target();
}

namespace {

std::string env_or_empty(const char *name) {
  const char *v = std::getenv(name);
  return v == nullptr ? std::string{} : std::string{v};
}

std::vector<std::string> read_lines(const std::string &path) {
  std::vector<std::string> lines;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    lines.push_back(line);
  }
  return lines;
}

void write_lines(const std::string &path, const std::vector<std::string> &lines) {
  std::ofstream out(path);
  for (const std::string &l : lines) {
    out << l << '\n';
  }
}

// The actual log against the frozen one: same lines, same order. Returns what to assert on
// (Unity's asserts longjmp, so nothing that owns memory may be alive when they run).
enum class Verdict { SAME, DIFFERENT, NO_GOLDEN_DIR, NO_GOLDEN };

Verdict compare_golden(const std::vector<std::string> &actual, const char *golden_name,
                       const char *actual_name) {
  const std::string dir = env_or_empty("GOLDEN_DIR");
  if (dir.empty()) {
    return Verdict::NO_GOLDEN_DIR;
  }
  const std::vector<std::string> expected = read_lines(dir + "/" + golden_name);
  std::size_t first_diff = 0;
  while (first_diff < expected.size() && first_diff < actual.size() &&
         expected[first_diff] == actual[first_diff]) {
    ++first_diff;
  }
  std::printf("%s: %zu lines expected, %zu actual\n", golden_name, expected.size(), actual.size());
  if (!expected.empty() && first_diff == expected.size() && first_diff == actual.size()) {
    return Verdict::SAME;
  }
  const std::string out_dir = env_or_empty("GOLDEN_ACTUAL_DIR");
  if (!out_dir.empty()) {
    write_lines(out_dir + "/" + actual_name, actual);
  }
  if (expected.empty()) {
    return Verdict::NO_GOLDEN;
  }
  std::printf("%s: first difference at line %zu\n  expected: %s\n  actual:   %s\n", golden_name,
              first_diff + 1, first_diff < expected.size() ? expected[first_diff].c_str() : "<end>",
              first_diff < actual.size() ? actual[first_diff].c_str() : "<end>");
  return Verdict::DIFFERENT;
}

void expect_golden(Verdict v) {
  TEST_ASSERT_FALSE_MESSAGE(v == Verdict::NO_GOLDEN_DIR, "GOLDEN_DIR is not set (the Makefile)");
  TEST_ASSERT_FALSE_MESSAGE(v == Verdict::NO_GOLDEN, "the golden file is missing or empty");
  TEST_ASSERT_TRUE_MESSAGE(v == Verdict::SAME,
                           "the drive code's calls differ from the frozen golden");
}

} // namespace

TEST_CASE("GLD-001 the main unit's drive code calls the port as the frozen port golden says",
          "[drive_golden]") {
  golden::run_all(golden::main_unit_target());
  expect_golden(compare_golden(golden::port_log(), "golden_port.txt", "actual_port_main_unit.txt"));
}

TEST_CASE("GLD-002 the main unit's drive code makes the lv_ and rtps calls of the frozen "
          "boundary golden",
          "[drive_golden]") {
  golden::run_all(golden::main_unit_target());
  expect_golden(compare_golden(golden::raw_log(), "golden_raw.txt", "actual_raw_main_unit.txt"));
}

TEST_CASE("GLD-003 the golden scenarios take every row of the drive table", "[drive_golden]") {
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
