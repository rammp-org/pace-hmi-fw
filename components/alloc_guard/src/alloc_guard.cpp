/// @file alloc_guard.cpp
/// @brief The on-chip allocation guard (alloc_guard.hpp): ESP-IDF's heap hook and the watched
///        tasks' stacks. Built only with CONFIG_HMI_BENCH_ALLOC_GUARD (CMakeLists.txt).

#include "alloc_guard/alloc_guard.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/freertos_debug.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

namespace {

using hmi::alloc_guard::AllocCounter;
using hmi::alloc_guard::MAX_TASKS;

// The guard's counts, written by the heap hook below.
// spec-deviation(CS-CMP-03): the one mutable object at namespace scope in this component. The
// hook ESP-IDF calls (esp_heap_trace_alloc_hook) takes no context argument, so what it writes
// can only be reached through a name; TS-DET-08 prescribes that hook on the chip. Constant-
// initialised (no constructor runs: G4) and in internal RAM (DRAM_ATTR), which the hook can
// reach with the flash cache off. Bench builds only (CONFIG_HMI_BENCH_ALLOC_GUARD).
DRAM_ATTR constinit AllocCounter<MAX_TASKS> counter;

// Watches task @p name's stack in slot @p slot: from its start to its high end, as FreeRTOS
// recorded them when it made the task. False when no task has that name.
bool watch(std::size_t slot, std::string_view name) {
  std::array<char, configMAX_TASK_NAME_LEN> c_name{};
  if (name.empty() || name.size() >= c_name.size()) {
    return false;
  }
  std::copy(name.begin(), name.end(), c_name.begin());
  TaskHandle_t task = xTaskGetHandle(c_name.data());
  TaskSnapshot_t snapshot{};
  if (task == nullptr || vTaskGetSnapshot(task, &snapshot) != pdTRUE) {
    return false;
  }
  // The stack grows down: pxEndOfStack is its highest word.
  const auto lo = std::bit_cast<std::uintptr_t>(xTaskGetStackStart(task));
  const auto hi = std::bit_cast<std::uintptr_t>(snapshot.pxEndOfStack) + sizeof(StackType_t);
  counter.watch(slot, lo, hi);
  return true;
}

} // namespace

void hmi::alloc_guard::start(std::span<const std::string_view> tasks) {
  for (std::size_t slot = 0; slot < tasks.size() && slot < MAX_TASKS; ++slot) {
    static_cast<void>(watch(slot, tasks[slot])); // not there yet: count() looks again
  }
  counter.arm();
}

std::optional<hmi::alloc_guard::Count> hmi::alloc_guard::count(std::size_t slot,
                                                               std::string_view task) {
  if (!counter.armed()) {
    return std::nullopt;
  }
  static_cast<void>(watch(slot, task)); // a task made after start(), or made again
  if (!counter.watched(slot)) {
    return std::nullopt;
  }
  return counter.count(slot);
}

// ESP-IDF's allocation hook (esp_heap_caps.h, CONFIG_HEAP_USE_HOOKS): called after every
// successful allocation, on the allocating task's stack (or an interrupt's), possibly with the
// flash cache disabled. In IRAM: the declaration in esp_heap_caps.h carries HEAP_IRAM_ATTR, the
// heap's own placement, which this definition takes (a second section attribute here would
// conflict with it). It reads the stack pointer and calls nothing else (AllocCounter::note is
// always inlined).
extern "C" void esp_heap_trace_alloc_hook(void * /*ptr*/, size_t size, uint32_t /*caps*/) {
  counter.note(std::bit_cast<std::uintptr_t>(esp_cpu_get_sp()), size);
}
