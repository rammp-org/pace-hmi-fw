/**
 * The navigation and data layer, ported from main/main.cpp.
 *
 * This is a line-for-line-where-possible port of the lv_subject_t data model,
 * the widget bindings, the push-and-hold gesture engine, the pager lock and
 * the seat-screen button grids that main.cpp builds inside app_main(). The
 * ESP32/M5Stack Tab5 specific pieces (haptics, audio, the GPIO48 button, the
 * RTPS transport, the ADC/joystick hardware) are either dropped or replaced
 * by the sim_input.h / sim_mcb.h substitutes; every such swap is called out
 * in a comment at the point it happens. See sim/README.md for the fidelity
 * caveats and CLAUDE.md for why widgets are only ever reached through a
 * binding, never poked at directly from a click handler.
 *
 * No mutex: main.cpp's lvgl_mutex existed because the ADC task, the GPIO48
 * button's interrupt task and the RTPS receive task all wrote subjects from
 * off the LVGL task. The sim is single-threaded -- sim_input's sampling timer,
 * the fake MCB's publish timer and every timer this file creates all run
 * inside lv_timer_handler() -- so there is nothing to lock.
 */
#include "sim_nav.h"

#include "sim_input.h"
#include "ui.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* ===========================================================================
 * Subjects (observer pattern), mirroring main.cpp's top-of-file block.
 * Static lifetime because the bound observers keep pointers to them.
 * ======================================================================= */

/* Settings-screen axis bars. */
static lv_subject_t adc_x_subject;
static lv_subject_t adc_y_subject;
static lv_subject_t adc_twist_subject;

/* MCB status, as reported by sim_nav_on_mcb_status(). Values are the
 * RAMMP_DRIVE_STATUS_* / RAMMP_STATE_* enums from rammp_rtps_spec.h. */
static lv_subject_t drive_status_subject;
static lv_subject_t mcb_state_subject;
/* Optional label overrides from the MCB. Empty means "use the enum's name". */
static lv_subject_t drive_text_subject;
static lv_subject_t state_text_subject;
static char drive_text_buf[RAMMP_MCB_TEXT_LEN];
static char drive_text_prev_buf[RAMMP_MCB_TEXT_LEN];
static char state_text_buf[RAMMP_MCB_TEXT_LEN];
static char state_text_prev_buf[RAMMP_MCB_TEXT_LEN];

/* DriveScreen readouts the MCB owns: speed, and the error banner body/footer. */
static lv_subject_t speed_tenths_subject;
static lv_subject_t error_text_subject;
static lv_subject_t error_footer_subject;
static char error_text_buf[RAMMP_ERROR_TEXT_LEN];
static char error_text_prev_buf[RAMMP_ERROR_TEXT_LEN];
static char error_footer_buf[RAMMP_ERROR_FOOTER_LEN];
static char error_footer_prev_buf[RAMMP_ERROR_FOOTER_LEN];

/* Which drive mode the user picked on the DriveScreen. main.cpp mirrored this
 * into an atomic so the ADC task could publish it over RTPS without taking
 * the LVGL lock; the sim has no wire to publish it on, so the subject is the
 * only copy. */
static lv_subject_t drive_mode_subject;

/* Link health. Drives the TopBar's RTPS indicator, and greys the status
 * labels when it is not CONNECTED. Fed from stored_link_state (see the RTPS
 * indicator section below) rather than a live rtps_comms_link_state() poll. */
static lv_subject_t rtps_link_subject;
static lv_subject_t rtps_blink_subject; /* 0/1 blink phase */

/* GPIO48 test button substitution: there is no separate hardware button on a
 * desktop, so these follow the joystick button instead (RAMMP_BUTTON_JOYSTICK
 * on the wire, sim_input_button() here). Updated from sim_nav_on_stick_sample(). */
static lv_subject_t button_count_subject;
static lv_subject_t button_pressed_subject;
static bool nav_button_prev = false; /* previous sim_input_button() level, for edge detection */

/* Lock state. 1 = locked, 0 = unlocked -- the single source of truth for both
 * padlock images and both labels. */
static lv_subject_t locked_subject;
/* 1 = the FlexPanel pager is usable. Deliberately NOT just !locked_subject:
 * see the long comment on this in main.cpp -- it stays 0 through the pause
 * between the unlock landing and the auto-advance to the Drive page. */
static lv_subject_t paging_subject;

/* Held direction, main.cpp's joy_key equivalent: whichever LV_KEY_* direction
 * the stick is deflected toward, or 0 when centered/released. sim_input.c
 * keeps its OWN copy of this Schmitt-trigger latch private (it is what feeds
 * the LVGL keypad indev -- see sim_input_sample_cb), so the hold-gesture
 * engine below needs its own, recomputed independently from sim_input_x()/
 * sim_input_y() in sim_nav_on_stick_sample(). Both copies use the same
 * constants at the same 33 ms cadence, so they never disagree in practice --
 * this is the sim-side analogue of main.cpp's single joy_key atomic being
 * shared by the ADC task (writer) and both the keypad indev and hold_poll()
 * (readers). */
static uint32_t nav_key = 0;
static bool nav_engaged = false;
static const float kKeyEngage = 0.20f;
static const float kKeyRelease = 0.10f;

/* Indev groups. An indev can own exactly one group, so the joystick's group
 * follows the active screen -- see screen_loaded_cb below. */
static lv_group_t * joystick_group = NULL;      /* MainScreenFlex pager */
static lv_group_t * seat_group = NULL;          /* seat screen, function buttons page */
static lv_group_t * seat_adjust_group = NULL;   /* seat screen, adjustment page */
static lv_group_t * settings_group = NULL;      /* rows inside ui_SettingsFlexPanel */

/* ===========================================================================
 * FlexPanel paging: which child is centered in the viewport, and the
 * settings-list navigation gated on it.
 * ======================================================================= */

/* Derived from live coordinates rather than a stored index, so it stays
 * correct no matter how the panel got scrolled -- joystick, touch drag, or
 * the on-screen arrows. */
static lv_obj_t * flex_current_page(void)
{
    lv_area_t panel;
    uint32_t i;

    if(!ui_FlexPanel) return NULL;

    lv_obj_get_coords(ui_FlexPanel, &panel);
    {
        const int32_t cx = (panel.x1 + panel.x2) / 2;
        for(i = 0; i < lv_obj_get_child_count(ui_FlexPanel); i++) {
            lv_obj_t * child = lv_obj_get_child(ui_FlexPanel, i);
            lv_area_t a;
            lv_obj_get_coords(child, &a);
            if(cx >= a.x1 && cx <= a.x2) return child;
        }
    }
    return NULL;
}

/* All keypad input lands here, because ui_FlexPanel is what the indev
 * focuses. Left/right page the flex panel; up/down walk the settings rows
 * once that page is showing; enter clicks the focused row. */
static void flex_key_cb(lv_event_t * e)
{
    lv_obj_t * panel = lv_event_get_target_obj(e);
    uint32_t key;
    lv_obj_t * focused;

    /* the group is global, so an inactive screen would otherwise scroll unseen */
    if(lv_obj_get_screen(panel) != lv_screen_active()) return;
    key = lv_event_get_key(e);

    /* Paging is blocked until the driving mode is unlocked. */
    if(key == LV_KEY_RIGHT || key == LV_KEY_LEFT) {
        if(!lv_subject_get_int(&paging_subject)) return;
        if(key == LV_KEY_RIGHT) flex_scroll_next(NULL);
        else flex_scroll_previous(NULL);
        return;
    }

    /* the remaining keys only mean anything on the settings page */
    if(flex_current_page() != ui_SettingsMenu || !settings_group) return;

    focused = lv_group_get_focused(settings_group);
    if(key == LV_KEY_UP || key == LV_KEY_DOWN) {
        if(!focused) {
            /* first nudge onto the page lands on the top row rather than wrapping */
            lv_group_focus_obj(lv_obj_get_child(ui_SettingsFlexPanel, 0));
        }
        else if(key == LV_KEY_DOWN) {
            lv_group_focus_next(settings_group);
        }
        else {
            lv_group_focus_prev(settings_group);
        }
    }
    else if(key == LV_KEY_ENTER && focused) {
        /* the group has no indev, so LVGL won't route the press itself; the
         * row's SquareLine handlers listen for LV_EVENT_CLICKED */
        lv_obj_send_event(focused, LV_EVENT_CLICKED, NULL);
    }
}

/* ===========================================================================
 * FPS COUNTER settings row -> LVGL's built-in perf overlay (lv_sysmon).
 * ======================================================================= */

static void fps_toggle_cb(lv_event_t * e)
{
    static bool shown = false;
    LV_UNUSED(e);
    shown = !shown;
    if(shown) lv_sysmon_show_performance(lv_display_get_default());
    else lv_sysmon_hide_performance(lv_display_get_default());
}

/* ===========================================================================
 * GPIO48 test button substitution -> the joystick button (see
 * sim_nav_on_stick_sample). The panel-background observer itself is
 * unchanged from main.cpp: still a style property with no built-in binding,
 * so it is still an object-bound observer.
 * ======================================================================= */

static void button_panel_observer(lv_observer_t * observer, lv_subject_t * subject)
{
    lv_obj_t * panel = lv_observer_get_target_obj(observer);
    /* "not pressed" is the *current theme's* background, not a hardcoded
     * black -- SquareLine registered this property as themeable, so reading
     * the theme keeps the panel correct after a dark/day switch */
    lv_obj_set_style_bg_color(panel,
                               lv_subject_get_int(subject)
                                   ? lv_palette_main(LV_PALETTE_BLUE)
                                   : lv_color_hex((uint32_t)ui_get_theme_value(_ui_theme_color_background)),
                               LV_PART_MAIN);
}

/* ===========================================================================
 * MCB status panel
 *
 * One observer serves both labels on all four StatusPanel instances.
 * ======================================================================= */

typedef enum {
    STATUS_KIND_DRIVE,
    STATUS_KIND_STATE,
} StatusKind;
static StatusKind kDriveStatusKind = STATUS_KIND_DRIVE;
static StatusKind kStateKind = STATUS_KIND_STATE;

static const uint32_t kStatusGreen = 0x01FF00; /* the export's ACTIVE/OK green */
static const uint32_t kStatusGrey = 0xAAAAAA;  /* nothing wrong, just not driving */
static const uint32_t kStatusRed = 0xFF0000;
static const uint32_t kStatusOrange = 0xFF8C00; /* network up, nothing peering */
/* Shown instead of a stale drive-status/state: the MCB has gone quiet, so the
 * last value it sent is no longer something the HMI can stand behind. */
static const char * const kStatusUnknownText = "---";

/* Bound to both the label's own value subject and rtps_link_subject, so it
 * is re-run either when the MCB says something new or when the link changes. */
static void mcb_status_label_observer(lv_observer_t * observer, lv_subject_t * subject)
{
    lv_obj_t * label = lv_observer_get_target_obj(observer);
    const StatusKind kind = *(const StatusKind *)lv_observer_get_user_data(observer);
    uint8_t value;
    const char * override_text;
    bool overridden;
    const char * text;
    uint32_t color;
    LV_UNUSED(subject);

    /* No live link means no current answer, whatever arrived last. */
    if((sim_link_state_t)lv_subject_get_int(&rtps_link_subject) != SIM_LINK_CONNECTED) {
        lv_label_set_text(label, kStatusUnknownText);
        lv_obj_set_style_text_color(label, lv_color_hex(kStatusGrey), LV_PART_MAIN);
        return;
    }

    value = (uint8_t)lv_subject_get_int(kind == STATUS_KIND_DRIVE ? &drive_status_subject : &mcb_state_subject);
    /* The override replaces the wording only; the colour below still comes
     * from the enum, so the MCB can say ACTIVE and still label it "CHARGING". */
    override_text = lv_subject_get_string(kind == STATUS_KIND_DRIVE ? &drive_text_subject : &state_text_subject);
    overridden = override_text != NULL && override_text[0] != '\0';

    if(kind == STATUS_KIND_DRIVE) {
        text = overridden ? override_text : rammp_drive_status_name(value);
        /* INACTIVE is a normal resting state, not a fault, so it reads grey --
         * red is reserved for a value neither board knows. */
        color = value == RAMMP_DRIVE_STATUS_ACTIVE     ? kStatusGreen
                : value == RAMMP_DRIVE_STATUS_INACTIVE ? kStatusGrey
                                                        : kStatusRed;
    }
    else {
        text = overridden ? override_text : rammp_state_name(value);
        color = value == RAMMP_STATE_OK ? kStatusGreen : kStatusRed;
    }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
}

/* Binds one StatusPanel instance's two labels to the two subjects. Called
 * once per instance at startup: ui_init already built all four and nothing
 * destroys them, so there is no rebinding to do on a screen change. */
static void bind_status_panel(lv_obj_t * panel)
{
    lv_obj_t * drive_label;
    lv_obj_t * state_label;
    if(panel == NULL) return;

    drive_label = ui_comp_get_child(panel, UI_COMP_STATUSPANEL_STATUSPANELLEFT_DRIVESTATUSLABEL);
    state_label = ui_comp_get_child(panel, UI_COMP_STATUSPANEL_STATUSPANELRIGHT_STATELABEL);
    lv_subject_add_observer_obj(&drive_status_subject, mcb_status_label_observer, drive_label, &kDriveStatusKind);
    lv_subject_add_observer_obj(&mcb_state_subject, mcb_status_label_observer, state_label, &kStateKind);
    /* Further observers, so losing the link or receiving a new override
     * repaints them even though the enum said nothing new. */
    lv_subject_add_observer_obj(&rtps_link_subject, mcb_status_label_observer, drive_label, &kDriveStatusKind);
    lv_subject_add_observer_obj(&rtps_link_subject, mcb_status_label_observer, state_label, &kStateKind);
    lv_subject_add_observer_obj(&drive_text_subject, mcb_status_label_observer, drive_label, &kDriveStatusKind);
    lv_subject_add_observer_obj(&state_text_subject, mcb_status_label_observer, state_label, &kStateKind);
}

/* ===========================================================================
 * TopBar RTPS indicator
 * ======================================================================= */

#define kRtpsPollMs 250u

/* The "poll": main.cpp read rtps_comms_link_state() here. The sim has no
 * link supervisor to poll, so sim_nav_set_link_state() just stores whatever
 * the fake MCB (sim_mcb.c, 'L' key) last said, and this timer reads that
 * stored value on the same cadence -- which is what keeps the NO_PEER blink
 * and the "the poll timer has the real answer a quarter second later" boot
 * transient behaving exactly as they do on the board. */
static sim_link_state_t stored_link_state = SIM_LINK_DOWN;

/* Bound to rtps_link_subject and rtps_blink_subject both; reads both
 * regardless of which one fired. */
static void rtps_label_observer(lv_observer_t * observer, lv_subject_t * subject)
{
    lv_obj_t * label = lv_observer_get_target_obj(observer);
    const sim_link_state_t state = (sim_link_state_t)lv_subject_get_int(&rtps_link_subject);
    uint32_t color;
    bool blink;
    bool visible;
    LV_UNUSED(subject);

    switch(state) {
        case SIM_LINK_ETH_FAILED: /* no hardware and no link are both "there is */
        case SIM_LINK_DOWN:       /* no network", and neither is worth blinking */
            color = kStatusRed;
            blink = false;
            break;
        case SIM_LINK_NO_IP:
            color = kStatusOrange;
            blink = false;
            break;
        case SIM_LINK_NO_PEER: /* link is fine, nobody is talking -- the one */
            color = kStatusOrange; /* state worth drawing the eye to */
            blink = true;
            break;
        case SIM_LINK_CONNECTED:
        default:
            color = kStatusGreen;
            blink = false;
            break;
    }
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    /* Blink on opacity rather than colour: the label keeps its size so
     * nothing in the bar reflows, and it reads the same against either theme. */
    visible = !blink || lv_subject_get_int(&rtps_blink_subject) != 0;
    lv_obj_set_style_text_opa(label, visible ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
}

static void bind_rtps_label(lv_obj_t * bar)
{
    lv_obj_t * label;
    if(bar == NULL) return;
    label = ui_comp_get_child(bar, UI_COMP_TOPBAR_RTPS);
    lv_subject_add_observer_obj(&rtps_link_subject, rtps_label_observer, label, NULL);
    lv_subject_add_observer_obj(&rtps_blink_subject, rtps_label_observer, label, NULL);
}

/* Runs every kRtpsPollMs regardless of whether stored_link_state changed --
 * staleness has to be polled, and the blink phase rides along here rather
 * than owning a second timer, same as main.cpp's rtps_poll_cb. */
static void rtps_poll_cb(lv_timer_t * timer)
{
    static uint32_t ticks = 0;
    /* C has no dynamic initializer for a function-local static (unlike the
     * `static uint8_t last_theme = ui_theme_idx;` main.cpp gets away with in
     * C++), so the first-run capture is done with an explicit flag instead. */
    static uint8_t last_theme = 0;
    static bool have_last_theme = false;
    LV_UNUSED(timer);

    lv_subject_set_int(&rtps_link_subject, (int32_t)stored_link_state);
    /* flip every other tick: a 500 ms half-period, i.e. a 1 Hz blink */
    ticks++;
    lv_subject_set_int(&rtps_blink_subject, (int32_t)((ticks / 2) & 1u));

    if(!have_last_theme) {
        last_theme = ui_theme_idx;
        have_last_theme = true;
    }
    /* Defensive, same as main.cpp: if the RTPS label's colour/opacity are
     * registered as themeable, ui_theme_set() re-applies the theme's values
     * over whatever this indicator last painted, and since the subject has
     * not changed nothing would repaint it on its own. Re-notifying on a
     * theme change takes the label back either way. */
    if(ui_theme_idx != last_theme) {
        last_theme = ui_theme_idx;
        lv_subject_notify(&rtps_link_subject);
    }
}

/* ===========================================================================
 * DriveScreen: drive-mode selection
 * ======================================================================= */

static uint32_t kModeHolo = RAMMP_DRIVE_MODE_HOLO;
static uint32_t kModeNormal = RAMMP_DRIVE_MODE_NORMAL;
static uint32_t kModeAuto = RAMMP_DRIVE_MODE_AUTO;

static void drive_mode_click_cb(lv_event_t * e)
{
    const uint32_t * mode = (const uint32_t *)lv_event_get_user_data(e);
    lv_subject_set_int(&drive_mode_subject, (int32_t)(*mode));
}

/* Highlights the button whose mode is selected. Border width rather than a
 * colour, so it reads the same in either theme. */
static void drive_mode_button_observer(lv_observer_t * observer, lv_subject_t * subject)
{
    lv_obj_t * button = lv_observer_get_target_obj(observer);
    const uint32_t mine = *(const uint32_t *)lv_observer_get_user_data(observer);
    const bool selected = (uint32_t)lv_subject_get_int(subject) == mine;
    lv_obj_set_style_border_width(button, selected ? 8 : 2, LV_PART_MAIN);
}

static void bind_drive_mode_button(lv_obj_t * button, uint32_t * mode)
{
    if(button == NULL) return;
    lv_obj_add_event_cb(button, drive_mode_click_cb, LV_EVENT_CLICKED, mode);
    lv_subject_add_observer_obj(&drive_mode_subject, drive_mode_button_observer, button, mode);
}

/* Note: main.cpp also had a drive_mode_publish_observer mirroring this
 * subject into an atomic the ADC task read to publish drive_mode over RTPS.
 * The sim has no RTPS wire to publish on, so that observer is dropped
 * entirely -- the subject is now the only copy of the selected mode. */

/* The speed arrives as tenths; the label wants "N.N". */
static void speed_label_observer(lv_observer_t * observer, lv_subject_t * subject)
{
    int32_t tenths = lv_subject_get_int(subject);
    if(tenths < 0) tenths = 0;
    if(tenths > RAMMP_SPEED_MAX_TENTHS) tenths = RAMMP_SPEED_MAX_TENTHS;
    lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "%d.%d", (int)(tenths / 10), (int)(tenths % 10));
}

/* The banner is one instance on the DriveScreen, so unlike StatusPanel/TopBar
 * this is not a per-screen loop. Note ui_SeatAdjustmentFlexScreen has its own
 * ui_ErrorWarningPanel1 instance that main.cpp never binds either -- not a
 * gap in this port, that panel is simply unused today. */
static void bind_error_panel(void)
{
    if(ui_ErrorWarningPanel == NULL) return;
    /* Visible whenever the state is anything other than OK: a value neither
     * board knows raises the banner instead of silently hiding it. */
    lv_obj_bind_flag_if_eq(ui_ErrorWarningPanel, &mcb_state_subject, LV_OBJ_FLAG_HIDDEN, RAMMP_STATE_OK);
    lv_label_bind_text(
        ui_comp_get_child(ui_ErrorWarningPanel, UI_COMP_ERRORWARNINGPANEL_ERRORMESSAGECONTAINER_ERRORMESSAGELABEL),
        &error_text_subject, NULL);
    lv_label_bind_text(
        ui_comp_get_child(ui_ErrorWarningPanel,
                          UI_COMP_ERRORWARNINGPANEL_ERRORMESSAGECONTAINER_ERRORMESSAGEFOOTERLABEL),
        &error_footer_subject, NULL);
}

/* ===========================================================================
 * Push-and-hold gestures
 *
 * Several places in the HMI ask the user to hold an input for kHoldMs before
 * something happens, and they all show the same thing while they wait: a
 * widget filling 0..kHoldMax, emptying the moment the hold breaks. See the
 * banner comment above HoldGesture in main.cpp for the full table of which
 * gesture lives where -- it is reproduced exactly by the six gestures below.
 *
 * These are #define, not `static const`: plain C requires a static object's
 * initializer to be a constant expression, and a const-qualified object does
 * not qualify for that (unlike C++'s constexpr, which is what main.cpp uses)
 * -- these values are used as designated-initializer values in the six
 * gestures further down, so they have to be macros.
 * ======================================================================= */

#define kHoldMs          1000u /* hold time to fill a gesture widget */
/* Dead time before an exit bar starts filling. Down on the seat buttons page
 * and left on the adjustment page each do double duty -- they step the focus
 * grid as well as feeding a hold gesture -- so without this every single
 * navigation step ticks its bar up and snaps it back. Longer than a
 * key-repeat step (250 ms), so stepping through a grid leaves the bars alone
 * entirely. The arcs get none of this: nothing shares their input. */
#define kBarGraceMs       500u
#define kHoldMax          100  /* arc/bar range (LVGL's default) */
/* Beat between the unlock landing and the pager moving on to the Drive page,
 * so the READY TO DRIVE state is legible rather than a flash. */
#define kUnlockAdvanceMs 1000u
/* Poll cadence for the inputs, matched to sim_input's own 33 ms sampling
 * period (main.cpp matched its ADC task's period for the same reason: the
 * held-direction latch cannot change faster than that). */
#define kHoldPollMs        33u

/* Whether each input is currently released, and so free to start a new hold.
 * Per *input* rather than per gesture on purpose -- see the long comment on
 * this in main.cpp: without it, completing one gesture would roll straight
 * into the next screen's gesture on the same still-held input. */
static bool joy_up_armed = true;
static bool joy_down_armed = true;
static bool joy_left_armed = true;
static bool joy_button_armed = true;

typedef struct HoldGesture {
    lv_subject_t progress;  /* 0..kHoldMax; the arc/bar is bound to this */
    bool * armed;           /* the input's "released since the last completion" */
    bool (*is_held)(void);  /* is the input held right now (sampled on the LVGL task) */
    bool (*applies)(void);  /* is this gesture live on the current screen/page */
    void (*completed)(void);/* what a full hold does */
    uint32_t grace_ms;      /* dead time before the widget starts filling */
    bool holding;           /* edge detector for is_held && applies */
    /* Sim addition: main.cpp buzzed the motor and played a click on
     * completion (see hold_anim_completed_cb below); the sim has neither, so
     * this names the gesture for a console line instead. */
    const char * name;
} HoldGesture;

static void hold_anim_exec_cb(void * var, int32_t value)
{
    lv_subject_set_int(&((HoldGesture *)var)->progress, value);
}

/* Cancels any in-flight fill and empties the widget. The gesture doubles as
 * the animation's `var`, so it is also the handle lv_anim_delete matches on. */
static void hold_reset(HoldGesture * g)
{
    lv_anim_delete(g, hold_anim_exec_cb);
    lv_subject_set_int(&g->progress, 0);
}

static void hold_anim_completed_cb(lv_anim_t * a)
{
    HoldGesture * g = (HoldGesture *)lv_anim_get_user_data(a);
    /* Disarm before running the action: `completed` navigates, and the input
     * is still held at this instant, so whatever gesture watches that input
     * on the far side must not read the same unbroken hold as a fresh one. */
    *g->armed = false;
    g->holding = false;
    /* Deleting the animation from inside its own completed_cb (via
     * hold_reset) is safe and expected, same as in main.cpp. */
    hold_reset(g);
    /* main.cpp: haptic_play(STRONG_CLICK) + play_click() here. No motor and
     * no speaker on a desktop, so the console narrates the transition instead. */
    printf("[nav] %s\n", g->name);
    g->completed();
}

/* Runs on the LVGL task -- there is only the one task in the sim, but this
 * keeps the split main.cpp had between the input-producing task and the
 * animation-owning one, since hold_poll_cb (below) is its own lv_timer
 * rather than being folded into sim_nav_on_stick_sample(). */
static void hold_poll(HoldGesture * g)
{
    const bool held = g->is_held();
    bool hold;

    if(!held) *g->armed = true;
    hold = held && *g->armed && g->applies();
    if(hold == g->holding) return;

    g->holding = hold;
    if(!hold) {
        hold_reset(g);
        return;
    }

    {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, g);
        lv_anim_set_user_data(&a, g);
        lv_anim_set_exec_cb(&a, hold_anim_exec_cb);
        lv_anim_set_values(&a, 0, kHoldMax);
        lv_anim_set_duration(&a, kHoldMs);
        /* The delay holds act_time negative, so the exec callback does not
         * run and the widget stays empty until it elapses. */
        lv_anim_set_delay(&a, g->grace_ms);
        lv_anim_set_completed_cb(&a, hold_anim_completed_cb);
        lv_anim_start(&a);
    }
}

/* The inputs. nav_key is the same Schmitt-triggered latch the flex pager
 * uses (see the comment on nav_key up top), so the engage/release thresholds
 * stay in one place, and it resolves a diagonal to a single direction --
 * which is what keeps these three mutually exclusive. */
static bool joy_up_held(void) { return nav_key == LV_KEY_UP; }
static bool joy_down_held(void) { return nav_key == LV_KEY_DOWN; }
static bool joy_left_held(void) { return nav_key == LV_KEY_LEFT; }
/* The stick's own button, read at its current level (not sim_input_key_edge,
 * which is a one-shot latch) -- a hold gesture needs to know the button is
 * still down, same distinction main.cpp drew between joy_button_pressed and
 * the edge-latched select_key. */
static bool joy_button_held(void) { return sim_input_button(); }

/* ===========================================================================
 * Lock / unlock, and the LockedPanel gesture
 * ======================================================================= */

typedef struct {
    const char * locked;
    const char * unlocked;
} LockText;
static LockText kLockTitleText = {"DRIVING MODE LOCKED", "READY TO DRIVE"};
static LockText kLockHintText = {"Push & hold joystick to unlock", "Driving Mode Unlocked"};

static void lock_text_observer(lv_observer_t * observer, lv_subject_t * subject)
{
    const LockText * text = (const LockText *)lv_observer_get_user_data(observer);
    lv_label_set_text(lv_observer_get_target_obj(observer),
                       lv_subject_get_int(subject) ? text->locked : text->unlocked);
}

/* One-shot, armed by the unlock and cancelled by a re-lock that beats it. */
static lv_timer_t * unlock_advance_timer = NULL;

static void unlock_advance_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    unlock_advance_timer = NULL; /* repeat_count 1: LVGL deletes it after this */
    lv_subject_set_int(&paging_subject, 1);
    flex_scroll_next(NULL);
}

/* Locks or unlocks the HMI. See main.cpp's set_locked() for the full
 * reasoning; ported verbatim. */
static void set_locked(bool locked)
{
    if(unlock_advance_timer) {
        lv_timer_delete(unlock_advance_timer);
        unlock_advance_timer = NULL;
    }
    if(locked) {
        lv_obj_scroll_to_x(ui_FlexPanel, 0, LV_ANIM_OFF);
        lv_subject_set_int(&paging_subject, 0);
    }
    else {
        unlock_advance_timer = lv_timer_create(unlock_advance_cb, kUnlockAdvanceMs, NULL);
        lv_timer_set_repeat_count(unlock_advance_timer, 1);
    }
    lv_subject_set_int(&locked_subject, locked ? 1 : 0);
}

/* Is `page` the flex page the user is actually looking at? */
static bool showing_flex_page(lv_obj_t * page)
{
    return lv_screen_active() == ui_MainScreenFlex && flex_current_page() == page;
}

static bool unlock_applies(void)
{
    return lv_subject_get_int(&locked_subject) != 0 && showing_flex_page(ui_LockedPanel);
}
static void unlock_completed(void) { set_locked(false); }

static HoldGesture unlock_gesture = {
    .armed = &joy_up_armed,
    .is_held = joy_up_held,
    .applies = unlock_applies,
    .completed = unlock_completed,
    .name = "unlocked",
};

/* ===========================================================================
 * Entering and leaving the DriveScreen and the SeatAdjustmentFlexScreen
 * ======================================================================= */

static void screen_return_to_main(void)
{
    _ui_screen_change(&ui_MainScreenFlex, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, &ui_MainScreenFlex_screen_init);
}

/* Is the MCB currently telling us the chair is fit to drive? Requires a live
 * link (read from the subject, same as main.cpp -- not stored_link_state
 * directly, so this lags the same up-to-250ms as everything else the link
 * subject drives) as well as an OK state. */
static bool drive_permitted(void)
{
    return (sim_link_state_t)lv_subject_get_int(&rtps_link_subject) == SIM_LINK_CONNECTED &&
           lv_subject_get_int(&mcb_state_subject) == RAMMP_STATE_OK;
}

static bool drive_enter_applies(void)
{
    return showing_flex_page(ui_DrivePanel) && drive_permitted();
}
static void drive_enter_completed(void)
{
    _ui_screen_change(&ui_DriveScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, &ui_DriveScreen_screen_init);
}

static HoldGesture drive_enter_gesture = {
    .armed = &joy_up_armed,
    .is_held = joy_up_held,
    .applies = drive_enter_applies,
    .completed = drive_enter_completed,
    .name = "drive enter",
};

/* Exits on the stick BUTTON, not on pulling the stick back: pulling back is
 * how you drive in reverse. */
static bool drive_exit_applies(void) { return lv_screen_active() == ui_DriveScreen; }

static HoldGesture drive_exit_gesture = {
    .armed = &joy_button_armed,
    .is_held = joy_button_held,
    .applies = drive_exit_applies,
    .completed = screen_return_to_main,
    .grace_ms = kBarGraceMs,
    .name = "drive exit",
};

/* Which page of the seat screen's own pager is showing: 0 = the function
 * buttons, 1 = the adjustment panel. */
static int seat_page = 0;

/* Defined with the rest of the seat navigation further down, which is where
 * the button grid it restores focus into is declared. */
static void seat_show_buttons_page(void);

static bool seat_exit_applies(void)
{
    return lv_screen_active() == ui_SeatAdjustmentFlexScreen && seat_page == 0;
}
static HoldGesture seat_exit_gesture = {
    .armed = &joy_down_armed,
    .is_held = joy_down_held,
    .applies = seat_exit_applies,
    .completed = screen_return_to_main,
    .grace_ms = kBarGraceMs,
    .name = "seat exit",
};

static bool seat_back_applies(void)
{
    return lv_screen_active() == ui_SeatAdjustmentFlexScreen && seat_page == 1;
}
static HoldGesture seat_back_gesture = {
    .armed = &joy_left_armed,
    .is_held = joy_left_held,
    .applies = seat_back_applies,
    .completed = seat_show_buttons_page,
    .grace_ms = kBarGraceMs,
    .name = "seat back",
};

static bool seat_enter_applies(void) { return showing_flex_page(ui_SeatAdjustmentMenu); }
static void seat_enter_completed(void)
{
    /* The seat screen has a pager of its own, and unlike the MainScreenFlex
     * one it starts fresh on every visit. Done before the swap so the reset
     * is never seen mid-scroll. */
    lv_obj_scroll_to_x(ui_SeatAdjustmentScreenFlexPanel, 0, LV_ANIM_OFF);
    _ui_screen_change(&ui_SeatAdjustmentFlexScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0,
                       &ui_SeatAdjustmentFlexScreen_screen_init);
}
static HoldGesture seat_enter_gesture = {
    .armed = &joy_up_armed,
    .is_held = joy_up_held,
    .applies = seat_enter_applies,
    .completed = seat_enter_completed,
    .name = "seat enter",
};

static void hold_poll_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    hold_poll(&unlock_gesture);
    hold_poll(&drive_enter_gesture);
    hold_poll(&drive_exit_gesture);
    hold_poll(&seat_enter_gesture);
    hold_poll(&seat_exit_gesture);
    hold_poll(&seat_back_gesture);
}

/* ===========================================================================
 * SeatAdjustmentFlexScreen: joystick navigation of both pages
 *
 * Both pages lay their buttons out with LV_FLEX_FLOW_ROW_WRAP, so the
 * joystick walks them as the grid the user sees. See the table in main.cpp's
 * banner comment above ButtonGrid for the exact layout of both pages.
 * ======================================================================= */

#define kGridMaxRows 3
#define kGridMaxCols 3

typedef struct {
    lv_obj_t * cell[kGridMaxRows][kGridMaxCols];
    int cols[kGridMaxRows]; /* buttons in each row; rows may differ in length */
    int rows;
    int row; /* cursor */
    int col;
} ButtonGrid;

static ButtonGrid seat_buttons_grid;
static ButtonGrid seat_adjust_grid;

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Neither page's buttons carry a FOCUSED style in the export, so joystick
 * focus would be invisible. Recolour the 2 px border they already have, the
 * same way the settings rows do, so it tracks the day/dark theme instead of
 * being a hardcoded accent. */
static void style_focus(lv_obj_t * button)
{
    const lv_style_selector_t focused = LV_PART_MAIN | LV_STATE_FOCUSED;
    ui_object_set_themeable_style_property(button, focused, LV_STYLE_BORDER_COLOR, _ui_theme_color_focused);
    ui_object_set_themeable_style_property(button, focused, LV_STYLE_BORDER_OPA, _ui_theme_alpha_focused);
}

/* Arrow keys arrive here as LV_EVENT_KEY on the focused button: lv_indev only
 * consumes NEXT/PREV/ENTER/ESC itself and passes everything else through
 * lv_group_send_data. That is what lets a grid do its own 2D movement.
 * user_data is the grid the button belongs to, so one callback serves both
 * pages. */
static void grid_key_cb(lv_event_t * e)
{
    ButtonGrid * g = (ButtonGrid *)lv_event_get_user_data(e);
    switch(lv_event_get_key(e)) {
        case LV_KEY_UP:    g->row--; break;
        case LV_KEY_DOWN:  g->row++; break;
        case LV_KEY_LEFT:  g->col--; break;
        case LV_KEY_RIGHT: g->col++; break;
        default: return;
    }
    /* Clamp rather than wrap -- on a control surface you steer by feel. The
     * column is re-clamped after a row change too, since rows can differ in
     * length. */
    g->row = clampi(g->row, 0, g->rows - 1);
    g->col = clampi(g->col, 0, g->cols[g->row] - 1);
    lv_group_focus_obj(g->cell[g->row][g->col]);
}

/* Puts the cursor on `button`, so joystick movement carries on from wherever
 * a finger last landed instead of from where the joystick left off. */
static void grid_sync_cursor(ButtonGrid * g, lv_obj_t * button)
{
    int r, c;
    for(r = 0; r < g->rows; r++) {
        for(c = 0; c < g->cols[r]; c++) {
            if(g->cell[r][c] == button) {
                g->row = r;
                g->col = c;
            }
        }
    }
    lv_group_focus_obj(button);
}

/* The adjustment buttons have no behaviour yet, so this is all a press does:
 * take focus. LVGL still shows the pressed state on its own. */
static void grid_click_cb(lv_event_t * e)
{
    grid_sync_cursor((ButtonGrid *)lv_event_get_user_data(e), lv_event_get_target_obj(e));
}

/* Which seat function the SeatAdjustmentPanel is showing. */
static char seat_function_buf[24];
static char seat_function_prev_buf[24];
_Static_assert(sizeof(seat_function_buf) > sizeof("Elevation"), "label buffer too small");
static lv_subject_t seat_function_subject;

/* Both a tap and the joystick button land here: with seat_group owning the
 * indev, LVGL raises LV_EVENT_CLICKED on the focused button for an ENTER key
 * exactly as it does for a touch release. user_data is the button's own
 * label for the four live buttons and NULL for the inert pair. */
static void seat_click_cb(lv_event_t * e)
{
    lv_obj_t * button = lv_event_get_target_obj(e);
    lv_obj_t * label;
    int32_t step;

    grid_sync_cursor(&seat_buttons_grid, button);

    label = (lv_obj_t *)lv_event_get_user_data(e);
    if(!label) return; /* Static and Dynamic */
    lv_subject_copy_string(&seat_function_subject, lv_label_get_text(label));

    /* The pager has SCROLLABLE cleared, so lv_obj_scroll_to_view would bail
     * out early -- but lv_obj_scroll_to_x is programmatic and still moves it. */
    lv_obj_update_layout(ui_SeatAdjustmentScreenFlexPanel);
    step = lv_obj_get_width(ui_SeatFunctionsButtonsPanel) +
           lv_obj_get_style_pad_column(ui_SeatAdjustmentScreenFlexPanel, LV_PART_MAIN);
    lv_obj_scroll_to_x(ui_SeatAdjustmentScreenFlexPanel, step, LV_ANIM_ON);

    /* Scrolling the buttons out of sight does not stop the keypad reaching
     * them: the group still holds a focused button. The adjustment page has
     * its own group, so the joystick moves across with the pager. */
    sim_input_set_group(seat_adjust_group);
    seat_adjust_grid.row = 0;
    seat_adjust_grid.col = 0;
    lv_group_focus_obj(seat_adjust_grid.cell[0][0]);
    seat_page = 1;
}

/* Declared up with seat_back_gesture, which is what calls it. */
static void seat_show_buttons_page(void)
{
    lv_obj_scroll_to_x(ui_SeatAdjustmentScreenFlexPanel, 0, LV_ANIM_ON);
    seat_page = 0;
    sim_input_set_group(seat_group);
    /* Back to the button the user selected rather than the top-left one. */
    lv_group_focus_obj(seat_buttons_grid.cell[seat_buttons_grid.row][seat_buttons_grid.col]);
}

/* Hands the joystick over to whichever group belongs to the screen being
 * shown. sim_input_set_group() stands in for lv_indev_set_group(joystick_indev, ...)
 * on the sim's one keypad indev. */
static void screen_loaded_cb(lv_event_t * e)
{
    if(lv_event_get_target_obj(e) == ui_SeatAdjustmentFlexScreen) {
        sim_input_set_group(seat_group);
        seat_page = 0;
        seat_buttons_grid.row = 0;
        seat_buttons_grid.col = 0;
        lv_group_focus_obj(seat_buttons_grid.cell[0][0]);
    }
    else {
        sim_input_set_group(joystick_group);
    }
}

/* ===========================================================================
 * Public API
 * ======================================================================= */

void sim_nav_init(void)
{
    uint32_t i;

    /* ---- Settings-screen axis bars (JoystickTest) ---------------------- */
    lv_subject_init_int(&adc_x_subject, 0);
    lv_subject_init_int(&adc_y_subject, 0);
    lv_subject_init_int(&adc_twist_subject, 0);
    lv_bar_set_range(ui_XBar, -100, 100);
    lv_bar_set_range(ui_YBar, -100, 100);
    lv_bar_set_range(ui_TwistBar, -100, 100);
    lv_bar_bind_value(ui_XBar, &adc_x_subject);
    lv_bar_bind_value(ui_YBar, &adc_y_subject);
    lv_bar_bind_value(ui_TwistBar, &adc_twist_subject);

    /* ---- MCB status labels, speed, error banner, drive mode ------------ */
    /* The joystick is a slave: until the fake MCB says otherwise the chair is
     * not accepting drive commands, so INACTIVE/OK is the honest default. */
    lv_subject_init_int(&drive_status_subject, RAMMP_DRIVE_STATUS_INACTIVE);
    lv_subject_init_int(&mcb_state_subject, RAMMP_STATE_OK);
    /* LINK_DOWN at boot is true and self-correcting: the poll timer has the
     * real answer a quarter second later, same as main.cpp. */
    lv_subject_init_int(&rtps_link_subject, (int32_t)SIM_LINK_DOWN);
    lv_subject_init_int(&rtps_blink_subject, 1);
    lv_subject_init_string(&drive_text_subject, drive_text_buf, drive_text_prev_buf, sizeof(drive_text_buf), "");
    lv_subject_init_string(&state_text_subject, state_text_buf, state_text_prev_buf, sizeof(state_text_buf), "");
    lv_subject_init_int(&speed_tenths_subject, 0);
    lv_subject_init_string(&error_text_subject, error_text_buf, error_text_prev_buf, sizeof(error_text_buf), "");
    lv_subject_init_string(&error_footer_subject, error_footer_buf, error_footer_prev_buf,
                            sizeof(error_footer_buf), "");
    bind_status_panel(ui_StatusPanel);  /* MainScreenFlex */
    bind_status_panel(ui_StatusPanel1); /* JoystickTest */
    bind_status_panel(ui_StatusPanel2); /* DriveScreen */
    bind_status_panel(ui_StatusPanel3); /* SeatAdjustmentFlexScreen */
    bind_rtps_label(ui_TopBar1);        /* JoystickTest */
    bind_rtps_label(ui_TopBar2);        /* DriveScreen */
    bind_rtps_label(ui_TopBar3);        /* MainScreenFlex */
    bind_rtps_label(ui_TopBar4);        /* SeatAdjustmentFlexScreen */
    lv_subject_add_observer_obj(&speed_tenths_subject, speed_label_observer, ui_SpeedNumber, NULL);
    lv_subject_init_int(&drive_mode_subject, RAMMP_DRIVE_MODE_NORMAL);
    bind_drive_mode_button(ui_DriveModeButton, &kModeHolo);
    bind_drive_mode_button(ui_DriveModeButton1, &kModeNormal);
    bind_drive_mode_button(ui_DriveModeButton2, &kModeAuto);
    bind_error_panel();
    lv_timer_create(rtps_poll_cb, kRtpsPollMs, NULL);

    /* ---- GPIO48 substitution: button counter/panel follow the joystick
     * button instead; see sim_nav_on_stick_sample(). The label still has a
     * built-in binding, the panel background is still an object-bound
     * observer -- only the input feeding them changed. */
    lv_subject_init_int(&button_count_subject, 0);
    lv_subject_init_int(&button_pressed_subject, 0);
    lv_label_bind_text(ui_ButtonCounter, &button_count_subject, "%d");
    lv_subject_add_observer_obj(&button_pressed_subject, button_panel_observer, ui_ButtonPanel, NULL);

    /* ---- Joystick keypad group ------------------------------------------
     * main.cpp built its own espp::KeypadInput indev here; the sim's indev
     * already exists (sim_input_init(), called from main.c before this), so
     * this only builds the group it drives on MainScreenFlex. */
    joystick_group = lv_group_create();
    lv_group_add_obj(joystick_group, ui_FlexPanel);
    sim_input_set_group(joystick_group);
    lv_group_focus_obj(ui_FlexPanel);
    /* drop the built-in arrow scroll so only flex_key_cb acts on the key */
    lv_obj_remove_flag(ui_FlexPanel, LV_OBJ_FLAG_SCROLL_WITH_ARROW);
    lv_obj_add_event_cb(ui_FlexPanel, flex_key_cb, LV_EVENT_KEY, NULL);

    /* ---- Lock/unlock for the LockedPanel page --------------------------- */
    lv_subject_init_int(&locked_subject, 1);
    lv_subject_init_int(&paging_subject, 0);
    /* padlock swap: each image hides on the state that is not its own */
    lv_obj_bind_flag_if_eq(ui_Lock, &locked_subject, LV_OBJ_FLAG_HIDDEN, 0);
    lv_obj_bind_flag_if_eq(ui_Unlock, &locked_subject, LV_OBJ_FLAG_HIDDEN, 1);
    lv_subject_add_observer_obj(&locked_subject, lock_text_observer, ui_TextPanel, &kLockTitleText);
    lv_subject_add_observer_obj(&locked_subject, lock_text_observer, ui_Info4, &kLockHintText);

    /* ---- The push-and-hold gestures -------------------------------------
     * Each one's subject drives exactly one widget, and one shared timer
     * polls them all -- only the gesture whose applies() is true on the
     * current screen can be filling at any moment. */
    lv_subject_init_int(&unlock_gesture.progress, 0);
    lv_subject_init_int(&drive_enter_gesture.progress, 0);
    lv_subject_init_int(&drive_exit_gesture.progress, 0);
    lv_subject_init_int(&seat_enter_gesture.progress, 0);
    lv_arc_bind_value(ui_UnlockArc, &unlock_gesture.progress);
    lv_arc_bind_value(ui_UnlockArc1, &drive_enter_gesture.progress);
    lv_arc_bind_value(ui_UnlockArc2, &seat_enter_gesture.progress);
    lv_subject_init_int(&seat_exit_gesture.progress, 0);
    lv_subject_init_int(&seat_back_gesture.progress, 0);
    lv_bar_set_range(ui_ExitBarPress1, 0, kHoldMax);
    lv_bar_bind_value(ui_ExitBarPress1, &drive_exit_gesture.progress);
    lv_bar_set_range(ui_ExitBarPull1, 0, kHoldMax);
    lv_bar_bind_value(ui_ExitBarPull1, &seat_exit_gesture.progress);
    lv_bar_set_range(ui_ExitBarPushLeft, 0, kHoldMax);
    lv_bar_bind_value(ui_ExitBarPushLeft, &seat_back_gesture.progress);
    lv_timer_create(hold_poll_cb, kHoldPollMs, NULL);

    /* ---- SeatAdjustmentFlexScreen: both button grids --------------------
     * Built here rather than declared with initialisers because the ui_*
     * globals only exist once ui_init() has run. */
    seat_buttons_grid.rows = 3;
    seat_buttons_grid.cols[0] = 2;
    seat_buttons_grid.cell[0][0] = ui_SeatButton1;
    seat_buttons_grid.cell[0][1] = ui_SeatButton2;
    seat_buttons_grid.cols[1] = 2;
    seat_buttons_grid.cell[1][0] = ui_SeatButton3;
    seat_buttons_grid.cell[1][1] = ui_SeatButton4;
    seat_buttons_grid.cols[2] = 2;
    seat_buttons_grid.cell[2][0] = ui_SeatButton5;
    seat_buttons_grid.cell[2][1] = ui_SeatButton6;

    /* 2 then 3: the wrap layout fits "-" and "+" on one row and the three
     * presets on the next. */
    seat_adjust_grid.rows = 2;
    seat_adjust_grid.cols[0] = 2;
    seat_adjust_grid.cell[0][0] = ui_SeatAdjustmentButton1;
    seat_adjust_grid.cell[0][1] = ui_SeatAdjustmentButton2;
    seat_adjust_grid.cols[1] = 3;
    seat_adjust_grid.cell[1][0] = ui_SeatAdjustmentButton3;
    seat_adjust_grid.cell[1][1] = ui_SeatAdjustmentButton4;
    seat_adjust_grid.cell[1][2] = ui_SeatAdjustmentButton5;

    {
        /* The label each function button names on the adjustment page, or
         * NULL for the two inert ones. Indexed to match seat_buttons_grid. */
        lv_obj_t * seat_labels[3][2] = {
            {ui_SeatButtonLabel1, ui_SeatButtonLabel2},
            {ui_SeatButtonLabel3, ui_SeatButtonLabel4},
            {NULL, NULL},
        };
        int r, c;

        seat_group = lv_group_create();
        for(r = 0; r < seat_buttons_grid.rows; r++) {
            for(c = 0; c < seat_buttons_grid.cols[r]; c++) {
                lv_obj_t * button = seat_buttons_grid.cell[r][c];
                lv_group_add_obj(seat_group, button);
                lv_obj_add_event_cb(button, grid_key_cb, LV_EVENT_KEY, &seat_buttons_grid);
                lv_obj_add_event_cb(button, seat_click_cb, LV_EVENT_CLICKED, seat_labels[r][c]);
                style_focus(button);
            }
        }

        /* The adjustment buttons get navigation and focus only -- grid_click_cb
         * moves the cursor and nothing else, so pressing one does nothing
         * beyond LVGL's own pressed state until their behaviour is written. */
        seat_adjust_group = lv_group_create();
        for(r = 0; r < seat_adjust_grid.rows; r++) {
            for(c = 0; c < seat_adjust_grid.cols[r]; c++) {
                lv_obj_t * button = seat_adjust_grid.cell[r][c];
                lv_group_add_obj(seat_adjust_group, button);
                lv_obj_add_event_cb(button, grid_key_cb, LV_EVENT_KEY, &seat_adjust_grid);
                lv_obj_add_event_cb(button, grid_click_cb, LV_EVENT_CLICKED, &seat_adjust_grid);
                style_focus(button);
            }
        }
    }

    lv_subject_init_string(&seat_function_subject, seat_function_buf, seat_function_prev_buf,
                            sizeof(seat_function_buf), lv_label_get_text(ui_AngleSettingLabel));
    lv_label_bind_text(ui_AngleSettingLabel, &seat_function_subject, NULL);

    /* Hand the joystick between groups as the screen changes. Registered on
     * both screens so every route in and out is covered. */
    lv_obj_add_event_cb(ui_SeatAdjustmentFlexScreen, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
    lv_obj_add_event_cb(ui_MainScreenFlex, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);

    /* ---- Freeze the pager until the unlock hands it over -----------------
     * Each of these closes one route into it, and they are genuinely
     * independent -- see paging_subject above. The joystick is handled by
     * the guard in flex_key_cb. */
    lv_obj_bind_flag_if_eq(ui_FlexPanel, &paging_subject, LV_OBJ_FLAG_SCROLLABLE, 1);
    lv_obj_bind_flag_if_eq(ui_ArrowsPanel, &paging_subject, LV_OBJ_FLAG_HIDDEN, 0);
    lv_obj_bind_flag_if_eq(ui_DrivePanel, &paging_subject, LV_OBJ_FLAG_HIDDEN, 0);
    /* ui_SeatAdjustmentMenu, not ui_SeatAdjustmentPanel -- see main.cpp's
     * comment on this exact line for why that distinction matters. */
    lv_obj_bind_flag_if_eq(ui_SeatAdjustmentMenu, &paging_subject, LV_OBJ_FLAG_HIDDEN, 0);
    lv_obj_bind_flag_if_eq(ui_SettingsMenu, &paging_subject, LV_OBJ_FLAG_HIDDEN, 0);

    /* ---- Focus group for the settings rows --------------------------------
     * Built by walking the children rather than naming individual buttons,
     * so rows added in SquareLine are picked up on the next import. */
    settings_group = lv_group_create();
    for(i = 0; i < lv_obj_get_child_count(ui_SettingsFlexPanel); i++) {
        lv_group_add_obj(settings_group, lv_obj_get_child(ui_SettingsFlexPanel, i));
    }

    /* ---- FPS overlay: start hidden, wire the settings row, enlarge the
     * label (LVGL's 14px default is unreadable on a 1280x720 panel). ------ */
    lv_sysmon_hide_performance(lv_display_get_default());
    lv_obj_add_event_cb(ui_FPSCounterButton, fps_toggle_cb, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t * perf_label = lv_obj_get_child(lv_display_get_layer_sys(lv_display_get_default()), 0);
        if(perf_label) {
            lv_obj_set_style_text_font(perf_label, &lv_font_montserrat_48, 0);
            lv_obj_set_style_pad_all(perf_label, 10, 0); /* grow the backing box to match */
        }
    }

    /* Note: main.cpp also wired up a HAPTIC TEST settings row here
     * (haptic_label_subject + haptic_test_cb + haptic_label_timer). There is
     * no motor to drive on a desktop, so that whole subsystem is dropped;
     * ui_HapticTestButton is left with no handler, same as a board with no
     * DRV2605 fitted -- inert rather than fatal, per main.cpp's own comment
     * on haptic_play(). */
}

void sim_nav_on_stick_sample(void)
{
    /* ---- Settings-screen axis bars ---------------------------------------
     * main.cpp's ADC task also streamed the raw mV over RTPS here for
     * rtps_adc_plot.py; the sim has no wire to put that on, so only the
     * on-screen percentage bars survive the port. */
    lv_subject_set_int(&adc_x_subject, (int32_t)(sim_input_x() * 100.0f));
    lv_subject_set_int(&adc_y_subject, (int32_t)(sim_input_y() * 100.0f));
    lv_subject_set_int(&adc_twist_subject, (int32_t)(sim_input_twist() * 100.0f));

    /* ---- Held-direction latch for the hold gestures (see nav_key above) -- */
    {
        const float x = sim_input_x();
        const float y = sim_input_y();
        const float ax = fabsf(x);
        const float ay = fabsf(y);
        const float mag = ax > ay ? ax : ay;

        if(mag > kKeyEngage) nav_engaged = true;
        else if(mag < kKeyRelease) nav_engaged = false;

        if(!nav_engaged) nav_key = 0;
        else if(ax >= ay) nav_key = x > 0 ? LV_KEY_RIGHT : LV_KEY_LEFT;
        else nav_key = y > 0 ? LV_KEY_UP : LV_KEY_DOWN;
    }

    /* ---- GPIO48 substitution: button counter/panel follow the joystick
     * button's level. main.cpp debounced this in the interrupt handler
     * against mechanical contact bounce; a keyboard key has none, so every
     * level change here is taken as a real edge. */
    {
        const bool level = sim_input_button();
        if(level != nav_button_prev) {
            nav_button_prev = level;
            lv_subject_set_int(&button_pressed_subject, level ? 1 : 0);
            if(level) {
                lv_subject_set_int(&button_count_subject, lv_subject_get_int(&button_count_subject) + 1);
            }
        }
    }

    /* ---- Sim-only hotkeys --------------------------------------------------
     * R and T have no firmware equivalent -- there is no power switch to
     * simulate a reboot, and the day/night toggle is normally a tap on a
     * settings row. main.c's own loop only owns Esc (quit) and F1 (help), so
     * this tick is as good a place as any to poll for them. */
    if(sim_input_key_edge('R')) {
        sim_nav_reset();
    }
    if(sim_input_key_edge('T')) {
        theme_toggle(NULL); /* the exact call ui_MainScreenFlex's day/night row makes */
    }
}

void sim_nav_on_mcb_status(const rammp_mcb_status_t * status)
{
    /* Direct port of the rtps_comms_on_mcb_status lambda in main.cpp's
     * app_main(), minus the lvgl_mutex lock: that lock existed because the
     * real lambda runs on the RTPS receive task, a different thread from the
     * LVGL task whose observers lv_subject_set_int() runs synchronously.
     * sim_mcb.c calls this from its own lv_timer, which already IS the LVGL
     * task -- there is nothing to lock. */
    lv_subject_set_int(&drive_status_subject, status->drive_status);
    lv_subject_set_int(&mcb_state_subject, status->system_state);
    /* decode()'s NUL-termination guarantee doesn't apply here (the sim builds
     * this struct directly, not off the wire), but copy_string() itself only
     * reads up to the subject's own buffer size either way. */
    lv_subject_copy_string(&drive_text_subject, status->drive_text);
    lv_subject_copy_string(&state_text_subject, status->state_text);
    lv_subject_set_int(&speed_tenths_subject, status->speed_tenths);
    lv_subject_copy_string(&error_text_subject, status->error_text);
    lv_subject_copy_string(&error_footer_subject, status->error_footer);
}

void sim_nav_set_link_state(sim_link_state_t link_state)
{
    /* Stored rather than pushed straight into rtps_link_subject: main.cpp's
     * link state came from a poll timer reading rtps_comms_link_state(), and
     * rtps_poll_cb above is that same poll timer reading this instead --
     * keeping the read on its own cadence is what makes the NO_PEER blink
     * and the boot-time staleness window behave identically to the board. */
    stored_link_state = link_state;
}

/* --- hooks for the bench's button-mapping mockup; no firmware caller ------ */

void sim_nav_go_home(void)
{
    screen_return_to_main();
}

void sim_nav_next_drive_mode(void)
{
    const int32_t mode = lv_subject_get_int(&drive_mode_subject);
    /* HOLO / Normal / Auto, in the order rammp_rtps_spec.h numbers them. */
    lv_subject_set_int(&drive_mode_subject, (mode + 1) % (RAMMP_DRIVE_MODE_AUTO + 1));
}

void sim_nav_reset(void)
{
    set_locked(true);

    /* Seat pager: park both pages back at the start, the same state a fresh
     * visit through seat_enter_gesture would leave it in. */
    seat_page = 0;
    lv_obj_scroll_to_x(ui_SeatAdjustmentScreenFlexPanel, 0, LV_ANIM_OFF);

    /* ui_BootScreen's own SquareLine handler (ui_event_BootScreen, in
     * main/ui/screens/ui_BootScreen.c) re-arms the 2500 ms auto-advance to
     * MainScreenFlex on LV_EVENT_SCREEN_LOADED, so this is a real "reboot"
     * rather than a shortcut back into the app -- matching hardware, which
     * has no way to skip the splash either. */
    _ui_screen_change(&ui_BootScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, &ui_BootScreen_screen_init);
}
