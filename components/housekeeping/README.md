# housekeeping

Reads the board's IMU, battery monitor and RTC on one slow task (CS-LAY-02). Namespace
`hmi::housekeeping`.

The housekeeping island of [docs/plans/app-main-shrink.md](../../docs/plans/app-main-shrink.md)
§3: today's "Data Display Task", lifted out of `app_main` clean then move (V2). The code is
that task's code; the function-local statics it kept (the Kalman filter, `tab5`, `imu`, the
first cycle's `t0`) are members.

| File | What |
| --- | --- |
| `housekeeping.hpp`, `housekeeping.cpp` | `Housekeeping`: the IMU's Kalman orientation filter (`orientation_filter()`, for `M5StackTab5::initialize_imu`), and the task that every `period` reads the RTC and the battery monitor and updates the IMU. Built once, an `app_main` local (app-main-shrink V6) |

What it leaves behind is what the self test reads, with no bus traffic of its own: the IMU's
cached accelerometer (`imu_accel_mg`) and the battery status (`battery_mv`).

## Requirements

None written yet. The behaviour is the Data Display Task's as it was (less the demo screen
it used to draw, deleted before the lift); the bench checks it (B0-B5, the self test's IMU
and battery checks, the G10 task dump).

## Tasks

| Task | Name | Stack | Priority | Core | Period |
| --- | --- | --- | --- | --- | --- |
| the island's own | `Data Display Task` | 6 KB | 10 | 1 | 20 ms |

`Config::task` is that row as a literal, copied from the code it replaced, so the G10 task
dump (`tools/guards/baselines/tasks.json`) is unchanged. The topology's `housekeeping` row
(`components/topology`, CS-CON-02) is the target design: another name, and its own settings.
Taking `Config::task` from `topo.task_config(...)` is a later step, made once the owner has
reviewed the TASKS rows; it changes the task's name, so it comes with its own G10 baseline.

The orientation filter runs inside `Imu::update`, which only this task calls.

## Dependencies

`m5stack-tab5` (the BSP: IMU, INA226, RTC), espp `filters` (Kalman) and `task`; ESP-IDF
`esp_timer`.

## Ratchet

Clean: no transfer grants. The task's wait is the espp Task idiom (CS-OWN-08).
