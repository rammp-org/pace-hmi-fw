#pragma once

/**
 * @file post_facts.hpp
 * @brief The quick POST's facts from ESP-IDF and FreeRTOS, for hmi_ui's PostPort
 *        (hazard-c3-spec.md §2.2, REQ-POST-19). Each is one read, allocates nothing and logs
 *        nothing. The POST runner (UI task) calls them; post/facts.hpp has the types.
 *
 * The calibration facts are not here: they come from main's joystick_cal adapter.
 */

#include <cstdint>
#include <optional>

#include "post/facts.hpp"

namespace hmi::post_facts {

/// @brief The UI side's ms clock: esp_timer's µs count / 1000, as uint32 (wraps after ~49 days;
/// durations on it are taken modulo 2^32). Any task.
/// @return Milliseconds since boot, truncated to 32 bits.
std::uint32_t now_ms();

/// @brief Why the chip last reset: esp_reset_reason(), as post::ResetReason (the same values,
/// POST-050). Any task.
/// @return The reset reason.
hmi::post::ResetReason reset_reason();

/// @brief The running image: verified (the bootloader validates every image, which the build
/// proves: no BOOTLOADER_SKIP_VALIDATE_* option) and its OTA state; UNDEFINED for a factory
/// image or without OTA data. Any task.
/// @return The image facts.
hmi::post::ImageFacts image();

/// @brief Heap headroom (C3 §2.2): read-only heap_caps_get_* queries, saturated to INT32_MAX.
/// Any task.
/// @return The memory facts; always measured.
std::optional<hmi::post::MemoryFacts> memory();

/// @brief Stack headroom: Read ADC's high-water mark (0 if the task does not exist) and the
/// calling task's, which is the POST runner's own: lv_task. The UI task.
/// @return The stack facts.
hmi::post::StackFacts stacks();

/// @brief Whether the stick task (StickIsland's "Read ADC", app_main) exists. Until it does
/// nothing publishes XYTwist, so the chair cannot move; a failed start is logged by app_main and
/// stays false until reset. FreeRTOS's task list is the record, so there is no flag to keep in
/// step. Any task, not an ISR. Read by the POST only at the budget. The name is the task's in
/// app_main's StickIsland config and in tools/guards/baselines/tasks.json: a rename reads as
/// "not running", the safe direction.
/// @return true while a task named "Read ADC" exists.
bool stick_task_running();

} // namespace hmi::post_facts
