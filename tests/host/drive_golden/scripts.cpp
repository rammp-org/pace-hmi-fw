// The scenarios of the drive goldens and their runner: see scripts.hpp.

#include "scripts.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <iterator>

#include "probe.hpp"

namespace golden {

namespace {

using hmi::drive_session::Input;

constexpr std::int64_t kMs = 1000;

const char *input_name(std::int64_t in) {
  static constexpr std::array<const char *, hmi::drive_session::kInputCount> kNames{
      "TICK_FOLLOW",      "TICK_EXIT_DUE",  "TICK_WARN_DUE",  "TICK_GIVEUP_DUE",
      "UNLOCK_HOLD_DONE", "EXIT_HOLD_DONE", "MENU_KEY_DRIVE", "PROFILE_CLICK",
      "UNLOCK_TIMER",     "ENTRY_PUSH",     "MENU_ROW_DRIVE"};
  return in >= 0 && static_cast<std::size_t>(in) < kNames.size()
             ? kNames[static_cast<std::size_t>(in)]
             : "CORRUPTED";
}

// --- step builders ---------------------------------------------------------------------------
Step tick() { return {Op::TICK}; }
Step unlock_hold() { return {Op::UNLOCK_HOLD}; }
Step exit_hold() { return {Op::EXIT_HOLD}; }
Step input(Input in) { return {Op::INPUT, static_cast<std::int64_t>(in)}; }
Step input_raw(std::int64_t in) { return {Op::INPUT, in}; }
Step profile(Profile p) { return {Op::PROFILE, static_cast<std::int64_t>(p)}; }
Step timer_fires() { return {Op::TIMER_FIRES}; }
Step link(bool up) { return {Op::LINK, up ? 1 : 0}; }
Step mib(Mib m) { return {Op::MIB, static_cast<std::int64_t>(m)}; }
Step screen(ScreenId s) { return {Op::SCREEN, static_cast<std::int64_t>(s)}; }
Step menu(bool open) { return {Op::MENU, open ? 1 : 0}; }
Step frame_step() { return {Op::FRAME}; }
Step advance(std::int64_t us) { return {Op::ADVANCE, us}; }
Step to_warn(std::int64_t delta) { return {Op::CLOCK_TO_WARN, delta}; }
Step to_giveup(std::int64_t delta) { return {Op::CLOCK_TO_GIVEUP, delta}; }
Step to_exit(std::int64_t delta) { return {Op::CLOCK_TO_EXIT, delta}; }

std::vector<Step> seq(std::initializer_list<std::initializer_list<Step>> parts) {
  std::vector<Step> out;
  for (const auto &part : parts) {
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}

// Common openings.
const std::initializer_list<Step> kReady{link(true), mib(Mib::IDLE)};
// The MIB enables unasked (row 1), the advance lands, Drive fades in: DRIVING.
const std::initializer_list<Step> kDriving{
    link(true),    mib(Mib::ENABLED), tick(), advance(1000 * kMs),
    timer_fires(), frame_step(),      tick()};
// Asked: the unlock hold with the MCB ready (row 18).
const std::initializer_list<Step> kAsking{link(true), mib(Mib::IDLE), tick(), unlock_hold()};

std::vector<Scenario> hand_written() {
  using enum Input;
  std::vector<Scenario> s;
  s.push_back({"unlock, ask, drive, profile, exit (rows 18, 2, 35, 32, 22, 8)",
               seq({kReady,
                    {tick(),
                     unlock_hold(),
                     advance(250 * kMs),
                     tick(),
                     mib(Mib::ENABLED),
                     advance(250 * kMs),
                     tick(),
                     advance(1000 * kMs),
                     timer_fires(),
                     frame_step(),
                     tick(),
                     profile(Profile::HIGH),
                     exit_hold(),
                     advance(250 * kMs),
                     tick(),
                     mib(Mib::IDLE),
                     advance(250 * kMs),
                     tick(),
                     frame_step(),
                     tick()}})});
  s.push_back({"the MIB enables unasked, then the link goes (rows 1, 4)",
               seq({{link(true), mib(Mib::ENABLED), tick(), advance(250 * kMs), link(false), tick(),
                     frame_step(), tick()}})});
  s.push_back(
      {"stopped while unlocking (row 3), then while driving (row 5)",
       seq({{link(true), mib(Mib::ENABLED), tick(), mib(Mib::IDLE), tick(), mib(Mib::ENABLED),
             tick()},
            {advance(1000 * kMs), timer_fires(), frame_step(), tick(), mib(Mib::ERROR), tick()}})});
  s.push_back({"the link lost while driving (row 6)", seq({kDriving, {link(false), tick()}})});
  s.push_back({"the burger key on Drive: stop, then the menu over Locked (rows 26, 27, 7)",
               seq({kDriving,
                    {input(MENU_KEY_DRIVE), input(MENU_KEY_DRIVE), advance(250 * kMs), tick(),
                     mib(Mib::IDLE), tick(), tick()}})});
  s.push_back({"exit refused, asked again, then granted (rows 22, 33, 14, 34, 24, 23, 28, 9)",
               seq({kDriving,
                    {exit_hold(), profile(Profile::LOW), advance(500 * kMs), tick(), to_exit(0),
                     tick(), profile(Profile::NORMAL), exit_hold(), exit_hold(), to_exit(0), tick(),
                     input(MENU_KEY_DRIVE), to_exit(0), tick(), mib(Mib::IDLE), tick(),
                     frame_step(), tick()}})});
  s.push_back({"while the unlock timer is armed: profile, exit hold, advance (rows 31, 21, 36)",
               seq({{link(true), mib(Mib::ENABLED), tick(), profile(Profile::HIGH), exit_hold(),
                     timer_fires(), frame_step(), tick()}})});
  s.push_back({"an exit refused while the unlock timer is armed, then it fires (rows 21, 14, 37)",
               seq({{link(true), mib(Mib::ENABLED), tick(), exit_hold(), to_exit(0), tick(),
                     timer_fires(), frame_step(), tick()}})});
  s.push_back({"the burger key while the unlock timer is armed (row 25)",
               seq({{link(true), mib(Mib::ENABLED), tick(), input(MENU_KEY_DRIVE), timer_fires(),
                     frame_step(), tick(), mib(Mib::IDLE), tick()}})});
  s.push_back({"on the Seat screen without the MCB, locked (row 10) and asking (row 11)",
               seq({{screen(ScreenId::SEAT), tick(), frame_step()},
                    kReady,
                    {unlock_hold(), screen(ScreenId::SEAT), link(false), tick(), tick(),
                     frame_step(), to_warn(0), tick(), tick()}})});
  s.push_back({"asked, not granted, given up (rows 15, 12, 16, 29)",
               seq({kAsking,
                    {to_warn(0), tick(), advance(250 * kMs), tick(), to_giveup(0), tick(),
                     profile(Profile::LOW), tick()}})});
  s.push_back({"asking on the Seat screen with the MCB ready (row 13)",
               seq({kAsking, {screen(ScreenId::SEAT), to_warn(0), tick(), tick(), tick()}})});
  s.push_back({"given up while still asking (rows 30, 15, 17)",
               seq({kAsking, {profile(Profile::LOW), to_giveup(0), tick(), tick()}})});
  s.push_back({"DRV-022: a deadline passing between the tick's two clock reads (warn)",
               seq({kAsking, {to_warn(-2), tick(), to_warn(-1), tick()}})});
  s.push_back({"DRV-022: a deadline passing between the tick's two clock reads (give-up)",
               seq({kAsking, {screen(ScreenId::SEAT), link(false), to_giveup(-1), tick()}})});
  s.push_back({"DRV-022: a deadline passing between the tick's two clock reads (exit)",
               seq({kDriving, {exit_hold(), to_exit(-2), tick(), to_exit(-1), tick()}})});
  s.push_back({"the unlock hold refused, then asked twice (rows 19, 18, 20)",
               seq({{tick(), unlock_hold(), link(true), mib(Mib::IDLE), unlock_hold(),
                     unlock_hold(), tick()}})});
  s.push_back(
      {"push and the DRIVE row without the MCB (rows 38, 40, 39, 41)",
       seq({{input(ENTRY_PUSH), input(MENU_ROW_DRIVE)},
            kAsking,
            {link(false), input(ENTRY_PUSH), input(MENU_ROW_DRIVE), menu(true), input(ENTRY_PUSH),
             menu(false), screen(ScreenId::JOYSTICK), input(ENTRY_PUSH), input(MENU_ROW_DRIVE)}})});
  s.push_back({"U3: the exit hold completing while locked, and its deadline",
               seq({kReady,
                    {exit_hold(), advance(250 * kMs), tick(), to_exit(0), tick(), tick(),
                     mib(Mib::ENABLED), tick()}})});
  s.push_back({"U3 while asking, its deadline between the tick's clock reads",
               seq({kAsking, {exit_hold(), to_exit(-1), tick(), tick()}})});
  s.push_back({"U3 then the burger key's exit: the latch carries over",
               seq({kReady,
                    {exit_hold(), mib(Mib::ENABLED), tick(), input(MENU_KEY_DRIVE), timer_fires(),
                     frame_step(), mib(Mib::IDLE), tick()}})});
  s.push_back({"the safe state: a corrupted input while driving",
               seq({kDriving, {input_raw(99), tick(), frame_step(), tick()}})});
  s.push_back(
      {"the safe state: a corrupted input while asking", seq({kAsking, {input_raw(11), tick()}})});
  s.push_back({"every input while locked with no link (no row but U3 and refusals)",
               seq({{input(TICK_FOLLOW), input(TICK_EXIT_DUE), input(TICK_WARN_DUE),
                     input(TICK_GIVEUP_DUE), input(UNLOCK_HOLD_DONE), input(EXIT_HOLD_DONE),
                     input(MENU_KEY_DRIVE), input(PROFILE_CLICK), input(UNLOCK_TIMER),
                     input(ENTRY_PUSH), input(MENU_ROW_DRIVE)}})});
  s.push_back(
      {"every input while driving",
       seq({kDriving,
            {input(TICK_FOLLOW), input(TICK_EXIT_DUE), input(TICK_WARN_DUE), input(TICK_GIVEUP_DUE),
             input(UNLOCK_HOLD_DONE), input(PROFILE_CLICK), input(UNLOCK_TIMER), input(ENTRY_PUSH),
             input(MENU_ROW_DRIVE), input(EXIT_HOLD_DONE), input(MENU_KEY_DRIVE)}})});
  s.push_back({"from the Boot screen, the menu over Drive, an unknown MIB state",
               seq({{screen(ScreenId::BOOT), link(true), mib(Mib::ENABLED), tick(), timer_fires(),
                     frame_step(), menu(true), tick(), input(MENU_KEY_DRIVE), menu(false),
                     mib(Mib::BOGUS), tick(), frame_step(), tick()}})});
  return s;
}

// --- seeded random walks ---------------------------------------------------------------------
struct Rng {
  std::uint64_t state;
  std::uint64_t next() {
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return state;
  }
  std::int64_t pick(std::int64_t n) {
    return static_cast<std::int64_t>(next() % static_cast<std::uint64_t>(n));
  }
};

constexpr std::array<std::int64_t, 7> kAdvances{50 * kMs,   250 * kMs,  500 * kMs, 760 * kMs,
                                                1000 * kMs, 2100 * kMs, 5000 * kMs};
constexpr std::array<Mib, 5> kMibs{Mib::INITIALIZING, Mib::IDLE, Mib::ENABLED, Mib::ERROR,
                                   Mib::BOGUS};
constexpr std::array<ScreenId, 5> kScreens{ScreenId::BOOT, ScreenId::LOCKED, ScreenId::DRIVE,
                                           ScreenId::SEAT, ScreenId::JOYSTICK};

Step random_step(Rng &r) {
  using enum Input;
  const std::int64_t k = r.pick(100);
  if (k < 20) {
    return tick();
  }
  if (k < 31) {
    return advance(kAdvances[static_cast<std::size_t>(r.pick(kAdvances.size()))]);
  }
  if (k < 37) {
    const std::int64_t delta = r.pick(3) - 1;
    const std::int64_t which = r.pick(3);
    return which == 0 ? to_warn(delta) : which == 1 ? to_giveup(delta) : to_exit(delta);
  }
  if (k < 42) {
    return link(r.pick(4) != 0);
  }
  if (k < 50) {
    return mib(kMibs[static_cast<std::size_t>(r.pick(kMibs.size()))]);
  }
  if (k < 56) {
    return unlock_hold();
  }
  if (k < 61) {
    return exit_hold();
  }
  if (k < 65) {
    return input(MENU_KEY_DRIVE);
  }
  if (k < 68) {
    return profile(static_cast<Profile>(r.pick(3)));
  }
  if (k < 73) {
    return timer_fires();
  }
  if (k < 77) {
    return input(ENTRY_PUSH);
  }
  if (k < 80) {
    return input(MENU_ROW_DRIVE);
  }
  if (k < 85) {
    return screen(kScreens[static_cast<std::size_t>(r.pick(kScreens.size()))]);
  }
  if (k < 89) {
    return menu(r.pick(2) != 0);
  }
  return frame_step();
}

constexpr std::size_t kWalks = 16;
constexpr std::size_t kWalkSteps = 40;

std::vector<Scenario> random_walks() {
  std::vector<Scenario> s;
  for (std::size_t w = 0; w < kWalks; ++w) {
    Rng r{0x9E3779B97F4A7C15ULL ^ (static_cast<std::uint64_t>(w + 1) * 0x2545F4914F6CDD1DULL)};
    Scenario sc{std::format("random walk {}", w), {}};
    for (std::size_t i = 0; i < kWalkSteps; ++i) {
      sc.steps.push_back(random_step(r));
    }
    s.push_back(std::move(sc));
  }
  return s;
}

// --- the runner ------------------------------------------------------------------------------
void clock_to(const char *which, std::int64_t deadline, std::int64_t delta) {
  if (deadline == 0) {
    both(std::format("> clock_to {}{:+} (not armed)", which, delta));
    return;
  }
  world().now = deadline + delta;
  both(std::format("> clock_to {}{:+}", which, delta));
}

void log_result(bool acted) { both(std::format("  -> {}", int{acted})); }

void play(const Step &step, const Target &t) {
  World &w = world();
  switch (step.op) {
  case Op::TICK:
    both("> tick");
    t.tick();
    break;
  case Op::UNLOCK_HOLD:
    both("> unlock_hold_done");
    t.unlock_hold_done();
    break;
  case Op::EXIT_HOLD:
    both("> exit_hold_done");
    t.exit_hold_done();
    break;
  case Op::INPUT:
    both(std::format("> input {}", input_name(step.arg)));
    log_result(t.input(static_cast<Input>(step.arg)));
    break;
  case Op::PROFILE:
    both(std::format("> profile {}", profile_name(static_cast<Profile>(step.arg))));
    w.profile = static_cast<Profile>(step.arg);
    w.profile_picked = true;
    t.set_profile(w.profile);
    log_result(t.input(Input::PROFILE_CLICK));
    break;
  case Op::TIMER_FIRES:
    if (!w.unlock_timer) {
      both("> timer_fires (not armed)");
      break;
    }
    both("> timer_fires");
    log_result(t.input(Input::UNLOCK_TIMER));
    w.unlock_timer = false; // unlock_advance_cb's own `unlock_advance_timer = nullptr`
    break;
  case Op::LINK:
    w.link = step.arg != 0;
    both(std::format("> link {}", step.arg));
    break;
  case Op::MIB:
    w.mib = static_cast<Mib>(step.arg);
    both(std::format("> mib {}", mib_name(w.mib)));
    break;
  case Op::SCREEN:
    both(std::format("> screen {}", screen_name(static_cast<ScreenId>(step.arg))));
    user_screen(static_cast<ScreenId>(step.arg));
    break;
  case Op::MENU:
    both(std::format("> menu {}", step.arg));
    user_menu(step.arg != 0);
    break;
  case Op::FRAME:
    both("> frame");
    frame();
    break;
  case Op::ADVANCE:
    w.now += step.arg;
    both(std::format("> advance {}", step.arg));
    break;
  case Op::CLOCK_TO_WARN:
    clock_to("warn", t.deadlines().warn, step.arg);
    break;
  case Op::CLOCK_TO_GIVEUP:
    clock_to("giveup", t.deadlines().giveup, step.arg);
    break;
  case Op::CLOCK_TO_EXIT:
    clock_to("exit", t.deadlines().exit, step.arg);
    break;
  }
  both(snapshot());
}

} // namespace

std::array<unsigned, hmi::drive_session::kTransitionCount + 1> &row_hits() {
  static std::array<unsigned, hmi::drive_session::kTransitionCount + 1> hits{};
  return hits;
}

void note_row(hmi::drive_session::Phase p, hmi::drive_session::Input in,
              hmi::drive_session::GuardMask guards) {
  ++row_hits()[hmi::drive_session::find_row(p, in, guards)];
}

std::vector<Scenario> scenarios() {
  std::vector<Scenario> s = hand_written();
  std::ranges::move(random_walks(), std::back_inserter(s));
  return s;
}

void run_all(const Target &target) {
  clear_logs();
  row_hits() = {};
  for (const Scenario &sc : scenarios()) {
    reset_world();
    target.reset();
    both("# " + sc.name);
    both(snapshot());
    for (const Step &step : sc.steps) {
      play(step, target);
    }
  }
}

} // namespace golden
