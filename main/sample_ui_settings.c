// Standalone sample-UI screen: static mockup only, not wired to any sensor.
// Staged here (main/, not main/ui/) so import_ui.ps1 never touches it.

#include "sample_ui_settings.h"

#include "sample_ui_common.h"

lv_obj_t *sample_ui_settings_screen = NULL;

static void create_switch_row(lv_obj_t *parent, const char *label_text, bool default_on) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *label = lv_label_create(row);
  lv_label_set_text(label, label_text);
  lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

  lv_obj_t *sw = lv_switch_create(row);
  lv_obj_set_style_bg_color(sw, lv_color_hex(SAMPLE_UI_COLOR_ACCENT),
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
  if (default_on) {
    lv_obj_add_state(sw, LV_STATE_CHECKED);
  }
}

void sample_ui_settings_init(void) {
  if (sample_ui_settings_screen) {
    return;
  }

  sample_ui_settings_screen = sample_ui_create_screen_base();
  sample_ui_create_bar(sample_ui_settings_screen, "Settings", true);

  lv_obj_t *content = lv_obj_create(sample_ui_settings_screen);
  lv_obj_set_width(content, LV_PCT(100));
  lv_obj_set_flex_grow(content, 1);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(content, 60, LV_PART_MAIN);
  lv_obj_set_style_pad_row(content, 30, LV_PART_MAIN);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  create_switch_row(content, "Dark Theme", true);
  create_switch_row(content, "Voice Assistant", true);
  create_switch_row(content, "Larger Text", false);
}
