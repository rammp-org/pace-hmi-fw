// The hazard fixes' hand-written goldens: C1's GLD-101..114 (docs/plans/hazard-c1-spec.md §5.2),
// C3's GLD-117..124 (hazard-c3-spec.md §6.2) and C2b's GLD-126..130 (hazard-c2-spec.md §8.5;
// GLD-129 and GLD-130 for the owner's D9b and §5.1's re-entry, hazard-decisions.md). Since C3, C1's
// run after the "booted" preamble (C3 F1: the POST gate PASS, link up, MIB IDLE, one tick, not
// compared); their expected lines are unchanged.
//
// Each golden is a list of steps played against the firmware's drive code (drive_ui's
// DrivePort over the recording shims, and the one DriveAdapter, main_unit.cpp). A checked
// step's expected tokens are the filtered log (world.hpp) that step must produce, in order and
// nothing else; an unchecked step (the openings) is not compared. The expected tokens are
// written from the spec's table, never recorded from the code (CORE never-list).
//
// Conventions (§5.2): a fixed clock, t in ms after T0 (the clock where the scenario's timing
// starts, > 0); a tick at t uses t for both Envs. "DRIVING" = link up, MIB ENABLED, Locked
// screen, tick (row 1), +1000 ms, the unlock timer fires (row 35), the Drive screen loaded;
// t = 0 is the end of that opening. The stick's hold reason is NONE unless stated. P(...)
// carries the profile once the scenario has picked one.

#include "goldens.hpp"

#include "stick/permit_types.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace golden {

namespace {

using hmi::drive_session::Input;
using hmi::stick::PostGate;

// The POST gate's values for the post() step.
constexpr std::uint8_t kPostPending = static_cast<std::uint8_t>(PostGate::PENDING);
constexpr std::uint8_t kPostPass = static_cast<std::uint8_t>(PostGate::PASS);
constexpr std::uint8_t kPostFail = static_cast<std::uint8_t>(PostGate::FAIL);

using Tokens = std::vector<std::string>;

// --- building a golden -----------------------------------------------------------------------

class Builder {
public:
  explicit Builder(std::string name) { g_.name = std::move(name); }

  // Steps that are not compared (an opening, a setting).
  Builder &run(std::initializer_list<Step> steps) {
    for (const Step &s : steps) {
      g_.steps.push_back({s, false, {}, false});
    }
    return *this;
  }
  // One step and exactly the tokens it must log.
  Builder &check(const Step &s, Tokens expect) {
    g_.steps.push_back({s, true, std::move(expect), false});
    return *this;
  }
  // As check, and the step must report one fault (the adapter's error log).
  Builder &check_fault(const Step &s, Tokens expect) {
    g_.steps.push_back({s, true, std::move(expect), true});
    return *this;
  }
  // The clock to T0 + ms, then a checked tick.
  Builder &tick_at(std::int64_t ms, Tokens expect) {
    run({at(ms)});
    return check(tick(), std::move(expect));
  }
  // The clock to T0 + ms, then a checked step.
  Builder &at_check(std::int64_t ms, const Step &s, Tokens expect) {
    run({at(ms)});
    return check(s, std::move(expect));
  }
  // The "booted" preamble (hazard-c3-spec.md F1): the POST gate PASS, link up, MIB IDLE, one
  // tick (the boot DISABLE, row 50); not compared, so the expected lines do not change.
  Builder &booted() {
    return run({post(kPostPass), link(true), mib(Mib::IDLE), screen(ScreenId::LOCKED), tick()});
  }
  // The DRIVING opening (§5.2) after the preamble; T0 at its end.
  Builder &driving() {
    booted();
    run({link(true), mib(Mib::ENABLED), screen(ScreenId::LOCKED), tick(), advance(1'000'000),
         timer_fires(), frame_step(), mark_t0()});
    return *this;
  }
  // The preamble, then T0, for a golden with no DRIVING opening.
  Builder &start() {
    booted();
    return run({mark_t0()});
  }
  // T0 at boot: no preamble (C3's goldens of the boot itself).
  Builder &boot() { return run({mark_t0()}); }

  [[nodiscard]] Golden done() { return std::move(g_); }

private:
  Golden g_;
};

const Tokens kNothing{};
// A relock (rows 3-9): the menu flag, the Locked screen, the gate, one DISABLE.
Tokens relock(const char *menu_flag, const char *publish) {
  return {menu_flag, "ring_rest", "L", "lock(1)", "gate", publish};
}
Tokens with(Tokens t, std::initializer_list<const char *> more) {
  t.insert(t.end(), more.begin(), more.end());
  return t;
}
const Tokens kEntry{"open", "lock(0)", "gate"}; // F1 (rows 1-2)

// GLD-105's ticks every 250 ms from `from` to `to` (ms) with the MIB ENABLED: before the fault
// (t < 5000) a DISABLE on every tick, the exit refused at `refused_at`; at 5000 the fault and
// no DISABLE; then a DISABLE only when 1 s has passed since the last (5750, 6750).
void stop_ignored_ticks(Builder &b, std::int64_t from, std::int64_t to, std::int64_t refused_at) {
  for (std::int64_t t = from; t <= to; t += 250) {
    if (t < 5000) {
      b.tick_at(t, t == refused_at ? Tokens{"B:EXIT_REFUSED", "P(D)"} : Tokens{"P(D)"});
    } else if (t == 5000) {
      b.tick_at(t, {"N:MCB_DID_NOT_STOP"});
    } else {
      b.tick_at(t, (t == 5750 || t == 6750) ? Tokens{"P(D)"} : kNothing);
    }
  }
}

Golden gld105_until(std::int64_t last_tick, std::string name) {
  Builder b(std::move(name));
  b.driving().check(exit_hold(), {"P(D)", "N:STOPPING"});
  stop_ignored_ticks(b, 250, last_tick, 750);
  return b.done();
}

} // namespace

std::vector<Golden> c1_goldens() {
  std::vector<Golden> out;

  out.push_back(Builder("GLD-101 the MCB enables unasked: the entry, no DriveCommand")
                    .start()
                    .run({link(true), mib(Mib::IDLE), screen(ScreenId::LOCKED)})
                    .check(tick(), kNothing)
                    .check(mib(Mib::ENABLED), kNothing)
                    .check(tick(), kEntry)
                    .check(advance(1'000'000), kNothing)
                    .check(timer_fires(), {"Dv"})
                    .done());

  out.push_back(Builder("GLD-102 the MCB stops on its own: relock, one DISABLE, stopped banner")
                    .driving()
                    .check(mib(Mib::IDLE), kNothing)
                    .check(tick(), with(relock("menu(0)", "P(D)"), {"B:DRIVE_STOPPED"}))
                    .check(tick(), kNothing)
                    .done());

  out.push_back(Builder("GLD-103 the link goes: relock, one DISABLE, lost banner; back ENABLED: "
                        "the entry again, no DriveCommand")
                    .driving()
                    .check(link(false), kNothing)
                    .check(tick(), with(relock("menu(0)", "P(D)"), {"B:DRIVE_LOST"}))
                    .check(link(true), kNothing)
                    .check(mib(Mib::ENABLED), kNothing)
                    .check(tick(), kEntry)
                    .done());

  out.push_back(Builder("GLD-104 the exit hold, the MCB obeys: Stopping, re-sent, relock")
                    .driving()
                    .check(exit_hold(), {"P(D)", "N:STOPPING"})
                    .tick_at(250, {"P(D)"})
                    .at_check(400, mib(Mib::IDLE), kNothing)
                    .tick_at(500, with(relock("menu(0)", "P(D)"), {"N:NONE"}))
                    .done());

  out.push_back(Builder("GLD-104b the burger key, the MCB obeys: as GLD-104, the menu after")
                    .driving()
                    .check(input(Input::MENU_KEY_DRIVE), {"P(D)", "N:STOPPING"})
                    .tick_at(250, {"P(D)"})
                    .at_check(400, mib(Mib::IDLE), kNothing)
                    .tick_at(500, with(relock("menu(1)", "P(D)"), {"N:NONE"}))
                    .done());

  out.push_back(gld105_until(7000, "GLD-105 the MCB ignores the stop: DISABLE every 250 ms, "
                                   "\"MCB did not stop\" at 5 s, then 1 Hz, never a relock"));

  {
    Builder b("GLD-106 the burger key 1.9 s into an ignored stop: the deadline re-armed, the "
              "fault still at 5 s");
    b.driving().check(exit_hold(), {"P(D)", "N:STOPPING"});
    stop_ignored_ticks(b, 250, 1750, 750);
    b.at_check(1900, input(Input::MENU_KEY_DRIVE), {"P(D)"});
    b.tick_at(2000, kNothing);
    stop_ignored_ticks(b, 2250, 7000, 2750);
    out.push_back(b.done());
  }

  {
    Golden g = gld105_until(7000, "GLD-107 GLD-105, then the MCB stops: relock, one DISABLE, "
                                  "the notice cleared, no banner");
    Builder b("");
    b.at_check(7000, mib(Mib::IDLE), kNothing)
        .tick_at(7000, with(relock("menu(0)", "P(D)"), {"N:NONE"}))
        .tick_at(7250, kNothing)
        .tick_at(7500, kNothing);
    Golden tail = b.done();
    g.steps.insert(g.steps.end(), tail.steps.begin(), tail.steps.end());
    out.push_back(std::move(g));
  }

  {
    Builder b("GLD-108 the MCB enables during a calibration: no entry until it ends");
    b.start().run({link(true), mib(Mib::ENABLED), screen(ScreenId::JOYSTICK), calibrating(true)});
    for (std::int64_t t = 0; t <= 3000; t += 250) {
      b.tick_at(t, kNothing);
    }
    b.at_check(3100, calibrating(false), kNothing)
        .tick_at(3250, kEntry)
        .check(advance(1'000'000), kNothing)
        .check(timer_fires(), {"Dv"});
    out.push_back(b.done());
  }

  {
    Builder b("GLD-109 the MCB enables behind the Boot screen: no entry until it goes");
    b.start().run({screen(ScreenId::BOOT), link(true), mib(Mib::ENABLED)});
    for (std::int64_t t = 0; t <= 1000; t += 250) {
      b.tick_at(t, kNothing);
    }
    b.check(screen(ScreenId::LOCKED), kNothing).check(tick(), kEntry);
    out.push_back(b.done());
  }

  out.push_back(Builder("GLD-110 a profile tap: ENABLE with it while ENABLED, nothing once the "
                        "MCB stopped; the relock and a later tap send DISABLE with it")
                    .driving()
                    .check(profile(Profile::HIGH), {"P(E,HIGH)"})
                    .check(mib(Mib::IDLE), kNothing)
                    .check(profile(Profile::LOW), kNothing)
                    .check(tick(), with(relock("menu(0)", "P(D,LOW)"), {"B:DRIVE_STOPPED"}))
                    .check(profile(Profile::NORMAL), {"P(D,NORMAL)"})
                    .done());

  out.push_back(Builder("GLD-111 the exit hold while locked: one DISABLE, no deadline, no banner")
                    .start()
                    .run({link(true), mib(Mib::IDLE), screen(ScreenId::LOCKED)})
                    .check(exit_hold(), {"P(D)"})
                    .check(advance(1'000'000), kNothing)
                    .check(tick(), kNothing)
                    .done());

  out.push_back(Builder("GLD-112 the exit hold while asking: the stop wins, no NOT_GRANTED, no "
                        "give-up DISABLE")
                    .start()
                    .run({link(true), mib(Mib::IDLE), screen(ScreenId::LOCKED)})
                    .check(unlock_hold(), {"ring_wait", "P(E)"})
                    .check(exit_hold(), {"P(D)", "ring_rest"})
                    .at_check(2100, tick(), kNothing)
                    .done());

  out.push_back(Builder("GLD-113 the link goes during a stop: the stop ends (relock, one "
                        "DISABLE, no banner); back ENABLED: the entry again")
                    .driving()
                    .check(exit_hold(), {"P(D)", "N:STOPPING"})
                    .tick_at(250, {"P(D)"})
                    .at_check(300, link(false), kNothing)
                    .tick_at(500, with(relock("menu(0)", "P(D)"), {"N:NONE"}))
                    .at_check(600, link(true), kNothing)
                    .check(mib(Mib::ENABLED), kNothing)
                    .tick_at(750, kEntry)
                    .done());

  {
    Golden g = gld105_until(5000, "GLD-114 a corrupted input during an ignored stop: the safe "
                                  "state, the stop's notice cleared, the fault reported");
    Builder b("");
    b.run({at(5100)})
        .check_fault(input_raw(99),
                     {"P(D)", "menu(0)", "ring_rest", "L", "lock(1)", "gate", "N:NONE"});
    Golden tail = b.done();
    g.steps.insert(g.steps.end(), tail.steps.begin(), tail.steps.end());
    out.push_back(std::move(g));
  }
  return out;
}

std::vector<Golden> c3_goldens() {
  std::vector<Golden> out;

  {
    Builder b("GLD-117 boot with the link down, then up: one DISABLE on the first CONNECTED tick");
    b.boot().run({link(false)});
    for (int i = 0; i < 3; ++i) {
      b.check(tick(), kNothing);
    }
    b.check(link(true), kNothing).check(mib(Mib::IDLE), kNothing);
    b.check(screen(ScreenId::LOCKED), kNothing).check(tick(), {"P(D)"});
    for (int i = 0; i < 2; ++i) { // the 2nd and 3rd of the 3 ticks
      b.check(tick(), kNothing);
    }
    out.push_back(b.done());
  }

  out.push_back(Builder("GLD-118 boot with the MCB ENABLED: the boot DISABLE, the entry a tick "
                        "later, never an ENABLE")
                    .boot()
                    .run({link(true), mib(Mib::ENABLED), screen(ScreenId::LOCKED)})
                    .check(tick(), {"P(D)"})
                    .check(tick(), kEntry)
                    .check(advance(1'000'000), kNothing)
                    .check(timer_fires(), {"Dv"})
                    .done());

  out.push_back(Builder("GLD-119 boot with the MCB ENABLED, IDLE before the next tick: the boot "
                        "DISABLE, then nothing")
                    .boot()
                    .run({link(true), mib(Mib::ENABLED), screen(ScreenId::LOCKED)})
                    .check(tick(), {"P(D)"})
                    .check(mib(Mib::IDLE), kNothing)
                    .check(tick(), kNothing)
                    .done());

  out.push_back(Builder("GLD-120 the unlock hold before POST pass: refused, no DriveCommand")
                    .start()
                    .run({post(kPostPending), mib(Mib::IDLE), screen(ScreenId::LOCKED)})
                    .check(unlock_hold(), {"ring_rest", "B:REFUSED_POST"})
                    .done());

  out.push_back(Builder("GLD-121 the MCB ENABLED before POST pass: the entry (M1), a profile tap "
                        "sends nothing until POST passes")
                    .start()
                    .run({post(kPostPending)})
                    .check(mib(Mib::ENABLED), kNothing)
                    .check(tick(), kEntry)
                    .check(advance(1'000'000), kNothing)
                    .check(timer_fires(), {"Dv"})
                    .check(profile(Profile::LOW), kNothing)
                    .check(post(kPostPass), kNothing)
                    .check(profile(Profile::LOW), {"P(E,LOW)"})
                    .done());

  out.push_back(Builder("GLD-122 POST failed: the push and the DRIVE row are refused, no "
                        "DriveCommand")
                    .start()
                    .run({post(kPostFail), mib(Mib::IDLE), screen(ScreenId::LOCKED)})
                    .check(input(Input::ENTRY_PUSH), {"B:REFUSED_POST"})
                    .check(input(Input::MENU_ROW_DRIVE), {"B:REFUSED_POST"})
                    .done());

  {
    Builder b("GLD-123 boot behind the Boot screen with the MCB ENABLED: the boot DISABLE, no "
              "entry until the Locked screen");
    b.boot().run({screen(ScreenId::BOOT), link(true), mib(Mib::ENABLED)});
    for (std::int64_t t = 0; t <= 1000; t += 250) {
      b.tick_at(t, t == 0 ? Tokens{"P(D)"} : kNothing);
    }
    b.check(screen(ScreenId::LOCKED), kNothing).check(tick(), kEntry);
    out.push_back(b.done());
  }

  out.push_back(Builder("GLD-124 the unlock hold before the first tick: the user's ENABLE wins, "
                        "no boot DISABLE")
                    .boot()
                    .run({link(true), mib(Mib::IDLE), screen(ScreenId::LOCKED)})
                    .check(unlock_hold(), {"ring_wait", "P(E)"})
                    .check(tick(), kNothing)
                    .done());

  // --- C2b (hazard-c2-spec.md §5.3, §8.5): the stick in FAULT -----------------------------------
  // Row 55/56's actions are row 21's (P(D)) then SHOW_STICK_FAULT (B:STICK_FAULT); the Drive
  // notice follows the tick's actions (C1 §2.7), so N:STOPPING comes last.
  const Tokens kStickStop{"P(D)", "B:STICK_FAULT", "N:STOPPING"};
  out.push_back(Builder("GLD-126 the stick in FAULT while driving: the stop as an exit hold, the "
                        "MCB obeys: relock as GLD-107, no banner")
                    .driving()
                    .check(stick_fault(true), kNothing)
                    .check(tick(), kStickStop)
                    .at_check(100, mib(Mib::IDLE), kNothing)
                    .tick_at(250, with(relock("menu(0)", "P(D)"), {"N:NONE"}))
                    .done());

  {
    Builder b("GLD-127 the stick in FAULT while driving, the MCB stays ENABLED: as GLD-105, "
              "never a relock");
    b.driving().check(stick_fault(true), kNothing).check(tick(), kStickStop);
    stop_ignored_ticks(b, 250, 7000, 750);
    out.push_back(b.done());
  }

  out.push_back(Builder("GLD-128 the stick in FAULT, locked: the unlock hold is refused, no "
                        "DriveCommand")
                    .start()
                    .run({mib(Mib::IDLE), screen(ScreenId::LOCKED), stick_fault(true)})
                    .check(unlock_hold(), {"ring_rest", "B:REFUSED_STICK"})
                    .done());

  out.push_back(Builder("GLD-129 the stick in FAULT, locked: the push and the DRIVE row are "
                        "refused, no DriveCommand (D9b)")
                    .start()
                    .run({mib(Mib::IDLE), screen(ScreenId::LOCKED), stick_fault(true)})
                    .check(input(Input::ENTRY_PUSH), {"B:REFUSED_STICK"})
                    .check(input(Input::MENU_ROW_DRIVE), {"B:REFUSED_STICK"})
                    .done());

  out.push_back(Builder("GLD-130 the stick in FAULT, the MCB enables on its own: the entry (M1), "
                        "then the stick fault's stop on the same tick")
                    .start()
                    .run({mib(Mib::IDLE), screen(ScreenId::LOCKED), stick_fault(true)})
                    .check(mib(Mib::ENABLED), kNothing)
                    .check(tick(), with(kEntry, {"P(D)", "B:STICK_FAULT", "N:STOPPING"}))
                    .done());

  // --- Owner decision T1 (hazard-decisions.md): the boot DISABLE waits for the DriveCommand
  // writer to have a matched reader; one message, dropped by the user's own ask --------------
  out.push_back(
      Builder("GLD-131 boot, the DriveCommand writer not matched yet: the boot "
              "DISABLE is held, then sent once on the first tick after the match")
          .boot()
          .run({drive_matched(false), screen(ScreenId::LOCKED), link(true), mib(Mib::IDLE)})
          .tick_at(0, kNothing)
          .tick_at(250, kNothing)
          .at_check(400, drive_matched(true), kNothing)
          .tick_at(500, {"P(D)"})
          .tick_at(750, kNothing)
          .done());

  out.push_back(
      Builder("GLD-132 the boot DISABLE held, then the user's own ask: it is dropped, "
              "not sent after the match")
          .boot()
          .run({drive_matched(false), screen(ScreenId::LOCKED), link(true), mib(Mib::IDLE)})
          .tick_at(0, kNothing)
          .at_check(100, unlock_hold(), {"ring_wait", "P(E)"})
          .at_check(200, drive_matched(true), kNothing)
          .tick_at(250, kNothing)
          .done());

  out.push_back(
      Builder("GLD-133 the boot DISABLE held, the MCB ENABLED: Drive is entered (M1) "
              "before the DISABLE goes (the residual), which leads the tick after the "
              "match")
          .boot()
          .run({drive_matched(false), screen(ScreenId::LOCKED), link(true), mib(Mib::ENABLED)})
          .tick_at(0, kNothing)
          .tick_at(250, kEntry)
          .at_check(400, drive_matched(true), kNothing)
          .tick_at(500, {"P(D)"})
          .tick_at(750, kNothing)
          .done());
  return out;
}

} // namespace golden
