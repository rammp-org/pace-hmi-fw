#ifndef SAMPLE_UI_COMMON_H
#define SAMPLE_UI_COMMON_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_UI_COLOR_BG 0x000000
#define SAMPLE_UI_COLOR_PANEL 0x101010
#define SAMPLE_UI_COLOR_ACCENT 0x2196F3

// Black, non-scrollable, column-flex screen root shared by every sample_ui_*
// screen.
lv_obj_t *sample_ui_create_screen_base(void);

// White label, no extra styling beyond text color.
lv_obj_t *sample_ui_create_status_label(lv_obj_t *parent, const char *text);

// Dark top bar. If with_back is true, a back button (returns to Home) is
// placed before the title.
lv_obj_t *sample_ui_create_bar(lv_obj_t *parent, const char *title, bool with_back);

#ifdef __cplusplus
}
#endif

#endif
