#pragma once
// The HMI's bring-up of the Tab5 on its BSP (app-main-shrink, owner 2026-10-08). Moved from
// main.cpp's app_main: each step is that code, in that order, with its log lines.

#include <cstdint>
#include <vector>

#include "logger.hpp"
#include "m5stack-tab5.hpp"

namespace hmi::board {

/// @brief The Tab5 as the HMI brings it up: the BSP's (`espp::M5StackTab5`) initialize calls
/// in app_main's order, each with app_main's log lines. Every driver is the BSP's; this class
/// only sequences and reports. A step that returns false has logged why, and app_main stops
/// there, as it did. Built once, an app_main local (app-main-shrink V6); app_main's task only.
class Board {
public:
  struct Config {
    espp::M5StackTab5 &tab5; ///< The BSP (app_main's first M5StackTab5::get()).
    espp::Logger &log;       ///< app_main's boot log: every step reports there, as before.
  };

  /// @brief Stores the Config; touches no hardware.
  /// @param config See Config.
  explicit Board(const Config &config);
  Board(const Board &) = delete;
  Board &operator=(const Board &) = delete;
  Board(Board &&) = delete;
  Board &operator=(Board &&) = delete;
  ~Board() = default;

  /// @brief Probes the internal I2C bus (0x08..0x77) and logs what answered.
  void probe_internal_i2c();

  /// @brief What probe_internal_i2c found (the DA7280 bench test and the self test read it).
  /// @return The 7-bit addresses that answered, in order.
  [[nodiscard]] const std::vector<uint8_t> &i2c_devices() const { return i2c_devices_; }

  /// @brief The IO expanders, then the EXT_5V_EN read-back.
  /// @return Whether the expanders came up.
  bool start_io_expanders();

  /// @brief The LCD, its controller's name, then the LVGL display with full-screen draw
  /// buffers (the vendored BSP allocates them in PSRAM).
  /// @return Whether the LCD and the display came up.
  bool start_display();

private:
  espp::M5StackTab5 &tab5_;
  espp::Logger &logger_;
  std::vector<uint8_t> i2c_devices_;
};

} // namespace hmi::board
