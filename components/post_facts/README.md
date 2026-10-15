# post_facts

The quick POST's facts from ESP-IDF and FreeRTOS, for hmi_ui's `PostPort` (hazard-c3-spec.md
§2.2, REQ-POST-19): the reset reason, the running image (verified, OTA state), heap and stack
headroom, whether the stick task exists, and the UI side's ms clock. The POST itself (the
evaluator, the runner, the indicator) is `components/post`, which is pure C++; this is its
ESP-IDF side. Moved from `main.cpp`'s `kPostPort` and its helpers unchanged (app-main-shrink
V15, CS-LAY-01).

Safety-relevant (CS-SAF-01): the POST gates motion, and `stick_task_running` is one of its
facts. So the component is held to the safety rules: 60-line functions (`.clang-tidy`),
`fw_component_options`, `-Wswitch-enum`. Clean: no ratchet grants.

## Interface

| Header | What |
| --- | --- |
| `post_facts/post_facts.hpp` | `hmi::post_facts::now_ms`, `reset_reason`, `image`, `memory`, `stacks`, `stick_task_running` |

`src/post_facts.cpp` also holds POST-050's `static_assert`s: `post::ResetReason` and
`post::OtaImageState` mirror ESP-IDF's enums value for value, and the bootloader validates the
image (no `BOOTLOADER_SKIP_VALIDATE_*` option), built against the IDF this firmware uses.

The calibration facts (`calibration`, `calibration_saved_now`) are not here: they come from
main's joystick_cal adapter, which main wires into the same `PostPort`.

## Tasks and dependencies

- Tasks: none. The POST runner calls them on the UI task (`stacks` reads the calling task's
  own high-water mark as lv_task's); `now_ms`, `reset_reason`, `image`, `memory` and
  `stick_task_running` work from any task.
- Dependencies: `post` (the fact types); ESP-IDF `app_update` (OTA state), `esp_partition`,
  `esp_timer`, the heap queries and FreeRTOS. Board-facing: no host L1 app; the bench's POST
  checks cover it.
