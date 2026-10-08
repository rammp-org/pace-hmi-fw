// OnDemandScreens: the screens built when opened and destroyed once left (moved from
// main/frag_screens_on_demand.inc).

#include "hmi_ui/on_demand_screens.hpp"

#include "ui.h"

void hmi::ui::OnDemandScreens::settings_destroy_cb(void *) {
  if (ui_SettingsScreen != nullptr && lv_screen_active() != ui_SettingsScreen) {
    ui_SettingsScreen_screen_destroy();
  }
}

void hmi::ui::OnDemandScreens::settings_unloaded_cb(lv_event_t *e) {
  // rows first, so the press-flash timer forgets its button
  static_cast<const OnDemandScreens *>(lv_event_get_user_data(e))->config_.settings_left();
  lv_async_call(settings_destroy_cb, nullptr);
}

void hmi::ui::OnDemandScreens::actions_destroy_cb(void *) {
  if (ui_SkunkWorksScreen != nullptr && lv_screen_active() != ui_SkunkWorksScreen) {
    ui_SkunkWorksScreen_screen_destroy();
  }
}

void hmi::ui::OnDemandScreens::actions_unloaded_cb(lv_event_t *e) {
  static_cast<const OnDemandScreens *>(lv_event_get_user_data(e))->config_.actions_left();
  lv_async_call(actions_destroy_cb, nullptr);
}

void hmi::ui::OnDemandScreens::ensure_settings() {
  if (ui_SettingsScreen != nullptr) {
    return;
  }
  ui_SettingsScreen_screen_init();
  // Parameter1 is only the row template: setting_page_open builds the rows.
  // The component's own delete handler frees the child-index array it made.
  lv_obj_delete(ui_Parameter1);
  ui_Parameter1 = nullptr;
  config_.bind_chrome(ui_DriveBand7, ui_TopBar8, ui_MenuKey7, ui_MenuOverlay7);
  config_.settings_bound();
  lv_obj_add_event_cb(ui_SettingsScreen, config_.screen_loaded, LV_EVENT_SCREEN_LOADED,
                      config_.screen_loaded_data);
  lv_obj_add_event_cb(ui_SettingsScreen, settings_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, this);
  config_.strip_overdraw(ui_SettingsScreen);
}

void hmi::ui::OnDemandScreens::ensure_actions() {
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
  config_.bind_chrome(ui_DriveBand8, ui_TopBar9, ui_MenuKey8, ui_MenuOverlay8);
  // Its ErrorBanner stays down: an action that needs the MCB greys out
  // instead (action_ready_observer), which keeps the local ones reachable.
  lv_obj_add_flag(ui_ErrorBanner7, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(ui_SkunkWorksScreen, config_.screen_loaded, LV_EVENT_SCREEN_LOADED,
                      config_.screen_loaded_data);
  lv_obj_add_event_cb(ui_SkunkWorksScreen, actions_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, this);
  config_.strip_overdraw(ui_SkunkWorksScreen);
}

void hmi::ui::OnDemandScreens::diagnostics_destroy_cb(void *) {
  if (ui_DiagnosticsScreen != nullptr && lv_screen_active() != ui_DiagnosticsScreen) {
    ui_DiagnosticsScreen_screen_destroy();
  }
}

void hmi::ui::OnDemandScreens::diagnostics_unloaded_cb(lv_event_t *e) {
  static_cast<const OnDemandScreens *>(lv_event_get_user_data(e))->config_.diagnostics_left();
  lv_async_call(diagnostics_destroy_cb, nullptr);
}

void hmi::ui::OnDemandScreens::ensure_diagnostics() {
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
  config_.bind_chrome(ui_DriveBand9, ui_TopBar10, ui_MenuKey9, ui_MenuOverlay9);
  // The red, blinking readings are this screen's warning: the ErrorBanner
  // would cover exactly what someone opened the screen to look at.
  lv_obj_add_flag(ui_ErrorBanner8, LV_OBJ_FLAG_HIDDEN);
  config_.diagnostics_bound();
  lv_obj_add_event_cb(ui_DiagnosticsScreen, config_.screen_loaded, LV_EVENT_SCREEN_LOADED,
                      config_.screen_loaded_data);
  lv_obj_add_event_cb(ui_DiagnosticsScreen, diagnostics_unloaded_cb, LV_EVENT_SCREEN_UNLOADED,
                      this);
  config_.strip_overdraw(ui_DiagnosticsScreen);
}
