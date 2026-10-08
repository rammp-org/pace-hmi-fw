#include "board/board.hpp"

namespace hmi::board {

Board::Board(const Config &config)
    : tab5_(config.tab5)
    , logger_(config.log) {}

void Board::probe_internal_i2c() {
  // first let's get the internal i2c bus and probe for all devices on the bus
  logger_.info("Probing internal I2C bus...");
  auto &i2c = tab5_.internal_i2c();
  // 0x08..0x77 only: the rest are reserved, and a glitched ACK there once put
  // 0x01 in this list, which the self test then reported as a lost device.
  for (uint8_t address = 0x08; address <= 0x77; address++) {
    if (i2c.probe_device(address)) {
      i2c_devices_.push_back(address);
    }
  }
  logger_.info("Found devices at addresses: {::#02x}", i2c_devices_);
}

bool Board::start_io_expanders() {
  // Initialize the IO expanders
  logger_.info("Initializing IO expanders...");
  if (!tab5_.initialize_io_expanders()) {
    logger_.error("Failed to initialize IO expanders!");
    return false;
  }

  // EXT5V_EN (0x43 P2) is asserted by the expander's default output mask; read it
  // back to confirm the M5-Bus / 2.54-10P / HY2.0-4P 5V rail is live
  auto ext_5v = tab5_.get_io_expander_output(0x43, 2);
  logger_.info("EXT_5V_EN: {}", ext_5v ? (*ext_5v ? "enabled" : "DISABLED") : "read failed");
  return true;
}

bool Board::start_display() {
  logger_.info("Initializing lcd...");
  // initialize the LCD
  if (!tab5_.initialize_lcd()) {
    logger_.error("Failed to initialize LCD!");
    return false;
  }

  // Query LCD controller
  const char *controller_name = tab5_.get_display_controller_name();
  logger_.info(controller_name);

  // initialize the display with full-screen draw buffers (the vendored BSP in
  // components/m5stack-tab5 allocates them in PSRAM)
  logger_.info("Initializing display...");
  auto pixel_buffer_size = tab5_.display_width() * tab5_.display_height();
  if (!tab5_.initialize_display(pixel_buffer_size)) {
    logger_.error("Failed to initialize display!");
    return false;
  }
  return true;
}

} // namespace hmi::board
