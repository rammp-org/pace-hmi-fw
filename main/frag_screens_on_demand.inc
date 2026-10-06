// split_main.py fragment of main.cpp (frag_screens_on_demand: original lines 4106-4236); one TU, see main.cpp.
/////////////////////////////////////////////////////////////////////////////
// Screens built on demand
//
// ui_init builds every screen at boot, and every widget lands in internal RAM
// - the DMA-capable pool the W5500's SPI bounce buffer is later allocated from,
// with no NULL check behind it. With the Settings and Skunk Works screens both
// resident that pool bottomed out at 415 B, after the board had already
// boot-looped on it. So these, and Diagnostics, are destroyed right after
// ui_init, built when opened, and destroyed again once left: at most one exists
// at a time, and none while RTPS starts.
//
// Everything the firmware hangs on one of them - DriveBand, TopBar and menu
// bindings, warning banner, load hooks - is redone by its *_ensure
// function each time it is built. All of it is object-bound, so it goes with
// the screen.
/////////////////////////////////////////////////////////////////////////////

// Deferred with lv_async_call: the SCREEN_UNLOADED event that asks for it is
// the screen's own, and an object is not deleted from inside its own event.
// Skipped if the screen was shown again in between.
static void settings_screen_destroy_cb(void *) {
  if (ui_SettingsScreen != nullptr && lv_screen_active() != ui_SettingsScreen) {
    ui_SettingsScreen_screen_destroy();
  }
}

static void setting_screen_unloaded_cb(lv_event_t *) {
  setting_rows_clear(); // rows first, so the press-flash timer forgets its button
  lv_async_call(settings_screen_destroy_cb, nullptr);
}

static void actions_screen_destroy_cb(void *) {
  if (ui_SkunkWorksScreen != nullptr && lv_screen_active() != ui_SkunkWorksScreen) {
    ui_SkunkWorksScreen_screen_destroy();
  }
}

static void actions_screen_unloaded_cb(lv_event_t *) {
  actions_clear();
  lv_async_call(actions_screen_destroy_cb, nullptr);
}

static void settings_screen_ensure() {
  if (ui_SettingsScreen != nullptr) {
    return;
  }
  ui_SettingsScreen_screen_init();
  // Parameter1 is only the row template: setting_page_open builds the rows.
  // The component's own delete handler frees the child-index array it made.
  lv_obj_delete(ui_Parameter1);
  ui_Parameter1 = nullptr;
  bind_status_panel(ui_DriveBand7);
  bind_rtps_label(ui_TopBar8);
  bind_topbar_labels(ui_TopBar8);
  nav_attach_chrome(ui_MenuKey7, ui_MenuOverlay7, ui_DriveBand7);
  bind_to_drive_blocked_cause(ui_ErrorBanner6, setting_warning_observer);
  lv_subject_add_observer_obj(&setting_page_subject, setting_warning_observer, ui_ErrorBanner6,
                              nullptr);
  lv_obj_add_event_cb(ui_SettingsScreen, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
  lv_obj_add_event_cb(ui_SettingsScreen, setting_screen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED,
                      nullptr);
  strip_screen_overdraw(ui_SettingsScreen);
}

static void actions_screen_ensure() {
  if (ui_SkunkWorksScreen != nullptr) {
    return;
  }
  ui_SkunkWorksScreen_screen_init();
  // Everything in the flex panel but the title is a placeholder drawn in
  // SquareLine: actions_open builds one button per actions_spec.h entry.
  // Deleted without naming them, so adding or removing placeholders in
  // SquareLine needs no change here.
  for (int32_t i = static_cast<int32_t>(lv_obj_get_child_count(ui_GenericActionsFlexPanel)) - 1;
       i >= 0; i--) {
    lv_obj_t *child = lv_obj_get_child(ui_GenericActionsFlexPanel, i);
    if (child != ui_SkunkWorksTitle) {
      lv_obj_delete(child);
    }
  }
  bind_status_panel(ui_DriveBand8);
  bind_rtps_label(ui_TopBar9);
  bind_topbar_labels(ui_TopBar9);
  nav_attach_chrome(ui_MenuKey8, ui_MenuOverlay8, ui_DriveBand8);
  // Its ErrorBanner stays down: an action that needs the MCB greys out
  // instead (action_ready_observer), which keeps the local ones reachable.
  lv_obj_add_flag(ui_ErrorBanner7, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(ui_SkunkWorksScreen, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
  lv_obj_add_event_cb(ui_SkunkWorksScreen, actions_screen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED,
                      nullptr);
  strip_screen_overdraw(ui_SkunkWorksScreen);
}

static void diagnostics_screen_destroy_cb(void *) {
  if (ui_DiagnosticsScreen != nullptr && lv_screen_active() != ui_DiagnosticsScreen) {
    ui_DiagnosticsScreen_screen_destroy();
  }
}

static void diagnostics_screen_unloaded_cb(lv_event_t *) {
  diag_rows_clear();
  lv_async_call(diagnostics_screen_destroy_cb, nullptr);
}

static void diagnostics_screen_ensure() {
  if (ui_DiagnosticsScreen != nullptr) {
    return;
  }
  ui_DiagnosticsScreen_screen_init();
  // Everything in the rows panel is SquareLine's template: diagnostics_open
  // builds one row per RAMMP_DIAG_TABLE entry instead.
  for (int32_t i = static_cast<int32_t>(lv_obj_get_child_count(ui_DiagnosticsFlexRows)) - 1; i >= 0;
       i--) {
    lv_obj_delete(lv_obj_get_child(ui_DiagnosticsFlexRows, i));
  }
  bind_status_panel(ui_DriveBand9);
  bind_rtps_label(ui_TopBar10);
  bind_topbar_labels(ui_TopBar10);
  nav_attach_chrome(ui_MenuKey9, ui_MenuOverlay9, ui_DriveBand9);
  // The red, blinking readings are this screen's warning: the ErrorBanner
  // would cover exactly what someone opened the screen to look at.
  lv_obj_add_flag(ui_ErrorBanner8, LV_OBJ_FLAG_HIDDEN);
  for (lv_subject_t *subject : {&diag_rate_subject, &diag_stale_subject, &rtps_blink_subject}) {
    lv_subject_add_observer_obj(subject, diag_freq_observer, ui_DiagnosticsFreqLabel, nullptr);
  }
  lv_obj_add_event_cb(ui_DiagnosticsScreen, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
  lv_obj_add_event_cb(ui_DiagnosticsScreen, diagnostics_screen_unloaded_cb,
                      LV_EVENT_SCREEN_UNLOADED, nullptr);
  strip_screen_overdraw(ui_DiagnosticsScreen);
}

