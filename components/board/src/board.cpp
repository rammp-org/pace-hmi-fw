#include "board/board.hpp"

#include <ctime>

#include "hmi_format/topbar.hpp"

namespace hmi::board {

Board::Board(const Config &config)
    : tab5_(config.tab5)
    , logger_(config.log)
    , brightness_step_(config.brightness_step)
    , touch_click_({.tab5 = config.tab5, .log = config.log, .click = config.click}) {}

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

bool Board::start_imu(const espp::M5StackTab5::Imu::filter_fn &orientation_filter) {
  logger_.info("Initializing IMU...");
  // initialize the IMU
  if (!tab5_.initialize_imu(orientation_filter)) {
    logger_.error("Failed to initialize IMU!");
    return false;
  }
  return true;
}

void Board::start_sdcard() {
  // initialize the uSD card
  using SdCardConfig = espp::M5StackTab5::SdCardConfig;
  SdCardConfig sdcard_config{};
  if (!tab5_.initialize_sdcard(sdcard_config)) {
    logger_.warn("Failed to initialize uSD card, there may not be a uSD card inserted!");
  } else {
    uint32_t size_mb = 0;
    uint32_t free_mb = 0;
    if (tab5_.get_sd_card_info(&size_mb, &free_mb)) {
      logger_.info("uSD card size: {} MB, free space: {} MB", size_mb, free_mb);
    } else {
      logger_.warn("Failed to get uSD card info");
    }
  }
}

bool Board::start_rtc(hmi::housekeeping::SystemClock &system_clock) {
  logger_.info("Initializing RTC...");
  // initialize the RTC
  if (!tab5_.initialize_rtc()) {
    logger_.error("Failed to initialize RTC!");
    return false;
  }

  auto current_time = std::tm{};
  if (!tab5_.get_rtc_time(current_time)) {
    logger_.error("Failed to get RTC time");
    return false;
  }

  // The RTC holds the MCB's local time (no TZ is set: the system clock simply holds
  // the local wall time the MCB reported). One that lost power reads a date long
  // gone (hmi_format clock_plausible, REQ-FMT-06); the clock then shows --:-- until
  // the MCB sends the time.
  if (hmi::format::clock_plausible(current_time)) {
    system_clock.set(current_time);
    logger_.info("RTC time {:%Y-%m-%d %H:%M:%S}", current_time);
  } else {
    logger_.warn("RTC not set ({:%Y-%m-%d}); the clock waits for the MCB", current_time);
  }
  return true;
}

bool Board::start_battery() {
  logger_.info("Initializing battery management...");
  // initialize battery monitoring
  if (!tab5_.initialize_battery_monitoring()) {
    logger_.error("Failed to initialize battery monitoring!");
    return false;
  }

  // enable charging
  tab5_.set_charging_enabled(true);
  return true;
}

bool Board::start_audio() {
  logger_.info("Initializing sound...");
  // initialize the sound
  if (!tab5_.initialize_audio()) {
    logger_.error("Failed to initialize sound!");
    return false;
  }
  return true;
}

void Board::start_side_button() {
  // Brightness control with button
  logger_.info("Initializing button...");
  if (!tab5_.initialize_button(SideButton{.logger = logger_, .on_press = brightness_step_})) {
    logger_.warn("Failed to initialize button");
  }
}

bool Board::start_touch() {
  logger_.info("Initializing touch...");
  if (!tab5_.initialize_touch([this](const espp::TouchpadData &touch) { touch_click_(touch); })) {
    logger_.error("Failed to initialize touch!");
    return false;
  }
  return true;
}

void Board::start_speaker(size_t sample_rate) {
  logger_.info("Setting audio sample rate to {} Hz", sample_rate);
  tab5_.audio_sample_rate(sample_rate);

  // unmute the audio and set the volume to 60%
  tab5_.mute(false);
  tab5_.volume(60.0f);
}

} // namespace hmi::board
