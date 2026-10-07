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
}

void hmi::ui::TopBarView::bind(lv_obj_t *bar) {
  if (bar != nullptr) {
    lv_label_bind_text(ui_comp_get_child(bar, UI_COMP_TOPBAR_CLOCK), &clock_subject_, nullptr);
    lv_label_bind_text(ui_comp_get_child(bar, UI_COMP_TOPBAR_LINK), &link_subject_, nullptr);
  }
}

void hmi::ui::TopBarView::start_clock() {
  clock_poll(); // the RTC's time, when it had one, from the first frame
  lv_timer_create(clock_poll_cb, CLOCK_POLL_MS, this);
}

void hmi::ui::TopBarView::set_link(const char *text) {
  lv_subject_copy_string(&link_subject_, text);
}
