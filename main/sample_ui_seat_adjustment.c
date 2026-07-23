// Standalone sample-UI screen: static mockup only, not wired to any sensor.
// Staged here (main/, not main/ui/) so import_ui.ps1 never touches it.

#include "sample_ui_seat_adjustment.h"

#include "sample_ui_common.h"

lv_obj_t *sample_ui_seat_adjustment_screen = NULL;

static void slider_event_cb(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target_obj(e);
  lv_obj_t *value_label = (lv_obj_t *)lv_event_get_user_data(e);
  lv_label_set_text_fmt(value_label, "%d%%", (int)lv_slider_get_value(slider));
}

static void create_slider_row(lv_obj_t *parent, const char *name, int32_t initial_value) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(row, 20, LV_PART_MAIN);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *name_label = lv_label_create(row);
  lv_label_set_text(name_label, name);
  lv_obj_set_style_text_color(name_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_width(name_label, 160);

  lv_obj_t *slider = lv_slider_create(row);
  lv_obj_set_flex_grow(slider, 1);
  lv_slider_set_range(slider, 0, 100);
  lv_slider_set_value(slider, initial_value, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slider, lv_color_hex(SAMPLE_UI_COLOR_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, lv_color_hex(SAMPLE_UI_COLOR_ACCENT), LV_PART_KNOB);

  lv_obj_t *value_label = lv_label_create(row);
  lv_label_set_text_fmt(value_label, "%d%%", (int)initial_value);
  lv_obj_set_style_text_color(value_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_width(value_label, 60);

  lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, value_label);
}

void sample_ui_seat_adjustment_init(void) {
  if (sample_ui_seat_adjustment_screen) {
    return;
  }

  sample_ui_seat_adjustment_screen = sample_ui_create_screen_base();
  sample_ui_create_bar(sample_ui_seat_adjustment_screen, "Seat Adjustment", true);

  lv_obj_t *content = lv_obj_create(sample_ui_seat_adjustment_screen);
  lv_obj_set_width(content, LV_PCT(100));
  lv_obj_set_flex_grow(content, 1);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(content, 60, LV_PART_MAIN);
  lv_obj_set_style_pad_row(content, 40, LV_PART_MAIN);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  create_slider_row(content, "Recline", 40);
  create_slider_row(content, "Height", 55);
  create_slider_row(content, "Tilt", 30);
}
