// L1 host app for drive_session: the transition-table oracle by input (TS-UNIT-08, TS-UNIT-09;
// docs/plans/hazard-fixes.md B3).
//
// The same verdict as the full-product oracle (test/oracle, DRV-001..012): where find_row()
// finds a row, the phase must become the row's `to`, the hidden mask apply(row.actions, hidden)
// and the actions the row's, in order; where it finds none, nothing may change and nothing may
// be returned. What differs is the set of states: per input, every value of every dimension the
// table reads for that input, in every phase, crossed with a fixed set of the other dimensions
// (oracle_space.hpp says which). The full-product oracle stays, side by side, until the owner
// retires it; `make equivalence` shows that this one visits every (phase, input, read-bit)
// class the full product does, with the same row, and `make mutants` that both reject the same
// table and code mutations.

#include "oracle_space.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <set>
#include <vector>

#include "test_case.hpp"

namespace {

namespace ds = hmi::drive_session;
namespace os = oracle_space;
using ds::GuardMask;
using ds::Input;
using ds::Phase;

bool in_contract(Phase p, GuardMask hidden, Input in, GuardMask guards) {
  const auto &pre = ds::INPUT_PRECONDITIONS[static_cast<std::size_t>(in)];
  return ds::hidden_valid(p, hidden) && ds::in(pre.phases, p) && ds::matches(pre.guard, guards);
}

// Unity's failed assertion longjmps out of the case, past destructors: every case does its
// work (which allocates) in a helper that returns plain numbers, then asserts.
struct Tally {
  unsigned long expected_steps = 0;
  unsigned long steps = 0;
  unsigned long contract = 0;
  unsigned long matched = 0;
  unsigned long mismatches = 0;
};

std::size_t points_per_class(const os::Plan &plan) {
  return plan.unread.size() + (plan.unread_full ? 0U : os::kRandomPerClass);
}

Tally run_by_input(Input in) {
  const os::Plan plan = os::plan_for(in);
  Tally t;
  t.expected_steps = ds::kPhaseCount * plan.classes * points_per_class(plan);
  for (std::size_t pi = 0; pi < ds::kPhaseCount; ++pi) {
    const auto p = static_cast<Phase>(pi);
    t.steps += os::for_each_point(p, in, plan, [&](const os::Values &v) {
      const os::Point pt = os::point_of(v);
      const os::Verdict r = os::judge(p, in, pt);
      t.matched += r.row < ds::TRANSITIONS.size() ? 1UL : 0UL;
      t.contract += in_contract(p, pt.hidden, in, os::guards_of(pt)) ? 1UL : 0UL;
      if (!r.agrees && t.mismatches++ < 5) {
        std::printf("MISMATCH input %u phase %zu hidden 0x%x env guards 0x%x\n",
                    static_cast<unsigned>(in), pi, static_cast<unsigned>(pt.hidden),
                    static_cast<unsigned>(ds::env_guards(pt.env)));
      }
    });
  }
  std::printf("ORACLE-BY-INPUT input %u: reads 0x%04x, %d of %zu dimensions in full (%zu "
              "classes per phase) x %zu other points (%s) = %lu steps (%lu in the contract), "
              "%lu matched a row\n",
              static_cast<unsigned>(in), static_cast<unsigned>(os::read_bits(in)),
              std::popcount(plan.read), os::kDims, plan.classes, points_per_class(plan),
              plan.unread_full ? "all" : "corners + random", t.steps, t.contract, t.matched);
  return t;
}

void expect_by_input(Input in) {
  const Tally t = run_by_input(in);
  TEST_ASSERT_EQUAL_UINT64(t.expected_steps, t.steps);
  TEST_ASSERT_TRUE(t.contract > 0);
  TEST_ASSERT_TRUE(t.matched > 0);
  TEST_ASSERT_EQUAL_UINT64(0, t.mismatches);
}

// The class of a point: its values on the read dimensions, as an index below plan.classes.
std::size_t class_of(const os::Values &v, os::DimSet read) {
  std::size_t i = 0;
  for (std::size_t d = os::kDims; d-- > 0;) {
    if (os::has(read, d)) {
      i = i * os::DIMS[d].count + v[d];
    }
  }
  return i;
}

// e_k of the dimensions' (count - 1): how many points lie at distance exactly k of a corner.
std::array<std::size_t, os::kMaxDistance + 1> at_distance_counts() {
  std::array<std::size_t, os::kMaxDistance + 1> e{1};
  for (const os::Dim &d : os::DIMS) {
    for (std::size_t k = os::kMaxDistance; k > 0; --k) {
      e[k] += e[k - 1] * (d.count - 1U);
    }
  }
  return e;
}

} // namespace

// ---- The oracle, one case per input (TS-UNIT-08) ------------------------------------------

TEST_CASE("DRV-101 TICK_FOLLOW, by input: the table's row in every phase and class, or no change",
          "[drive][safety][oracle]") {
  expect_by_input(Input::TICK_FOLLOW);
}

TEST_CASE("DRV-102 TICK_EXIT_DUE, by input: the table's row or no change",
          "[drive][safety][oracle]") {
  expect_by_input(Input::TICK_EXIT_DUE);
}

TEST_CASE("DRV-103 TICK_WARN_DUE, by input: the table's row or no change",
          "[drive][safety][oracle]") {
  expect_by_input(Input::TICK_WARN_DUE);
}

TEST_CASE("DRV-104 TICK_GIVEUP_DUE, by input: the table's row or no change",
          "[drive][safety][oracle]") {
  expect_by_input(Input::TICK_GIVEUP_DUE);
}

TEST_CASE("DRV-105 UNLOCK_HOLD_DONE, by input: the table's row or no change",
          "[drive][safety][oracle]") {
  expect_by_input(Input::UNLOCK_HOLD_DONE);
}

TEST_CASE("DRV-106 EXIT_HOLD_DONE, by input: the table's row or no change",
          "[drive][safety][oracle]") {
  expect_by_input(Input::EXIT_HOLD_DONE);
}

TEST_CASE("DRV-107 MENU_KEY_DRIVE, by input: the table's row or no change",
          "[drive][safety][oracle]") {
  expect_by_input(Input::MENU_KEY_DRIVE);
}

TEST_CASE("DRV-108 PROFILE_CLICK, by input: the table's row or no change",
          "[drive][safety][oracle]") {
  expect_by_input(Input::PROFILE_CLICK);
}

TEST_CASE("DRV-109 UNLOCK_TIMER, by input: the table's row or no change",
          "[drive][safety][oracle]") {
  expect_by_input(Input::UNLOCK_TIMER);
}

TEST_CASE("DRV-110 ENTRY_PUSH, by input: the table's row or no change", "[drive][safety][oracle]") {
  expect_by_input(Input::ENTRY_PUSH);
}

TEST_CASE("DRV-111 MENU_ROW_DRIVE, by input: the table's row or no change",
          "[drive][safety][oracle]") {
  expect_by_input(Input::MENU_ROW_DRIVE);
}

// ---- What the by-input oracle visits -----------------------------------------------------

namespace {

struct Visits {
  unsigned long steps = 0;
  unsigned long missing_classes = 0;
  std::array<bool, ds::TRANSITIONS.size()> reached{};
};

Visits visit_all() {
  Visits out;
  for (std::size_t i = 0; i < ds::kInputCount; ++i) {
    const auto in = static_cast<Input>(i);
    const os::Plan plan = os::plan_for(in);
    for (std::size_t pi = 0; pi < ds::kPhaseCount; ++pi) {
      const auto p = static_cast<Phase>(pi);
      std::vector<bool> visited(plan.classes, false);
      out.steps += os::for_each_point(p, in, plan, [&](const os::Values &v) {
        visited[class_of(v, plan.read)] = true;
        const os::Point pt = os::point_of(v);
        const GuardMask g = os::guards_of(pt);
        const std::size_t row = ds::find_row(p, in, g);
        if (row < out.reached.size() && in_contract(p, pt.hidden, in, g)) {
          out.reached[row] = true;
        }
      });
      out.missing_classes += static_cast<unsigned long>(std::ranges::count(visited, false));
    }
  }
  return out;
}

bool same_state(const os::Point &a, const os::Point &b) {
  return a.hidden == b.hidden && a.env.link_connected == b.env.link_connected &&
         a.env.mib == b.env.mib && a.env.screen == b.env.screen &&
         a.env.menu_open == b.env.menu_open && a.env.exit_elapsed == b.env.exit_elapsed &&
         a.env.warn_elapsed == b.env.warn_elapsed && a.env.giveup_elapsed == b.env.giveup_elapsed;
}

os::Values high_corner() {
  os::Values high{};
  for (std::size_t d = 0; d < os::kDims; ++d) {
    high[d] = os::DIMS[d].corner;
  }
  return high;
}

// How many of dimension d's checks fail, over the given contexts for the other dimensions.
unsigned long check_dimension(std::size_t d, const std::vector<os::Values> &contexts) {
  unsigned long bad = 0;
  std::array<GuardMask, 8> bits_of{};
  if (os::DIMS[d].count > bits_of.size()) {
    return 1;
  }
  for (std::size_t c = 0; c < contexts.size(); ++c) {
    for (std::uint8_t x = 0; x < os::DIMS[d].count; ++x) {
      os::Values v = contexts[c];
      v[d] = x;
      const os::Point pt = os::point_of(v);
      const GuardMask mine = os::guards_of(pt) & os::DIMS[d].bits;
      // Its own bits: the same for this value in every context.
      bits_of[x] = c == 0 ? mine : bits_of[x];
      bad += bits_of[x] == mine ? 0UL : 1UL;
      // The other dimensions' bits do not move with this one.
      os::Values base = contexts[c];
      base[d] = 0;
      bad += (os::guards_of(os::point_of(base)) & ~os::DIMS[d].bits) ==
                     (os::guards_of(pt) & ~os::DIMS[d].bits)
                 ? 0UL
                 : 1UL;
      // Each value is its own state: some Env field or hidden bit differs from every other.
      for (std::uint8_t y = 0; y < x; ++y) {
        os::Values w = v;
        w[d] = y;
        bad += same_state(os::point_of(w), pt) ? 1UL : 0UL;
      }
    }
  }
  // The corner of the dimension sets the most of its bits.
  for (std::uint8_t x = 0; x < os::DIMS[d].count; ++x) {
    bad += std::popcount(bits_of[x]) <= std::popcount(bits_of[os::DIMS[d].corner]) ? 0UL : 1UL;
  }
  return bad;
}

struct DimCheck {
  unsigned long bad_dimensions = 0;
  unsigned long far_from_corners = 0;
  std::size_t sample = 0;
  std::size_t distinct = 0;
};

DimCheck check_dimensions() {
  DimCheck out;
  const os::Values high = high_corner();
  // Contexts for the other dimensions: both corners and seeded random points (not the full
  // product, which is what this oracle avoids).
  std::vector<os::Values> contexts{os::Values{}, high};
  std::uint64_t seed = 0xD113;
  for (std::size_t r = 0; r < 64; ++r) {
    os::Values v{};
    for (std::size_t d = 0; d < os::kDims; ++d) {
      v[d] = static_cast<std::uint8_t>(os::splitmix64(seed) % os::DIMS[d].count);
    }
    contexts.push_back(v);
  }
  for (std::size_t d = 0; d < os::kDims; ++d) {
    const unsigned long bad = check_dimension(d, contexts);
    if (bad != 0) {
      std::printf("dimension %zu: %lu failed checks\n", d, bad);
    }
    out.bad_dimensions += bad != 0 ? 1UL : 0UL;
  }
  // The corner sample over every dimension.
  const std::vector<os::Values> sample = os::corner_points(os::kAllDims);
  std::set<std::uint64_t> distinct;
  for (const os::Values &v : sample) {
    std::size_t from_low = 0;
    std::size_t from_high = 0;
    for (std::size_t d = 0; d < os::kDims; ++d) {
      from_low += v[d] != 0 ? 1U : 0U;
      from_high += v[d] != high[d] ? 1U : 0U;
    }
    out.far_from_corners += std::min(from_low, from_high) <= os::kMaxDistance ? 0UL : 1UL;
    distinct.insert(os::index_of(v));
  }
  out.sample = sample.size();
  out.distinct = distinct.size();
  return out;
}

} // namespace

TEST_CASE("DRV-112 the by-input oracle visits every class of every input in every phase, and "
          "reaches every row from a state inside the contract",
          "[drive][safety][oracle]") {
  const Visits v = visit_all();
  const unsigned long full = ds::kInputCount * ds::kPhaseCount * os::product_size(os::kAllDims);
  std::printf("ORACLE-BY-INPUT total: %lu steps (the full product: %lu)\n", v.steps, full);
  TEST_ASSERT_EQUAL_UINT64(0, v.missing_classes);
  for (std::size_t r = 0; r < v.reached.size(); ++r) {
    if (!v.reached[r]) {
      std::printf("row %zu not reached\n", r + 1);
    }
    TEST_ASSERT_TRUE_MESSAGE(v.reached[r], "a row no contract state reaches");
  }
}

TEST_CASE("DRV-113 the oracle's dimensions are faithful: each decides only its own guard bits, "
          "each value is its own state, and the corner sample is what it says",
          "[drive][oracle]") {
  const DimCheck c = check_dimensions();
  TEST_ASSERT_EQUAL_UINT64(0, c.bad_dimensions);
  // The full-product oracle's space: 2 x 4 MIB states x 5 screens x 2^4 flags x 2^5 masks.
  TEST_ASSERT_EQUAL_UINT64(std::size_t{2} * os::MIBS.size() * os::SCREENS.size() * 16U *
                               (std::size_t{1} << os::kHiddenDims),
                           os::product_size(os::kAllDims));
  // The corner sample: distinct points, each within kMaxDistance of a corner, as many as the
  // two neighbourhoods hold (they cannot meet: the corners differ in every dimension, more
  // than 2 x kMaxDistance of them).
  static_assert(os::kDims > 2 * os::kMaxDistance);
  TEST_ASSERT_EQUAL_UINT64(0, c.far_from_corners);
  TEST_ASSERT_EQUAL_UINT64(c.sample, c.distinct);
  const auto e = at_distance_counts();
  const std::size_t one_corner = std::accumulate(e.begin(), e.end(), std::size_t{0});
  TEST_ASSERT_EQUAL_UINT64(2 * one_corner, c.sample);
}
