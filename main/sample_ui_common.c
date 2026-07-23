// Shared helpers for the hand-written sample_ui_* screens. Staged in main/
// (not main/ui/) so import_ui.ps1 never touches it.

#include "sample_ui_common.h"

#include "sample_ui_home.h"

static void back_button_event_cb(lv_event_t *e) {
  (void)e;
  lv_screen_load(sample_ui_home_screen);
}

lv_obj_t *sample_ui_create_screen_base(void) {
  lv_obj_t *screen = lv_obj_create(NULL);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(screen, lv_color_hex(SAMPLE_UI_COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
  return screen;
}

lv_obj_t *sample_ui_create_status_label(lv_obj_t *parent, const char *text) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  return label;
}

lv_obj_t *sample_ui_create_bar(lv_obj_t *parent, const char *title, bool with_back) {
  lv_obj_t *bar = lv_obj_create(parent);
  lv_obj_set_size(bar, LV_PCT(100), 60);
  lv_obj_set_style_bg_color(bar, lv_color_hex(SAMPLE_UI_COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(bar, 16, LV_PART_MAIN);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  // left_cluster below uses flex_grow to consume the remaining width instead
  // of relying on SPACE_BETWEEN, which is an edge case with a single child
  // (every screen except Home, which has no status cluster next to it).
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *left_cluster = lv_obj_create(bar);
  lv_obj_set_height(left_cluster, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(left_cluster, 1);
  lv_obj_set_style_bg_opa(left_cluster, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(left_cluster, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(left_cluster, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(left_cluster, 16, LV_PART_MAIN);
  lv_obj_remove_flag(left_cluster, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(left_cluster, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(left_cluster, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  if (with_back) {
    lv_obj_t *back_btn = lv_button_create(left_cluster);
    lv_obj_set_size(back_btn, 36, 36);
    lv_obj_set_style_radius(back_btn, 8, LV_PART_MAIN);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, back_button_event_cb, LV_EVENT_CLICKED, NULL);
  }

  sample_ui_create_status_label(left_cluster, title);

  return bar;
}
