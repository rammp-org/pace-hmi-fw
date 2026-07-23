// Standalone sample-UI screen: static mockup only, not wired to any sensor.
// Staged here (main/, not main/ui/) so import_ui.ps1 never touches it.

#include "sample_ui_wellness.h"

#include "sample_ui_common.h"

lv_obj_t *sample_ui_wellness_screen = NULL;

static lv_obj_t *create_stat_card(lv_obj_t *parent, const char *title, const char *value) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, 320, 260);
  lv_obj_set_style_radius(card, 24, LV_PART_MAIN);
  lv_obj_set_style_bg_color(card, lv_color_hex(SAMPLE_UI_COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *value_label = lv_label_create(card);
  lv_label_set_text(value_label, value);
  lv_obj_set_style_text_color(value_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_font(value_label, &lv_font_montserrat_24, LV_PART_MAIN);

  lv_obj_t *title_label = lv_label_create(card);
  lv_label_set_text(title_label, title);
  lv_obj_set_style_text_color(title_label, lv_color_hex(0xAAAAAA), LV_PART_MAIN);

  return card;
}

void sample_ui_wellness_init(void) {
  if (sample_ui_wellness_screen) {
    return;
  }

  sample_ui_wellness_screen = sample_ui_create_screen_base();
  sample_ui_create_bar(sample_ui_wellness_screen, "Wellness", true);

  lv_obj_t *content = lv_obj_create(sample_ui_wellness_screen);
  lv_obj_set_width(content, LV_PCT(100));
  lv_obj_set_flex_grow(content, 1);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(content, 40, LV_PART_MAIN);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  create_stat_card(content, "Heart Rate", "72 bpm");
  create_stat_card(content, "Posture", "Good");
  create_stat_card(content, "Sitting Time", "1h 20m");
}
