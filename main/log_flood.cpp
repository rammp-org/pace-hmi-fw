#include "log_flood.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "esp_log_timestamp.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger.hpp"
#include "sdkconfig.h"

namespace {

constexpr bool kEnabled = CONFIG_HMI_DEBUG_LOG_FLOOD_AS_INT != 0;
constexpr bool kExact = CONFIG_HMI_DEBUG_LOG_FLOOD_EXACT_AS_INT != 0;
constexpr uint32_t kTasks = CONFIG_HMI_DEBUG_LOG_FLOOD_TASKS;
constexpr uint32_t kPairs = CONFIG_HMI_DEBUG_LOG_FLOOD_PAIRS;
constexpr uint32_t kMaxTasks = 4;
constexpr uint32_t kStartMs = 3000; // after HEAPWATCH's first map at 2.5 s
constexpr uint32_t kPeriodMs = 50;  // the failed scan printed a pair every ~52 ms
constexpr uint32_t kStackBytes = 8192;
constexpr size_t kTeeBufferBytes = 128; // funopen's inline buffer in picolibc

std::array<StaticTask_t, kMaxTasks> task_tcbs;
std::array<std::array<StackType_t, kStackBytes / sizeof(StackType_t)>, kMaxTasks> task_stacks;

void exact_trigger() {
  // Exactly one tee buffer's worth, ending in a newline: fully buffered, so
  // nothing flushes it, and the buffer is left with len == size.
  // On the stack, not the heap: the heap is what is being watched.
  std::array<char, kTeeBufferBytes> fill{};
  fill.fill('.');
  constexpr std::string_view kText =
      "HEAPFLOOD exact: these 128 bytes fill the stdout tee's buffer";
  std::copy(kText.begin(), kText.end(), fill.begin());
  fill.back() = '\n';
  flockfile(stdout); // nobody else prints in between
  std::fflush(stdout);
  const size_t wrote = std::fwrite(fill.data(), 1, fill.size(), stdout);
  // vprintf: character by character, so __bufio_put stores this 'E' first.
  std::printf("E (%lu) heapflood: exact trigger after a %u-byte fwrite\n",
              static_cast<unsigned long>(esp_log_timestamp()), static_cast<unsigned>(wrote));
  funlockfile(stdout);
}

void flood(uint32_t task) {
  espp::Logger logger({.tag = "I2cMasterBus", .level = espp::Logger::Verbosity::INFO});
  for (uint32_t i = 0; i < kPairs; ++i) {
    // espp line (one fwrite), its length varying like the scan's timestamps
    // and addresses did, then an IDF line (vprintf) starting with 'E'.
    const std::string pad((i * 7 + task * 13) % 23, '-');
    logger.error("Probe timeout for device {:#04x} on bus {}: ESP_ERR_TIMEOUT {}", i % 0x70 + 8,
                 task, pad);
    std::printf("E (%lu) i2c.master: I2C transaction timeout detected\n",
                static_cast<unsigned long>(esp_log_timestamp()));
    vTaskDelay(pdMS_TO_TICKS(kPeriodMs));
  }
}

void flood_task(void *arg) {
  const auto task = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg));
  vTaskDelay(pdMS_TO_TICKS(kStartMs));
  esp_rom_printf("HEAPFLOOD task %u: %s\n", static_cast<unsigned>(task),
                 kExact ? "exact trigger" : "flood");
  if (kExact) {
    exact_trigger();
  } else {
    flood(task);
  }
  esp_rom_printf("HEAPFLOOD task %u done\n", static_cast<unsigned>(task));
  vTaskDelete(nullptr);
}

} // namespace

void log_flood_start() {
  if constexpr (kEnabled) {
    const uint32_t tasks = kExact ? 1 : (kTasks < kMaxTasks ? kTasks : kMaxTasks);
    for (uint32_t t = 0; t < tasks; ++t) {
      const TaskHandle_t handle = xTaskCreateStaticPinnedToCore(
          flood_task, "heapflood", kStackBytes, reinterpret_cast<void *>(static_cast<uintptr_t>(t)),
          2, task_stacks[t].data(), &task_tcbs[t], tskNO_AFFINITY);
      if (handle == nullptr) {
        esp_rom_printf("HEAPFLOOD could not start task %u\n", static_cast<unsigned>(t));
      }
    }
  }
}
