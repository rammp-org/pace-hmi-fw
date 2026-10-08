// The Drive screen's notice slot: see drive_notice_view.hpp.

#include "hmi_ui/drive_notice_view.hpp"

#include "hmi_rtps_spec.hpp"
#include "ui.h"

const char *hmi::ui::drive_notice_text(hmi::drive_adapter::DriveNotice notice) {
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
    return rammp::kHmiNoticePostNotRun;
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
}

void hmi::ui::DriveNoticeView::show(hmi::drive_adapter::DriveNotice notice) {
  lv_subject_set_int(&notice_, static_cast<int32_t>(notice));
}

void hmi::ui::DriveNoticeView::observer(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *label = lv_observer_get_target_obj(observer);
  const char *text =
      drive_notice_text(static_cast<hmi::drive_adapter::DriveNotice>(lv_subject_get_int(subject)));
  if (text == nullptr) {
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_label_set_text_static(label, text);
  lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
}
