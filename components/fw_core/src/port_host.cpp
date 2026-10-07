// The host port (L1 tests in WSL): pthreads stand in for FreeRTOS tasks.

#include "fw_core/port/port.hpp"

#include <pthread.h>

#include <array>
#include <cstdio>
#include <string_view>

#include "fw_core/config.hpp"

namespace hmi::fw::port {

TaskId current_task() noexcept { return static_cast<TaskId>(pthread_self()); }

bool in_isr() noexcept {
  std::array<char, TASK_NAME_SIZE> name{};
  current_task_name(name);
  return std::string_view{name.data()}.starts_with("isr");
}

void current_task_name(std::span<char> out) noexcept {
  if (out.empty()) {
    return;
  }
  std::array<char, TASK_NAME_SIZE> name{};
  if (pthread_getname_np(pthread_self(), name.data(), name.size()) != 0) {
    name[0] = '\0';
  }
  const std::string_view text{name.data()};
  const std::size_t length = text.size() < out.size() - 1 ? text.size() : out.size() - 1;
  text.copy(out.data(), length);
  out[length] = '\0';
}

void log_error(std::string_view text) noexcept {
  // The host stand-in for espp::Logger (CS-LOG-01 applies to the firmware build).
  std::fprintf(stderr, "[fw_core/E] %.*s\n", static_cast<int>(text.size()), text.data());
}

} // namespace hmi::fw::port
