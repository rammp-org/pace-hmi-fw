#pragma once
// The UI island (app-main-shrink §3). Moved from main.cpp's app_main (lv_task).

#include <chrono>

#include "hmi_ui/fps_meter.hpp"
#include "task.hpp"

namespace hmi::ui {

/// @brief The UI island: the task that runs LVGL. Every `period` at most it runs one LVGL
/// cycle (Config::cycle: lv_task_handler, which renders, runs the timers and reads the input
/// devices) and yields. Built once, an app_main local (app-main-shrink V6), and started once
/// the whole UI is built.
class UiIsland {
public:
  struct Config {
    /// The task, as a literal copy of today's (name, stack, priority, core).
    espp::Task::BaseConfig task;
    /// One LVGL cycle, under the LVGL lock (main's lvgl_cycle).
    void (*cycle)();
    /// The cadence: the next cycle is due this long after the last one started.
    std::chrono::milliseconds period;
    /// Reported after every cycle when not null (CONFIG_HMI_DEBUG_FPS).
    FpsMeter *fps_meter;
  };

  /// @brief Stores the config; starts nothing.
  /// @param config See Config.
  explicit UiIsland(const Config &config);
  UiIsland(const UiIsland &) = delete;
  UiIsland &operator=(const UiIsland &) = delete;
  UiIsland(UiIsland &&) = delete;
  UiIsland &operator=(UiIsland &&) = delete;
  ~UiIsland() = default;

  /// @brief Starts the task (app_main, once the UI is built and touch is up).
  /// @return Whether the task started.
  bool start();

  /// @brief Stops the task and waits for it to end, as the destructor does (app_main, when a
  /// boot that stops after start() ends the UI).
  /// @return Whether the task stopped.
  bool stop();

private:
  void (*cycle_)();
  std::chrono::milliseconds period_;
  FpsMeter *fps_meter_;
  espp::Task task_;
};

} // namespace hmi::ui
