// DriveBandView: the Drive screen's speed readout and drive-profile buttons (moved from
// main/frag_drive_band.inc).

#include "hmi_ui/drive_band_view.hpp"

#include <cstddef>

#include "hmi_format/speed.hpp"

void hmi::ui::DriveBandView::profile_click_cb(lv_event_t *e) {
  const auto *slot = static_cast<const ProfileButton *>(lv_event_get_user_data(e));
  // Ask, do not assume: the profile subject is what the MIB reports, and it is fed
  // from MibStatus.activeProfile. Setting it here would highlight a button the chair
  // may refuse, which is the one thing these three must not do.
  slot->view->config_.store_profile(slot->profile);
  slot->view->config_.profile_clicked();
}

// Highlights the button whose mode the MIB reports: CHECKED, which the design
// draws as the negative of the resting button (spec: Button states), themed
// both ways. All three are drawn at rest, so nothing is lit until the MIB says.
void hmi::ui::DriveBandView::profile_button_observer(lv_observer_t *observer,
                                                     lv_subject_t *subject) {
  lv_obj_t *button = lv_observer_get_target_obj(observer);
  const auto *slot = static_cast<const ProfileButton *>(lv_observer_get_user_data(observer));
  const bool selected = lv_subject_get_int(subject) == slot->profile;
  lv_obj_set_state(button, LV_STATE_CHECKED, selected);
}

void hmi::ui::DriveBandView::bind_profile_button(lv_obj_t *button, ProfileButton *slot) {
  if (button == nullptr) {
    return;
  }
  lv_obj_add_event_cb(button, profile_click_cb, LV_EVENT_CLICKED, slot);
  // The label takes the inverted colour with the button (pressed and checked).
  config_.nav->mirror_states(button);
  lv_subject_add_observer_obj(&profile_, profile_button_observer, button, slot);
}

void hmi::ui::DriveBandView::bind_profile_buttons(lv_obj_t *manual, lv_obj_t *assist,
                                                  lv_obj_t *automatic) {
  const std::array<lv_obj_t *, 3> objs{manual, assist, automatic};
  for (std::size_t i = 0; i < objs.size(); ++i) {
    buttons_[i] = ProfileButton{.view = this, .profile = config_.profiles[i]};
    bind_profile_button(objs[i], &buttons_[i]);
  }
}

// Mirrors what the MIB reports out to the ADC task, which cannot take the LVGL lock,
// so the next DriveCommand carries the profile the chair actually took. It does not
// publish: the subject is fed from MibStatus now, and republishing on every sample
// would put a DriveCommand on the wire twice a second.
void hmi::ui::DriveBandView::profile_mirror_observer(lv_observer_t *observer,
                                                     lv_subject_t *subject) {
  static_cast<const DriveBandView *>(lv_observer_get_user_data(observer))
      ->config_.store_profile(lv_subject_get_int(subject));
}

void hmi::ui::DriveBandView::bind_profile_mirror() {
  lv_subject_add_observer(&profile_, profile_mirror_observer, this);
}

// The speed reaches the subject as tenths; the label wants "N.N". No built-in
// binding formats an integer that way, so hmi_format does the divide.
void hmi::ui::DriveBandView::speed_label_observer(lv_observer_t *observer, lv_subject_t *subject) {
  char text[8]; // "9.9" at most: the value is clamped to 0..SPEED_MAX_TENTHS
  hmi::format::speed_text(lv_subject_get_int(subject), text);
  lv_label_set_text(lv_observer_get_target_obj(observer), text);
}

void hmi::ui::DriveBandView::bind_speed(lv_obj_t *label) {
  lv_subject_add_observer_obj(&speed_tenths_, speed_label_observer, label, nullptr);
}
