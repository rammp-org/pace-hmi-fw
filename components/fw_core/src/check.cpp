#include "fw_core/check.hpp"

#include "log_line.hpp"

namespace hmi::fw::detail {

void report_check_failed(std::string_view what, const std::source_location &where) noexcept {
  log_error_line("check failed: {} in {} at {}:{}", what.empty() ? "(no description)" : what,
                 where.function_name(), base_name(where.file_name()), where.line());
}

} // namespace hmi::fw::detail
