// Standalone sample-UI screen: static mockup only, not wired to any sensor.
// Staged here (main/, not main/ui/) so import_ui.ps1 never touches it.

#include "sample_ui_home.h"

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

static lv_obj_t *create_status_label(lv_obj_t *parent, const char *text) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  return label;
}

static lv_obj_t *create_nav_button(lv_obj_t *parent, const char *text) {
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_set_size(btn, 340, 340);
  lv_obj_set_style_radius(btn, 24, LV_PART_MAIN);

  lv_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_center(label);
  return btn;
}

void sample_ui_home_init(void) {
  if (sample_ui_home_screen) {
    return;
  }

  sample_ui_home_screen = lv_obj_create(NULL);
  lv_obj_remove_flag(sample_ui_home_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(sample_ui_home_screen, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(sample_ui_home_screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_flex_flow(sample_ui_home_screen, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *status_bar = lv_obj_create(sample_ui_home_screen);
  lv_obj_set_size(status_bar, LV_PCT(100), 60);
  lv_obj_set_style_bg_color(status_bar, lv_color_hex(0x101010), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(status_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(status_bar, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(status_bar, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(status_bar, 16, LV_PART_MAIN);
  lv_obj_remove_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(status_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(status_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  create_status_label(status_bar, "RAMMP Sample UI");

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

  clock_label = create_status_label(status_cluster, "09:41");
  create_status_label(status_cluster, LV_SYMBOL_WIFI);
  create_status_label(status_cluster, LV_SYMBOL_BATTERY_FULL " 82%");

  lv_obj_t *content = lv_obj_create(sample_ui_home_screen);
  lv_obj_set_width(content, LV_PCT(100));
  lv_obj_set_flex_grow(content, 1);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(content, 60, LV_PART_MAIN);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  create_nav_button(content, "Drive Mode");
  create_nav_button(content, "Seat\nAdjustment");

  lv_timer_t *clock_timer = lv_timer_create(clock_timer_cb, 60000, NULL);
  lv_obj_add_event_cb(sample_ui_home_screen, screen_delete_cb, LV_EVENT_DELETE, clock_timer);
}
