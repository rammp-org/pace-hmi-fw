#include "sim_bench.h"

#include "sim_dpad.h"
#include "sim_nav.h"

#include <stdio.h>

#include <windows.h>

#define BENCH_PAD 12

#define COLOR_BENCH_BG 0x141414
#define COLOR_CAPTION  0x4A4A4A
#define COLOR_MOCKUP   0xB08000 /* amber: this row is a proposal, not the board */
#define COLOR_REAL     0x5E9E5E /* green: this one is in the firmware */

/* ---------------------------------------------------------------------------
 * The candidate button functions.
 *
 * Exactly one of these is real. The rest are here to be tried on, and each is
 * deliberately something the UI can already do by some other route, so the
 * mockup never demonstrates behaviour the board could not have. FN_HORN and
 * FN_LIGHTS have no UI at all today, so they can only announce themselves --
 * which is itself a useful answer, because it says the screen would need
 * something new before that mapping means anything.
 * ------------------------------------------------------------------------ */
typedef enum {
    FN_NONE = 0,
    FN_JOYSTICK,
    FN_HOME,
    FN_DRIVE_MODE,
    FN_HORN,
    FN_LIGHTS,
    FN_COUNT,
} bench_fn_t;

/* Order must match the dropdown's option list below. */
static const struct {
    const char * name;
    bool         in_firmware;
    bool         momentary; /* acts on the press edge rather than being held */
} kFunctions[FN_COUNT] = {
    {"Not assigned",    false, false},
    {"Joystick button", true,  false},
    {"Home / back",     false, true},
    {"Next drive mode", false, true},
    {"Horn",            false, true},
    {"Lights",          false, true},
};

#define FUNCTION_OPTIONS \
    "Not assigned\n"     \
    "Joystick button\n"  \
    "Home / back\n"      \
    "Next drive mode\n"  \
    "Horn\n"             \
    "Lights"

typedef struct {
    const char * label;  /* "BUTTON 1" */
    const char * wiring; /* what the board actually has on this button */
    bench_fn_t   fn;
    bool         held;
    lv_obj_t *   caption;
} bench_button_t;

static bench_button_t buttons[] = {
    {"BUTTON 1", "GPIO48", FN_JOYSTICK, false, NULL},
    {"BUTTON 2", "no pin", FN_NONE, false, NULL},
};

#define BUTTON_COUNT ((int)(sizeof(buttons) / sizeof(buttons[0])))

/* --------------------------------------------------------------- actions --- */

/** Runs a momentary function, and says loudly that it is only a proposal. */
static void invoke(const bench_button_t * b)
{
    const bench_fn_t fn = b->fn;

    if(!kFunctions[fn].momentary) return;

    printf("[proposal] %s -> %s (not in the firmware; sim mockup only)\n",
           b->label, kFunctions[fn].name);

    switch(fn) {
        case FN_HOME:
            sim_nav_go_home();
            break;
        case FN_DRIVE_MODE:
            sim_nav_next_drive_mode();
            break;
        case FN_HORN:
        case FN_LIGHTS:
            /* Nothing to drive: the HMI has no horn or lamp indicator. That
             * absence is the finding, not an omission here. */
            printf("           nothing on screen shows this yet\n");
            break;
        default:
            break;
    }
}

static void caption_refresh(bench_button_t * b)
{
    const bool real = kFunctions[b->fn].in_firmware;
    lv_label_set_text_fmt(b->caption, "%s  %s", b->wiring,
                          real ? "in firmware" : "proposal");
    lv_obj_set_style_text_color(b->caption,
                                lv_color_hex(real ? COLOR_REAL : COLOR_MOCKUP),
                                LV_PART_MAIN);
}

static void button_event_cb(lv_event_t * e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    bench_button_t * b = lv_event_get_user_data(e);

    if(code == LV_EVENT_PRESSED) {
        b->held = true;
        invoke(b);
    }
    else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        b->held = false;
    }
}

static void dropdown_event_cb(lv_event_t * e)
{
    bench_button_t * b = lv_event_get_user_data(e);
    lv_obj_t * dd = lv_event_get_target_obj(e);

    b->fn = (bench_fn_t)lv_dropdown_get_selected(dd);
    caption_refresh(b);
    printf("[mockup] %s mapped to \"%s\"%s\n", b->label, kFunctions[b->fn].name,
           kFunctions[b->fn].in_firmware ? "" : " (proposal)");
}

/* ----------------------------------------------------------------- build --- */

static lv_obj_t * plain_label(lv_obj_t * parent, const char * text, uint32_t color)
{
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    return label;
}

/** A button, the dropdown that assigns it a function, and its status caption. */
static void build_button(lv_obj_t * parent, bench_button_t * b)
{
    lv_obj_t * cell = lv_obj_create(parent);
    lv_obj_set_size(cell, 165, 118);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cell, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cell, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(cell, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * button = lv_button_create(cell);
    lv_obj_set_size(button, 165, 46);
    lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_ALL, b);
    lv_obj_center(plain_label(button, b->label, 0xFFFFFF));

    lv_obj_t * dd = lv_dropdown_create(cell);
    lv_obj_set_size(dd, 165, 34);
    lv_dropdown_set_options_static(dd, FUNCTION_OPTIONS);
    lv_dropdown_set_selected(dd, (uint32_t)b->fn);
    lv_obj_add_event_cb(dd, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, b);

    b->caption = plain_label(cell, "", COLOR_CAPTION);
    caption_refresh(b);
}

void sim_bench_init(lv_display_t * display)
{
    lv_obj_t * strip = lv_display_get_screen_active(display);
    lv_obj_t * row;
    int i;

    /* lv_display.c shows a perf overlay on every display it creates, and
     * sim_nav.c only ever hides the panel's. Left alone, the bench's would sit
     * on top of these controls for the whole run, and it would be measuring a
     * window full of buttons rather than the UI under test. */
    lv_sysmon_hide_performance(display);

    lv_obj_set_style_bg_color(strip, lv_color_hex(COLOR_BENCH_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(strip, BENCH_PAD, LV_PART_MAIN);
    lv_obj_set_style_pad_row(strip, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(strip, LV_OBJ_FLAG_SCROLLABLE);

    sim_dpad_init(strip);

    row = lv_obj_create(strip);
    lv_obj_set_size(row, LV_PCT(100), 124);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    for(i = 0; i < BUTTON_COUNT; i++) build_button(row, &buttons[i]);

    /* Says on the face of the window what the dropdowns are, so a screenshot
     * of this cannot be mistaken for a description of the product. */
    plain_label(strip, "button mapping is a mockup, not firmware", COLOR_MOCKUP);
}

bool sim_bench_joystick_button(void)
{
    int i;
    /* Held state is only trusted while the mouse is genuinely down: LVGL drops
     * a release when an indev is reset mid-press, and a latched button would
     * hold the drive screen's exit gesture forever. Same reasoning as the
     * comment in sim_dpad.c. */
    const bool mouse_down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    for(i = 0; i < BUTTON_COUNT; i++) {
        if(buttons[i].fn == FN_JOYSTICK && buttons[i].held && mouse_down) return true;
    }
    return false;
}
