// The target port: FreeRTOS tasks, espp::Logger.

#include "fw_core/port/port.hpp"

#include <string_view>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger.hpp"

namespace hmi::fw::port {

namespace {

const espp::Logger &logger() {
  // const: no mutable function-local state (CS-CMP-03); espp::Logger's log calls are const.
  static const espp::Logger log({.tag = "fw_core", .level = espp::Logger::Verbosity::WARN});
  return log;
}

} // namespace

TaskId current_task() noexcept {
  // TaskHandle_t is an opaque pointer; it is kept only as an identity, never dereferenced.
  return reinterpret_cast<TaskId>(xTaskGetCurrentTaskHandle());
}

bool in_isr() noexcept { return xPortInIsrContext() != 0; }

void current_task_name(std::span<char> out) noexcept {
  if (out.empty()) {
    return;
  }
  const char *name = pcTaskGetName(nullptr);
  const std::string_view text{name != nullptr ? name : "?"};
  const std::size_t length = text.size() < out.size() - 1 ? text.size() : out.size() - 1;
  text.copy(out.data(), length);
  out[length] = '\0';
}

void log_error(std::string_view text) noexcept { logger().error("{}", text); }

} // namespace hmi::fw::port
