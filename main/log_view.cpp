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

// Refresh cadence while a log pane is on screen. New lines are batched into one
// rebuild per tick: relaying out a few hundred lines is the expensive part, so
// it happens a few times a second at most, and never while nobody is looking.
constexpr uint32_t kPollMs = 250;
// Scrolled to within this many px of the end still counts as following.
constexpr int32_t kFollowSlackPx = 8;
// A sideways page keeps this much of the previous one in view.
constexpr int32_t kHorizontalOverlapPx = 80;
constexpr uint32_t kErrorColour = 0xFF5050;
constexpr uint32_t kWarningColour = 0xE0A000;
// Worst case per line: every character a '#', each doubled to escape it, plus
// "#rrggbb " before and "#" after for a coloured line, plus the newline.
constexpr size_t kLineBudget = 2 * kLogCaptureLineLen + 9 + 2;
constexpr size_t kTextLen = kLogCaptureLines * kLineBudget + 1;

// The text the log labels draw, in PSRAM and owned here. The labels are pointed
// at it with lv_label_set_text_static rather than handed a copy: LVGL's own
// allocator is a fixed 64 KB pool shared by the whole UI, and 500 lines of log
// can be larger than that on their own. One buffer serves every pane: only the
// one on screen is ever rebuilt, and a pane coming on screen rebuilds first.
char *text = nullptr;

// One log inside a textarea frame. The textarea is only the frame: a textarea
// scrolls itself to its cursor every time its text changes size, and with no
// typing the cursor sits at the top, so the log would snap back to its oldest
// line on every refresh. The log lives instead in `view`, a plain scroll
// container filling the frame, as `label`.
struct Pane {
  lv_obj_t *screen = nullptr;
  lv_obj_t *view = nullptr;
  lv_obj_t *label = nullptr;
  uint32_t rendered_count = 0; // log_capture_count() as of the text on screen
  bool follow_next = true;     // jump to the newest line on the next rebuild
};
Pane log_pane;    // the LogScreen's, for the life of the firmware
Pane update_pane; // the UpdateScreen's, while that screen exists

// Bumped by the poll timer when lines have arrived while a pane is on screen;
// the observer bound to each view rebuilds that pane's text if it is the one up.
lv_subject_t log_version_subject;
lv_group_t *group = nullptr;

// Lines gone from the ring's front once `count` lines have been captured.
uint32_t dropped(uint32_t count) { return count > kLogCaptureLines ? count - kLogCaptureLines : 0; }

int32_t line_height(const Pane &pane) {
  return lv_font_get_line_height(lv_obj_get_style_text_font(pane.label, LV_PART_MAIN)) +
         lv_obj_get_style_text_line_space(pane.label, LV_PART_MAIN);
}

void scroll_to_newest(const Pane &pane) {
  lv_obj_update_layout(pane.view);
  lv_obj_scroll_by(pane.view, 0, -lv_obj_get_scroll_bottom(pane.view), LV_ANIM_OFF);
}

Pane *active_pane() {
  const lv_obj_t *screen = lv_screen_active();
  for (Pane *pane : {&log_pane, &update_pane}) {
    if (pane->view != nullptr && pane->screen == screen) {
      return pane;
    }
  }
  return nullptr;
}

uint32_t rebuild_text() {
  size_t pos = 0;
  const uint32_t count = log_capture_visit([&pos](LogLevel level, std::string_view line) {
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
  return count;
}

void log_text_observer(lv_observer_t *observer, lv_subject_t *) {
  Pane &pane = *static_cast<Pane *>(lv_observer_get_user_data(observer));
  if (&pane != active_pane()) {
    return;
  }
  // Follow the newest line only if the reader is already at the bottom: someone
  // who scrolled up to read something keeps their place as lines arrive.
  const bool follow = pane.follow_next || lv_obj_get_scroll_bottom(pane.view) <= kFollowSlackPx;
  pane.follow_next = false;
  const uint32_t before = pane.rendered_count;
  pane.rendered_count = rebuild_text();
  lv_label_set_text_static(pane.label, text);
  if (follow) {
    scroll_to_newest(pane);
    return;
  }
  // Once the ring is full every new line pushes the oldest out, which moves
  // the whole text up a line under a reader who is standing still. Scroll back
  // by as much, so the line they were reading stays where it was.
  const uint32_t evicted = dropped(pane.rendered_count) - dropped(before);
  if (evicted > 0) {
    lv_obj_update_layout(pane.view);
    lv_obj_scroll_by_bounded(pane.view, 0, static_cast<int32_t>(evicted) * line_height(pane),
                             LV_ANIM_OFF);
  }
}

void poll_cb(lv_timer_t *) {
  const Pane *pane = active_pane();
  if (pane == nullptr) {
    return;
  }
  const uint32_t count = log_capture_count();
  if (count == pane->rendered_count) {
    return;
  }
  lv_subject_set_int(&log_version_subject, static_cast<int32_t>(count));
}

// The view goes with its screen; forget it so nothing reaches for it.
void view_deleted_cb(lv_event_t *e) {
  Pane &pane = *static_cast<Pane *>(lv_event_get_user_data(e));
  pane = Pane{};
}

// Turns `frame` (a textarea from the export) into `pane`.
void attach(Pane &pane, lv_obj_t *frame) {
  // The frame only: its own text, placeholder and cursor go, and it takes no
  // clicks, keys or scrolling, so none of its editing behaviour can reach the log.
  lv_textarea_set_text(frame, "");
  lv_textarea_set_placeholder_text(frame, "");
  lv_obj_add_flag(lv_textarea_get_label(frame), LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_opa(frame, LV_OPA_TRANSP, LV_PART_CURSOR);
  lv_textarea_set_cursor_click_pos(frame, false);
  lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICK_FOCUSABLE);
  lv_obj_remove_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

  // The view keeps the theme's scrollbars but none of its box: the frame
  // already draws the background and border.
  pane.screen = lv_obj_get_screen(frame);
  pane.view = lv_obj_create(frame);
  lv_obj_set_size(pane.view, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(pane.view, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(pane.view, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(pane.view, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(pane.view, 0, LV_PART_MAIN);
  lv_obj_set_scroll_dir(pane.view, LV_DIR_ALL);
  lv_obj_set_scrollbar_mode(pane.view, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_remove_flag(pane.view, LV_OBJ_FLAG_SCROLL_CHAIN);

  // Content-sized, so a line is never wrapped: a long one runs off to the
  // right and the view scrolls sideways to it.
  pane.label = lv_label_create(pane.view);
  lv_obj_set_size(pane.label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  // The theme's text colour, set on the label itself: inherited, it would come
  // from the view, which the LVGL theme gives its own (dark) text colour.
  // Themeable, so a theme switch recolours it with the rest of the UI.
  ui_object_set_themeable_style_property(pane.label, LV_PART_MAIN, LV_STYLE_TEXT_COLOR,
                                         _ui_theme_color_text);
  ui_object_set_themeable_style_property(pane.label, LV_PART_MAIN, LV_STYLE_TEXT_OPA,
                                         _ui_theme_alpha_text);
  lv_label_set_recolor(pane.label, true);
  lv_label_set_text_static(pane.label, "");

  lv_subject_add_observer_obj(&log_version_subject, log_text_observer, pane.view, &pane);
  lv_obj_add_event_cb(pane.view, view_deleted_cb, LV_EVENT_DELETE, &pane);
}

// Brings `pane` up to date and to its newest line: for a screen that just loaded.
void show_newest(Pane &pane) {
  if (text == nullptr || pane.view == nullptr) {
    return;
  }
  pane.follow_next = true;
  lv_obj_scroll_to_x(pane.view, 0, LV_ANIM_OFF);
  lv_subject_notify(&log_version_subject); // rebuild now rather than on the next tick
}

// A page per push, less a line so the last line of one page is the first of
// the next. Up the stick is up the log: a positive dy moves the content down,
// revealing earlier lines. Left and right page sideways through long lines.
// LVGL's key repeat turns a held stick into paging.
void key_cb(lv_event_t *e) {
  const uint32_t key = lv_event_get_key(e);
  if (key == LV_KEY_UP || key == LV_KEY_DOWN) {
    const int32_t line = line_height(log_pane);
    const int32_t page = std::max<int32_t>(line, lv_obj_get_content_height(log_pane.view) - line);
    lv_obj_scroll_by_bounded(log_pane.view, 0, key == LV_KEY_UP ? page : -page, LV_ANIM_ON);
  } else if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
    const int32_t page = std::max<int32_t>(
        kHorizontalOverlapPx, lv_obj_get_content_width(log_pane.view) - kHorizontalOverlapPx);
    lv_obj_scroll_by_bounded(log_pane.view, key == LV_KEY_LEFT ? page : -page, 0, LV_ANIM_ON);
  }
}

void oldest_cb(lv_event_t *) { lv_obj_scroll_to(log_pane.view, 0, 0, LV_ANIM_OFF); }

void newest_cb(lv_event_t *) {
  lv_obj_scroll_to_x(log_pane.view, 0, LV_ANIM_OFF);
  scroll_to_newest(log_pane);
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
  lv_subject_init_int(&log_version_subject, 0);
  lv_timer_create(poll_cb, kPollMs, nullptr);
  attach(log_pane, ui_TextArea1);

  if (ui_GoToOldestButton != nullptr) {
    lv_obj_add_event_cb(ui_GoToOldestButton, oldest_cb, LV_EVENT_CLICKED, nullptr);
  }
  if (ui_GoToNewestButton != nullptr) {
    lv_obj_add_event_cb(ui_GoToNewestButton, newest_cb, LV_EVENT_CLICKED, nullptr);
  }

  // The joystick gets a zero-size target to hold focus and take its keys, so
  // nothing on the screen shows a focus ring.
  lv_obj_t *keys = lv_obj_create(ui_LogScreen);
  lv_obj_remove_style_all(keys);
  lv_obj_set_size(keys, 0, 0);
  lv_obj_add_event_cb(keys, key_cb, LV_EVENT_KEY, nullptr);
  group = lv_group_create();
  lv_group_add_obj(group, keys);
}

lv_group_t *log_view_group() { return group; }

void log_view_on_load() { show_newest(log_pane); }

void log_view_attach_update(lv_obj_t *frame) {
  if (text != nullptr && frame != nullptr) {
    attach(update_pane, frame);
  }
}

void log_view_update_on_load() { show_newest(update_pane); }
