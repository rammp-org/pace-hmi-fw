#pragma once
// The LogScreen: what main keeps of the serial output.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

#include "lvgl.h"

namespace hmi::ui {

/// How a log line is shown (main's LogLevel, value for value).
enum class LogLineLevel : uint8_t {
  PLAIN,   ///< the theme's text colour
  WARNING, ///< amber
  ERROR,   ///< red
};

/// TextArea1 shows the lines main's log capture keeps, one per log line, newest at the
/// bottom, errors red and warnings amber. It opens at the newest line; the joystick pages
/// through it, and a finger drags it. Lines reach the widget the way all firmware state does:
/// a poll timer bumps a subject and an observer bound to the view does the widget work.
/// Everything here runs on the UI task.
class LogView {
public:
  /// Refresh cadence while the LogScreen is up: new lines are batched into one rebuild a tick.
  static constexpr uint32_t POLL_MS = 250;
  /// Scrolled to within this many px of the end still counts as following.
  static constexpr int32_t FOLLOW_SLACK_PX = 8;
  /// A sideways page keeps this much of the previous one in view.
  static constexpr int32_t HORIZONTAL_OVERLAP_PX = 80;
  static constexpr uint32_t ERROR_COLOUR = 0xFF5050;
  static constexpr uint32_t WARNING_COLOUR = 0xE0A000;

  using LineVisitor = std::function<void(LogLineLevel, std::string_view)>;

  struct Config {
    size_t lines;        ///< how many lines the capture keeps
    size_t line_len;     ///< the longest line it keeps
    uint32_t (*count)(); ///< lines captured since boot (keeps counting past `lines`)
    /// Visits the lines kept, oldest first; returns count() as of the lines visited. The
    /// visitor must be quick and must not print (main holds the capture's lock throughout).
    uint32_t (*visit)(const LineVisitor &visit);
    /// The text buffer the label draws, `size` bytes zeroed; null if there is no memory to
    /// spare. Called once, from init.
    char *(*alloc_text)(size_t size);
  };

  constexpr explicit LogView(const Config &config) noexcept
      : config_(config) {}

  /// @brief Builds the view inside TextArea1, its subject, the poll timer and the joystick's
  ///        key target. Without memory for the text it leaves the export's placeholder.
  /// app_main, once, after ui_init and before lv_task starts.
  void init();
  /// @brief The joystick's group while the LogScreen is up (null before init or without
  ///        memory): up/down scroll a page, left/right a page sideways.
  /// UI task.
  lv_group_t *group() const { return group_; }
  /// @brief For LV_EVENT_SCREEN_LOADED: brings the text up to date and jumps to the newest
  ///        line.
  /// UI task.
  void on_load();
  /// @brief What a push down does with the newest line already showing: the caller moves the
  ///        joystick on (main: the burger key). Null = nothing.
  /// @param down_past_end the callback
  /// app_main.
  void set_escape(void (*down_past_end)()) { escape_down_ = down_past_end; }

private:
  size_t line_budget() const;
  size_t text_len() const;
  uint32_t dropped(uint32_t count) const;
  int32_t line_height() const;
  void scroll_to_newest();
  uint32_t rebuild_text();
  static void text_observer(lv_observer_t *observer, lv_subject_t *subject);
  static void poll_cb(lv_timer_t *timer);
  static void key_cb(lv_event_t *e);

  Config config_;
  // The text the label draws, owned here (allocated through the Config). The label points
  // at it with lv_label_set_text_static rather than taking a copy: LVGL's own allocator is a
  // fixed pool shared by the whole UI, and the whole log can be larger than that.
  char *text_ = nullptr;
  // TextArea1 is only the frame. A textarea scrolls itself to its cursor every time its text
  // changes size, so the log lives instead in view_, a plain scroll container, as label_.
  lv_obj_t *view_ = nullptr;
  lv_obj_t *label_ = nullptr;
  // Bumped by the poll timer when lines have arrived while the LogScreen is up; the observer
  // bound to view_ rebuilds the text.
  lv_subject_t version_subject_{};
  uint32_t rendered_count_ = 0; ///< count() as of the text on screen
  bool follow_next_ = true;     ///< jump to the newest line on the next rebuild
  lv_group_t *group_ = nullptr;
  void (*escape_down_)() = nullptr;
};

} // namespace hmi::ui
