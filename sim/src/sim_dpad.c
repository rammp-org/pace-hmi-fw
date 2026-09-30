#include "sim_dpad.h"

#include <windows.h>

/* Geometry. A 3x3 cross: four keys around an empty centre, sized so the whole
 * pad matches the two buttons below it in the bench column. */
#define KEY_SIZE 64
#define KEY_GAP  4
#define PAD_SIZE (3 * KEY_SIZE + 2 * KEY_GAP)

/* Deliberately distinct from the HMI's white-on-black so nobody mistakes the
 * bench for part of the product UI. Same blue as the buttons beside it. */
#define COLOR_KEY         0x2E6BD1
#define COLOR_KEY_PRESSED 0x6AA3FF
#define COLOR_PAD_BG      0x1E1E1E
#define COLOR_PAD_BORDER  0x3D3D3D

typedef enum {
    DIR_UP = 0,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT,
    DIR_COUNT,
} dpad_dir_t;

static lv_obj_t *    keys[DIR_COUNT];
static lv_display_t * dpad_display;
static bool          held[DIR_COUNT];

/* ---------------------------------------------------------------------------
 * Why this polls the OS instead of listening for LV_EVENT_PRESSED/RELEASED.
 *
 * LVGL's press and release events are not trustworthy here. When an indev is
 * reset mid-press -- which happens on the panel display every time a gesture
 * completes and a screen or pager change resets every indev -- LVGL forgets
 * which object was pressed. The physical release is then routed nowhere, and
 * from that point LVGL still believes the button is down, so the *next* press
 * produces no LV_EVENT_PRESSED either. The bench goes dead after the first
 * completed gesture, which is precisely the moment you want to press it again.
 *
 * So the held state is derived, every sample, from two things the OS knows for
 * certain: whether the left button is down, and where the cursor is. That
 * cannot drift out of step with LVGL's internal state, because it never
 * consults it. A latched direction on a wheelchair HMI would pin the stick at
 * full deflection, which is the worst thing this sim could misrepresent.
 * ------------------------------------------------------------------------ */
static void dpad_sample(void)
{
    POINT cursor;
    HWND hwnd;
    RECT client;
    int32_t hor_res, ver_res;
    lv_point_t point;
    uint8_t i;

    for(i = 0; i < DIR_COUNT; i++) held[i] = false;

    if((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) return;
    if(dpad_display == NULL) return;

    hwnd = lv_windows_get_display_window_handle(dpad_display);
    if(hwnd == NULL) return;
    if(!GetCursorPos(&cursor)) return;
    if(!ScreenToClient(hwnd, &cursor)) return;
    if(!GetClientRect(hwnd, &client)) return;
    if(client.right <= 0 || client.bottom <= 0) return;

    /* The window is a scaled blit of the display, so client pixels have to be
     * mapped back to display pixels before they can be compared with the
     * widgets' own coordinates. */
    hor_res = lv_display_get_horizontal_resolution(dpad_display);
    ver_res = lv_display_get_vertical_resolution(dpad_display);
    point.x = (int32_t)cursor.x * hor_res / client.right;
    point.y = (int32_t)cursor.y * ver_res / client.bottom;

    for(i = 0; i < DIR_COUNT; i++) {
        lv_area_t area;
        if(keys[i] == NULL) continue;
        lv_obj_get_coords(keys[i], &area);
        /* Spelled out rather than using LVGL's helper, which lives in a
         * private header. */
        if(point.x >= area.x1 && point.x <= area.x2 &&
           point.y >= area.y1 && point.y <= area.y2) {
            held[i] = true;
            break; /* a single cursor can only be on one key */
        }
    }
}

static void dpad_poll_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    dpad_sample();
}

static void add_key(lv_obj_t * parent, dpad_dir_t dir, const char * symbol,
                    lv_align_t align)
{
    lv_obj_t * key = lv_button_create(parent);
    lv_obj_set_size(key, KEY_SIZE, KEY_SIZE);
    lv_obj_align(key, align, 0, 0);
    lv_obj_set_style_bg_color(key, lv_color_hex(COLOR_KEY), LV_PART_MAIN);
    lv_obj_set_style_bg_color(key, lv_color_hex(COLOR_KEY_PRESSED),
                              LV_PART_MAIN | LV_STATE_PRESSED);

    lv_obj_t * label = lv_label_create(key);
    lv_label_set_text(label, symbol);
    lv_obj_center(label);

    keys[dir] = key;
}

void sim_dpad_init(lv_obj_t * parent)
{
    lv_obj_t * pad = lv_obj_create(parent);
    lv_obj_set_size(pad, PAD_SIZE, PAD_SIZE);
    lv_obj_remove_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(pad, lv_color_hex(COLOR_PAD_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(pad, lv_color_hex(COLOR_PAD_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(pad, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(pad, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pad, 0, LV_PART_MAIN);

    /* Up is "push" on the real stick, which is what enters a page. */
    add_key(pad, DIR_UP, LV_SYMBOL_UP, LV_ALIGN_TOP_MID);
    add_key(pad, DIR_DOWN, LV_SYMBOL_DOWN, LV_ALIGN_BOTTOM_MID);
    add_key(pad, DIR_LEFT, LV_SYMBOL_LEFT, LV_ALIGN_LEFT_MID);
    add_key(pad, DIR_RIGHT, LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID);

    dpad_display = lv_obj_get_display(parent);

    /* The widgets have to be laid out before their coordinates mean anything
     * to the hit test above. */
    lv_obj_update_layout(pad);

    /* Same cadence as the ADC sampler that consumes it. */
    lv_timer_create(dpad_poll_cb, 33, NULL);
}

bool sim_dpad_up(void)    { return held[DIR_UP]; }
bool sim_dpad_down(void)  { return held[DIR_DOWN]; }
bool sim_dpad_left(void)  { return held[DIR_LEFT]; }
bool sim_dpad_right(void) { return held[DIR_RIGHT]; }
