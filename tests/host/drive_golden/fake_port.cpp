// DriveAdapter over a stateful fake port: golden 1 checked at the port itself (V5), with no
// LVGL, no shims and nothing from main/. FakeDriveView records each port method in the port
// log and does to the world (world.hpp) what the firmware's MainDriveView's calls do, so the
// screen, menu and lock change on go_locked_screen, set_locked and menu_on_arrival and a sample
// taken after them reads the change.

#include <cstddef>
#include <format>
#include <memory>

#include "drive_adapter.hpp"
#include "scripts.hpp"
#include "world.hpp"

#include "adapter_peer.hpp"

namespace golden {

namespace {

namespace ds = hmi::drive_session;
using hmi::drive_adapter::DriveBanner;

ds::MibState session_mib(Mib m) {
  switch (m) {
  case Mib::INITIALIZING:
    return ds::MibState::INITIALIZING;
  case Mib::IDLE:
    return ds::MibState::IDLE;
  case Mib::ENABLED:
    return ds::MibState::ENABLED;
  case Mib::ERROR:
  case Mib::BOGUS:
    return ds::MibState::OTHER;
  }
  return ds::MibState::OTHER;
}

ds::Screen session_screen(ScreenId s) {
  switch (s) {
  case ScreenId::BOOT:
    return ds::Screen::BOOT;
  case ScreenId::LOCKED:
    return ds::Screen::LOCKED;
  case ScreenId::DRIVE:
    return ds::Screen::DRIVE;
  case ScreenId::SEAT:
    return ds::Screen::SEAT;
  case ScreenId::JOYSTICK:
    return ds::Screen::OTHER;
  }
  return ds::Screen::OTHER;
}

struct BannerInfo {
  const char *name;
  std::int32_t which; // kRefused* (frag_refusal.inc)
  std::uint32_t show_ms;
};
constexpr std::array<BannerInfo, 7> kBanners{{
    {"REFUSED_DRIVE", 1, 3000},
    {"REFUSED_SEAT", 2, 3000},
    {"NOT_GRANTED", 3, 3000},
    {"DRIVE_STOPPED", 4, 3000},
    {"EXIT_REFUSED", 5, 2000},
    {"DRIVE_LOST", 6, 3000},
    {"REFUSED_DRIVE_MENU", 7, 3000},
}};

struct FakeDriveView {
  hmi::drive_adapter::DriveSample sample() const {
    const World &w = world();
    port(std::format("sample -> link={} mib={} screen={} menu={}", int{w.link}, mib_name(w.mib),
                     screen_name(w.screen), int{w.menu_open}));
    return {w.link, session_mib(w.mib), session_screen(w.screen), w.menu_open};
  }
  std::int64_t now_us() const {
    const std::int64_t t = read_clock();
    port(std::format("now_us -> {}", t));
    return t;
  }
  bool publish(bool enable) const {
    port(std::format("publish({})", enable ? "ENABLE" : "DISABLE"));
    return true;
  }
  void ring_wait() const {
    port("ring_wait");
    world().lock_waiting = true;
  }
  void ring_rest() const {
    port("ring_rest");
    world().lock_waiting = false;
  }
  void lock_open_visual() const {
    world().lock_waiting = false;
    port("lock_open_visual");
  }
  void unlock_timer_start() const {
    port("unlock_timer_start");
    world().unlock_timer = true;
  }
  void unlock_timer_cancel() const {
    port("unlock_timer_cancel");
    world().unlock_timer = false;
  }
  void unlock_timer_forget() const {
    port("unlock_timer_forget");
    world().unlock_timer = false;
  }
  void set_locked(bool locked) const {
    port(std::format("set_locked({})", int{locked}));
    world().locked = locked;
  }
  void gate_update() const {
    golden::gate_update();
    port(std::format("gate_update -> gate={}", int{world().gate}));
  }
  void menu_on_arrival(bool open) const {
    port(std::format("menu_on_arrival({})", int{open}));
    world().menu_on_arrival = open;
  }
  void go_locked_screen() const {
    port("go_locked_screen");
    load_instant(ScreenId::LOCKED);
  }
  void go_drive_screen() const {
    port("go_drive_screen");
    load_faded(ScreenId::DRIVE);
  }
  void nav_home() const {
    port("nav_home");
    golden::nav_home();
  }
  void show_banner(DriveBanner banner) const {
    const BannerInfo &b = kBanners.at(static_cast<std::size_t>(banner));
    port(std::format("show_banner({})", b.name));
    world().banner = b.which;
    world().banner_ms = b.show_ms;
  }
  void refusal_feedback() const { port("refusal_feedback"); }
};
static_assert(hmi::drive_adapter::DrivePort<FakeDriveView>);

using Adapter = hmi::drive_adapter::DriveAdapter<FakeDriveView>;
Adapter g_adapter{{.view = FakeDriveView{}}};

void reset() {
  std::destroy_at(&g_adapter);
  std::construct_at(&g_adapter, Adapter::Config{.view = FakeDriveView{}});
}
bool input(ds::Input in) { return g_adapter.input(in); }
void tick() { g_adapter.tick(); }
void unlock_hold_done() { g_adapter.unlock_hold_done(); }
void exit_hold_done() { g_adapter.exit_hold_done(); }
void set_profile(Profile) {} // the port's publish carries no profile
Deadlines deadlines() { return hmi::drive_adapter::DriveAdapterTestPeer::deadlines(g_adapter); }

} // namespace

Target fake_port_target() {
  return Target{&reset,          &input,       &tick,     &unlock_hold_done,
                &exit_hold_done, &set_profile, &deadlines};
}

} // namespace golden
