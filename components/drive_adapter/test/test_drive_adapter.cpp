// L1 host app for components/drive_adapter: the adapter's own contract (DAD-001..006). Its
// behaviour against the firmware's is pinned by the drive goldens (tests/host/drive_golden).

#include "drive_adapter.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include "test_case.hpp"

namespace {

namespace da = hmi::drive_adapter;
namespace ds = hmi::drive_session;
using da::DriveBanner;
using ds::Input;

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
  REFUSAL_FEEDBACK,
};

// What the fake port sees and answers. Fixed-size: nothing is allocated, so a failed assert
// (which longjmps) leaks nothing.
struct Port {
  std::array<Call, 256> calls{};
  std::size_t count = 0;
  std::int64_t now = 1'000'000;
  da::DriveSample sample{.link_connected = true,
                         .mib = ds::MibState::IDLE,
                         .screen = ds::Screen::LOCKED,
                         .menu_open = false};
  DriveBanner last_banner = DriveBanner::REFUSED_DRIVE;
  std::size_t banners = 0;
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
  void publish(bool enable) const {
    g_port.add(enable ? Call::PUBLISH_ENABLE : Call::PUBLISH_DISABLE);
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
  void refusal_feedback() const { g_port.add(Call::REFUSAL_FEEDBACK); }
};
static_assert(da::DrivePort<FakeView>);

// A view without refusal_feedback is not a port (DAD-006).
struct NotAPort {
  da::DriveSample sample() const { return {}; }
  std::int64_t now_us() const { return 0; }
};
static_assert(!da::DrivePort<NotAPort>);
static_assert(!da::DrivePort<int>);

using Adapter = da::DriveAdapter<FakeView>;
Adapter *g_adapter = nullptr;

void fresh_port() { g_port = Port{}; }

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
  g_port.sample.mib = ds::MibState::ENABLED; // row 1: LOCKED + DRIVING_OK -> UNLOCKING (F1)
  g_port.on_gate_update = [] { return g_adapter->input(Input::PROFILE_CLICK); };
  adapter.tick();
  g_adapter = nullptr;
  TEST_ASSERT_FALSE(g_port.reentry_result);                          // the nested input was refused
  TEST_ASSERT_EQUAL_UINT(0, g_port.count_of(Call::PUBLISH_DISABLE)); // and did nothing
  TEST_ASSERT_EQUAL_UINT(1, g_port.count_of(Call::SET_UNLOCKED));    // the outer one completed
  TEST_ASSERT_FALSE(adapter.locked());
  // The adapter is usable again afterwards.
  g_port.on_gate_update = nullptr;
  TEST_ASSERT_TRUE(adapter.input(Input::PROFILE_CLICK)); // row 31
  TEST_ASSERT_EQUAL_UINT(1, g_port.count_of(Call::PUBLISH_DISABLE));
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

TEST_CASE("DAD-006 the exit hold while locked is TABLE.md U3: DISABLE, and the exit deadline's "
          "banner on a later tick",
          "[drive_adapter]") {
  fresh_port();
  Adapter adapter{{.view = FakeView{}, .answer_us = 1000}};
  adapter.exit_hold_done();
  TEST_ASSERT_TRUE(adapter.locked());
  TEST_ASSERT_EQUAL_UINT(1, g_port.count_of(Call::PUBLISH_DISABLE));
  g_port.now += 1000;
  adapter.tick();
  TEST_ASSERT_EQUAL_UINT(1, g_port.banners);
  TEST_ASSERT_TRUE(g_port.last_banner == DriveBanner::EXIT_REFUSED);
  adapter.tick(); // raised once
  TEST_ASSERT_EQUAL_UINT(1, g_port.banners);
}
