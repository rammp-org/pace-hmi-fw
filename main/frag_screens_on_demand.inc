// split_main.py fragment of main.cpp (frag_screens_on_demand: original lines 4106-4236); one TU, see main.cpp.
/////////////////////////////////////////////////////////////////////////////
// Screens built on demand (hmi::ui::OnDemandScreens)
/////////////////////////////////////////////////////////////////////////////
#include "hmi_ui/on_demand_screens.hpp"

// The one instance.
static constinit hmi::ui::OnDemandScreens on_demand_screens{{
    .bind_chrome =
        [](lv_obj_t *band, lv_obj_t *bar, lv_obj_t *key, lv_obj_t *overlay) {
          bind_status_panel(band);
          bind_rtps_label(bar);
          bind_topbar_labels(bar);
          nav_attach_chrome(key, overlay, band);
        },
    .settings_bound =
        [] {
          bind_to_drive_blocked_cause(ui_ErrorBanner6, setting_warning_observer);
          lv_subject_add_observer_obj(settings_view.page(), setting_warning_observer,
                                      ui_ErrorBanner6, nullptr);
        },
    .diagnostics_bound =
        [] {
          for (lv_subject_t *subject :
               {diag_view.rate(), diag_view.stale(), &rtps_blink_subject}) {
            lv_subject_add_observer_obj(subject, diag_freq_observer, ui_DiagnosticsFreqLabel,
                                        nullptr);
          }
        },
    .settings_left = setting_rows_clear,
    .actions_left = actions_clear,
    .diagnostics_left = diag_rows_clear,
    .screen_loaded = screen_loaded_cb,
    .strip_overdraw = [](const lv_obj_t *screen) { (void)strip_screen_overdraw(screen); },
}};

static void settings_screen_ensure() { on_demand_screens.ensure_settings(); }
static void actions_screen_ensure() { on_demand_screens.ensure_actions(); }
static void diagnostics_screen_ensure() { on_demand_screens.ensure_diagnostics(); }
