#include "sim_input.h"

#include "sim_nav.h"
#include "sim_bench.h"
#include "sim_dpad.h"

#include <math.h>
#include <stdio.h>

#include <windows.h>

/* ---------------------------------------------------------------------------
 * Calibration, transcribed from main.cpp's kHorizontalCal / kVerticalCal /
 * kTwistCal and espp::Joystick's recalculate(). The espp component itself is
 * not checked in (the IDF component manager fetches it), so the math is
 * reproduced here rather than compiled -- sim_input_selftest() below re-runs
 * the assertions from espp::joystick_selftest() to keep it honest.
 * ------------------------------------------------------------------------ */

typedef struct {
    float center;
    float center_deadband;
    float minimum;
    float maximum;
    float range_deadband;
    bool  invert_output;
} range_mapper_t;

/* 0-3300 mV pot centred at 1650 mV, no deadbands. */
static const range_mapper_t kHorizontalCal = {1650.0f, 0.0f, 0.0f, 3300.0f, 0.0f, false};
/* Same, but the vertical gimbal is wired so rising mV is "pull". */
static const range_mapper_t kVerticalCal = {1650.0f, 0.0f, 0.0f, 3300.0f, 0.0f, true};
/* Twist has its own deadbands and is never touched by the x/y circular clamp. */
static const range_mapper_t kTwistCal = {1650.0f, 60.0f, 0.0f, 3300.0f, 40.0f, false};

static const float kCenterDeadzoneRadius = 0.10f;
static const float kRangeDeadzone = 0.05f;

/* main.cpp's joy_key Schmitt trigger. */
static const float kKeyEngage = 0.20f;
static const float kKeyRelease = 0.10f;

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float range_map(const range_mapper_t * m, float raw)
{
    const float hi_edge = m->center + m->center_deadband;
    const float lo_edge = m->center - m->center_deadband;
    float out;

    if(raw > hi_edge) {
        const float span = (m->maximum - m->range_deadband) - hi_edge;
        out = span > 0.0f ? clampf((raw - hi_edge) / span, 0.0f, 1.0f) : 0.0f;
    }
    else if(raw < lo_edge) {
        const float span = lo_edge - (m->minimum + m->range_deadband);
        out = span > 0.0f ? clampf((raw - lo_edge) / span, -1.0f, 0.0f) : 0.0f;
    }
    else {
        out = 0.0f;
    }

    return m->invert_output ? -out : out;
}

/* --------------------------------------------------------------- state --- */

typedef struct {
    /* Keyboard target and the ramped value behind it. The real stick takes a
     * moment to reach the rail and springs back to centre; a key that snapped
     * instantly to 1.0 would skip straight past the deadzone every time and
     * hide bugs in it. kRampPerTick reaches full deflection in ~130 ms. */
    float target_x, target_y, target_z;
    float ramp_x, ramp_y, ramp_z;

    float x, y, z;        /* calibrated, [-1, 1] */
    bool  button;         /* joystick button, level */
    bool  select_pending; /* latched button edge, consumed as one ENTER */

    uint32_t key;         /* held LV_KEY_*, 0 for centred */
    bool     engaged;     /* Schmitt trigger state */

    lv_indev_t * indev;
    HWND         hwnd;
} sim_input_t;

static sim_input_t s;

static const float kRampPerTick = (float)SIM_ADC_PERIOD_MS / 130.0f;

static float ramp_toward(float cur, float target)
{
    if(cur < target) return cur + kRampPerTick > target ? target : cur + kRampPerTick;
    if(cur > target) return cur - kRampPerTick < target ? target : cur - kRampPerTick;
    return cur;
}

/** Only read the keyboard when one of our own windows has focus --
 *  GetAsyncKeyState is global, and a sim that reacts to typing in another
 *  window is maddening. Matching on the process rather than a single HWND
 *  keeps the keys live whichever of the two windows you clicked last. */
static bool focused(void)
{
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if(fg == NULL) return false;

    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

static bool key_down(int vk)
{
    return focused() && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool sim_input_key_edge(int vk)
{
    /* One slot per virtual key, indexed by the VK code itself. */
    static bool was_down[256];
    if(vk < 0 || vk > 255) return false;

    const bool now = key_down(vk);
    const bool edge = now && !was_down[vk];
    was_down[vk] = now;
    return edge;
}

uint16_t sim_input_x_mv(void)     { return (uint16_t)(1650.0f + s.ramp_x * 1650.0f); }
uint16_t sim_input_y_mv(void)     { return (uint16_t)(1650.0f - s.ramp_y * 1650.0f); }
uint16_t sim_input_twist_mv(void) { return (uint16_t)(1650.0f + s.ramp_z * 1650.0f); }

float sim_input_x(void)      { return s.x; }
float sim_input_y(void)      { return s.y; }
float sim_input_twist(void)  { return s.z; }
bool  sim_input_button(void) { return s.button; }

/* ------------------------------------------------------------ sampling --- */

/** The 33 ms ADC task: raw mV -> calibrated stick -> held LVGL key. */
static void sim_input_sample_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);

    /* Arrows, WASD and the bench D-pad all drive the gimbal; up is "push".
     * The D-pad is ORed in here rather than handled separately because a held
     * key and a held D-pad button are the same thing: both want full
     * deflection in one direction, ramped in and out through the deadzone. */
    const bool up    = key_down(VK_UP) || key_down('W') || sim_dpad_up();
    const bool dn    = key_down(VK_DOWN) || key_down('S') || sim_dpad_down();
    const bool left  = key_down(VK_LEFT) || key_down('A') || sim_dpad_left();
    const bool right = key_down(VK_RIGHT) || key_down('D') || sim_dpad_right();

    s.target_x = (right ? 1.0f : 0.0f) + (left ? -1.0f : 0.0f);
    s.target_y = (up ? 1.0f : 0.0f) + (dn ? -1.0f : 0.0f);
    s.target_z = (key_down('E') ? 1.0f : 0.0f) + (key_down('Q') ? -1.0f : 0.0f);

    s.ramp_x = ramp_toward(s.ramp_x, s.target_x);
    s.ramp_y = ramp_toward(s.ramp_y, s.target_y);

    /* Twist has no honest mouse gesture on a flat ring, so it stays on Q/E
     * and the bench does not pretend to offer it. */
    s.ramp_z = ramp_toward(s.ramp_z, s.target_z);

    /* Same order as espp::Joystick::recalculate(): map each axis, then apply
     * the circular clamp to x/y only. */
    float x = range_map(&kHorizontalCal, (float)sim_input_x_mv());
    float y = range_map(&kVerticalCal, (float)sim_input_y_mv());

    const float magnitude = sqrtf(x * x + y * y);
    if(magnitude < kCenterDeadzoneRadius) {
        x = 0.0f;
        y = 0.0f;
    }
    else if(magnitude >= 1.0f - kRangeDeadzone) {
        x /= magnitude;
        y /= magnitude;
    }
    else {
        const float band = 1.0f - kCenterDeadzoneRadius - kRangeDeadzone;
        const float scaled = (magnitude - kCenterDeadzoneRadius) / band;
        x = (x / magnitude) * scaled;
        y = (y / magnitude) * scaled;
    }

    s.x = x;
    s.y = y;
    s.z = range_map(&kTwistCal, (float)sim_input_twist_mv());

    /* Joystick button: level for the hold gestures, plus a latched edge that
     * the indev turns into a single ENTER (main.cpp's select_key.exchange). */
    const bool button_now = key_down(VK_SPACE) || sim_bench_joystick_button();
    if(button_now && !s.button) s.select_pending = true;
    s.button = button_now;

    /* Schmitt trigger -> held key. Larger axis wins, so a diagonal resolves to
     * one direction instead of chattering between two. */
    const float mag = fabsf(s.x) > fabsf(s.y) ? fabsf(s.x) : fabsf(s.y);
    if(mag > kKeyEngage) s.engaged = true;
    else if(mag < kKeyRelease) s.engaged = false;

    if(!s.engaged) {
        s.key = 0;
    }
    else if(fabsf(s.x) >= fabsf(s.y)) {
        s.key = s.x > 0 ? LV_KEY_RIGHT : LV_KEY_LEFT;
    }
    else {
        s.key = s.y > 0 ? LV_KEY_UP : LV_KEY_DOWN;
    }

    sim_nav_on_stick_sample();
}

static void sim_input_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
    LV_UNUSED(indev);

    if(s.select_pending) {
        s.select_pending = false;
        data->key = LV_KEY_ENTER;
        data->state = LV_INDEV_STATE_PRESSED;
        return;
    }

    if(s.key != 0) {
        data->key = s.key;
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void sim_input_set_group(lv_group_t * group)
{
    if(s.indev) lv_indev_set_group(s.indev, group);
}

/* ------------------------------------------------------------ selftest --- */

static bool close_enough(float a, float b) { return fabsf(a - b) < 1e-3f; }

/** The assertions from espp::joystick_selftest(), re-run against the C port so
 *  a typo in range_map() shows up at startup instead of as "the deadzone feels
 *  wrong on the sim". */
static bool sim_input_selftest(void)
{
    bool ok = true;

    ok &= close_enough(range_map(&kHorizontalCal, 1650.0f), 0.0f);
    ok &= close_enough(range_map(&kHorizontalCal, 3300.0f), 1.0f);
    ok &= close_enough(range_map(&kHorizontalCal, 0.0f), -1.0f);
    ok &= close_enough(range_map(&kHorizontalCal, 2475.0f), 0.5f);
    /* the vertical axis is inverted: rising mV reads as "pull" */
    ok &= close_enough(range_map(&kVerticalCal, 3300.0f), -1.0f);
    /* twist deadbands: inside the centre band is zero, inside the range band saturates */
    ok &= close_enough(range_map(&kTwistCal, 1700.0f), 0.0f);
    ok &= close_enough(range_map(&kTwistCal, 3270.0f), 1.0f);
    ok &= close_enough(range_map(&kTwistCal, 0.0f), -1.0f);

    printf(ok ? "[sim] joystick calibration selftest passed\n"
              : "[sim] joystick calibration selftest FAILED\n");
    return ok;
}

void sim_input_init(lv_display_t * display)
{
    s.hwnd = lv_windows_get_display_window_handle(display);

    sim_input_selftest();

    s.indev = lv_indev_create();
    lv_indev_set_type(s.indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s.indev, sim_input_read_cb);
    lv_indev_set_display(s.indev, display);

    /* main.cpp sets the same two, so held-key repeat walks focus at the rate
     * the board does. */
    lv_indev_set_long_press_time(s.indev, 500);
    lv_indev_set_long_press_repeat_time(s.indev, 250);

    lv_timer_create(sim_input_sample_cb, SIM_ADC_PERIOD_MS, NULL);
}
