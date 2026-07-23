// Standalone sample-UI screen: static mockup only, not wired to any sensor.
// Staged here (main/, not main/ui/) so import_ui.ps1 never touches it.

#include "sample_ui_navigation.h"

#include "sample_ui_common.h"

lv_obj_t *sample_ui_navigation_screen = NULL;

void sample_ui_navigation_init(void) {
  if (sample_ui_navigation_screen) {
    return;
  }

  sample_ui_navigation_screen = sample_ui_create_screen_base();
  sample_ui_create_bar(sample_ui_navigation_screen, "Navigation", true);

  lv_obj_t *content = lv_obj_create(sample_ui_navigation_screen);
  lv_obj_set_width(content, LV_PCT(100));
  lv_obj_set_flex_grow(content, 1);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(content, 30, LV_PART_MAIN);
  lv_obj_set_style_pad_row(content, 20, LV_PART_MAIN);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *banner = lv_label_create(content);
  lv_label_set_text(banner, LV_SYMBOL_GPS " AI navigating to: Community Center");
  lv_obj_set_style_text_color(banner, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_font(banner, &lv_font_montserrat_24, LV_PART_MAIN);

  lv_obj_t *map = lv_obj_create(content);
  lv_obj_set_width(map, LV_PCT(100));
  lv_obj_set_flex_grow(map, 1);
  lv_obj_set_style_bg_color(map, lv_color_hex(SAMPLE_UI_COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(map, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(map, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(map, 16, LV_PART_MAIN);
  lv_obj_remove_flag(map, LV_OBJ_FLAG_SCROLLABLE);

  static lv_style_t route_style;
  lv_style_init(&route_style);
  lv_style_set_line_width(&route_style, 6);
  lv_style_set_line_color(&route_style, lv_color_hex(SAMPLE_UI_COLOR_ACCENT));
  lv_style_set_line_rounded(&route_style, true);

  lv_obj_t *route = lv_line_create(map);
  static lv_point_precise_t route_points[] = {{60, 350}, {400, 200}, {900, 80}};
  lv_line_set_points(route, route_points, 3);
  lv_obj_add_style(route, &route_style, 0);

  lv_obj_t *destination_dot = lv_obj_create(map);
  lv_obj_set_size(destination_dot, 20, 20);
  lv_obj_set_style_radius(destination_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(destination_dot, lv_color_hex(SAMPLE_UI_COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(destination_dot, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(destination_dot, 0, LV_PART_MAIN);
  lv_obj_set_pos(destination_dot, 890, 70);

  lv_obj_t *stats_row = lv_obj_create(content);
  lv_obj_set_size(stats_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(stats_row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(stats_row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(stats_row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(stats_row, 40, LV_PART_MAIN);
  lv_obj_remove_flag(stats_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(stats_row, LV_FLEX_FLOW_ROW);

  lv_obj_t *eta_label = lv_label_create(stats_row);
  lv_label_set_text(eta_label, "ETA: 6 min");
  lv_obj_set_style_text_color(eta_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

  lv_obj_t *distance_label = lv_label_create(stats_row);
  lv_label_set_text(distance_label, "Distance: 0.4 mi");
  lv_obj_set_style_text_color(distance_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
}
