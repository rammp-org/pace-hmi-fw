/**
 * The D-pad, on screen.
 *
 * Four directional buttons standing in for the stick's gimbal. Navigation on
 * this HMI is four discrete directions plus a hold, so a D-pad is a truer
 * control for it than a ring a mouse can only hold at one point: you can see
 * which way you are pushing, and you cannot half-press a direction by
 * accident.
 *
 * A held button is exactly a held arrow key, and sim_input.c treats it as one:
 * same ramp toward full deflection, same calibration, same Schmitt trigger. So
 * the deadzone is still crossed on the way out and back, it is just not
 * something you steer through by hand.
 *
 * Two things it deliberately cannot do, both of which the keyboard still can:
 * diagonals (a mouse holds one button at a time) and twist (Q and E).
 */
#ifndef SIM_DPAD_H
#define SIM_DPAD_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Builds the control into `parent`, which the bench supplies. */
void sim_dpad_init(lv_obj_t * parent);

/** True while that direction is held. ORed with the arrow keys. */
bool sim_dpad_up(void);
bool sim_dpad_down(void);
bool sim_dpad_left(void);
bool sim_dpad_right(void);

#ifdef __cplusplus
}
#endif

#endif /* SIM_DPAD_H */
