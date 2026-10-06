#include "fw_core/queue.hpp"

#include "log_line.hpp"

namespace hmi::fw::detail {

void log_queue_fault(std::string_view name) noexcept {
  log_error_line("queue '{}' overflowed: fault raised (RAISE_FAULT)", name);
}

} // namespace hmi::fw::detail
