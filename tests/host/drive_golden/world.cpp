// The stateful world of the drive goldens: see world.hpp.

#include "world.hpp"

#include <format>

#include "drive_session_table.hpp"

namespace golden {

namespace {
World g_world;
std::vector<std::string> g_port;
std::vector<std::string> g_raw;

hmi::drive_session::Screen session_screen(ScreenId s) {
  using hmi::drive_session::Screen;
  switch (s) {
  case ScreenId::BOOT:
    return Screen::BOOT;
  case ScreenId::LOCKED:
    return Screen::LOCKED;
  case ScreenId::DRIVE:
    return Screen::DRIVE;
  case ScreenId::SEAT:
    return Screen::SEAT;
  case ScreenId::JOYSTICK:
    return Screen::OTHER;
  }
  return Screen::OTHER;
}
} // namespace

World &world() { return g_world; }
void reset_world() { g_world = World{}; }

std::vector<std::string> &port_log() { return g_port; }
std::vector<std::string> &raw_log() { return g_raw; }
void port(const std::string &line) { g_port.push_back("  " + line); }
void raw(const std::string &line) { g_raw.push_back("  " + line); }
void both(const std::string &line) {
  g_port.push_back(line);
  g_raw.push_back(line);
}
void clear_logs() {
  g_port.clear();
  g_raw.clear();
}

const char *screen_name(ScreenId s) {
  switch (s) {
  case ScreenId::BOOT:
    return "BOOT";
  case ScreenId::LOCKED:
    return "LOCKED";
  case ScreenId::DRIVE:
    return "DRIVE";
  case ScreenId::SEAT:
    return "SEAT";
  case ScreenId::JOYSTICK:
    return "JOYSTICK";
  }
  return "?";
}

const char *mib_name(Mib m) {
  switch (m) {
  case Mib::INITIALIZING:
    return "INITIALIZING";
  case Mib::IDLE:
    return "IDLE";
  case Mib::ENABLED:
    return "ENABLED";
  case Mib::ERROR:
    return "ERROR";
  case Mib::BOGUS:
    return "BOGUS";
  }
  return "?";
}

const char *profile_name(Profile p) {
  switch (p) {
  case Profile::LOW:
    return "LOW";
  case Profile::NORMAL:
    return "NORMAL";
  case Profile::HIGH:
    return "HIGH";
  }
  return "?";
}

std::string snapshot() {
  const World &w = g_world;
  return std::format("  = t={} link={} mib={} screen={}{}{} menu={} moa={} locked={} wait={} "
                     "timer={} gate={} banner={}/{}",
                     w.now, int{w.link}, mib_name(w.mib), screen_name(w.screen),
                     w.pending ? "->" : "", w.pending ? screen_name(w.pending_screen) : "",
                     int{w.menu_open}, int{w.menu_on_arrival}, int{w.locked}, int{w.lock_waiting},
                     int{w.unlock_timer}, int{w.gate}, w.banner, w.banner_ms);
}

std::int64_t read_clock() { return g_world.now++; }

void gate_update() {
  g_world.gate = hmi::drive_session::stick_drives(g_world.locked, session_screen(g_world.screen),
                                                  g_world.menu_open);
}

void arrive(ScreenId s) {
  g_world.screen = s;
  g_world.menu_open = false;
  if (g_world.menu_on_arrival) {
    g_world.menu_on_arrival = false;
    g_world.menu_open = true;
  }
  gate_update();
}

void load_instant(ScreenId s) {
  g_world.pending = false; // an instant load replaces a fade still in flight
  arrive(s);
}

void load_faded(ScreenId s) {
  g_world.pending = true;
  g_world.pending_screen = s;
}

void frame() {
  if (g_world.pending) {
    g_world.pending = false;
    arrive(g_world.pending_screen);
  }
}

void nav_home() {
  g_world.menu_open = false;
  gate_update();
  load_faded(g_world.locked ? ScreenId::LOCKED : ScreenId::DRIVE);
}

void user_screen(ScreenId s) {
  g_world.menu_open = false;
  load_instant(s);
}

void user_menu(bool open) {
  g_world.menu_open = open;
  gate_update();
}

} // namespace golden
