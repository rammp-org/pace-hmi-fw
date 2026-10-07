#pragma once
// The TopBar's clock and link labels.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "lvgl.h"

namespace hmi::ui {

/// The clock and the link text of every TopBar: two string subjects, each label bound to one.
/// The clock shows the system clock, which main keeps on the local wall time the MIB reports
/// (no TZ is set, so nothing converts again); `--:--` until that time is known. The link label
/// names what RTPS runs over.
class TopBarView {
public:
  /// How often the clock is read.
  static constexpr uint32_t CLOCK_POLL_MS = 1000;

  struct Config {
    /// True once the system clock holds a real time (main's clock_set). Written on any task,
    /// read on the UI task.
    const std::atomic<bool> *clock_valid;
  };

  constexpr explicit TopBarView(const Config &config) noexcept
      : config_(config) {}

  /// @brief Initialises both subjects: the clock at `--:--`, the link at `link`. Before
  ///        any `bind` (V7).
  /// @param link the link the settings chose; corrected once RTPS has started
  /// app_main, before lv_task starts.
  void init(const char *link);
  /// @brief Binds one TopBar instance's clock and link labels.
  /// @param bar the TopBar instance; null does nothing
  /// UI task (app_main at boot, a screen's *_ensure later); lvgl_mutex held once lv_task runs.
  void bind(lv_obj_t *bar);
  /// @brief Shows the clock now (the RTC's time, when it had one, from the first frame), then
  ///        every CLOCK_POLL_MS.
  /// app_main, before lv_task starts.
  void start_clock();
  /// @brief Sets the link text.
  /// @param text the link up
  /// Any task, with lvgl_mutex held by the caller.
  void set_link(const char *text);

private:
  static constexpr size_t CLOCK_TEXT_SIZE = 8; ///< "HH:MM" and its terminator, with room
  static constexpr size_t LINK_TEXT_SIZE = 16;

  // UI task (an LVGL timer).
  static void clock_poll_cb(lv_timer_t *timer);
  void clock_poll();

  Config config_;
  lv_subject_t clock_subject_{};
  std::array<char, CLOCK_TEXT_SIZE> clock_buf_{};
  std::array<char, CLOCK_TEXT_SIZE> clock_prev_buf_{};
  lv_subject_t link_subject_{};
  std::array<char, LINK_TEXT_SIZE> link_buf_{};
  std::array<char, LINK_TEXT_SIZE> link_prev_buf_{};
};

} // namespace hmi::ui
