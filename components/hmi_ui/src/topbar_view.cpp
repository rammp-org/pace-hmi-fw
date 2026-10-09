// TopBarView: the TopBar's clock and link labels (moved from main/frag_clock.inc).

#include "hmi_ui/topbar_view.hpp"

#include <array>
#include <cstring>
#include <ctime>

#include "hmi_format/topbar.hpp"
#include "ui.h"

#include "components/ui_comp_topbar.h"

// LVGL task.
void hmi::ui::TopBarView::clock_poll() {
  std::array<char, CLOCK_TEXT_SIZE> text{"--:--"};
  if (*config_.clock_valid) {
    const time_t now = time(nullptr);
    std::tm t{};
    gmtime_r(&now, &t);
    hmi::format::clock_text(t, text);
  }
  if (strcmp(lv_subject_get_string(&clock_subject_), text.data()) != 0) {
    lv_subject_copy_string(&clock_subject_, text.data());
  }
}

void hmi::ui::TopBarView::clock_poll_cb(lv_timer_t *timer) {
  static_cast<TopBarView *>(lv_timer_get_user_data(timer))->clock_poll();
}

void hmi::ui::TopBarView::init(const char *link) {
  lv_subject_init_string(&clock_subject_, clock_buf_.data(), clock_prev_buf_.data(),
                         clock_buf_.size(), "--:--");
  lv_subject_init_string(&link_subject_, link_buf_.data(), link_prev_buf_.data(), link_buf_.size(),
                         link);
  lv_subject_init_string(&post_text_subject_, post_buf_.data(), post_prev_buf_.data(),
                         post_buf_.size(), "");
  lv_subject_init_int(&post_colour_subject_, static_cast<int32_t>(PostColour::NONE));
}

namespace {

// The POST indicator on one TopBar (hazard-c3-spec.md §2.8): code, not SquareLine. It covers
// the battery placeholder (78 %, out of scope, C3 Q16) while it shows, and scrolls its words.
constexpr int32_t POST_X = 470;
constexpr int32_t POST_Y = 9;
constexpr int32_t POST_W = 240;
constexpr int32_t POST_H = 38;
constexpr int32_t POST_RADIUS = 6;

lv_color_t post_colour(hmi::ui::TopBarView::PostColour colour) {
  using PostColour = hmi::ui::TopBarView::PostColour;
  switch (colour) {
  case PostColour::GREY:
    return lv_color_hex(0x6B7280);
  case PostColour::AMBER:
    return lv_color_hex(0xB45309);
  case PostColour::RED:
  case PostColour::NONE:
    break;
  }
  return lv_color_hex(0xB91C1C);
}

} // namespace

void hmi::ui::TopBarView::post_colour_observer(lv_observer_t *observer, lv_subject_t *subject) {
  auto *label = static_cast<lv_obj_t *>(lv_observer_get_target(observer));
  const auto colour = static_cast<PostColour>(lv_subject_get_int(subject));
  if (colour == PostColour::NONE) {
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_set_style_bg_color(label, post_colour(colour), LV_PART_MAIN);
  lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
}

void hmi::ui::TopBarView::bind(lv_obj_t *bar) {
  if (bar != nullptr) {
    lv_label_bind_text(ui_comp_get_child(bar, UI_COMP_TOPBAR_CLOCK), &clock_subject_, nullptr);
    lv_label_bind_text(ui_comp_get_child(bar, UI_COMP_TOPBAR_LINK), &link_subject_, nullptr);
    lv_obj_t *post = lv_label_create(bar);
    lv_obj_set_pos(post, POST_X, POST_Y);
    lv_obj_set_size(post, POST_W, POST_H);
    lv_label_set_long_mode(post, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_bg_opa(post, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(post, POST_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_text_color(post, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_hor(post, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_top(post, 6, LV_PART_MAIN);
    lv_obj_remove_flag(post, LV_OBJ_FLAG_CLICKABLE);
    lv_label_bind_text(post, &post_text_subject_, nullptr);
    lv_subject_add_observer_obj(&post_colour_subject_, post_colour_observer, post, nullptr);
  }
}

void hmi::ui::TopBarView::set_post(PostColour colour, const char *text) {
  if (strcmp(lv_subject_get_string(&post_text_subject_), text) != 0) {
    lv_subject_copy_string(&post_text_subject_, text);
  }
  if (lv_subject_get_int(&post_colour_subject_) != static_cast<int32_t>(colour)) {
    lv_subject_set_int(&post_colour_subject_, static_cast<int32_t>(colour));
  }
}

void hmi::ui::TopBarView::start_clock() {
  clock_poll(); // the RTC's time, when it had one, from the first frame
  lv_timer_create(clock_poll_cb, CLOCK_POLL_MS, this);
}

void hmi::ui::TopBarView::set_link(const char *text) {
  lv_subject_copy_string(&link_subject_, text);
}
