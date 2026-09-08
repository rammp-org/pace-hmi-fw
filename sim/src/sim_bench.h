/**
 * The bench: the chair's control surface, in its own window under the panel.
 *
 * A D-pad and two buttons, which is the setup this HMI is driven from. The
 * MCB simulation, the theme and the reset are all still available, but on the
 * keyboard, because none of them is something a hand reaches for on the chair.
 * Press F1 in the console for that list.
 *
 * It is a second lv_display rather than a strip inside the first one because
 * LVGL will not let a screen be smaller than its display: lv_display.c's
 * update_resolution() writes every screen's coords straight from the display
 * resolution, and the Win32 backend calls it whenever the window is sized. A
 * wider display therefore stretches the SquareLine screens and drags every
 * LV_ALIGN_CENTER in main/ui/ off-centre, which would make the sim actively
 * misleading. A separate display keeps the panel exactly 720x1280.
 *
 * ---------------------------------------------------------------------------
 * The button-mapping dropdowns are a MOCKUP, not a feature.
 *
 * The firmware has one button. main.cpp brings up a single espp::Button on
 * GPIO48 and rammp_rtps_spec.h defines a single bit, RAMMP_BUTTON_JOYSTICK.
 * Everything else the dropdowns offer is a *proposal*: a way to feel what a
 * second button might be for before anyone commits it to the firmware and the
 * wire spec, which is the order those changes have to happen in.
 *
 * So the mockup is labelled on its face, every proposed press says so on
 * stdout, and the actions it can perform are all things the UI can already do
 * by other means. Nothing here invents behaviour the board could not have;
 * what is unproven is whether a button should trigger it. Do not read a
 * selection in this window as a statement about what the chair does.
 * ------------------------------------------------------------------------ */
#ifndef SIM_BENCH_H
#define SIM_BENCH_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Size of the bench window, in display pixels. */
#define SIM_BENCH_WIDTH  380
#define SIM_BENCH_HEIGHT 430

/** Builds the controls onto `display`'s active screen. */
void sim_bench_init(lv_display_t * display);

/**
 * True while a button currently mapped to the joystick button is held.
 *
 * This is the one function that is real: GPIO48 on the board,
 * RAMMP_BUTTON_JOYSTICK on the wire. Either button can be assigned to it, so
 * the mapping can be tried both ways round.
 */
bool sim_bench_joystick_button(void);

#ifdef __cplusplus
}
#endif

#endif /* SIM_BENCH_H */
