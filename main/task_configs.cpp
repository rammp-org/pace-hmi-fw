/**
 * @file task_configs.cpp
 * @brief The tasks app_main starts, as literal configs (task_configs.hpp). Moved from app_main
 *        unchanged: same names, stacks, priorities, cores and periods.
 */

#include "task_configs.hpp"

#include "app_instances.hpp"
#include "stick_io.hpp"

using namespace std::chrono_literals;

// The housekeeping island (IMU, battery, RTC): the "Data Display Task", every 20 ms.
hmi::housekeeping::Housekeeping::Config housekeeping_config() {
  return {
      .task =
          {
              .name = "Data Display Task",
              .stack_size_bytes = 6 * 1024,
              .priority = 10,
              .core_id = 1,
          },
      .period = 20ms,
      .angle_noise = 0.001f,
      .rate_noise = 0.1f,
  };
}

// GPIO48, pulled up and shorted to ground on press (board-wide convention), so active LOW. The
// internal pull-up is redundant against the external one but harmless.
espp::Button::Config gpio48_button_config(void (*callback)(const espp::Interrupt::Event &)) {
  return {
      .name = "GPIO48 Button",
      .interrupt_config =
          {
              .gpio_num = 48,
              .callback = callback,
              .active_level = espp::Button::ActiveLevel::LOW,
              .interrupt_type = espp::Button::InterruptType::ANY_EDGE,
              .pullup_enabled = true,
          },
      .task_config = {.name = "Button", .stack_size_bytes = 4 * 1024, .priority = 5},
  };
}

// The UI island: lv_task_handler every 8ms -- the refresh timer runs at 16ms (60 fps), polling at
// twice that rate keeps its firing jitter well under a frame.
hmi::ui::UiIsland::Config ui_island_config(void (*cycle)(), hmi::ui::FpsMeter *fps_meter) {
  return {
      .task =
          {
              .name = "lv_task",
              // Measured peak ~6 KB (self test mem.stk_lvgl: 26964 B of 32 KB
              // never used). The stack is internal DMA-capable RAM, which RTPS
              // start-up runs dry on: at 32 KB the W5500 driver's bounce buffer
              // failed to allocate and the board boot-looped. mem.stk_lvgl
              // guards the headroom.
              .stack_size_bytes = 16 * 1024,
              .priority = 20,
              .core_id = 1,
          },
      .cycle = cycle,
      .period = 8ms,
      .fps_meter = fps_meter,
      // The motion guard's UI heartbeat (hazard-c4-spec.md §3.1), on the ADC side's clock.
      .heartbeat =
          [] {
            // Bench builds: STALL UI, before this cycle's stamp: the cycle in progress runs
            // `ms` longer, so the heartbeat's age counts from the last stamp before the request
            // (B5i: XYTwist 0 by request + 240 ms), and the watchdog reset waits for it too.
            if constexpr (BENCH_STICK_INJECT) {
              hmi::control::spin_for_ms(ui_app.bench_stall().take(hmi::control::StallTask::UI),
                                        adc_clock_ms);
            }
            ui_app.guard_sources().note_ui_cycle(adc_clock_ms());
          },
      // The task watchdog (C4 §4.3): the UI task subscribes at its first cycle; the result is
      // the motion guard's ui_wdt_ok.
      .watchdog_subscribe = task_watchdog_subscribe,
      .watchdog_subscribed = [](bool ok) { ui_app.guard_sources().note_ui_wdt(ok); },
      .watchdog_reset = task_watchdog_reset,
  };
}
