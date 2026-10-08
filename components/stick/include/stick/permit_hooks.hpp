#pragma once

/// @file permit_hooks.hpp
/// @brief The stick output permit's hooks as channels: the three lock-free values that carry
///        the POST gate, stick health and the hold reason between tasks (the types are
///        permit_types.hpp).
/// @details hazard-c1-spec.md §3 (the one output permit of C1, C3, C4 and C2) and
///          hazard-c3-spec.md §2.4. The permit runs on the ADC task every cycle; it is
///          `hmi::stick::OutputPermit`. These are only its inputs and its published reason, so
///          other components (the drive table's port, the POST runner, the TopBar, the bench
///          verbs) can read or write them without the pipeline.
///
///          | Channel        | Type        | Writer (task)                         | Readers | |
///          -------------- | ----------- | ------------------------------------- |
///          ---------------------------- | | `post_gate`    | PostGate    | the POST runner
///          (`lv_task`) only (C3) | `Read ADC`, `lv_task`        | | `stick_health` | StickHealth |
///          C2's monitor (`Read ADC`) only        | `Read ADC`, `lv_task`        | | `hold_reason`
///          | HoldReason  | `Read ADC` (the permit), every cycle  | `lv_task` (the Drive notice) |
///
///          Release on store, acquire on load (fw::AtomicValue): "every safety atomic is
///          release/acquire" (hazard-c3-spec.md §2.4). One writer each (CS-OWN); the bench
///          verbs (`PERMIT`, CONFIG_HMI_BENCH_STICK_INJECT only) go through those writers.
///
///          The motion guard (C4) has no channel here: it runs on the ADC task itself, so its
///          verdict reaches the permit as an input of the cycle (OutputPermit's
///          `motion_guard_ok`). Until C4 lands main passes "OK" (decision D2: the hook passes
///          until C4). Stick health stays NOT_MONITORED, which passes, until C2 (D2); only C2's
///          monitor writes OK, CHECK or FAULT.
///
///          No ESP-IDF, no LVGL, no allocation. Every enum is one byte, so each channel is a
///          lock-free std::atomic (static_assert in fw::AtomicValue).

#include "fw_core/atomic_value.hpp"
#include "stick/permit_types.hpp"

namespace hmi::stick {

/// @brief The three channels of the table above, in one place. One instance, constant
///        initialised (UiApp's member; no global constructor), alive for the firmware's life.
struct PermitHooks {
  /// The POST gate: NOT_RUN until the POST runner's first tick.
  hmi::fw::AtomicValue<PostGate> post_gate{{.initial = PostGate::NOT_RUN}};
  /// Stick health: NOT_MONITORED until C2's monitor writes it.
  hmi::fw::AtomicValue<StickHealth> stick_health{{.initial = StickHealth::NOT_MONITORED}};
  /// The permit's hold reason as of the ADC task's last valid cycle; GATE_SHUT before the
  /// first (the gate is shut at boot).
  hmi::fw::AtomicValue<HoldReason> hold_reason{{.initial = HoldReason::GATE_SHUT}};
};

} // namespace hmi::stick
