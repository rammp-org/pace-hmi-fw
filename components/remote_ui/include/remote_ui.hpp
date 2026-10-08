#pragma once

/**
 * @file remote_ui.hpp
 * @brief A debug channel that lets a PC see the panel and drive it.
 *
 * Without this the only feedback from the HMI is a person looking at it, which
 * is no way to check twelve screens, seven menu rows and two themes. This opens
 * a TCP server that hands out the current frame and injects touch, joystick and
 * button input, so the whole UI can be walked and captured from a script
 * (scripts/hmi_ui.py).
 *
 * Line-oriented ASCII in, one line of ASCII back -- except SHOT, which answers
 * with a `FRAME <w> <h> <bytes>` line and then that many bytes of raw RGB565.
 *
 *   SHOT [2]                  the active screen; "2" halves each axis
 *   TAP x y                   press, hold kTapMs, release
 *   PRESS x y / RELEASE       a held touch, for a drag
 *   SWIPE x0 y0 x1 y1 ms      interpolated between the two
 *   KEY UP|DOWN|LEFT|RIGHT    the joystick, HELD until KEY NONE
 *   KEY NONE                  let the stick go
 *   KEY ENTER                 one press of the stick button (select)
 *   BTN 0|1                   the GPIO48 button, held
 *   THEME 0|1                 night / day
 *   SCREEN                    the active screen's name
 *   FOCUS                     where input stands: the joystick's focused object
 *                             (x,y,size,state) and each pointer's state
 *   PING                      OK, for a liveness check
 *   TASKS                     every task's name, priority, core and stack, as one
 *                             JSON line (tools/guards/task_dump.py, guard G10)
 *   STICK h v t mask seq      bench stick injection (CONFIG_HMI_BENCH_STICK_INJECT
 *                             only, else ERR): the three raw stick reads in mV
 *                             (0..3300), the reads that fail (bit 0 horizontal, 1
 *                             vertical, 2 twist), a sequence number. Holds 300 ms;
 *                             refresh it to hold longer (stick/bench_inject.hpp)
 *   STATE                     the hazard bench verbs (CONFIG_HMI_BENCH_STICK_INJECT
 *   PERMIT POST|STICK <v>     only, else ERR): one JSON line of the HMI's state;
 *   CAL UNSAVED               set the POST gate or stick health; forget the
 *   POST RERUN                calibration until reboot; rerun POST; abort (a PANIC
 *   CRASH                     reset); stall the UI, ADC or ContinuousAdc task.
 *   STALL UI|ADC|CADC <ms>    bench_verbs.hpp has the line formats and the hooks
 *
 * One client at a time: a second connection is accepted and closed, so a
 * half-dead session cannot lock the channel out.
 *
 * What it can and cannot reach. The joystick directions KEY injects go into the
 * keypad latch LVGL reads, never into the stick values sent to the MCB. STICK
 * does reach them: in a CONFIG_HMI_BENCH_STICK_INJECT build it replaces the raw
 * stick reads, so it steers the chair whenever the gate is open (bench only,
 * with a simulated MCB). It CAN press anything on screen and hold the stick
 * button (BTN): asking the MIB to drive, the seat and actuator jog buttons, the
 * drive mode. Those move things. There is no
 * authentication, so anything on the network can do it -- which is why the
 * channel is compiled out unless CONFIG_HMI_REMOTE_UI is set, and must stay out
 * of anything that leaves the bench.
 */

#include <cstdint>
#include <functional>
#include <mutex>

/// The port the server listens on. Well clear of RTPS's 7400-7411.
inline constexpr uint16_t kRemoteUiPort = 3333;

struct RemoteUiConfig {
  /// The lock every LVGL call outside the LVGL task has to hold.
  std::recursive_mutex *lvgl_mutex = nullptr;
  /// The held joystick direction, as an LV_KEY_* value; 0 = let go. Writes the
  /// same latch the ADC task writes, so what gets tested is the real handling.
  std::function<void(uint32_t)> set_key;
  /// One press of the stick button (the ENTER the keypad indev consumes).
  std::function<void()> press_select;
  /// The GPIO48 test button, held.
  std::function<void(bool)> set_button;
  /// The active screen's name, for SCREEN. Called under the LVGL lock.
  std::function<const char *()> screen_name;
};

/// Start the server task. Call once from app_main, after ui_init and the LVGL
/// task, and after the network is up. A no-op when CONFIG_HMI_REMOTE_UI is off.
void remote_ui_start(const RemoteUiConfig &config);

// remote_ui_attach_stick_inject (the STICK verb's write end) is declared in
// stick_inject.hpp, so this header, which release builds include, pulls in
// none of the bench injection.
