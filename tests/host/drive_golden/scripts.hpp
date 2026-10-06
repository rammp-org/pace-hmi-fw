#pragma once
// The scripted scenarios of the drive goldens and the runner that plays them against a target
// (the drive code in the main.cpp unit, or the adapter over the fake port).

#include <cstdint>
#include <string>
#include <vector>

#include "drive_session_types.hpp"
#include "world.hpp"

namespace golden {

// The deadlines as the drive code holds them (0 = not armed).
struct Deadlines {
  std::int64_t warn;
  std::int64_t giveup;
  std::int64_t exit;
};

// One way into the drive code. Every entry is what the firmware calls on the LVGL task.
struct Target {
  void (*reset)();                          // a fresh drive state, as at boot
  bool (*input)(hmi::drive_session::Input); // drive_session_input
  void (*tick)();                           // drive_wait_poll, from rtps_poll_cb
  void (*unlock_hold_done)();               // unlock_gesture.completed
  void (*exit_hold_done)();                 // drive_exit_gesture.completed
  void (*set_profile)(Profile);             // drive_profile_click_cb's store
  Deadlines (*deadlines)();                 // for the CLOCK_TO steps only
};

enum class Op : std::uint8_t {
  TICK,          // the 250 ms tick
  UNLOCK_HOLD,   // the unlock hold completed
  EXIT_HOLD,     // the exit hold completed
  INPUT,         // arg: an Input, sent as is (also out-of-range values)
  PROFILE,       // arg: a Profile: the click stores it, then PROFILE_CLICK
  TIMER_FIRES,   // the unlock advance timer fires, when armed (then frag_lock forgets it)
  LINK,          // arg: 0/1
  MIB,           // arg: a Mib
  SCREEN,        // arg: a ScreenId: the user goes there (instant, menu closed)
  MENU,          // arg: 0/1: the user closes or opens the menu
  FRAME,         // a faded screen load lands
  ADVANCE,       // arg: microseconds
  CLOCK_TO_WARN, // arg: delta; the clock to the warn deadline + delta (if armed)
  CLOCK_TO_GIVEUP,
  CLOCK_TO_EXIT,
};

struct Step {
  Op op;
  std::int64_t arg = 0;
};

struct Scenario {
  std::string name;
  std::vector<Step> steps;
};

// The hand-written scenarios (one or more per table row, U3, DRV-022, the safe state), then
// the seeded random walks.
std::vector<Scenario> scenarios();

// Plays every scenario against the target from a fresh world and drive state each; the logs
// are left in port_log() and raw_log(), the row hits in row_hits().
void run_all(const Target &target);

} // namespace golden
