// `make equivalence` (docs/plans/hazard-fixes.md B3): the by-input oracle is not weaker than
// the full-product oracle on today's table.
//
// For every input it walks both state sets:
//   - the full product, enumerated exactly as test/oracle_full/test_drive_session_oracle_full.cpp
//   does
//     (env_at and hidden_at below are copied from it, lines 54-88);
//   - the by-input set (oracle_space.hpp).
// Each step is reduced to its class: (phase, the guard bits the input reads). It checks that:
//   1. every class the full product visits, the by-input set visits too;
//   2. within each set a class always gives the same row (find_row reads only those bits), and
//      both sets give each class the same row: same rows reached, same classes per row;
//   3. the session agrees with the table at every step of both sets (the same verdict);
//   4. the by-input set holds no state outside the full product.
// Prints one EQUIV line per input and a total; exit code 0 = all hold.

#include "oracle_space.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>

namespace {

namespace ds = hmi::drive_session;
namespace os = oracle_space;
using ds::Env;
using ds::GuardMask;
using ds::Input;
using ds::Phase;

// ---- Copied from test/oracle_full/test_drive_session_oracle_full.cpp (the full-product oracle)
// ----
constexpr unsigned HIDDEN_COMBOS = 1U << static_cast<unsigned>(std::popcount(ds::kHiddenGuards));
constexpr std::size_t ENV_COUNT =
    2 * os::MIBS.size() * os::SCREENS.size() * 2 * 8 * 4 * os::RESENDS.size() * 2;

Env env_at(std::size_t i) {
  const bool link = (i % 2) != 0;
  i /= 2;
  const ds::MibState mib = os::MIBS[i % os::MIBS.size()];
  i /= os::MIBS.size();
  const ds::Screen screen = os::SCREENS[i % os::SCREENS.size()];
  i /= os::SCREENS.size();
  const bool menu = (i % 2) != 0;
  i /= 2;
  const std::size_t deadlines = i % 8;
  i /= 8;
  const std::size_t stop = i % 4;
  i /= 4;
  return Env{.link_connected = link,
             .mib = mib,
             .screen = screen,
             .menu_open = menu,
             .exit_elapsed = (deadlines & 1U) != 0,
             .warn_elapsed = (deadlines & 2U) != 0,
             .giveup_elapsed = (deadlines & 4U) != 0,
             .calibrating = (stop & 1U) != 0,
             .stop_fault_elapsed = (stop & 2U) != 0,
             .resend = os::RESENDS[i % os::RESENDS.size()],
             .post_ok = (i / os::RESENDS.size()) % 2 != 0};
}

GuardMask hidden_at(unsigned i) {
  GuardMask m = 0;
  unsigned k = 0;
  for (unsigned b = 0; b < ds::kGuardCount; ++b) {
    const GuardMask v = GuardMask{1} << b;
    if ((v & ds::kHiddenGuards) != 0) {
      if (((i >> k) & 1U) != 0) {
        m |= v;
      }
      ++k;
    }
  }
  return m;
}
// ---- end of the copy ----

std::uint64_t state_key(const os::Point &pt) {
  return (std::uint64_t{pt.env.link_connected} << 0U) |
         (std::uint64_t{static_cast<std::uint8_t>(pt.env.mib)} << 1U) |
         (std::uint64_t{static_cast<std::uint8_t>(pt.env.screen)} << 4U) |
         (std::uint64_t{pt.env.menu_open} << 8U) | (std::uint64_t{pt.env.exit_elapsed} << 9U) |
         (std::uint64_t{pt.env.warn_elapsed} << 10U) |
         (std::uint64_t{pt.env.giveup_elapsed} << 11U) | (std::uint64_t{pt.hidden} << 16U);
}

struct Walk {
  std::map<std::uint64_t, std::size_t> row_of; // class -> row
  unsigned long steps = 0;
  unsigned long disagree = 0;     // the session differs from the table
  unsigned long inconsistent = 0; // one class, two rows
};

void record(Walk &w, Phase p, Input in, const os::Point &pt) {
  const os::Verdict v = os::judge(p, in, pt);
  const std::uint64_t cls = (std::uint64_t{static_cast<std::uint8_t>(p)} << 32U) |
                            (os::guards_of(pt) & os::read_bits(in));
  const auto [it, fresh] = w.row_of.emplace(cls, v.row);
  w.inconsistent += (!fresh && it->second != v.row) ? 1UL : 0UL;
  w.disagree += v.agrees ? 0UL : 1UL;
  ++w.steps;
}

std::set<std::size_t> rows_of(const Walk &w) {
  std::set<std::size_t> rows;
  for (const auto &[cls, row] : w.row_of) {
    if (row < ds::TRANSITIONS.size()) {
      rows.insert(row);
    }
  }
  return rows;
}

// Checks one input; returns the number of failed conditions.
unsigned check_input(Input in, unsigned long &full_steps, unsigned long &new_steps) {
  Walk full;
  Walk by;
  std::set<std::uint64_t> full_states;
  for (std::size_t pi = 0; pi < ds::kPhaseCount; ++pi) {
    const auto p = static_cast<Phase>(pi);
    for (unsigned hi = 0; hi < HIDDEN_COMBOS; ++hi) {
      for (std::size_t ei = 0; ei < ENV_COUNT; ++ei) {
        const os::Point pt{env_at(ei), hidden_at(hi)};
        record(full, p, in, pt);
        if (pi == 0) {
          full_states.insert(state_key(pt));
        }
      }
    }
  }
  const os::Plan plan = os::plan_for(in);
  unsigned long outside = 0;
  for (std::size_t pi = 0; pi < ds::kPhaseCount; ++pi) {
    const auto p = static_cast<Phase>(pi);
    (void)os::for_each_point(p, in, plan, [&](const os::Values &v) {
      const os::Point pt = os::point_of(v);
      outside += full_states.contains(state_key(pt)) ? 0UL : 1UL;
      record(by, p, in, pt);
    });
  }
  unsigned long missing = 0;
  unsigned long other_row = 0;
  for (const auto &[cls, row] : full.row_of) {
    const auto it = by.row_of.find(cls);
    missing += it == by.row_of.end() ? 1UL : 0UL;
    other_row += (it != by.row_of.end() && it->second != row) ? 1UL : 0UL;
  }
  const bool same_rows = rows_of(full) == rows_of(by);
  const std::array conditions{
      missing == 0,         other_row == 0,     full.inconsistent == 0,
      by.inconsistent == 0, full.disagree == 0, by.disagree == 0,
      outside == 0,         same_rows,          by.row_of.size() == full.row_of.size()};
  const auto failed = static_cast<unsigned>(std::ranges::count(conditions, false));
  std::printf("EQUIV input %2u: classes full %4zu by-input %4zu, missing %lu, other row %lu, "
              "rows %zu/%zu %s, one-class-two-rows %lu/%lu, session disagrees %lu/%lu, "
              "outside %lu, steps %lu -> %lu  %s\n",
              static_cast<unsigned>(in), full.row_of.size(), by.row_of.size(), missing, other_row,
              rows_of(full).size(), rows_of(by).size(), same_rows ? "same" : "DIFFERENT",
              full.inconsistent, by.inconsistent, full.disagree, by.disagree, outside, full.steps,
              by.steps, failed == 0 ? "ok" : "FAIL");
  full_steps += full.steps;
  new_steps += by.steps;
  return failed;
}

} // namespace

int main() {
  unsigned failed = 0;
  unsigned long full_steps = 0;
  unsigned long new_steps = 0;
  for (std::size_t i = 0; i < ds::kInputCount; ++i) {
    failed += check_input(static_cast<Input>(i), full_steps, new_steps);
  }
  std::printf("EQUIV total: %lu full-product steps, %lu by-input steps; %u failed conditions "
              "-> %s\n",
              full_steps, new_steps, failed, failed == 0 ? "PASS" : "FAIL");
  return failed == 0 ? 0 : 1;
}
