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
 *   PING                      OK, for a liveness check
 *
 * One client at a time: a second connection is accepted and closed, so a
 * half-dead session cannot lock the channel out.
 *
 * What it can and cannot reach. The joystick directions it injects go into the
 * keypad latch LVGL reads, never into the stick values sent to the MCB, so it
 * cannot drive the chair. It CAN press anything on screen: ACTIVATE DRIVE, the
 * seat and actuator jog buttons, the drive mode. Those move things. There is no
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
