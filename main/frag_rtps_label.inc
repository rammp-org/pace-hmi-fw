// split_main.py fragment of main.cpp (frag_rtps_label: original lines 519-566); one TU, see main.cpp.
// Bound to rtps_link_subject and rtps_blink_subject both; reads both regardless
// of which one fired.
static void rtps_label_observer(lv_observer_t *observer, lv_subject_t *) {
  lv_obj_t *label = lv_observer_get_target_obj(observer);
  const auto state = static_cast<RtpsLinkState>(lv_subject_get_int(&rtps_link_subject));

  uint32_t color = ui_get_theme_value(_ui_theme_color_alert);
  bool blink = false;
  switch (state) {
  case RtpsLinkState::NET_FAILED: // no hardware and no link are both "there is
  case RtpsLinkState::LINK_DOWN:  // no network", and neither is worth blinking
    color = ui_get_theme_value(_ui_theme_color_alert);
    break;
  case RtpsLinkState::NO_IP:
    color = kStatusOrange;
    break;
  case RtpsLinkState::NO_PEER: // link is fine, nobody is talking - the one
    color = kStatusOrange;     // state worth drawing the eye to
    blink = true;
    break;
  case RtpsLinkState::CONNECTED:
    // The theme's green, not a literal: the day theme's is darker, so it
    // still reads against white.
    color = ui_get_theme_value(_ui_theme_color_ok);
    break;
  }
  lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
  // Blink on opacity rather than colour: the label keeps its size so nothing in
  // the bar reflows, and it reads the same against either theme.
  const bool visible = !blink || lv_subject_get_int(&rtps_blink_subject) != 0;
  lv_obj_set_style_text_opa(label, visible ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
}

static void bind_rtps_label(lv_obj_t *bar) {
  if (bar == nullptr) {
    return;
  }
  // The bar's own RTPS label. The spec's battery percentage took the old
  // label's place, and this used to colour and blink that instead.
  lv_obj_t *label = ui_comp_get_child(bar, UI_COMP_TOPBAR_RTPS);
  lv_subject_add_observer_obj(&rtps_link_subject, rtps_label_observer, label, nullptr);
  lv_subject_add_observer_obj(&rtps_blink_subject, rtps_label_observer, label, nullptr);
}

/////////////////////////////////////////////////////////////////////////////
// DriveScreen: speed readout and the error banner
/////////////////////////////////////////////////////////////////////////////

