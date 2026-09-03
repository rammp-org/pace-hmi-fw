/**
 * Keyboard stand-in for the 3D hall joystick and its button.
 *
 * On the board, main.cpp's ADC task samples three channels at 33 ms, pushes
 * them through espp::Joystick's calibration into [-1, 1], and turns the result
 * into a held LVGL key with a Schmitt trigger. This does the same thing from
 * the same 33 ms cadence, with key presses standing in for stick deflection --
 * so everything downstream (the deadzone, the Schmitt trigger, the hold
 * gestures, the ADC bars on the Joystick Test screen) is the real logic.
 */
#ifndef SIM_INPUT_H
#define SIM_INPUT_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Poll cadence, matching main.cpp's kAdcUpdatePeriod. */
#define SIM_ADC_PERIOD_MS 33

/** Creates the keypad indev and starts the 33 ms sampling timer. */
void sim_input_init(lv_display_t * display);

/** Calibrated stick position, [-1, 1], same convention as espp::Joystick. */
float sim_input_x(void);
float sim_input_y(void);
float sim_input_twist(void);

/** True while the joystick button (Space) is down. */
bool sim_input_button(void);

/** The LVGL group the joystick indev drives; swapped per screen. */
void sim_input_set_group(lv_group_t * group);

/** Raw millivolts as the MCB would see them, for the ADC subjects. */
uint16_t sim_input_x_mv(void);
uint16_t sim_input_y_mv(void);
uint16_t sim_input_twist_mv(void);

/** True once per press of a Win32 virtual key, and only while the sim window
 *  has focus. Used for the fake-MCB hotkeys, which are sim scaffolding rather
 *  than anything the board has. */
bool sim_input_key_edge(int vk);

#ifdef __cplusplus
}
#endif

#endif /* SIM_INPUT_H */
