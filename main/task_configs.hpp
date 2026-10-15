#pragma once

/**
 * @file task_configs.hpp
 * @brief The tasks app_main starts directly, as literal configs: the housekeeping island ("Data
 *        Display Task"), the GPIO48 stick button ("Button") and the UI island ("lv_task", with
 *        the motion guard's UI heartbeat and the task watchdog). The stick task's config is
 *        stick_io.hpp's (stick_island_config). Each name, stack, priority and core is the
 *        declared value in tools/guards/baselines/tasks.json. Composition; app_main builds each
 *        task where it did, in the same order.
 */

#include "button.hpp"
#include "hmi_ui/fps_meter.hpp"
#include "hmi_ui/ui_island.hpp"
#include "housekeeping/housekeeping.hpp"
#include "interrupt.hpp"

/// @brief The housekeeping island: "Data Display Task", 6144 B, priority 10, core 1, every
/// 20 ms; the IMU's orientation filter noise.
/// @return The Config for hmi::housekeeping::Housekeeping.
hmi::housekeeping::Housekeeping::Config housekeeping_config();

/// @brief The GPIO48 stick button: active low, any edge, pull-up; its task "Button", 4096 B,
/// priority 5.
/// @param callback The edge callback, on the Button's interrupt task.
/// @return The Config for espp::Button.
espp::Button::Config gpio48_button_config(void (*callback)(const espp::Interrupt::Event &));

/// @brief The UI island: "lv_task", 16384 B, priority 20, core 1, a cycle every 8 ms; the motion
/// guard's UI heartbeat (hazard-c4-spec.md §3.1) and the task watchdog (C4 §4.3).
/// @param cycle One LVGL cycle under the LVGL lock (main.cpp's lvgl_cycle).
/// @param fps_meter The FPS meter, or null without CONFIG_HMI_DEBUG_FPS.
/// @return The Config for hmi::ui::UiIsland.
hmi::ui::UiIsland::Config ui_island_config(void (*cycle)(), hmi::ui::FpsMeter *fps_meter);
