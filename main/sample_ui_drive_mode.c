// Standalone sample-UI screen: static mockup only, not wired to any sensor.
// Staged here (main/, not main/ui/) so import_ui.ps1 never touches it.

#include "sample_ui_drive_mode.h"

#include "sample_ui_common.h"

lv_obj_t *sample_ui_drive_mode_screen = NULL;

static lv_obj_t *mode_cards[3];
static const char *kModeNames[3] = {"Indoor", "Outdoor", "Sport"};
static const char *kModeDescriptions[3] = {"Smooth, low speed", "Stable, all terrain",
                                           "Max speed, responsive"};

static void mode_card_event_cb(lv_event_t *e) {
  lv_obj_t *clicked = lv_event_get_target_obj(e);
  for (int i = 0; i < 3; i++) {
    if (mode_cards[i] == clicked) {
      lv_obj_add_state(mode_cards[i], LV_STATE_CHECKED);
    } else {
      lv_obj_remove_state(mode_cards[i], LV_STATE_CHECKED);
    }
  }
}

static lv_obj_t *create_mode_card(lv_obj_t *parent, int index) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, 320, 320);
  lv_obj_set_style_radius(card, 24, LV_PART_MAIN);
  lv_obj_set_style_bg_color(card, lv_color_hex(SAMPLE_UI_COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 3, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(SAMPLE_UI_COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(SAMPLE_UI_COLOR_ACCENT),
                                LV_PART_MAIN | LV_STATE_CHECKED);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *name_label = lv_label_create(card);
  lv_label_set_text(name_label, kModeNames[index]);
  lv_obj_set_style_text_color(name_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_font(name_label, &lv_font_montserrat_24, LV_PART_MAIN);

  lv_obj_t *desc_label = lv_label_create(card);
  lv_label_set_text(desc_label, kModeDescriptions[index]);
  lv_obj_set_style_text_color(desc_label, lv_color_hex(0xAAAAAA), LV_PART_MAIN);

  lv_obj_add_event_cb(card, mode_card_event_cb, LV_EVENT_CLICKED, NULL);
  return card;
}

void sample_ui_drive_mode_init(void) {
  if (sample_ui_drive_mode_screen) {
    return;
  }

  sample_ui_drive_mode_screen = sample_ui_create_screen_base();
  sample_ui_create_bar(sample_ui_drive_mode_screen, "Drive Mode", true);

  lv_obj_t *content = lv_obj_create(sample_ui_drive_mode_screen);
  lv_obj_set_width(content, LV_PCT(100));
  lv_obj_set_flex_grow(content, 1);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(content, 40, LV_PART_MAIN);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  for (int i = 0; i < 3; i++) {
    mode_cards[i] = create_mode_card(content, i);
  }
  lv_obj_add_state(mode_cards[0], LV_STATE_CHECKED); // default selection: Indoor
}
