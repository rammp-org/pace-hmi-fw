// L1 host app for components/drive_adapter: the adapter's own contract (DAD-001..005,
// DAD-007..011). Its behaviour at the port is pinned by the drive goldens
// (tests/host/drive_golden, GLD-101..116). DAD-006 (TABLE.md U3) is removed: the exit hold while
// locked is now the table's rows 42-43 (hazard-c1-spec.md E8).

#include "drive_adapter.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include "test_case.hpp"

namespace hmi::drive_adapter {
// Test-only access (declared a friend in drive_adapter.hpp).
struct DriveAdapterTestPeer {
  template <class View> static std::int64_t stop_first_us(const DriveAdapter<View> &a) {
    return a.stop_first_us_;
  }
  template <class View> static std::int64_t last_fail_log_us(const DriveAdapter<View> &a) {
    return a.last_fail_log_us_;
  }
  template <class View>
  static hmi::drive_session::Env env(DriveAdapter<View> &a, std::int64_t now) {
    return a.env(now);
  }
};
} // namespace hmi::drive_adapter

namespace {

namespace da = hmi::drive_adapter;
namespace ds = hmi::drive_session;
using da::DriveAdapterTestPeer;
using da::DriveBanner;
using da::DriveNotice;
using ds::Input;
using hmi::stick::HoldReason;

enum class Call : std::uint8_t {
  SAMPLE,
  NOW,
  PUBLISH_ENABLE,
  PUBLISH_DISABLE,
  RING_WAIT,
  RING_REST,
  LOCK_OPEN_VISUAL,
  TIMER_START,
  TIMER_CANCEL,
  TIMER_FORGET,
  SET_LOCKED,
  SET_UNLOCKED,
  GATE_UPDATE,
  MENU_ON_ARRIVAL,
  NO_MENU_ON_ARRIVAL,
  GO_LOCKED,
  GO_DRIVE,
  NAV_HOME,
  BANNER,
  NOTICE,
  REFUSAL_FEEDBACK,
};

// What the fake port sees and answers. Fixed-size: nothing is allocated, so a failed assert
// (which longjmps) leaks nothing.
struct Port {
  std::array<Call, 512> calls{};
  std::size_t count = 0;
  std::int64_t now = 1'000'000;
  da::DriveSample sample{.link_connected = true,
                         .mib = ds::MibState::IDLE,
                         .screen = ds::Screen::LOCKED,
                         .menu_open = false,
                         .calibrating = false,
                         .hold = HoldReason::NONE,
                         .post_ok = true};
  DriveBanner last_banner = DriveBanner::REFUSED_DRIVE;
  std::size_t banners = 0;
  std::array<DriveNotice, 64> notices{};
  std::size_t notice_count = 0;
  bool publish_result = true;
  // A port method that calls back into the adapter (DAD-002).
  bool (*on_gate_update)() = nullptr;
  bool reentry_result = true;

  void add(Call c) {
    if (count < calls.size()) {
      calls[count] = c;
    }
    ++count;
  }
  [[nodiscard]] std::size_t count_of(Call c) const {
    std::size_t n = 0;
    for (std::size_t i = 0; i < count && i < calls.size(); ++i) {
      n += calls[i] == c ? 1U : 0U;
    }
    return n;
  }
  [[nodiscard]] std::size_t publishes() const {
    return count_of(Call::PUBLISH_ENABLE) + count_of(Call::PUBLISH_DISABLE);
  }
};

Port g_port;

struct FakeView {
  [[nodiscard]] da::DriveSample sample() const {
    g_port.add(Call::SAMPLE);
    return g_port.sample;
  }
  [[nodiscard]] std::int64_t now_us() const {
    g_port.add(Call::NOW);
    return g_port.now;
  }
  bool publish(bool enable) const {
    g_port.add(enable ? Call::PUBLISH_ENABLE : Call::PUBLISH_DISABLE);
    return g_port.publish_result;
  }
  void ring_wait() const { g_port.add(Call::RING_WAIT); }
  void ring_rest() const { g_port.add(Call::RING_REST); }
  void lock_open_visual() const { g_port.add(Call::LOCK_OPEN_VISUAL); }
  void unlock_timer_start() const { g_port.add(Call::TIMER_START); }
  void unlock_timer_cancel() const { g_port.add(Call::TIMER_CANCEL); }
  void unlock_timer_forget() const { g_port.add(Call::TIMER_FORGET); }
  void set_locked(bool locked) const { g_port.add(locked ? Call::SET_LOCKED : Call::SET_UNLOCKED); }
  void gate_update() const {
    g_port.add(Call::GATE_UPDATE);
    if (g_port.on_gate_update != nullptr) {
      g_port.reentry_result = g_port.on_gate_update();
    }
  }
  void menu_on_arrival(bool open) const {
    g_port.add(open ? Call::MENU_ON_ARRIVAL : Call::NO_MENU_ON_ARRIVAL);
  }
  void go_locked_screen() const { g_port.add(Call::GO_LOCKED); }
  void go_drive_screen() const { g_port.add(Call::GO_DRIVE); }
  void nav_home() const { g_port.add(Call::NAV_HOME); }
  void show_banner(DriveBanner banner) const {
    g_port.add(Call::BANNER);
    g_port.last_banner = banner;
    ++g_port.banners;
  }
  void show_notice(DriveNotice notice) const {
    g_port.add(Call::NOTICE);
    if (g_port.notice_count < g_port.notices.size()) {
      g_port.notices[g_port.notice_count] = notice;
    }
    ++g_port.notice_count;
  }
  void refusal_feedback() const { g_port.add(Call::REFUSAL_FEEDBACK); }
};
static_assert(da::DrivePort<FakeView>);

// A view without refusal_feedback is not a port; nor is one without show_notice.
struct NotAPort {
  da::DriveSample sample() const { return {}; }
  std::int64_t now_us() const { return 0; }
};
static_assert(!da::DrivePort<NotAPort>);
static_assert(!da::DrivePort<int>);

using Adapter = da::DriveAdapter<FakeView>;
Adapter *g_adapter = nullptr;

void fresh_port() { g_port = Port{}; }

constexpr std::int64_t kMsUs = 1000;

// Booted: the first CONNECTED tick's boot DISABLE (C3, row 50), then the port's record cleared
// (the "booted" preamble of hazard-c3-spec.md F1).
void boot(Adapter &adapter) {
  adapter.tick();
  g_port.count = 0;
}

// DRIVING: booted, the MIB enables (row 1), the advance fires (row 35), the Drive screen is up.
void drive(Adapter &adapter) {
  boot(adapter);
  g_port.sample.mib = ds::MibState::ENABLED;
  adapter.tick();
  (void)adapter.input(Input::UNLOCK_TIMER);
  g_port.sample.screen = ds::Screen::DRIVE;
}

} // namespace

TEST_CASE("DAD-001 a new adapter is locked and calls nothing until an input arrives",
          "[drive_adapter]") {
  fresh_port();
  const Adapter adapter{{.view = FakeView{}}};
  TEST_ASSERT_TRUE(adapter.locked());
  TEST_ASSERT_EQUAL_UINT(0, g_port.count);
}

TEST_CASE("DAD-002 an input re-entered from a port method is dropped and the one in progress "
          "completes",
          "[drive_adapter]") {
  fresh_port();
  Adapter adapter{{.view = FakeView{}}};
  g_adapter = &adapter;
  boot(adapter);
  g_port.sample.mib = ds::MibState::ENABLED; // row 1: LOCKED + DRIVING_OK -> UNLOCKING (F1)
  g_port.on_gate_update = [] { return g_adapter->input(Input::PROFILE_CLICK); };
  adapter.tick();
  g_adapter = nullptr;
  TEST_ASSERT_FALSE(g_port.reentry_result);                       // the nested input was refused
  TEST_ASSERT_EQUAL_UINT(0, g_port.publishes());                  // and did nothing
  TEST_ASSERT_EQUAL_UINT(1, g_port.count_of(Call::SET_UNLOCKED)); // the outer one completed
  TEST_ASSERT_FALSE(adapter.locked());
  // The adapter is usable again afterwards: a profile tap with the MCB ENABLED (row 31).
  g_port.on_gate_update = nullptr;
  TEST_ASSERT_TRUE(adapter.input(Input::PROFILE_CLICK));
  TEST_ASSERT_EQUAL_UINT(1, g_port.publishes());
}

TEST_CASE("DAD-003 the default windows are the drive table's kDriveAnswer and kDriveWait",
          "[drive_adapter]") {
  TEST_ASSERT_EQUAL_INT64(750'000, da::kDefaultAnswerUs);
  TEST_ASSERT_EQUAL_INT64(2'000'000, da::kDefaultWaitUs);
  const Adapter::Config config{.view = FakeView{}};
  TEST_ASSERT_EQUAL_INT64(da::kDefaultAnswerUs, config.answer_us);
  TEST_ASSERT_EQUAL_INT64(da::kDefaultWaitUs, config.wait_us);
}

TEST_CASE("DAD-004 the warn deadline counts from the ask: NOT_GRANTED once now reaches ask plus "
          "the answer window",
          "[drive_adapter]") {
  fresh_port();
  Adapter adapter{{.view = FakeView{}, .answer_us = 1000, .wait_us = 5000}};
  adapter.unlock_hold_done(); // row 18: SEND_ENABLE at now = 1'000'000, ARM_WARN, ARM_GIVEUP
  TEST_ASSERT_EQUAL_UINT(1, g_port.count_of(Call::PUBLISH_ENABLE));
  g_port.now = 1'000'999;
  adapter.tick();
  TEST_ASSERT_EQUAL_UINT(0, g_port.banners);
  g_port.now = 1'001'000;
  adapter.tick(); // row 15
  TEST_ASSERT_EQUAL_UINT(1, g_port.banners);
  TEST_ASSERT_TRUE(g_port.last_banner == DriveBanner::NOT_GRANTED);
  g_port.now = 1'005'000;
  adapter.tick(); // row 12 (ring rest), then row 16 (give up: DISABLE)
  TEST_ASSERT_EQUAL_UINT(1, g_port.count_of(Call::PUBLISH_DISABLE));
}

TEST_CASE("DAD-005 a corrupted input performs the session's safe state through the port",
          "[drive_adapter]") {
  fresh_port();
  Adapter adapter{{.view = FakeView{}}};
  boot(adapter);
  g_port.sample.mib = ds::MibState::ENABLED;
  adapter.tick(); // unlocked
  TEST_ASSERT_FALSE(adapter.locked());
  g_port = Port{};
  TEST_ASSERT_TRUE(adapter.input(static_cast<Input>(99)));
  TEST_ASSERT_TRUE(adapter.locked());
  TEST_ASSERT_EQUAL_UINT(1, g_port.count_of(Call::PUBLISH_DISABLE));
  TEST_ASSERT_EQUAL_UINT(1, g_port.count_of(Call::NO_MENU_ON_ARRIVAL));
  TEST_ASSERT_EQUAL_UINT(1, g_port.count_of(Call::GO_LOCKED));
  TEST_ASSERT_EQUAL_UINT(1, g_port.count_of(Call::SET_LOCKED));
  TEST_ASSERT_TRUE(g_port.calls[g_port.count - 1] == Call::GATE_UPDATE);
}

TEST_CASE("DAD-007 the stop timer counts from the first stop: a second stop at +1900 ms does not "
          "move the fault",
          "[drive_adapter][REQ-DAD-08]") {
  fresh_port();
  Adapter adapter{{.view = FakeView{}}};
  drive(adapter);
  const std::int64_t t0 = g_port.now;
  adapter.exit_hold_done(); // row 22: the first stop arms the timer
  TEST_ASSERT_EQUAL_INT64(t0, DriveAdapterTestPeer::stop_first_us(adapter));
  g_port.now = t0 + 1900 * kMsUs;
  adapter.exit_hold_done(); // row 23: a repeated stop keeps it
  TEST_ASSERT_EQUAL_INT64(t0, DriveAdapterTestPeer::stop_first_us(adapter));
  g_port.now = t0 + 4999 * kMsUs;
  adapter.tick();
  TEST_ASSERT_EQUAL_UINT(0, adapter.stop_faults());
  g_port.now = t0 + 5000 * kMsUs;
  adapter.tick(); // row 44 or 45: at 5 s from the first stop, not from the second
  TEST_ASSERT_EQUAL_UINT(1, adapter.stop_faults());
  TEST_ASSERT_TRUE(adapter.notice() == DriveNotice::MCB_DID_NOT_STOP);
  g_port.now = t0 + 9000 * kMsUs;
  adapter.tick(); // raised once
  TEST_ASSERT_EQUAL_UINT(1, adapter.stop_faults());
  // The relock clears the timer.
  g_port.sample.mib = ds::MibState::IDLE;
  adapter.tick();
  TEST_ASSERT_TRUE(adapter.locked());
  TEST_ASSERT_EQUAL_INT64(0, DriveAdapterTestPeer::stop_first_us(adapter));
  TEST_ASSERT_TRUE(adapter.notice() == DriveNotice::NONE);
}

TEST_CASE("DAD-008 the re-send boundaries: 124 ms since the last DISABLE not due, 125 due; with "
          "the fault 874 not due, 875 due",
          "[drive_adapter][REQ-DAD-08]") {
  using ds::Resend;
  fresh_port();
  Adapter adapter{{.view = FakeView{}}};
  drive(adapter);
  const std::int64_t t0 = g_port.now;
  adapter.exit_hold_done(); // the DISABLE at t0
  TEST_ASSERT_TRUE(DriveAdapterTestPeer::env(adapter, t0 + 124 * kMsUs).resend == Resend::NOT_DUE);
  TEST_ASSERT_TRUE(DriveAdapterTestPeer::env(adapter, t0 + 125 * kMsUs).resend == Resend::FAST);
  TEST_ASSERT_TRUE(DriveAdapterTestPeer::env(adapter, t0 + 874 * kMsUs).resend == Resend::FAST);
  TEST_ASSERT_TRUE(DriveAdapterTestPeer::env(adapter, t0 + 875 * kMsUs).resend == Resend::SLOW);
  // The stop window's edge.
  TEST_ASSERT_FALSE(DriveAdapterTestPeer::env(adapter, t0 + 4999 * kMsUs).stop_fault_elapsed);
  TEST_ASSERT_TRUE(DriveAdapterTestPeer::env(adapter, t0 + 5000 * kMsUs).stop_fault_elapsed);
  // Through the session: with the fault (rows 47/49) a tick 874 ms after the last DISABLE sends
  // nothing, one at 875 ms sends DISABLE.
  for (std::int64_t t = 250; t <= 5000; t += 250) {
    g_port.now = t0 + t * kMsUs;
    adapter.tick();
  }
  TEST_ASSERT_EQUAL_UINT(1, adapter.stop_faults());
  const std::size_t before = g_port.count_of(Call::PUBLISH_DISABLE);
  g_port.now = t0 + (4750 + 874) * kMsUs; // the last DISABLE was the tick at 4750
  adapter.tick();
  TEST_ASSERT_EQUAL_UINT(before, g_port.count_of(Call::PUBLISH_DISABLE));
  g_port.now = t0 + (4750 + 875) * kMsUs;
  adapter.tick();
  TEST_ASSERT_EQUAL_UINT(before + 1, g_port.count_of(Call::PUBLISH_DISABLE));
}

TEST_CASE("DAD-009 a refused DriveCommand is counted and logged at most once a second; the next "
          "re-send still comes at its time",
          "[drive_adapter][REQ-DAD-09]") {
  fresh_port();
  Adapter adapter{{.view = FakeView{}}};
  drive(adapter);
  g_port.publish_result = false;
  const std::int64_t t0 = g_port.now;
  adapter.exit_hold_done(); // a refused DISABLE: counted and logged
  TEST_ASSERT_EQUAL_UINT(1, adapter.publish_failures());
  TEST_ASSERT_EQUAL_INT64(t0, DriveAdapterTestPeer::last_fail_log_us(adapter));
  const std::size_t sent = g_port.count_of(Call::PUBLISH_DISABLE);
  for (std::int64_t t = 250; t <= 750; t += 250) {
    g_port.now = t0 + t * kMsUs;
    adapter.tick(); // a re-send every tick, each refused, none logged within the second
  }
  TEST_ASSERT_EQUAL_UINT(sent + 3, g_port.count_of(Call::PUBLISH_DISABLE));
  TEST_ASSERT_EQUAL_UINT(4, adapter.publish_failures());
  TEST_ASSERT_EQUAL_INT64(t0, DriveAdapterTestPeer::last_fail_log_us(adapter));
  g_port.now = t0 + 1000 * kMsUs;
  adapter.tick(); // a second after the last log: logged again
  TEST_ASSERT_EQUAL_UINT(5, adapter.publish_failures());
  TEST_ASSERT_EQUAL_INT64(t0 + 1000 * kMsUs, DriveAdapterTestPeer::last_fail_log_us(adapter));
  g_port.now = t0 + 1250 * kMsUs;
  adapter.tick(); // the re-send schedule did not change
  TEST_ASSERT_EQUAL_UINT(sent + 5, g_port.count_of(Call::PUBLISH_DISABLE));
}

TEST_CASE("DAD-010 the Drive notice reaches the port once per change, the stop before the hold "
          "reason in the hold reasons' order",
          "[drive_adapter][REQ-DAD-10]") {
  using ds::StopNotice;
  // Every pair, the pure rule (hazard-c1-spec.md §2.7, §3.3).
  constexpr std::array kHolds{
      HoldReason::NONE,        HoldReason::GATE_SHUT,      HoldReason::MOTION_GUARD,
      HoldReason::CALIBRATING, HoldReason::NOT_CALIBRATED, HoldReason::POST_NOT_PASSED,
      HoldReason::STICK_FAULT, HoldReason::STICK_CHECK,    HoldReason::CENTRE_FIRST};
  constexpr std::array kHoldNotice{
      DriveNotice::NONE,        DriveNotice::NONE,           DriveNotice::MOTION_GUARD,
      DriveNotice::NONE,        DriveNotice::NOT_CALIBRATED, DriveNotice::POST_NOT_PASSED,
      DriveNotice::STICK_FAULT, DriveNotice::STICK_CHECK,    DriveNotice::CENTRE_FIRST};
  for (std::size_t h = 0; h < kHolds.size(); ++h) {
    TEST_ASSERT_TRUE(da::drive_notice(StopNotice::MCB_DID_NOT_STOP, kHolds[h]) ==
                     DriveNotice::MCB_DID_NOT_STOP);
    TEST_ASSERT_TRUE(da::drive_notice(StopNotice::STOPPING, kHolds[h]) == DriveNotice::STOPPING);
    TEST_ASSERT_TRUE(da::drive_notice(StopNotice::NONE, kHolds[h]) == kHoldNotice[h]);
  }
  TEST_ASSERT_TRUE(da::drive_notice(StopNotice::NONE, static_cast<HoldReason>(0xEE)) ==
                   DriveNotice::NONE);
  // Through the adapter: once per change, never repeated.
  fresh_port();
  Adapter adapter{{.view = FakeView{}}};
  drive(adapter);
  TEST_ASSERT_EQUAL_UINT(0, g_port.notice_count); // NONE all along
  g_port.sample.hold = HoldReason::CENTRE_FIRST;
  adapter.tick();
  adapter.tick();
  TEST_ASSERT_EQUAL_UINT(1, g_port.notice_count);
  TEST_ASSERT_TRUE(g_port.notices[0] == DriveNotice::CENTRE_FIRST);
  adapter.exit_hold_done(); // the stop ranks above the hold reason
  TEST_ASSERT_EQUAL_UINT(2, g_port.notice_count);
  TEST_ASSERT_TRUE(g_port.notices[1] == DriveNotice::STOPPING);
  g_port.sample.mib = ds::MibState::IDLE;
  g_port.sample.hold = HoldReason::GATE_SHUT;
  adapter.tick(); // relock: the stop ends; the gate is shut: no text
  TEST_ASSERT_EQUAL_UINT(3, g_port.notice_count);
  TEST_ASSERT_TRUE(g_port.notices[2] == DriveNotice::NONE);
  TEST_ASSERT_TRUE(adapter.notice() == DriveNotice::NONE);
}

TEST_CASE("DAD-011 a running calibration and the Boot screen in the sample reach the Env",
          "[drive_adapter][REQ-DAD-08]") {
  fresh_port();
  Adapter adapter{{.view = FakeView{}}};
  g_port.sample.calibrating = true;
  g_port.sample.screen = ds::Screen::BOOT;
  const ds::Env e = DriveAdapterTestPeer::env(adapter, g_port.now);
  TEST_ASSERT_TRUE(e.calibrating);
  TEST_ASSERT_TRUE(e.screen == ds::Screen::BOOT);
  TEST_ASSERT_TRUE((ds::env_guards(e) & ds::bit(ds::Guard::CALIBRATING)) != 0);
  TEST_ASSERT_TRUE((ds::env_guards(e) & ds::bit(ds::Guard::ON_BOOT_SCREEN)) != 0);
  // So the MIB enabling does not enter Drive (rows 1-2), until both clear.
  g_port.sample.mib = ds::MibState::ENABLED;
  adapter.tick();
  TEST_ASSERT_TRUE(adapter.locked());
  g_port.sample.calibrating = false;
  adapter.tick();
  TEST_ASSERT_TRUE(adapter.locked());
  g_port.sample.screen = ds::Screen::LOCKED;
  adapter.tick();
  TEST_ASSERT_FALSE(adapter.locked());
}
