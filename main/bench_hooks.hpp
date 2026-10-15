#pragma once

/**
 * @file bench_hooks.hpp
 * @brief The hazard bench verbs' hooks (components/remote_ui/include/bench_verbs.hpp): what the
 *        remote UI's STATE, PERMIT, CAL UNSAVED, POST RERUN, STALL and SEATFAIL verbs read and
 *        request, wired to the UiApp, the drive adapter and the calibration. Bench inject builds
 *        only: main hands them over inside `if constexpr (BENCH_STICK_INJECT)`, so a release
 *        image links none of it (the linker drops the unreferenced code; build.yml checks the
 *        release ELF has no bench_verb symbol).
 */

#include "bench_verbs.hpp"
#include "hmi_rtps_spec.hpp"

/// @brief Every hook of the bench verbs, filled. Called once, by app_main, before the remote UI
/// server starts. Each hook runs on the remote UI's task: atomics only, no LVGL lock (the
/// calibration record's copy takes joystick_cal's short lock, as the ADC task's take_new does).
/// @return The hooks for remote_ui_attach_bench_verbs.
hmi::bench_verbs::Hooks bench_verb_hooks();

/// @brief The LinkPort's SeatCommand publish in a stick-injection build: while SEATFAIL's count
/// is above 0 a publish takes one and returns false without reaching the wire (REQ-RUI-09);
/// otherwise rtps_comms_publish_seat. The UI task (the seat path).
/// @param axis The seat axis.
/// @param target Its absolute target, in the axis' whole units.
/// @return Whether the SeatCommand was published.
bool bench_verb_seat_publish(rammp::SeatAxis axis, float target);
