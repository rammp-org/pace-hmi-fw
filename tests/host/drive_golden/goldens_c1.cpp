// The hazard fix C1's hand-written goldens, GLD-101..114 (docs/plans/hazard-c1-spec.md §5.2).
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

#include <cstddef>
#include <format>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace golden {

namespace {

using hmi::drive_session::Input;

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
  // The DRIVING opening (§5.2); T0 at its end.
  Builder &driving() {
    run({link(true), mib(Mib::ENABLED), screen(ScreenId::LOCKED), tick(), advance(1'000'000),
         timer_fires(), frame_step(), mark_t0()});
    return *this;
  }
  // T0 now, for a golden with no DRIVING opening.
  Builder &start() { return run({mark_t0()}); }

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
  for (const char *m : more) {
    t.emplace_back(m);
  }
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

} // namespace golden
