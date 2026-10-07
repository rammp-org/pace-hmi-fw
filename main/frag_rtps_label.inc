// split_main.py fragment of main.cpp (frag_rtps_label: original lines 519-566); one TU, see main.cpp.
#include "hmi_ui/rtps_label_view.hpp"

// Bound to rtps_link_subject and rtps_blink_subject both; reads both regardless
// of which one fired.
void hmi::ui::RtpsLabelView::observer_cb(lv_observer_t *observer, lv_subject_t *) {
  const auto *view = static_cast<const RtpsLabelView *>(lv_observer_get_user_data(observer));
  lv_obj_t *label = lv_observer_get_target_obj(observer);
  const auto state = static_cast<LinkState>(lv_subject_get_int(view->config_.shared->rtps_link));

  auto color = static_cast<uint32_t>(ui_get_theme_value(_ui_theme_color_alert));
  bool blink = false;
  switch (state) {
  case LinkState::NET_FAILED: // no hardware and no link are both "there is
  case LinkState::LINK_DOWN:  // no network", and neither is worth blinking
    color = static_cast<uint32_t>(ui_get_theme_value(_ui_theme_color_alert));
    break;
  case LinkState::NO_IP:
    color = STATUS_ORANGE;
    break;
  case LinkState::NO_PEER: // link is fine, nobody is talking - the one
    color = STATUS_ORANGE; // state worth drawing the eye to
    blink = true;
    break;
  case LinkState::CONNECTED:
    // The theme's green, not a literal: the day theme's is darker, so it
    // still reads against white.
    color = static_cast<uint32_t>(ui_get_theme_value(_ui_theme_color_ok));
    break;
  }
  lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
  // Blink on opacity rather than colour: the label keeps its size so nothing in
  // the bar reflows, and it reads the same against either theme.
  const bool visible = !blink || lv_subject_get_int(view->config_.shared->rtps_blink) != 0;
  lv_obj_set_style_text_opa(label, visible ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
}

void hmi::ui::RtpsLabelView::bind(lv_obj_t *bar) {
  if (bar == nullptr) {
    return;
  }
  // The bar's own RTPS label. The spec's battery percentage took the old
  // label's place, and this used to colour and blink that instead.
  lv_obj_t *label = ui_comp_get_child(bar, UI_COMP_TOPBAR_RTPS);
  lv_subject_add_observer_obj(config_.shared->rtps_link, observer_cb, label, this);
  lv_subject_add_observer_obj(config_.shared->rtps_blink, observer_cb, label, this);
}

// The one instance, for every TopBar; bound from app_main and the screens built on demand.
static constinit hmi::ui::RtpsLabelView rtps_label_view{{.shared = &ui_shared_subjects}};
static void bind_rtps_label(lv_obj_t *bar) { rtps_label_view.bind(bar); }

/////////////////////////////////////////////////////////////////////////////
// DriveScreen: speed readout and the error banner
/////////////////////////////////////////////////////////////////////////////

