#include "hmi_ui/ui_island.hpp"

#include <algorithm>
#include <condition_variable>
#include <mutex>

using namespace std::chrono_literals;

namespace hmi::ui {

UiIsland::UiIsland(const Config &config)
    : cycle_(config.cycle)
    , period_(config.period)
    , fps_meter_(config.fps_meter)
    , heartbeat_(config.heartbeat)
    , task_({.callback = [this](std::mutex &m, std::condition_variable &cv) -> bool {
               // steady_clock, never high_resolution_clock: on ESP-IDF that one is the
               // wall clock, which the MCB's time moves (see "TopBar clock"), and
               // wait_until on a wall clock that steps back sleeps out the whole step
               // - the screen froze for as long as the clock went back.
               auto start_time = std::chrono::steady_clock::now();
               cycle_();
               if (heartbeat_ != nullptr) {
                 heartbeat_(); // hazard fix C4: this cycle completed (REQ-UI-24)
               }
               if (fps_meter_ != nullptr) {
                 fps_meter_->report_if_due();
               }
               std::unique_lock<std::mutex> lock(m);
               // Always yield at least one tick: once a cycle takes longer than the period
               // the deadline is already past and wait_until returns without
               // yielding, which pins core 1 at priority 20 and starves IDLE1.
               const auto deadline =
                   std::max(start_time + period_, std::chrono::steady_clock::now() + 1ms);
               cv.wait_until(lock, deadline, []() { return false; });
               return false;
             },
             .task_config = config.task}) {}

bool UiIsland::start() { return task_.start(); }

bool UiIsland::stop() { return task_.stop(); }

} // namespace hmi::ui
