#include "log_view.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "esp_heap_caps.h"

#include "log_capture.hpp"
#include "ui.h"

namespace {

// Refresh cadence while the LogScreen is up. New lines are batched into one
// rebuild per tick: relaying out a few hundred lines is the expensive part, so
// it happens a few times a second at most, and never while nobody is looking.
constexpr uint32_t kPollMs = 250;
// Scrolled to within this many px of the end still counts as following.
constexpr int32_t kFollowSlackPx = 8;

constexpr uint32_t kErrorColour = 0xFF5050;
constexpr uint32_t kWarningColour = 0xE0A000;
// Worst case per line: every character a '#', each doubled to escape it, plus
// "#rrggbb " before and "#" after for a coloured line, plus the newline.
constexpr size_t kLineBudget = 2 * kLogCaptureLineLen + 9 + 2;
constexpr size_t kTextLen = kLogCaptureLines * kLineBudget + 1;

// The text TextArea1's label draws, in PSRAM and owned here. The label is
// pointed at it with lv_label_set_text_static rather than handed a copy:
// LVGL's own allocator is a fixed 64 KB pool shared by the whole UI, and 500
// lines of log can be larger than that on their own.
char *text = nullptr;

// Bumped by the poll timer when lines have arrived while the LogScreen is up;
// the observer bound to TextArea1 rebuilds the text.
lv_subject_t log_version_subject;
uint32_t shown_count = 0;
bool follow_next = true; // jump to the newest line on the next rebuild
lv_group_t *group = nullptr;

void rebuild_text() {
  size_t pos = 0;
  log_capture_visit([&pos](LogLevel level, std::string_view line) {
    const bool coloured = level != LogLevel::Plain;
    if (coloured) {
      pos +=
          static_cast<size_t>(snprintf(text + pos, kTextLen - pos, "#%06" PRIx32 " ",
                                       level == LogLevel::Error ? kErrorColour : kWarningColour));
    }
    for (const char c : line) {
      if (c != '#') {
        text[pos++] = c;
      } else if (coloured) {
        // '#' is LVGL's recolour command character. Inside a coloured run it
        // ends the run whatever follows, so there it can only be shown as a
        // look-alike; outside one, doubled, it is a literal '#'.
        text[pos++] = '*';
      } else {
        text[pos++] = '#';
        text[pos++] = '#';
      }
    }
    if (coloured) {
      text[pos++] = '#';
    }
    text[pos++] = '\n';
  });
  if (pos > 0) {
    pos--; // no newline after the last line
  }
  text[pos] = '\0';
}

void log_text_observer(lv_observer_t *observer, lv_subject_t *) {
  lv_obj_t *area = lv_observer_get_target_obj(observer);
  // Follow the newest line only if the reader is already at the bottom: someone
  // who scrolled up to read something keeps their place as lines arrive.
  const bool follow = follow_next || lv_obj_get_scroll_bottom(area) <= kFollowSlackPx;
  follow_next = false;
  rebuild_text();
  lv_label_set_text_static(lv_textarea_get_label(area), text);
  if (follow) {
    lv_obj_update_layout(area);
    lv_obj_scroll_to_y(area, lv_obj_get_scroll_y(area) + lv_obj_get_scroll_bottom(area),
                       LV_ANIM_OFF);
  }
}

void poll_cb(lv_timer_t *) {
  if (lv_screen_active() != ui_LogScreen) {
    return;
  }
  const uint32_t count = log_capture_count();
  if (count == shown_count) {
    return;
  }
  shown_count = count;
  lv_subject_set_int(&log_version_subject, static_cast<int32_t>(count));
}

// A page per push, less a line so the last line of one page is the first of
// the next. Up the stick is up the log: a positive dy moves the content down,
// revealing earlier lines. LVGL's key repeat turns a held stick into paging.
void key_cb(lv_event_t *e) {
  const uint32_t key = lv_event_get_key(e);
  if (key != LV_KEY_UP && key != LV_KEY_DOWN) {
    return;
  }
  const int32_t line =
      lv_font_get_line_height(lv_obj_get_style_text_font(ui_TextArea1, LV_PART_MAIN));
  const int32_t page = std::max<int32_t>(line, lv_obj_get_content_height(ui_TextArea1) - line);
  lv_obj_scroll_by_bounded(ui_TextArea1, 0, key == LV_KEY_UP ? page : -page, LV_ANIM_ON);
}

} // namespace

void log_view_init() {
  if (ui_TextArea1 == nullptr || ui_LogScreen == nullptr) {
    return;
  }
  text = static_cast<char *>(heap_caps_calloc(kTextLen, 1, MALLOC_CAP_SPIRAM));
  if (text == nullptr) {
    return; // no PSRAM to spare: the export's placeholder text stays
  }

  // A read-only view: no cursor, and a tap scrolls rather than placing one.
  lv_textarea_set_cursor_click_pos(ui_TextArea1, false);
  lv_obj_remove_flag(ui_TextArea1, LV_OBJ_FLAG_CLICK_FOCUSABLE);
  lv_obj_set_style_opa(ui_TextArea1, LV_OPA_TRANSP, LV_PART_CURSOR);
  lv_label_set_recolor(lv_textarea_get_label(ui_TextArea1), true);

  lv_subject_init_int(&log_version_subject, 0);
  lv_subject_add_observer_obj(&log_version_subject, log_text_observer, ui_TextArea1, nullptr);
  lv_timer_create(poll_cb, kPollMs, nullptr);

  // The joystick gets a zero-size target to hold focus and take its keys.
  // Not the text area itself: in a group it would read the keys as cursor
  // moves, and ENTER as typing a newline into the log.
  lv_obj_t *keys = lv_obj_create(ui_LogScreen);
  lv_obj_remove_style_all(keys);
  lv_obj_set_size(keys, 0, 0);
  lv_obj_add_event_cb(keys, key_cb, LV_EVENT_KEY, nullptr);
  group = lv_group_create();
  lv_group_add_obj(group, keys);
}

lv_group_t *log_view_group() { return group; }

void log_view_on_load() {
  if (text == nullptr) {
    return;
  }
  follow_next = true;
  shown_count = log_capture_count();
  lv_subject_notify(&log_version_subject); // rebuild now rather than on the next tick
}
