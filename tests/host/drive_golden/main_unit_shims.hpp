#pragma once
// What the drive code (hmi_ui's DrivePort, drive_port.hpp, moved from main/frag_drive.inc)
// needs from the rest of the firmware, LVGL, rtps_comms and esp_timer, as recording shims over
// the stateful world (world.hpp), and MainUnitUi, the Ui the port is instantiated over here:
// each of its methods is the shim of the main-unit name the code called before the move.
// Included first by main_unit.cpp; the headers drive_port.hpp includes (lvgl.h, ui.h,
// esp_timer.h, messages/*.hpp) are shim/ stand-ins that include this file.
//
// Every call the port makes across this boundary appends one line to the boundary log
// (golden 2), with its arguments and what it returned. A call that stands for a DrivePort
// method also appends that method to the port log (golden 1):
//   esp_timer_get_time                      -> now_us
//   lv_subject_get_int(rtps_link_subject)   -> sample (the first of drive_env's four reads)
//   rtps_comms_publish_drive(r, profile)    -> publish(r)
//   lock_visual_wait / lock_visual_rest     -> ring_wait / ring_rest
//   lv_anim_delete(ui_LockRing, ...)        -> lock_open_visual (its first call)
//   unlock_timer_start / unlock_timer_cancel, `unlock_advance_timer = nullptr`
//                                           -> unlock_timer_start / _cancel / _forget
//   lv_subject_set_int(locked_subject, v)   -> set_locked(v)
//   nav_update_stick_gate                   -> gate_update
//   `nav_menu_on_arrival = v`               -> menu_on_arrival(v)
//   locked_screen_go / nav_home             -> go_locked_screen / nav_home
//   _ui_screen_change(ui_DriveScreen, FADE) -> go_drive_screen
//   lv_subject_set_int(entry_refused_subject, which) -> show_banner(<which>)
//   refusal_feedback                        -> refusal_feedback
// The variables the port writes (nav_menu_on_arrival, lock_waiting, unlock_advance_timer)
// and reads (nav_menu_open) are proxies, so their writes and reads are logged too.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

#include "drive_session.hpp"
#include "logger.hpp"
#include "world.hpp"

// --- types from rammp_rtps_messages, rtps_comms.hpp and espp, values as there ---------------
namespace rammp {
enum class DriveRequest : std::uint8_t { DISABLE = 0, ENABLE = 1 };
} // namespace rammp
namespace MIB {
enum class MibSystemState : std::uint8_t { INITIALIZING, IDLE, ENABLED, ERROR };
enum class DriveProfile : std::uint8_t { LOW, NORMAL, HIGH };
} // namespace MIB
enum class RtpsLinkState { NET_FAILED, LINK_DOWN, NO_IP, NO_PEER, CONNECTED };
namespace espp {
struct Drv2605 {
  enum class Waveform : std::uint8_t { STRONG_CLICK = 1 };
};
} // namespace espp

// --- LVGL and the SquareLine export ---------------------------------------------------------
struct lv_obj_t {
  const char *name;
};
struct lv_subject_t {
  const char *name;
};
struct lv_timer_t {
  const char *name;
};
struct lv_anim_t; // hmi_ui/hold_gesture.hpp (HOLD_MAX) names it; the port never uses one
using lv_anim_exec_xcb_t = void (*)(void *, std::int32_t);
enum lv_screen_load_anim_t { LV_SCREEN_LOAD_ANIM_NONE = 0, LV_SCREEN_LOAD_ANIM_FADE_ON = 9 };

namespace shim {
inline lv_obj_t boot_screen{"ui_BootScreen"};
inline lv_obj_t locked_screen{"ui_LockedScreen"};
inline lv_obj_t drive_screen{"ui_DriveScreen"};
inline lv_obj_t seat_screen{"ui_SeatScreen"};
inline lv_obj_t joystick_screen{"ui_JoystickScreen"};
inline lv_obj_t lock_ring{"ui_LockRing"};
inline lv_obj_t shackle{"ui_Shackle"};
inline lv_timer_t refused_timer{"entry_refused_timer"};

inline const char *name_of(const void *p) {
  return p == nullptr ? "null" : static_cast<const lv_obj_t *>(p)->name;
}
inline const char *request_name(rammp::DriveRequest r) {
  return r == rammp::DriveRequest::ENABLE ? "ENABLE" : "DISABLE";
}
inline const char *profile_name(MIB::DriveProfile p) {
  return golden::profile_name(static_cast<golden::Profile>(p));
}
inline const char *banner_name(std::int32_t which) {
  switch (which) {
  case 1:
    return "REFUSED_DRIVE";
  case 2:
    return "REFUSED_SEAT";
  case 3:
    return "NOT_GRANTED";
  case 4:
    return "DRIVE_STOPPED";
  case 5:
    return "EXIT_REFUSED";
  case 6:
    return "DRIVE_LOST";
  case 7:
    return "REFUSED_DRIVE_MENU";
  default:
    return "?";
  }
}
inline lv_obj_t *screen_obj(golden::ScreenId s) {
  switch (s) {
  case golden::ScreenId::BOOT:
    return &boot_screen;
  case golden::ScreenId::LOCKED:
    return &locked_screen;
  case golden::ScreenId::DRIVE:
    return &drive_screen;
  case golden::ScreenId::SEAT:
    return &seat_screen;
  case golden::ScreenId::JOYSTICK:
    return &joystick_screen;
  }
  return &joystick_screen;
}
} // namespace shim

inline lv_obj_t *ui_BootScreen = &shim::boot_screen;
inline lv_obj_t *ui_LockedScreen = &shim::locked_screen;
inline lv_obj_t *ui_DriveScreen = &shim::drive_screen;
inline lv_obj_t *ui_SeatScreen = &shim::seat_screen;
inline lv_obj_t *ui_LockRing = &shim::lock_ring;
inline lv_obj_t *ui_Shackle = &shim::shackle;
inline void ui_DriveScreen_screen_init() {}

inline lv_subject_t rtps_link_subject{"rtps_link_subject"};
inline lv_subject_t mib_state_subject{"mib_state_subject"};
inline lv_subject_t locked_subject{"locked_subject"};
inline lv_subject_t entry_refused_subject{"entry_refused_subject"};
// main's RefusalView owns the dwell timer since K1 step 3.2; frag_drive's entry_refused_show
// re-arms it through refusal_view.timer(). Same timer object (and name) as before.
namespace shim {
struct RefusalViewShim {
  [[nodiscard]] lv_timer_t *timer() const { return &refused_timer; }
};
} // namespace shim
inline shim::RefusalViewShim refusal_view;

inline std::int32_t lv_subject_get_int(lv_subject_t *subject) {
  const golden::World &w = golden::world();
  std::int32_t v = 0;
  if (subject == &rtps_link_subject) {
    golden::port(std::format("sample -> link={} mib={} screen={} menu={}", int{w.link},
                             golden::mib_name(w.mib), golden::screen_name(w.screen),
                             int{w.menu_open}));
    v = static_cast<std::int32_t>(w.link ? RtpsLinkState::CONNECTED : RtpsLinkState::NO_PEER);
  } else if (subject == &mib_state_subject) {
    v = static_cast<std::int32_t>(w.mib);
  } else if (subject == &locked_subject) {
    v = w.locked ? 1 : 0;
  } else if (subject == &entry_refused_subject) {
    v = w.banner;
  }
  golden::raw(std::format("lv_subject_get_int({}) -> {}", subject->name, v));
  return v;
}

inline void lv_subject_set_int(lv_subject_t *subject, std::int32_t value) {
  golden::raw(std::format("lv_subject_set_int({}, {})", subject->name, value));
  golden::World &w = golden::world();
  if (subject == &locked_subject) {
    golden::port(std::format("set_locked({})", int{value != 0}));
    w.locked = value != 0;
  } else if (subject == &entry_refused_subject) {
    golden::port(std::format("show_banner({})", shim::banner_name(value)));
    w.banner = value;
  }
}

inline lv_obj_t *lv_screen_active() {
  lv_obj_t *s = shim::screen_obj(golden::world().screen);
  golden::raw(std::format("lv_screen_active -> {}", s->name));
  return s;
}

inline void lv_timer_set_period(lv_timer_t *timer, std::uint32_t period) {
  golden::raw(std::format("lv_timer_set_period({}, {})", timer->name, period));
  golden::world().banner_ms = period;
}
inline void lv_timer_reset(lv_timer_t *timer) {
  golden::raw(std::format("lv_timer_reset({})", timer->name));
}
inline void lv_timer_resume(lv_timer_t *timer) {
  golden::raw(std::format("lv_timer_resume({})", timer->name));
}

inline void ring_spin_cb(void *, std::int32_t) {}
inline bool lv_anim_delete(const void *var, lv_anim_exec_xcb_t exec_cb) {
  golden::raw(std::format("lv_anim_delete({}, {})", shim::name_of(var),
                          exec_cb == &ring_spin_cb ? "ring_spin_cb" : "?"));
  golden::port("lock_open_visual");
  return true;
}
inline void lv_arc_set_rotation(lv_obj_t *obj, std::int32_t rotation) {
  golden::raw(std::format("lv_arc_set_rotation({}, {})", obj->name, rotation));
}
inline void lv_arc_set_value(lv_obj_t *obj, std::int32_t value) {
  golden::raw(std::format("lv_arc_set_value({}, {})", obj->name, value));
}
inline void lv_obj_set_y(lv_obj_t *obj, std::int32_t y) {
  golden::raw(std::format("lv_obj_set_y({}, {})", obj->name, y));
}
inline void lv_obj_set_height(lv_obj_t *obj, std::int32_t h) {
  golden::raw(std::format("lv_obj_set_height({}, {})", obj->name, h));
}

inline void _ui_screen_change(lv_obj_t **target, lv_screen_load_anim_t fademode, int spd, int delay,
                              void (*target_init)()) {
  const bool fade = fademode == LV_SCREEN_LOAD_ANIM_FADE_ON;
  golden::raw(std::format("_ui_screen_change({}, {}, {}, {}, {})", (*target)->name,
                          fade ? "FADE_ON" : "NONE", spd, delay,
                          target_init == &ui_DriveScreen_screen_init ? "init" : "?"));
  const golden::ScreenId s =
      *target == ui_DriveScreen ? golden::ScreenId::DRIVE : golden::ScreenId::LOCKED;
  if (*target == ui_DriveScreen && fade) {
    golden::port("go_drive_screen");
  }
  if (fade) {
    golden::load_faded(s);
  } else {
    golden::load_instant(s);
  }
}

// --- esp_timer, rtps_comms ------------------------------------------------------------------
inline std::int64_t esp_timer_get_time() {
  const std::int64_t t = golden::read_clock();
  golden::raw(std::format("esp_timer_get_time -> {}", t));
  golden::port(std::format("now_us -> {}", t));
  return t;
}

inline std::atomic<MIB::DriveProfile> drive_profile_published{MIB::DriveProfile::NORMAL};

inline bool rtps_comms_publish_drive(rammp::DriveRequest request, MIB::DriveProfile profile) {
  golden::raw(std::format("rtps_comms_publish_drive({}, {})", shim::request_name(request),
                          shim::profile_name(profile)));
  golden::port(std::format("publish({})", shim::request_name(request)));
  return true;
}

// --- the rest of the main.cpp unit ------------------------------------------------------------
// The constants the port uses (HOLD_MAX, SHACKLE_RISE_PX, UNLOCK_DISSOLVE_MS, REFUSED_*, the
// dwells) come from hmi_ui's own headers, as in the firmware.
// frag_refusal.inc: kMibStatusPeriod + 250 ms, kMibStatusTimeout (hmi_rtps_spec.hpp).
inline constexpr std::int64_t kDriveAnswerUs = 750'000;
inline constexpr std::int64_t kDriveWaitUs = 2'000'000;
static_assert(kDriveAnswerUs ==
              std::chrono::microseconds{hmi::drive_session::kDriveAnswer}.count());
static_assert(kDriveWaitUs == std::chrono::microseconds{hmi::drive_session::kDriveWait}.count());

inline std::int32_t shackle_rest_y = 412;
inline std::int32_t shackle_rest_h = 103;

namespace shim {
// `nav_menu_open != nullptr` reads the world's menu.
struct MenuOpenProxy {
  friend bool operator==(const MenuOpenProxy &, std::nullptr_t) {
    const bool open = golden::world().menu_open;
    golden::raw(std::format("nav_menu_open -> {}", open ? "overlay" : "null"));
    return !open;
  }
};
// `nav_menu_on_arrival = v`
struct MenuOnArrivalProxy {
  MenuOnArrivalProxy &operator=(bool v) {
    golden::raw(std::format("nav_menu_on_arrival = {}", int{v}));
    golden::port(std::format("menu_on_arrival({})", int{v}));
    golden::world().menu_on_arrival = v;
    return *this;
  }
};
// `lock_waiting = v`
struct LockWaitingProxy {
  LockWaitingProxy &operator=(bool v) {
    golden::raw(std::format("lock_waiting = {}", int{v}));
    golden::world().lock_waiting = v;
    return *this;
  }
};
// `unlock_advance_timer = nullptr`
struct UnlockTimerProxy {
  UnlockTimerProxy &operator=(std::nullptr_t) {
    golden::raw("unlock_advance_timer = nullptr");
    golden::port("unlock_timer_forget");
    golden::world().unlock_timer = false;
    return *this;
  }
};
} // namespace shim

inline shim::MenuOpenProxy nav_menu_open;
inline shim::MenuOnArrivalProxy nav_menu_on_arrival;
inline shim::LockWaitingProxy lock_waiting;
inline shim::UnlockTimerProxy unlock_advance_timer;

inline void lock_visual_wait() {
  golden::raw("lock_visual_wait");
  golden::port("ring_wait");
  golden::world().lock_waiting = true;
}
inline void lock_visual_rest() {
  golden::raw("lock_visual_rest");
  golden::port("ring_rest");
  golden::world().lock_waiting = false;
}
inline void unlock_timer_cancel() {
  golden::raw("unlock_timer_cancel");
  golden::port("unlock_timer_cancel");
  golden::world().unlock_timer = false;
}
inline void unlock_timer_start() {
  golden::raw("unlock_timer_start");
  golden::port("unlock_timer_start");
  golden::world().unlock_timer = true;
}
inline void nav_update_stick_gate() {
  golden::gate_update();
  golden::raw(std::format("nav_update_stick_gate -> gate={}", int{golden::world().gate}));
  golden::port(std::format("gate_update -> gate={}", int{golden::world().gate}));
}
inline void locked_screen_go() {
  golden::raw("locked_screen_go");
  golden::port("go_locked_screen");
  golden::load_instant(golden::ScreenId::LOCKED);
}
inline void nav_home() {
  golden::raw("nav_home");
  golden::port("nav_home");
  golden::nav_home();
}
inline void refusal_feedback() {
  golden::raw("refusal_feedback");
  golden::port("refusal_feedback");
}
inline bool haptic_play(espp::Drv2605::Waveform w, std::uint8_t slots) {
  golden::raw(std::format("haptic_play({}, {})",
                          w == espp::Drv2605::Waveform::STRONG_CLICK ? "STRONG_CLICK" : "?",
                          slots));
  return true;
}

// The Ui DrivePort is instantiated over (the firmware's is hmi::ui::DriveUi): each method is
// the shim above of the name the drive code used in the main.cpp unit, so the boundary log
// reads as it did.
struct MainUnitUi {
  lv_subject_t *rtps_link_subject() const { return &::rtps_link_subject; }
  lv_subject_t *mib_state_subject() const { return &::mib_state_subject; }
  lv_subject_t *locked_subject() const { return &::locked_subject; }
  lv_subject_t *entry_refused_subject() const { return &::entry_refused_subject; }
  lv_timer_t *refused_timer() const { return refusal_view.timer(); }
  shim::MenuOpenProxy &nav_menu_open() const { return ::nav_menu_open; }
  shim::MenuOnArrivalProxy &nav_menu_on_arrival() const { return ::nav_menu_on_arrival; }
  shim::LockWaitingProxy &lock_waiting() const { return ::lock_waiting; }
  shim::UnlockTimerProxy &unlock_advance_timer() const { return ::unlock_advance_timer; }
  std::int32_t shackle_rest_y() const { return ::shackle_rest_y; }
  std::int32_t shackle_rest_h() const { return ::shackle_rest_h; }
  MIB::DriveProfile drive_profile() const { return drive_profile_published.load(); }
  bool publish_drive(rammp::DriveRequest request, MIB::DriveProfile profile) const {
    return rtps_comms_publish_drive(request, profile);
  }
  void lock_visual_wait() const { ::lock_visual_wait(); }
  void lock_visual_rest() const { ::lock_visual_rest(); }
  void unlock_timer_start() const { ::unlock_timer_start(); }
  void unlock_timer_cancel() const { ::unlock_timer_cancel(); }
  void locked_screen_go() const { ::locked_screen_go(); }
  void update_stick_gate() const { ::nav_update_stick_gate(); }
  void nav_home() const { ::nav_home(); }
  void refusal_feedback() const { ::refusal_feedback(); }
  void haptic_click() const { (void)haptic_play(espp::Drv2605::Waveform::STRONG_CLICK, 1); }
  static constexpr lv_anim_exec_xcb_t ring_spin_cb = &::ring_spin_cb;
};
