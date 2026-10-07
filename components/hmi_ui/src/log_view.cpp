// LogView: the LogScreen (moved from main/log_view.cpp). The lines come from main's log capture.

#include "hmi_ui/log_view.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "ui.h"

// Worst case per line: every character a '#', each doubled to escape it, plus
// "#rrggbb " before and "#" after for a coloured line, plus the newline.
size_t hmi::ui::LogView::line_budget() const { return 2 * config_.line_len + 9 + 2; }
size_t hmi::ui::LogView::text_len() const { return config_.lines * line_budget() + 1; }

// Lines gone from the ring's front once `count` lines have been captured.
uint32_t hmi::ui::LogView::dropped(uint32_t count) const {
  return count > config_.lines ? count - static_cast<uint32_t>(config_.lines) : 0;
}

int32_t hmi::ui::LogView::line_height() const {
  return lv_font_get_line_height(lv_obj_get_style_text_font(label_, LV_PART_MAIN)) +
         lv_obj_get_style_text_line_space(label_, LV_PART_MAIN);
}

void hmi::ui::LogView::scroll_to_newest() {
  lv_obj_update_layout(view_);
  lv_obj_scroll_by(view_, 0, -lv_obj_get_scroll_bottom(view_), LV_ANIM_OFF);
}

uint32_t hmi::ui::LogView::rebuild_text() {
  size_t pos = 0;
  const size_t len = text_len();
  char *text = text_;
  const uint32_t count =
      config_.visit([&pos, len, text](LogLineLevel level, std::string_view line) {
        const bool coloured = level != LogLineLevel::PLAIN;
        if (coloured) {
          pos += static_cast<size_t>(
              snprintf(text + pos, len - pos, "#%06" PRIx32 " ",
                       level == LogLineLevel::ERROR ? ERROR_COLOUR : WARNING_COLOUR));
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

void hmi::ui::LogView::text_observer(lv_observer_t *observer, lv_subject_t *) {
  auto *self = static_cast<LogView *>(lv_observer_get_user_data(observer));
  // Follow the newest line only if the reader is already at the bottom: someone
  // who scrolled up to read something keeps their place as lines arrive.
  const bool follow =
      self->follow_next_ || lv_obj_get_scroll_bottom(self->view_) <= FOLLOW_SLACK_PX;
  self->follow_next_ = false;
  const uint32_t before = self->rendered_count_;
  self->rendered_count_ = self->rebuild_text();
  lv_label_set_text_static(self->label_, self->text_);
  if (follow) {
    self->scroll_to_newest();
    return;
  }
  // Once the ring is full every new line pushes the oldest out, which moves
  // the whole text up a line under a reader who is standing still. Scroll back
  // by as much, so the line they were reading stays where it was.
  const uint32_t evicted = self->dropped(self->rendered_count_) - self->dropped(before);
  if (evicted > 0) {
    lv_obj_update_layout(self->view_);
    lv_obj_scroll_by_bounded(self->view_, 0, static_cast<int32_t>(evicted) * self->line_height(),
                             LV_ANIM_OFF);
  }
}

void hmi::ui::LogView::poll_cb(lv_timer_t *timer) {
  auto *self = static_cast<LogView *>(lv_timer_get_user_data(timer));
  if (lv_screen_active() != ui_LogScreen) {
    return;
  }
  const uint32_t count = self->config_.count();
  if (count == self->rendered_count_) {
    return;
  }
  lv_subject_set_int(&self->version_subject_, static_cast<int32_t>(count));
}

// A page per push, less a line so the last line of one page is the first of
// the next. Up the stick is up the log: a positive dy moves the content down,
// revealing earlier lines. Left and right page sideways through long lines.
// LVGL's key repeat turns a held stick into paging.
void hmi::ui::LogView::key_cb(lv_event_t *e) {
  auto *self = static_cast<LogView *>(lv_event_get_user_data(e));
  const uint32_t key = lv_event_get_key(e);
  // Already showing the newest line: a further push down leaves the log for
  // whatever follows it, rather than doing nothing forever.
  if (key == LV_KEY_DOWN && self->escape_down_ != nullptr &&
      lv_obj_get_scroll_bottom(self->view_) <= 0) {
    self->escape_down_();
    return;
  }
  if (key == LV_KEY_UP || key == LV_KEY_DOWN) {
    const int32_t line = self->line_height();
    const int32_t page = std::max<int32_t>(line, lv_obj_get_content_height(self->view_) - line);
    lv_obj_scroll_by_bounded(self->view_, 0, key == LV_KEY_UP ? page : -page, LV_ANIM_ON);
  } else if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
    const int32_t page = std::max<int32_t>(
        HORIZONTAL_OVERLAP_PX, lv_obj_get_content_width(self->view_) - HORIZONTAL_OVERLAP_PX);
    lv_obj_scroll_by_bounded(self->view_, key == LV_KEY_LEFT ? page : -page, 0, LV_ANIM_ON);
  }
}

void hmi::ui::LogView::init() {
  if (ui_TextArea1 == nullptr || ui_LogScreen == nullptr) {
    return;
  }
  text_ = config_.alloc_text(text_len());
  if (text_ == nullptr) {
    return; // no PSRAM to spare: the export's placeholder text stays
  }

  // TextArea1 as a frame only: its own text, placeholder and cursor go, and it
  // takes no clicks, keys or scrolling, so none of its editing behaviour can
  // reach the log.
  lv_textarea_set_text(ui_TextArea1, "");
  lv_textarea_set_placeholder_text(ui_TextArea1, "");
  lv_obj_add_flag(lv_textarea_get_label(ui_TextArea1), LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_opa(ui_TextArea1, LV_OPA_TRANSP, LV_PART_CURSOR);
  lv_textarea_set_cursor_click_pos(ui_TextArea1, false);
  lv_obj_remove_flag(ui_TextArea1, LV_OBJ_FLAG_CLICK_FOCUSABLE);
  lv_obj_remove_flag(ui_TextArea1, LV_OBJ_FLAG_SCROLLABLE);

  // The view keeps the theme's scrollbars but none of its box: the frame
  // already draws the background and border.
  view_ = lv_obj_create(ui_TextArea1);
  lv_obj_set_size(view_, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(view_, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(view_, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(view_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(view_, 0, LV_PART_MAIN);
  lv_obj_set_scroll_dir(view_, LV_DIR_ALL);
  lv_obj_set_scrollbar_mode(view_, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_remove_flag(view_, LV_OBJ_FLAG_SCROLL_CHAIN);

  // Content-sized, so a line is never wrapped: a long one runs off to the
  // right and the view scrolls sideways to it.
  label_ = lv_label_create(view_);
  lv_obj_set_size(label_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  // The theme's text colour, set on the label itself: inherited, it would come
  // from the view, which the LVGL theme gives its own (dark) text colour.
  // Themeable, so a theme switch recolours it with the rest of the UI.
  ui_object_set_themeable_style_property(label_, LV_PART_MAIN, LV_STYLE_TEXT_COLOR,
                                         _ui_theme_color_text);
  ui_object_set_themeable_style_property(label_, LV_PART_MAIN, LV_STYLE_TEXT_OPA,
                                         _ui_theme_alpha_text);
  lv_label_set_recolor(label_, true);
  lv_label_set_text_static(label_, "");

  lv_subject_init_int(&version_subject_, 0);
  lv_subject_add_observer_obj(&version_subject_, text_observer, view_, this);
  lv_timer_create(poll_cb, POLL_MS, this);

  // The joystick gets a zero-size target to hold focus and take its keys, so
  // nothing on the screen shows a focus ring.
  lv_obj_t *keys = lv_obj_create(ui_LogScreen);
  lv_obj_remove_style_all(keys);
  lv_obj_set_size(keys, 0, 0);
  lv_obj_add_event_cb(keys, key_cb, LV_EVENT_KEY, this);
  group_ = lv_group_create();
  lv_group_add_obj(group_, keys);
}

void hmi::ui::LogView::on_load() {
  if (text_ == nullptr) {
    return;
  }
  follow_next_ = true;
  lv_obj_scroll_to_x(view_, 0, LV_ANIM_OFF);
  lv_subject_notify(&version_subject_); // rebuild now rather than on the next tick
}
