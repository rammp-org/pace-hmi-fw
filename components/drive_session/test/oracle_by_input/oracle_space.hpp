#pragma once
// The by-input oracle's state space (TS-UNIT-08, TS-UNIT-09; docs/plans/hazard-fixes.md B3).
//
// The full-product oracle (test/oracle_full, DRV-001..012, on demand) drives every (phase,
// input, hidden mask, Env): 6 x 11 x 32 x 640 = 1,351,680 steps on the AS-IS table,
// 6 x 13 x 64 x 7,680 = 38,338,560 with C1, and 6 x 14 x 128 x 15,360 = 165,150,720 with C3.
// Each new hidden bit doubles that. This oracle
// splits the guard bits, per input, into the ones the table reads for that input (any row of
// the input, and its precondition) and the rest:
//
//   - every value of every dimension that holds a read bit is enumerated, in every phase: the
//     table's verdict (which row, or none) depends only on these, so every (phase, input,
//     read-bit) class of the full product is visited (`make equivalence` checks that);
//   - the dimensions the input does not read are enumerated in full when that is at most
//     kFullLimit points (or no more than the sample below); otherwise they are sampled: every
//     point within distance kMaxDistance (3) of two corners (every dimension at its value 0:
//     all clear; every dimension at its `corner`: all set), plus kRandomPerClass seeded random
//     points per class. The table does not read these bits for this input, so the sample only
//     tests that the code does not either. A dependence on a conjunction of at most three of
//     them set (any number clear), or at most three clear (any number set), is always hit;
//     a wider one only by chance, through the random points.
//
// A dimension is one field of the state the oracle varies: (link, MIB state), screen, menu,
// each *_elapsed flag, calibrating, the re-send, and each hidden bit. Together they partition the
// guard bits (static_assert below), so a new guard bit that no dimension holds does not compile; a
// new hidden bit gets its own dimension automatically.

#include "drive_session.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <utility>
#include <vector>

namespace hmi::drive_session {
// Test-only access to the session's state (declared a friend in drive_session.hpp; the
// full-product oracle defines the same struct in its own program).
struct DriveSessionTestPeer {
  static void set(DriveSession &s, Phase p, GuardMask hidden) {
    s.phase_ = p;
    s.hidden_ = hidden;
  }
};
} // namespace hmi::drive_session

namespace oracle_space {

namespace ds = hmi::drive_session;

inline constexpr std::array MIBS{ds::MibState::INITIALIZING, ds::MibState::IDLE,
                                 ds::MibState::ENABLED, ds::MibState::OTHER};
inline constexpr std::array SCREENS{ds::Screen::BOOT, ds::Screen::LOCKED, ds::Screen::DRIVE,
                                    ds::Screen::SEAT, ds::Screen::OTHER};

// ---- Dimensions -----------------------------------------------------------------------------

inline constexpr std::size_t kEnvDims = 10;
inline constexpr std::size_t kHiddenDims = std::popcount(ds::kHiddenGuards);
inline constexpr std::size_t kDims = kEnvDims + kHiddenDims;
// More than 32 dimensions does not fit DimSet: a compile error, not a silent cut.
static_assert(kDims <= 32);
using DimSet = std::uint32_t;
using Values = std::array<std::uint8_t, kDims>; // one value index per dimension

// The k-th hidden guard bit, in bit order.
constexpr ds::GuardMask hidden_bit(std::size_t k) noexcept {
  std::size_t seen = 0;
  for (unsigned b = 0; b < ds::kGuardCount; ++b) {
    const ds::GuardMask v = ds::GuardMask{1} << b;
    if ((v & ds::kHiddenGuards) != 0) {
      if (seen == k) {
        return v;
      }
      ++seen;
    }
  }
  return 0;
}

struct Dim {
  std::uint8_t count;  // how many values
  ds::GuardMask bits;  // the guard bits its value decides
  std::uint8_t corner; // the value with the most of its bits set (ties: the highest)
};

constexpr std::array<Dim, kDims> make_dims() noexcept {
  std::array<Dim, kDims> d{};
  // (link, MIB): value v is link = v % 2, MIBS[v / 2]; v = 5 is link up, ENABLED.
  d[0] = Dim{static_cast<std::uint8_t>(2 * MIBS.size()),
             ds::mask({ds::Guard::LINK_CONNECTED, ds::Guard::DRIVING_OK, ds::Guard::MCB_READY}), 5};
  d[1] = Dim{static_cast<std::uint8_t>(SCREENS.size()),
             ds::mask({ds::Guard::ON_LOCKED_SCREEN, ds::Guard::ON_DRIVE_SCREEN,
                       ds::Guard::ON_SEAT_SCREEN, ds::Guard::ON_BOOT_SCREEN}),
             3};
  d[2] = Dim{2, ds::bit(ds::Guard::MENU_OPEN), 1};
  d[3] = Dim{2, ds::bit(ds::Guard::EXIT_ELAPSED), 1};
  d[4] = Dim{2, ds::bit(ds::Guard::WARN_ELAPSED), 1};
  d[5] = Dim{2, ds::bit(ds::Guard::GIVEUP_ELAPSED), 1};
  // C1: a calibration running, the stop window passed, and the re-send (NOT_DUE, FAST, SLOW:
  // value 2 sets both bits). ON_BOOT_SCREEN is the screen dimension's (Screen::BOOT).
  d[6] = Dim{2, ds::bit(ds::Guard::CALIBRATING), 1};
  d[7] = Dim{2, ds::bit(ds::Guard::STOP_FAULT_ELAPSED), 1};
  d[8] = Dim{3, ds::mask({ds::Guard::RESEND_FAST_DUE, ds::Guard::RESEND_SLOW_DUE}), 2};
  // C3: the POST gate PASS at the sample.
  d[9] = Dim{2, ds::bit(ds::Guard::POST_OK), 1};
  for (std::size_t k = 0; k < kHiddenDims; ++k) {
    d[kEnvDims + k] = Dim{2, hidden_bit(k), 1};
  }
  return d;
}
inline constexpr std::array<Dim, kDims> DIMS = make_dims();

constexpr bool dims_partition_the_guards() noexcept {
  ds::GuardMask seen = 0;
  for (const Dim &d : DIMS) {
    if ((seen & d.bits) != 0 || d.bits == 0 || d.count < 2 || d.corner >= d.count) {
      return false;
    }
    seen |= d.bits;
  }
  return seen == (ds::kEnvGuards | ds::kHiddenGuards);
}
static_assert(dims_partition_the_guards(),
              "every guard bit belongs to exactly one oracle dimension: a new env guard needs "
              "its dimension here (a new hidden bit gets one by itself)");

// The (Env, hidden mask) a point stands for.
struct Point {
  ds::Env env;
  ds::GuardMask hidden;
};

inline constexpr std::array RESENDS{ds::Resend::NOT_DUE, ds::Resend::FAST, ds::Resend::SLOW};

inline Point point_of(const Values &v) noexcept {
  const ds::Env env{.link_connected = (v[0] % 2U) != 0,
                    .mib = MIBS[v[0] / 2U],
                    .screen = SCREENS[v[1]],
                    .menu_open = v[2] != 0,
                    .exit_elapsed = v[3] != 0,
                    .warn_elapsed = v[4] != 0,
                    .giveup_elapsed = v[5] != 0,
                    .calibrating = v[6] != 0,
                    .stop_fault_elapsed = v[7] != 0,
                    .resend = RESENDS[v[8]],
                    .post_ok = v[9] != 0};
  ds::GuardMask hidden = 0;
  for (std::size_t k = 0; k < kHiddenDims; ++k) {
    hidden |= v[kEnvDims + k] != 0 ? hidden_bit(k) : 0;
  }
  return Point{env, hidden};
}

inline ds::GuardMask guards_of(const Point &p) noexcept { return ds::env_guards(p.env) | p.hidden; }

// ---- What an input reads -----------------------------------------------------------------

// The guard bits any row of `in` reads, and the bits of its precondition.
constexpr ds::GuardMask read_bits(ds::Input in) noexcept {
  const auto &pre = ds::INPUT_PRECONDITIONS[static_cast<std::size_t>(in)];
  return std::accumulate(ds::TRANSITIONS.begin(), ds::TRANSITIONS.end(),
                         ds::GuardMask{pre.guard.need_true | pre.guard.need_false},
                         [in](ds::GuardMask m, const ds::Transition &t) {
                           return m | (t.input == in ? (t.guard.need_true | t.guard.need_false)
                                                     : ds::GuardMask{0});
                         });
}

// The dimensions that hold a bit `in` reads.
constexpr DimSet read_dims(ds::Input in) noexcept {
  DimSet s = 0;
  for (std::size_t d = 0; d < kDims; ++d) {
    s |= (DIMS[d].bits & read_bits(in)) != 0 ? (DimSet{1} << d) : 0;
  }
  return s;
}

constexpr bool has(DimSet s, std::size_t d) noexcept { return ((s >> d) & 1U) != 0; }

// The dimensions an input does not read are enumerated in full up to kFullLimit points, or
// while that is no more than the corner points; otherwise every point within kMaxDistance of
// either corner, plus kRandomPerClass seeded random points per class.
inline constexpr std::size_t kFullLimit = 256;
inline constexpr std::size_t kMaxDistance = 3;
inline constexpr std::size_t kRandomPerClass = 16;

inline std::uint64_t splitmix64(std::uint64_t &state) noexcept {
  state += 0x9E3779B97F4A7C15ULL;
  std::uint64_t z = state;
  z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31U);
}

inline std::size_t product_size(DimSet free) noexcept {
  std::size_t n = 1;
  for (std::size_t d = 0; d < kDims; ++d) {
    n *= has(free, d) ? DIMS[d].count : 1U;
  }
  return n;
}

// Every Values over the dimensions in `free` (the others 0), in odometer order.
inline std::vector<Values> product(DimSet free) {
  const std::size_t n = product_size(free);
  std::vector<Values> out;
  out.reserve(n);
  Values v{};
  for (std::size_t i = 0; i < n; ++i) {
    out.push_back(v);
    for (std::size_t d = 0; d < kDims; ++d) {
      if (has(free, d)) {
        if (++v[d] < DIMS[d].count) {
          break;
        }
        v[d] = 0;
      }
    }
  }
  return out;
}

// Every point that differs from `base` in exactly k of the dimensions in `free` (k <= 3).
inline void at_distance(const Values &base, DimSet free, std::size_t k, std::vector<Values> &out) {
  std::array<std::size_t, kDims> dims{};
  std::size_t nd = 0;
  for (std::size_t d = 0; d < kDims; ++d) {
    if (has(free, d)) {
      dims[nd++] = d;
    }
  }
  if (k == 0 || k > kMaxDistance || k > nd) {
    if (k == 0) {
      out.push_back(base);
    }
    return;
  }
  std::array<std::size_t, kMaxDistance> pick{0, 1, 2}; // positions in dims, increasing
  const std::size_t combos = product_size(free);       // an upper bound on the combinations
  for (std::size_t c = 0; c < combos; ++c) {
    // Each picked dimension takes each of its values other than the base value.
    std::array<std::uint8_t, kMaxDistance> off{};
    const std::size_t values = std::accumulate(
        pick.begin(), pick.begin() + static_cast<std::ptrdiff_t>(k), std::size_t{1},
        [&dims](std::size_t n, std::size_t j) { return n * (DIMS[dims[j]].count - 1U); });
    for (std::size_t i = 0; i < values; ++i) {
      Values v = base;
      for (std::size_t j = 0; j < k; ++j) {
        const std::size_t d = dims[pick[j]];
        v[d] = static_cast<std::uint8_t>(off[j] < base[d] ? off[j] : off[j] + 1U);
      }
      out.push_back(v);
      for (std::size_t j = 0; j < k; ++j) {
        if (++off[j] < DIMS[dims[pick[j]]].count - 1U) {
          break;
        }
        off[j] = 0;
      }
    }
    // The next k-combination of positions, or done.
    std::size_t j = k;
    while (j > 0 && pick[j - 1] == nd - k + (j - 1)) {
      --j;
    }
    if (j == 0) {
      return;
    }
    ++pick[j - 1];
    for (std::size_t m = j; m < k; ++m) {
      pick[m] = pick[m - 1] + 1;
    }
  }
}

inline std::uint64_t index_of(const Values &v) noexcept {
  std::uint64_t i = 0;
  for (std::size_t d = kDims; d-- > 0;) {
    i = i * DIMS[d].count + v[d];
  }
  return i;
}

// Both corners' kMaxDistance neighbourhoods over `unread` (deduplicated); read dims are 0.
inline std::vector<Values> corner_points(DimSet unread) {
  Values high{};
  for (std::size_t d = 0; d < kDims; ++d) {
    high[d] = has(unread, d) ? DIMS[d].corner : 0;
  }
  std::vector<Values> out;
  for (std::size_t k = 0; k <= kMaxDistance; ++k) {
    at_distance(Values{}, unread, k, out);
    at_distance(high, unread, k, out);
  }
  std::ranges::sort(out, {}, index_of);
  const auto dup = std::ranges::unique(out, {}, index_of);
  out.erase(dup.begin(), dup.end());
  return out;
}

// ---- One step against the table ----------------------------------------------------------

struct Verdict {
  bool agrees;
  std::size_t row; // TRANSITIONS index, or TRANSITIONS.size() for none
};

// The session in (p, hidden) given `in` and `env`, against find_row on the same guards.
inline Verdict judge(ds::Phase p, ds::Input in, const Point &pt) {
  ds::DriveSession s;
  ds::DriveSessionTestPeer::set(s, p, pt.hidden);
  ds::Actions got{};
  const bool ok = s.step(in, pt.env, got);
  const std::size_t row = ds::find_row(p, in, guards_of(pt));
  if (row >= ds::TRANSITIONS.size()) {
    return {ok && s.phase() == p && s.hidden() == pt.hidden && got == ds::Actions{}, row};
  }
  const ds::Transition &t = ds::TRANSITIONS[row];
  return {ok && s.phase() == t.to && s.hidden() == ds::apply(t.actions, pt.hidden) &&
              got == t.actions,
          row};
}

// ---- The by-input enumeration ------------------------------------------------------------

struct Plan {
  DimSet read;                // enumerated in full
  std::size_t classes;        // value combinations of the read dimensions
  std::vector<Values> unread; // the fixed points over the other dimensions
  bool unread_full;           // `unread` is their full product (no random points then)
};

inline constexpr DimSet kAllDims = (kDims == 32) ? ~DimSet{0} : ((DimSet{1} << kDims) - 1U);

inline Plan plan_for(ds::Input in) {
  const DimSet read = read_dims(in);
  const DimSet unread = kAllDims & ~read;
  if (product_size(unread) <= kFullLimit) {
    return Plan{read, product_size(read), product(unread), true};
  }
  std::vector<Values> corners = corner_points(unread);
  if (corners.size() >= product_size(unread)) {
    return Plan{read, product_size(read), product(unread), true};
  }
  return Plan{read, product_size(read), std::move(corners), false};
}

// Calls f(point) for every point of (p, in): each class of the read dimensions with each
// fixed unread point, then (when the unread ones are sampled) kRandomPerClass seeded random
// unread points for that class. Returns the number of points.
template <typename F>
std::size_t for_each_point(ds::Phase p, ds::Input in, const Plan &plan, F &&f) {
  std::size_t n = 0;
  const std::vector<Values> classes = product(plan.read);
  for (std::size_t c = 0; c < classes.size(); ++c) {
    for (const Values &u : plan.unread) {
      Values v = classes[c];
      for (std::size_t d = 0; d < kDims; ++d) {
        v[d] = has(plan.read, d) ? v[d] : u[d];
      }
      f(v);
      ++n;
    }
    if (plan.unread_full) {
      continue;
    }
    std::uint64_t seed = 0xB3B3'0000'0000ULL ^
                         (std::uint64_t{static_cast<std::uint8_t>(p)} << 40U) ^
                         (std::uint64_t{static_cast<std::uint8_t>(in)} << 32U) ^ c;
    for (std::size_t r = 0; r < kRandomPerClass; ++r) {
      Values v = classes[c];
      for (std::size_t d = 0; d < kDims; ++d) {
        if (!has(plan.read, d)) {
          v[d] = static_cast<std::uint8_t>(splitmix64(seed) % DIMS[d].count);
        }
      }
      f(v);
      ++n;
    }
  }
  return n;
}

} // namespace oracle_space
