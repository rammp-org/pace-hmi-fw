#pragma once
// Host stand-in for espp's logger.hpp (managed_components/espp__logger), for the drive goldens:
// the same Config and Verbosity, and error() records its line in both golden logs instead of
// printing. Only what the drive code uses.

#include <chrono>
#include <format>
#include <string>
#include <string_view>

#include "world.hpp"

namespace espp {

class Logger {
public:
  enum class Verbosity { DEBUG, INFO, WARN, ERROR, NONE };

  struct Config {
    std::string_view tag;
    bool include_time{true};
    std::chrono::duration<float> rate_limit = std::chrono::duration<float>(0);
    Verbosity level = Verbosity::WARN;
  };

  explicit Logger(const Config &config)
      : tag_(config.tag)
      , level_(config.level) {}

  template <typename... Args> void error(std::string_view rt_fmt_str, Args &&...args) const {
    if (level_ > Verbosity::ERROR) {
      return;
    }
    const std::string text = std::vformat(rt_fmt_str, std::make_format_args(args...));
    const std::string line = std::format("log.error[{}] {}", tag_, text);
    golden::port(line);
    golden::raw(line);
  }

private:
  std::string_view tag_;
  Verbosity level_;
};

} // namespace espp
