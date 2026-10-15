/**
 * @file bench_hooks.cpp
 * @brief The hazard bench verbs' hooks (bench_hooks.hpp). Moved from main.cpp unchanged.
 */

#include "bench_hooks.hpp"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "app_instances.hpp"
#include "control/bench_stall.hpp"
#include "drive_session.hpp"
#include "esp_system.h"
#include "joystick_cal.hpp"
#include "post/checks.hpp"
#include "post/indicator.hpp"
#include "rtps_comms.hpp"
#include "seat/seat_reading.hpp"
#include "stick/monitor_cycle.hpp"

// The hazard bench verbs' hooks (components/remote_ui/include/bench_verbs.hpp), the fields of
// the stick's output permit (C1) and the quick POST (C3). Bench inject builds only: app_main
// hands them over inside `if constexpr (BENCH_STICK_INJECT)`. Each runs on the remote UI's
// task: atomics only, no LVGL lock (the calibration record's copy takes joystick_cal's short
// lock, as the ADC task's take_new does).
static const char *post_indicator_name(hmi::post::IndicatorKind kind) {
  switch (kind) {
  case hmi::post::IndicatorKind::NONE:
    return "NONE";
  case hmi::post::IndicatorKind::CHECKING:
    return "CHECKING";
  case hmi::post::IndicatorKind::WAITING:
    return "WAITING";
  case hmi::post::IndicatorKind::FAILED:
    return "FAILED";
  case hmi::post::IndicatorKind::NOT_RUN:
    return "NOT_RUN";
  }
  return "?";
}

static hmi::bench_verbs::CalRecord bench_cal_record() {
  const JoystickCal cal = joystick_cal_current();
  hmi::bench_verbs::CalRecord out{};
  for (size_t i = 0; i < out.size(); ++i) {
    out[i] = {.min_mv = static_cast<int32_t>(std::lround(cal[i].min_mv)),
              .centre_mv = static_cast<int32_t>(std::lround(cal[i].center_mv)),
              .max_mv = static_cast<int32_t>(std::lround(cal[i].max_mv))};
  }
  return out;
}

// The drive table's side of the bench STATE line (L1, hazard-c1-spec.md §6): the session's
// phase, the Drive notice, the menu and the refusal banner, each from an atomic mirror the UI
// task writes (no LVGL lock on the remote-UI task). The notice is §2.7's combination made at the
// read: the stop notice as of the last input or tick first, else the stick's hold reason now, so
// it agrees with the hold reason the same line reports.
static void add_drive_hooks(hmi::bench_verbs::Hooks &h) {
  h.phase = [] {
    return std::string(hmi::drive_session::to_string(drive_adapter.published_phase()));
  };
  h.notice = [] {
    const hmi::stick::HoldReason hold = ui_app.permit_hooks().hold_reason.read();
    return std::string(hmi::drive_adapter::to_string(drive_adapter.notice_for(hold)));
  };
  h.menu_open = [] { return ui_app.menu_open_any_task(); };
  h.banner = [] { return std::string(hmi::ui::refused_name(ui_app.refused_any_task())); };
}

// STATE's stick block (C2 REQ-RUI-08): the monitor's telemetry as the bench reads it. Remote-UI
// task, lock-free reads only.
static hmi::bench_verbs::StickBlock bench_stick_block() {
  using hmi::stick::MonitorState;
  using hmi::stick::SampleFault;
  using hmi::stick::StickAxis;
  constexpr auto relaxed = std::memory_order_relaxed;
  const hmi::stick::MonitorTelemetry &t = ui_app.stick_telemetry();
  hmi::bench_verbs::StickBlock b;
  b.state = hmi::stick::to_string(static_cast<MonitorState>(t.state.load(relaxed)));
  b.health = hmi::stick::to_string(ui_app.permit_hooks().stick_health.read());
  b.reason = hmi::stick::to_string(static_cast<SampleFault>(t.last_kind.load(relaxed)));
  b.reason_axis = hmi::stick::to_string(static_cast<StickAxis>(t.last_axis.load(relaxed)));
  b.fault_reason = hmi::stick::to_string(static_cast<SampleFault>(t.fault_kind.load(relaxed)));
  b.fault_axis = hmi::stick::to_string(static_cast<StickAxis>(t.fault_axis.load(relaxed)));
  b.suspect_onsets = t.suspect_onsets.load(relaxed);
  b.faults = t.faults.load(relaxed);
  // bad: kImplausibleKinds' order, P1..P6, is SampleFault's MISSING..TWIST_PARTIAL
  static_assert(static_cast<std::size_t>(SampleFault::MISSING) == 1 &&
                static_cast<std::size_t>(SampleFault::TWIST_PARTIAL) ==
                    hmi::bench_verbs::kImplausibleKinds.size());
  for (std::size_t k = 0; k < b.bad.size(); ++k) {
    b.bad.at(k) = t.implausible.at(k + 1).load(relaxed);
  }
  b.xy_age_max_ms = t.xy_age_max_ms.load(relaxed);
  for (std::size_t i = 0; i < b.max_mv.size(); ++i) {
    b.max_mv.at(i) = t.max_mv.at(i).load(relaxed);
  }
  b.joy_key = static_cast<std::int32_t>(::joy_key.load());
  return b;
}

// The STALL verb: UI|ADC <ms> (C4 REQ-RUI-07), the task stalls itself at its next cycle;
// CADC <ms> (C2 O14, REQ-RUI-08), Read ADC suspends espp's ContinuousAdc task at its next
// cycle. The reply comes at once.
static void add_motion_guard_hooks(hmi::bench_verbs::Hooks &h) {
  h.stall = [](hmi::bench_verbs::StallTarget target, uint32_t ms) {
    using hmi::bench_verbs::StallTarget;
    using hmi::control::StallTask;
    ui_app.bench_stall().request(target == StallTarget::UI    ? StallTask::UI
                                 : target == StallTarget::ADC ? StallTask::ADC
                                                              : StallTask::CADC,
                                 ms);
    return true;
  };
}

// STATE's seat keys and SEATFAIL (hazard-seat-spec.md REQ-RUI-09): the UiApp's seat telemetry,
// and the count bench_verb_seat_publish takes from. Remote-UI task, atomics only.
static void add_seat_hooks(hmi::bench_verbs::Hooks &h) {
  h.seat = [] {
    const hmi::ui::UiApp::SeatTelemetry t = ui_app.seat_telemetry();
    hmi::bench_verbs::SeatBlock b;
    for (const int32_t raw : t.values) {
      b.axes.push_back(hmi::seat::known(raw) ? std::optional<int32_t>{raw} : std::nullopt);
    }
    b.sent = t.sent;
    b.refused = t.refused;
    b.reason = t.pressed ? std::string(hmi::seat::to_string(t.reason)) : std::string("NONE");
    b.publish_failures = t.publish_failures;
    return b;
  };
  h.seat_fail = [](uint32_t n) {
    ui_app.bench_seat_fails().store(n);
    return true;
  };
}

// The LinkPort's SeatCommand publish in a stick-injection build: while SEATFAIL's count is
// above 0 a publish takes one and returns false without reaching the wire (REQ-RUI-09).
bool bench_verb_seat_publish(rammp::SeatAxis axis, float target) {
  std::atomic<uint32_t> &left = ui_app.bench_seat_fails();
  uint32_t n = left.load();
  while (n > 0 && !left.compare_exchange_weak(n, n - 1)) {
  }
  return n == 0 && rtps_comms_publish_seat(axis, target);
}

hmi::bench_verbs::Hooks bench_verb_hooks() {
  using hmi::bench_verbs::PostGateName;
  hmi::bench_verbs::Hooks h;
  add_drive_hooks(h);
  add_motion_guard_hooks(h);
  add_seat_hooks(h);
  h.hold_reason = [] {
    return std::string(hmi::stick::to_string(ui_app.permit_hooks().hold_reason.read()));
  };
  h.calibrating = [] { return joystick_cal_running(); };
  h.cal = bench_cal_record;
  h.post = [] {
    return std::string(hmi::stick::to_string(ui_app.permit_hooks().post_gate.read()));
  };
  h.post_check = []() -> std::optional<std::string> {
    const hmi::ui::PostStage::Shown shown = ui_app.post().shown();
    if (!shown.check) {
      return std::nullopt;
    }
    return std::string(hmi::post::check(*shown.check).name);
  };
  // The persistent indicator: the POST's, or below every POST state C2's stick FAULT
  // (REQ-UI-26), as the TopBar shows it.
  h.indicator = [] {
    return std::string(ui_app.stick_fault_indicated()
                           ? "STICK_FAULT"
                           : post_indicator_name(ui_app.post().shown().kind));
  };
  h.indicator_text = [] {
    return std::string(ui_app.stick_fault_indicated() ? rammp::kHmiNoticeStickFault
                                                      : ui_app.post().text_now().text.data());
  };
  h.stick = bench_stick_block; // C2 REQ-RUI-08: the monitor's telemetry
  h.reset_reason = [] {
    return std::string(
        hmi::post::reset_reason_id(static_cast<hmi::post::ResetReason>(esp_reset_reason())));
  };
  // PERMIT POST: applied by the POST runner on its next tick (C3 §2.4), so the gate keeps one
  // writer. PostGateName is PostGate's order (static_asserted below).
  h.permit_post = [](PostGateName gate) {
    ui_app.post().request_gate(static_cast<hmi::stick::PostGate>(gate));
    return true;
  };
  // CAL UNSAVED: RAM only, until reboot: joystick_cal_measured() and the POST's "saved" fact
  // read false (C1 §6, REQ-RUI-06).
  h.cal_unsaved = [] {
    joystick_cal_forget_measured();
    return true;
  };
  // POST RERUN: the runner restarts from NOT_RUN on its next tick; Read ADC sees the gate back
  // at NOT_RUN and drops its partial window (C3 §2.4).
  h.post_rerun = [] {
    ui_app.post().request_rerun();
    return true;
  };
  return h;
}
static_assert(static_cast<int>(hmi::bench_verbs::PostGateName::NOT_RUN) ==
                      static_cast<int>(hmi::stick::PostGate::NOT_RUN) &&
                  static_cast<int>(hmi::bench_verbs::PostGateName::PENDING) ==
                      static_cast<int>(hmi::stick::PostGate::PENDING) &&
                  static_cast<int>(hmi::bench_verbs::PostGateName::PASS) ==
                      static_cast<int>(hmi::stick::PostGate::PASS) &&
                  static_cast<int>(hmi::bench_verbs::PostGateName::FAIL) ==
                      static_cast<int>(hmi::stick::PostGate::FAIL),
              "PERMIT POST's names are PostGate's values");
