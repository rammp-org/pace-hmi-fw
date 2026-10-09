#pragma once
// The stateful world the drive goldens run in (app-main-shrink.md V5).
//
// One model of what the drive code's callees do to the screen, the menu, the lock state and
// the clock: the main-unit shims (main_unit_shims.hpp), the lv_*, rtps and main-unit helpers
// that drive_ui's DrivePort calls, each recording one line in the boundary log and, where it
// stands for a port method, one line in the port log and a token in the filtered log. The port
// and boundary logs carry a snapshot of the world after every scripted step, so where the drive
// code samples, reads the clock or changes the screen shows up in them (they are for reading;
// the goldens that compared them, GLD-001/002/004, are retired).
//
// Model (what the real callees do, in the order the drive code can observe):
//   - the clock: every read returns `now` and then advances it by 1 us, so the number and
//     order of reads is in the logs, and a deadline can be put between two reads (DRV-022);
//   - an instant screen load (locked_screen_go) runs nav_arrive inside the load: the menu
//     goes, and opens again over the new screen when the menu-on-arrival flag was set (the
//     flag is consumed); then the stick gate is re-evaluated;
//   - a faded load (Drive after the unlock, nav_home) lands later: it is pending until the
//     script's FRAME step, which runs nav_arrive for it;
//   - nav_home closes the menu (nav_close_menu re-evaluates the gate) and fades to Locked
//     while locked, else to Drive.

#include <cstdint>
#include <string>
#include <vector>

namespace golden {

enum class ScreenId : std::uint8_t { BOOT, LOCKED, DRIVE, SEAT, JOYSTICK };
// MIB::MibSystemState's values (mib_message.hpp), plus an out-of-range one.
enum class Mib : std::uint8_t { INITIALIZING = 0, IDLE = 1, ENABLED = 2, ERROR = 3, BOGUS = 9 };
// MIB::DriveProfile's values.
enum class Profile : std::uint8_t { LOW = 0, NORMAL = 1, HIGH = 2 };

struct World {
  std::int64_t now = 1'000'000;
  bool link = false;
  Mib mib = Mib::INITIALIZING;
  ScreenId screen = ScreenId::LOCKED;
  bool pending = false;
  ScreenId pending_screen = ScreenId::LOCKED;
  bool menu_open = false;
  bool menu_on_arrival = false;
  bool locked = true;
  bool lock_waiting = false;
  bool unlock_timer = false;
  bool gate = false;
  std::int32_t banner = 0;
  std::uint32_t banner_ms = 0;
  Profile profile = Profile::NORMAL;
  // The filtered log's conventions (hazard-c1-spec.md §5.2): a fixed clock (a read does not
  // advance it), and the profile written into P(...) once the scenario has picked one.
  bool clock_fixed = false;
  bool profile_picked = false;
  // C1's sample: the link as rtps_comms_link_state reads it now (the subject is `link`;
  // both move together unless a scenario splits them), a calibration running, the stick's
  // hold reason (hmi::stick::HoldReason's value), and the notice last shown.
  bool live_link = false;
  bool calibrating = false;
  // C3: the POST gate (hmi::stick::PostGate's value; PASS unless a scenario says otherwise).
  std::uint8_t post_gate = 2;
  // The next DriveCommand publish blocks this long (the clock moves by it), then 0 again.
  std::int64_t publish_delay_us = 0;
  std::uint8_t hold = 0;
  std::string notice = "NONE";
  // The adapter's logged errors (the safe state's report), counted.
  unsigned errors = 0;
};

World &world();
void reset_world();

// The two logs.
std::vector<std::string> &port_log();
std::vector<std::string> &raw_log();
void port(const std::string &line);
void raw(const std::string &line);
void both(const std::string &line); // a script step or a snapshot: in both logs
void clear_logs();

// The filtered log (hazard-c1-spec.md §5.2): only what the hand-written C1/C3 goldens compare,
// one token per call: P(D|E[,profile]) publish, L Locked screen, Dv Drive screen, B:x banner,
// N:x Drive notice, lock(0|1), gate, ring_rest, ring_wait, open, menu(0|1) (menu on arrival).
// Script steps and snapshots are not in it.
std::vector<std::string> &filtered_log();
void filtered(const std::string &token);

// Names, for the logs.
const char *screen_name(ScreenId s);
const char *mib_name(Mib m);
const char *profile_name(Profile p);
std::string snapshot();

// The model's operations (see the header comment).
std::int64_t read_clock();
void arrive(ScreenId s);       // nav_arrive on a screen
void load_instant(ScreenId s); // an instant load: arrive inside it
void load_faded(ScreenId s);   // a faded load: pending until FRAME
void frame();                  // a pending faded load lands
void nav_home();               // nav_home()
void gate_update();            // nav_update_stick_gate(): the gate from lock, screen, menu
void user_screen(ScreenId s);  // the user navigates (instant, menu closed)
void user_menu(bool open);     // the user opens or closes the menu (gate re-evaluated)

} // namespace golden
