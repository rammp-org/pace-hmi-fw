// Standalone sample-UI screen: static mockup only, not wired to any sensor.
// Staged here (main/, not main/ui/) so import_ui.ps1 never touches it.

#include "sample_ui_home.h"

#include "sample_ui_common.h"
#include "sample_ui_drive_mode.h"
#include "sample_ui_navigation.h"
#include "sample_ui_seat_adjustment.h"
#include "sample_ui_settings.h"
#include "sample_ui_wellness.h"

lv_obj_t *sample_ui_home_screen = NULL;

static lv_obj_t *clock_label;

static void clock_timer_cb(lv_timer_t *timer) {
  (void)timer;
  static int hour = 9, minute = 41;
  if (++minute >= 60) {
    minute = 0;
    hour = (hour + 1) % 24;
  }
  lv_label_set_text_fmt(clock_label, "%02d:%02d", hour, minute);
}

static void screen_delete_cb(lv_event_t *e) {
  lv_timer_delete((lv_timer_t *)lv_event_get_user_data(e));
  clock_label = NULL;
}

static void nav_tile_event_cb(lv_event_t *e) {
  lv_obj_t **target_screen = (lv_obj_t **)lv_event_get_user_data(e);
  lv_screen_load(*target_screen);
}

static lv_obj_t *create_nav_tile(lv_obj_t *parent, const char *text, lv_obj_t **target_screen) {
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_set_size(btn, 220, 220);
  lv_obj_set_style_radius(btn, 24, LV_PART_MAIN);

  lv_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_center(label);

  lv_obj_add_event_cb(btn, nav_tile_event_cb, LV_EVENT_CLICKED, target_screen);
  return btn;
}

void sample_ui_home_init(void) {
  if (sample_ui_home_screen) {
    return;
  }

  // Sub-screens are created up front so their extern screen pointers are
  // valid by the time a nav tile below is clicked.
  sample_ui_drive_mode_init();
  sample_ui_seat_adjustment_init();
  sample_ui_navigation_init();
  sample_ui_wellness_init();
  sample_ui_settings_init();

  sample_ui_home_screen = sample_ui_create_screen_base();

  lv_obj_t *status_bar = sample_ui_create_bar(sample_ui_home_screen, "RAMMP Sample UI", false);

  lv_obj_t *status_cluster = lv_obj_create(status_bar);
  lv_obj_set_size(status_cluster, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(status_cluster, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(status_cluster, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(status_cluster, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(status_cluster, 20, LV_PART_MAIN);
  lv_obj_remove_flag(status_cluster, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(status_cluster, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(status_cluster, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  clock_label = sample_ui_create_status_label(status_cluster, "09:41");
  sample_ui_create_status_label(status_cluster, LV_SYMBOL_WIFI);
  sample_ui_create_status_label(status_cluster, LV_SYMBOL_BATTERY_FULL " 82%");

  lv_obj_t *content = lv_obj_create(sample_ui_home_screen);
  lv_obj_set_width(content, LV_PCT(100));
  lv_obj_set_flex_grow(content, 1);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(content, 40, LV_PART_MAIN);
  lv_obj_set_style_pad_row(content, 30, LV_PART_MAIN);
  lv_obj_set_style_pad_column(content, 30, LV_PART_MAIN);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  create_nav_tile(content, "Drive Mode", &sample_ui_drive_mode_screen);
  create_nav_tile(content, "Seat\nAdjustment", &sample_ui_seat_adjustment_screen);
  create_nav_tile(content, "Navigation", &sample_ui_navigation_screen);
  create_nav_tile(content, "Wellness", &sample_ui_wellness_screen);
  create_nav_tile(content, "Settings", &sample_ui_settings_screen);

  lv_timer_t *clock_timer = lv_timer_create(clock_timer_cb, 60000, NULL);
  lv_obj_add_event_cb(sample_ui_home_screen, screen_delete_cb, LV_EVENT_DELETE, clock_timer);
}
