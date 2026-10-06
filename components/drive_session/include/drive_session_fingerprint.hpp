// SPDX-License-Identifier: MIT
//
// drive_session_fingerprint.hpp: one number for the drive session table's data.
//
// table_fingerprint() is a 64-bit FNV-1a hash, at compile time, over every field of every
// row of the spec in drive_session_table.hpp: TRANSITIONS, HOLD_TRANSITIONS,
// PHASE_INVARIANTS, ACTION_EFFECTS, INPUT_PRECONDITIONS, TICK_SEQUENCE, HOLD_POLL_SEQUENCE,
// UNLOCK_APPLIES and EXIT_APPLIES. Enumerators and masks go in as their numeric values, each
// action list as its length and its actions (the NONE padding is left out), and each code
// citation as its length and its characters.
//
// The static_assert below pins the value of the reviewed table. A change that moves
// declarations around (a refactor) keeps the value, which is what proves that no row moved.
// A change to any row changes it: that is a table change, which needs the owner's approval
// (CS-SAF-05), and the new value is written here in the same commit as the row change.
//
// Pure C++23: no LVGL, no ESP-IDF, no FreeRTOS.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "drive_session_table.hpp"

namespace hmi::drive_session {

namespace fingerprint_detail {

inline constexpr std::uint64_t FNV_OFFSET = 0xCBF29CE484222325ULL;
inline constexpr std::uint64_t FNV_PRIME = 0x100000001B3ULL;

// 64-bit FNV-1a over a stream of little-endian 32-bit words and strings.
class Fnv1a {
public:
  constexpr void byte(std::uint8_t b) noexcept { hash_ = (hash_ ^ b) * FNV_PRIME; }
  constexpr void word(std::uint32_t v) noexcept {
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
      byte(static_cast<std::uint8_t>((v >> shift) & 0xFFU));
    }
  }
  template <typename T> constexpr void value(T v) noexcept { word(static_cast<std::uint32_t>(v)); }
  constexpr void text(std::string_view s) noexcept {
    value(s.size());
    for (const char c : s) {
      byte(static_cast<std::uint8_t>(c));
    }
  }
  template <typename A, typename E> constexpr void list(const A &actions, E none) noexcept {
    std::uint32_t n = 0;
    for (const E a : actions) {
      n += a == none ? 0U : 1U;
    }
    word(n);
    for (const E a : actions) {
      if (a != none) {
        value(a);
      }
    }
  }
  constexpr void guard(const GuardExpr &g) noexcept {
    word(g.need_true);
    word(g.need_false);
  }
  [[nodiscard]] constexpr std::uint64_t hash() const noexcept { return hash_; }

private:
  std::uint64_t hash_ = FNV_OFFSET;
};

constexpr void add_transitions(Fnv1a &f) noexcept {
  f.value(TRANSITIONS.size());
  for (const Transition &t : TRANSITIONS) {
    f.value(t.from);
    f.value(t.input);
    f.guard(t.guard);
    f.value(t.to);
    f.list(t.actions, Action::NONE);
    f.text(t.code);
  }
}

constexpr void add_hold_transitions(Fnv1a &f) noexcept {
  f.value(HOLD_TRANSITIONS.size());
  for (const HoldTransition &t : HOLD_TRANSITIONS) {
    f.value(t.from);
    f.value(t.input);
    f.value(t.guard.need_true);
    f.value(t.guard.need_false);
    f.value(t.to);
    f.list(t.actions, HoldAction::NONE);
    f.text(t.code);
  }
}

constexpr void add_invariants_and_effects(Fnv1a &f) noexcept {
  f.value(PHASE_INVARIANTS.size());
  for (const PhaseInvariant &p : PHASE_INVARIANTS) {
    f.value(p.phase);
    f.word(p.must_true);
    f.word(p.must_false);
  }
  f.value(ACTION_EFFECTS.size());
  for (const ActionEffect &e : ACTION_EFFECTS) {
    f.value(e.action);
    f.word(e.sets);
    f.word(e.clears);
  }
  f.value(INPUT_PRECONDITIONS.size());
  for (const InputPrecondition &p : INPUT_PRECONDITIONS) {
    f.value(p.input);
    f.value(p.phases);
    f.guard(p.guard);
  }
}

constexpr void add_sequences_and_applies(Fnv1a &f) noexcept {
  f.value(TICK_SEQUENCE.size());
  for (const Input in : TICK_SEQUENCE) {
    f.value(in);
  }
  f.value(HOLD_POLL_SEQUENCE.size());
  for (const HoldPollStep s : HOLD_POLL_SEQUENCE) {
    f.value(s);
  }
  for (const HoldApplies &a : {UNLOCK_APPLIES, EXIT_APPLIES}) {
    f.text(a.gesture);
    f.value(a.phases);
    f.guard(a.guard);
    f.text(a.code);
  }
}

} // namespace fingerprint_detail

/// @brief The fingerprint of the table's data (see the file comment for what it covers).
[[nodiscard]] constexpr std::uint64_t table_fingerprint() noexcept {
  fingerprint_detail::Fnv1a f;
  fingerprint_detail::add_transitions(f);
  fingerprint_detail::add_hold_transitions(f);
  fingerprint_detail::add_invariants_and_effects(f);
  fingerprint_detail::add_sequences_and_applies(f);
  return f.hash();
}

/// @brief The reviewed table's fingerprint. Changes only together with a reviewed row change.
inline constexpr std::uint64_t TABLE_FINGERPRINT = 0xD8AAB0E61BE44A91ULL;

static_assert(table_fingerprint() == TABLE_FINGERPRINT,
              "the drive session table's data changed: a row change needs the owner's approval "
              "(CS-SAF-05) and the new fingerprint in the same commit");

} // namespace hmi::drive_session
