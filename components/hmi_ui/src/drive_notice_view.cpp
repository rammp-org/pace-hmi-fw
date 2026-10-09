// The Drive screen's notice slot: see drive_notice_view.hpp.

#include "hmi_ui/drive_notice_view.hpp"

#include "hmi_rtps_spec.hpp"
#include "ui.h"

const char *hmi::ui::post_reason_text(hmi::stick::PostGate gate, const char *check_text) {
  using hmi::stick::PostGate;
  switch (gate) {
  case PostGate::PASS:
    return nullptr;
  case PostGate::NOT_RUN:
    return rammp::kHmiNoticePostNotRun;
  case PostGate::PENDING:
    return check_text != nullptr && check_text[0] != '\0' ? check_text
                                                          : rammp::kHmiNoticePostRunning;
  case PostGate::FAIL:
    break;
  }
  // FAIL, or a byte outside the enum: not passed.
  return check_text != nullptr && check_text[0] != '\0' ? check_text : rammp::kHmiNoticePostFailed;
}

const char *hmi::ui::drive_notice_text(hmi::drive_adapter::DriveNotice notice,
                                       const char *post_reason) {
  using hmi::drive_adapter::DriveNotice;
  switch (notice) {
  case DriveNotice::MCB_DID_NOT_STOP:
    return rammp::kHmiNoticeMcbDidNotStop;
  case DriveNotice::STOPPING:
    return rammp::kHmiNoticeStopping;
  case DriveNotice::MOTION_GUARD:
    return rammp::kHmiNoticeWaitingForMcb;
  case DriveNotice::NOT_CALIBRATED:
    return rammp::kHmiNoticeNotCalibrated;
  case DriveNotice::POST_NOT_PASSED:
    return post_reason != nullptr ? post_reason : rammp::kHmiNoticePostNotRun;
  case DriveNotice::STICK_FAULT:
    return rammp::kHmiNoticeStickFault;
  case DriveNotice::STICK_CHECK:
    return rammp::kHmiNoticeStickCheck;
  case DriveNotice::CENTRE_FIRST:
    return rammp::kHmiNoticeCentreFirst;
  case DriveNotice::NONE:
    return nullptr;
  }
  return nullptr; // not a notice: nothing shown
}

void hmi::ui::DriveNoticeView::build(lv_obj_t *screen) {
  lv_subject_init_int(&notice_, static_cast<int32_t>(hmi::drive_adapter::DriveNotice::NONE));
  label_ = lv_label_create(screen);
  lv_obj_set_width(label_, lv_pct(100));
  lv_obj_set_height(label_, LV_SIZE_CONTENT);
  lv_obj_set_pos(label_, 0, SLOT_Y);
  lv_obj_set_style_pad_all(label_, SLOT_PAD, LV_PART_MAIN);
  lv_obj_set_style_bg_color(label_, lv_color_hex(SLOT_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(label_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(label_, lv_color_hex(SLOT_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(label_, &ui_font_IBMPlexSansRegular34, LV_PART_MAIN);
  lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_long_mode(label_, LV_LABEL_LONG_WRAP);
  lv_obj_remove_flag(label_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(label_, LV_OBJ_FLAG_HIDDEN);
  lv_subject_add_observer_obj(&notice_, observer, label_, this);
  lv_timer_create(refresh_cb, REFRESH_MS, this);
}

// While the notice says why POST has not passed, its words follow the gate (PENDING to FAIL,
// the blocking check changing): re-run the observer.
void hmi::ui::DriveNoticeView::refresh_cb(lv_timer_t *timer) {
  auto *self = static_cast<DriveNoticeView *>(lv_timer_get_user_data(timer));
  if (lv_subject_get_int(&self->notice_) ==
      static_cast<int32_t>(hmi::drive_adapter::DriveNotice::POST_NOT_PASSED)) {
    lv_subject_notify(&self->notice_);
  }
}

void hmi::ui::DriveNoticeView::show(hmi::drive_adapter::DriveNotice notice) {
  lv_subject_set_int(&notice_, static_cast<int32_t>(notice));
}

void hmi::ui::DriveNoticeView::observer(lv_observer_t *observer, lv_subject_t *subject) {
  const auto *self = static_cast<const DriveNoticeView *>(lv_observer_get_user_data(observer));
  lv_obj_t *label = lv_observer_get_target_obj(observer);
  const char *text =
      drive_notice_text(static_cast<hmi::drive_adapter::DriveNotice>(lv_subject_get_int(subject)),
                        self->config_.post_reason ? self->config_.post_reason() : nullptr);
  if (text == nullptr) {
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_label_set_text(label, text); // copied: the POST words change under the same pointer
  lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
}
