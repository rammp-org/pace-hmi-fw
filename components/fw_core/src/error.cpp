#include "fw_core/error.hpp"

#include <string>

namespace hmi::fw {

namespace {

class FwCoreErrorCategory final : public std::error_category {
public:
  [[nodiscard]] const char *name() const noexcept override { return "fw_core"; }

  [[nodiscard]] std::string message(int code) const override {
    return std::string{to_string(static_cast<Error>(code))};
  }
};

} // namespace

const std::error_category &error_category() noexcept {
  static const FwCoreErrorCategory category; // const: no mutable state (CS-CMP-03)
  return category;
}

} // namespace hmi::fw
