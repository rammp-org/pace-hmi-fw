#include "m5stack-tab5.hpp"

#include "esp_idf_version.h"
#ifndef ESP_IDF_VERSION_VAL
#define ESP_IDF_VERSION_VAL(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#endif
#ifndef ESP_IDF_VERSION
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(0, 0, 0)
#endif

#include <algorithm>
#include <cstdlib>

#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_ldo_regulator.h>

using namespace std::chrono_literals;

namespace espp {

M5StackTab5::DisplayController M5StackTab5::detect_display_controller() {
  auto &i2c = internal_i2c();

  // This probe usually runs right after the LCD hardware reset. On the ST7123
  // (TDDI) the reset momentarily drops its I2C endpoint (0x55) off the bus, and
  // it can take longer than the post-reset delay to re-enumerate. Probing once
  // is therefore racy — it intermittently reports UNKNOWN even though the chip
  // is present. Retry a few times with a short delay so a slow re-enumeration
  // doesn't abort LCD init.
  static constexpr int kProbeAttempts = 10;
  for (int attempt = 0; attempt < kProbeAttempts; ++attempt) {
    // Probe for the GT911 touch controller, if it exists we have an ILI9881 display
    if (i2c.probe_device(0x14)) {
      return M5StackTab5::DisplayController::ILI9881;
    }

    // Probe for the ST7123 display controller
    if (i2c.probe_device(0x55)) {
      return M5StackTab5::DisplayController::ST7123;
    }

    if (attempt + 1 < kProbeAttempts) {
      logger_.debug("Display controller not found on probe {} — retrying", attempt);
      std::this_thread::sleep_for(20ms);
    }
  }

  // Unknown display controller
  return M5StackTab5::DisplayController::UNKNOWN;
}
bool M5StackTab5::initialize_lcd() {
  logger_.info("Initializing M5Stack Tab5 LCD (MIPI-DSI, {}x{})", display_width_, display_height_);

  if (!ioexp_0x43_) {
    if (!initialize_io_expanders()) {
      logger_.error("Failed to init IO expanders for LCD reset");
      return false;
    }
  }

  esp_err_t ret = ESP_OK;

  // enable DSI PHY power
  static esp_ldo_channel_handle_t phy_pwr_chan = nullptr;
  {
    logger_.info("Acquiring MIPI DSI PHY power LDO channel");
    esp_ldo_channel_config_t phy_pwr_cfg{};
    memset(&phy_pwr_cfg, 0, sizeof(phy_pwr_cfg));
    static constexpr int MIPI_DSI_PHY_PWR_LDO_CHANNEL = 3;
    static constexpr int MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV = 2500;
    phy_pwr_cfg.chan_id = MIPI_DSI_PHY_PWR_LDO_CHANNEL;
    phy_pwr_cfg.voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV;
    ret = esp_ldo_acquire_channel(&phy_pwr_cfg, &phy_pwr_chan);
    if (ret != ESP_OK) {
      logger_.error("Failed to acquire MIPI DSI PHY power LDO channel: {}", esp_err_to_name(ret));
      return false;
    }
  }

  // Configure backlight PWM
  if (!backlight_) {
    backlight_channel_configs_.push_back({.gpio = static_cast<size_t>(lcd_backlight_io),
                                          .channel = LEDC_CHANNEL_0,
                                          .timer = LEDC_TIMER_0,
                                          .duty = 0.0f,
                                          .speed_mode = LEDC_LOW_SPEED_MODE,
                                          .output_invert = !backlight_value});
    backlight_ = std::make_shared<Led>(Led::Config{.timer = LEDC_TIMER_0,
                                                   .frequency_hz = 5000,
                                                   .channels = backlight_channel_configs_,
                                                   .duty_resolution = LEDC_TIMER_10_BIT});
  }

  // default to 100% brightness to ensure users can see screen
  brightness(100.0f);

  // Perform hardware reset sequence via IO expander
  logger_.info("Performing LCD hardware reset sequence");
  lcd_reset(true); // Assert reset
  std::this_thread::sleep_for(10ms);
  lcd_reset(false); // Release reset
  std::this_thread::sleep_for(120ms);

  // Detect and initialize the appropriate display controller
  logger_.info("Detecting display controller type");
  auto detected_controller = detect_display_controller();

  // create MIPI DSI bus first, it will initialize the DSI PHY as well
  if (lcd_handles_.mipi_dsi_bus == nullptr) {
    logger_.info("Creating MIPI DSI bus");
    esp_lcd_dsi_bus_config_t bus_config = {};
    bus_config.bus_id = 0;
    bus_config.num_data_lanes = 2;
    bus_config.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    // ST7123/ST7121 lane rate must match the value the vendor init table is
    // tuned for. M5Stack's own M5Tab5-UserDemo (known-good) uses 1300 Mbps for
    // this panel; feeding a lower rate (e.g. 965) leaves the panel's internal
    // MIPI PLL desynced from the incoming HS stream -> backlit but black screen,
    // even though the pixel clock is close enough for the touch engine to run.
    bus_config.lane_bit_rate_mbps =
        (detected_controller == DisplayController::ILI9881) ? 730 : 1300;
    ret = esp_lcd_new_dsi_bus(&bus_config, &lcd_handles_.mipi_dsi_bus);
    if (ret != ESP_OK) {
      logger_.error("New DSI bus init failed: {}", esp_err_to_name(ret));
      return false;
    }
  }

  if (lcd_handles_.io == nullptr) {
    logger_.info("Install MIPI DSI LCD panel I/O");
    // we use DBI interface to send LCD commands and parameters
    esp_lcd_dbi_io_config_t dbi_config = {};
    dbi_config.virtual_channel = 0;
    dbi_config.lcd_cmd_bits = 8;
    dbi_config.lcd_param_bits = 8;
    ret = esp_lcd_new_panel_io_dbi(lcd_handles_.mipi_dsi_bus, &dbi_config, &lcd_handles_.io);
    if (ret != ESP_OK) {
      logger_.error("New panel IO failed: {}", esp_err_to_name(ret));
      // TODO: free previously allocated resources
      return false;
    }
  }

  if (detected_controller == DisplayController::UNKNOWN) {
    logger_.error("Unable to detect display controller");
    return false;
  }
  logger_.info("Detected display controller: {}", get_display_controller_name(detected_controller));

  esp_lcd_dpi_panel_config_t dpi_cfg{};
  memset(&dpi_cfg, 0, sizeof(dpi_cfg));

  if (detected_controller == DisplayController::ILI9881 && lcd_handles_.panel == nullptr) {
    // Create DPI panel with M5Stack Tab5 official ILI9881 timing parameters
    logger_.info("Creating MIPI DSI DPI panel with M5Stack Tab5 ILI9881 configuration");
    dpi_cfg.virtual_channel = 0;
    dpi_cfg.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    // 74 MHz -> ~59.5 Hz refresh with the porches below (M5Stack's official 60
    // MHz only reaches ~48 Hz). Stays within the 730 Mbps lane budget
    // (74 MHz * 16 bpp / 2 lanes = 592 Mbps). If the panel blanks or
    // flickers, revert to 60.
    dpi_cfg.dpi_clock_freq_mhz = 74;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    dpi_cfg.in_color_format = LCD_COLOR_FMT_RGB565;
    dpi_cfg.out_color_format = LCD_COLOR_FMT_RGB565;
#else
    dpi_cfg.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
#endif
    // 2 frame buffers so esp_lcd_panel_draw_bitmap() can draw into an
    // off-screen buffer and flip, instead of writing live into the buffer
    // the DPI hardware is actively scanning out (visible tearing otherwise).
    dpi_cfg.num_fbs = 2;
    dpi_cfg.video_timing.h_size = display_width_;
    dpi_cfg.video_timing.v_size = display_height_;
    dpi_cfg.video_timing.hsync_back_porch = 140;
    dpi_cfg.video_timing.hsync_pulse_width = 40;
    dpi_cfg.video_timing.hsync_front_porch = 40;
    dpi_cfg.video_timing.vsync_back_porch = 20;
    dpi_cfg.video_timing.vsync_pulse_width = 4;
    dpi_cfg.video_timing.vsync_front_porch = 20;
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
    dpi_cfg.flags.use_dma2d = true;
#endif

  } else if (detected_controller == DisplayController::ST7123 && lcd_handles_.panel == nullptr) {
    dpi_cfg.virtual_channel = 0;
    dpi_cfg.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    // ST7123/ST7121 timing taken from M5Stack's own M5Tab5-UserDemo (known-good):
    // 78 MHz pixel clock + 1300 Mbps lane rate, which the vendor init table
    // (byte-identical to the demo) tunes the panel's internal PLL for;
    // mismatched pixel clock/lane rate leaves the panel backlit but black.
    dpi_cfg.dpi_clock_freq_mhz = 78;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    dpi_cfg.in_color_format = LCD_COLOR_FMT_RGB565;
    dpi_cfg.out_color_format = LCD_COLOR_FMT_RGB565;
#else
    dpi_cfg.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
#endif
    // 2 frame buffers so esp_lcd_panel_draw_bitmap() can draw into an
    // off-screen buffer and flip, instead of writing live into the buffer
    // the DPI hardware is actively scanning out (visible tearing otherwise).
    dpi_cfg.num_fbs = 2;
    dpi_cfg.video_timing.h_size = display_width_;
    dpi_cfg.video_timing.v_size = display_height_;
    dpi_cfg.video_timing.hsync_back_porch = 40;
    dpi_cfg.video_timing.hsync_pulse_width = 2;
    dpi_cfg.video_timing.hsync_front_porch = 40;
    // vsync_back_porch/front_porch retuned from the vendor defaults (4/320) to
    // correct a vertical content offset on this board; total frame lines held
    // constant (2 + 42 + 1280 + 282 == 2 + 4 + 1280 + 320 == 1606) so the
    // pixel-clock/PLL tuning above is unaffected.
    dpi_cfg.video_timing.vsync_back_porch = 42;
    dpi_cfg.video_timing.vsync_pulse_width = 2;
    dpi_cfg.video_timing.vsync_front_porch = 282;
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
    dpi_cfg.flags.use_dma2d = true;
#endif
  }

  if (lcd_handles_.panel == nullptr) {
    logger_.info("Creating DPI panel with resolution {}x{}", dpi_cfg.video_timing.h_size,
                 dpi_cfg.video_timing.v_size);
    ret = esp_lcd_new_panel_dpi(lcd_handles_.mipi_dsi_bus, &dpi_cfg, &lcd_handles_.panel);
    if (ret != ESP_OK) {
      logger_.error("Failed to create MIPI DSI DPI panel: {}", esp_err_to_name(ret));
      return false;
    }
  }

  espp::display_drivers::Config display_config{
      .panel_io = nullptr,
      .write_command = std::bind_front(&M5StackTab5::dsi_write_command, this),
      .read_command = std::bind_front(&M5StackTab5::dsi_read_command, this),
      .lcd_send_lines = nullptr,
      .reset_pin = GPIO_NUM_NC,
      .data_command_pin = GPIO_NUM_NC,
      .reset_value = false,
      .invert_colors = invert_colors,
      .swap_color_order = swap_color_order,
      .offset_x = 0,
      .offset_y = 0,
      .swap_xy = swap_xy,
      .mirror_x = mirror_x,
      .mirror_y = mirror_y,
      .mirror_portrait = false,
  };

  display_driver_.reset();
  if (detected_controller == DisplayController::ILI9881) {
    logger_.info("Initializing as ILI9881");
    auto display_driver = std::make_shared<espp::Ili9881>(display_config);
    if (display_driver->initialize()) {
      logger_.info("Successfully initialized ILI9881 display controller");
      display_driver_ = std::move(display_driver);
      display_controller_ = DisplayController::ILI9881;
    }
  } else if (detected_controller == DisplayController::ST7123) {
    logger_.info("Initializing as ST7123");
    auto display_driver = std::make_shared<espp::St7123>(display_config);
    if (display_driver->initialize()) {
      logger_.info("Successfully initialized ST7123 display controller");
      display_driver_ = std::move(display_driver);
      display_controller_ = DisplayController::ST7123;
    }
  } else {
    logger_.error("Failed to detect display controller");
    return false;
  }

  if (!display_driver_) {
    logger_.error("Failed to initialize {} display controller",
                  get_display_controller_name(detected_controller));
    return false;
  }

  logger_.info("Display controller: {}", get_display_controller_name());

  // call init on the panel
  logger_.info("Calling low-level panel init");
  ret = lcd_handles_.panel->init(lcd_handles_.panel);
  if (ret != ESP_OK) {
    logger_.error("Low-level panel init failed: {}", esp_err_to_name(ret));
    return false;
  }

  logger_.info("Display initialized with resolution {}x{}", display_width_, display_height_);

  // Fill both DPI frame buffers with white as the default background (2 now
  // that num_fbs=2, so whichever one the hardware scans out first isn't
  // uninitialized memory). This also acts as a hardware smoke test that is
  // independent of LVGL: if the panel and DSI link are working the screen
  // turns solid white right here. If it then stays white with no UI drawn on
  // top, the problem is in LVGL's flush path; if it stays black, the problem
  // is the panel init / DSI link itself.
  {
    void *fb0 = nullptr;
    void *fb1 = nullptr;
    esp_err_t fb_ret = esp_lcd_dpi_panel_get_frame_buffer(lcd_handles_.panel, 2, &fb0, &fb1);
    if (fb_ret == ESP_OK && fb0 != nullptr && fb1 != nullptr) {
      size_t fb_size = display_width_ * display_height_ * sizeof(uint16_t);
      memset(fb0, 0xFF, fb_size); // 0xFFFF per RGB565 pixel = white
      memset(fb1, 0xFF, fb_size);
      esp_cache_msync(fb0, fb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
      esp_cache_msync(fb1, fb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
      logger_.info("Filled DPI frame buffers with white ({} bytes each) as default background",
                   fb_size);
    } else {
      logger_.error("Failed to get DPI frame buffer for default fill: {}", esp_err_to_name(fb_ret));
    }
  }

  logger_.info("Register DPI panel event callback for LVGL flush ready notification");
  esp_lcd_dpi_panel_event_callbacks_t cbs = {
      .on_color_trans_done = &M5StackTab5::notify_lvgl_flush_ready,
      .on_refresh_done = nullptr,
  };
  ret = esp_lcd_dpi_panel_register_event_callbacks(lcd_handles_.panel, &cbs, this);
  if (ret != ESP_OK) {
    logger_.error("Failed to register panel event callback: {}", esp_err_to_name(ret));
    return false;
  }

  logger_.info("M5Stack Tab5 LCD initialization completed successfully");
  return true;
}

static uint16_t *third_buffer = nullptr;

// Draw buffers live in PSRAM so they can be full-screen sized; internal RAM
// cannot hold two 1280x720x2 buffers. Aligned to the larger (L2, 128 B) cache
// line size as required for PPA/DMA2D access to external memory.
static void *alloc_display_buffer(size_t size_bytes) {
  void *buf = heap_caps_aligned_alloc(128, size_bytes,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  if (!buf) {
    // not all configs tag PSRAM as DMA-capable in the heap; the PPA and DMA2D
    // can still access it
    buf = heap_caps_aligned_alloc(128, size_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  return buf;
}

bool M5StackTab5::initialize_display(size_t pixel_buffer_size) {
  logger_.info("Initializing LVGL display with pixel buffer size: {} pixels", pixel_buffer_size);
  if (!display_) {
    auto *vram0 = (Pixel *)alloc_display_buffer(pixel_buffer_size * sizeof(Pixel));
    auto *vram1 = (Pixel *)alloc_display_buffer(pixel_buffer_size * sizeof(Pixel));
    if (!vram0 || !vram1) {
      logger_.error("Failed to allocate display buffers in PSRAM!");
      heap_caps_free(vram0);
      heap_caps_free(vram1);
      return false;
    }
    display_ = std::make_shared<Display<Pixel>>(
        Display<Pixel>::LvglConfig{.width = display_width_,
                                   .height = display_height_,
                                   .flush_callback = std::bind_front(&M5StackTab5::flush, this),
                                   .rotation_callback = nullptr, // DisplayDriver::rotate,
                                   .rotation = rotation},
        Display<Pixel>::OledConfig{
            .set_brightness_callback =
                [this](float brightness) { this->brightness(brightness * 100.0f); },
            .get_brightness_callback = [this]() { return this->brightness() / 100.0f; }},
        Display<Pixel>::StaticMemoryConfig{
            .pixel_buffer_size = pixel_buffer_size,
            .vram0 = vram0,
            .vram1 = vram1,
        },
        Logger::Verbosity::WARN);
  }

  third_buffer = (uint16_t *)alloc_display_buffer(pixel_buffer_size * sizeof(uint16_t));
  if (!third_buffer) {
    logger_.error("Failed to allocate rotation buffer in PSRAM!");
    return false;
  }

  logger_.info("LVGL display initialized");
  return true;
}

size_t M5StackTab5::rotated_display_width() const {
  auto rotation = lv_display_get_rotation(lv_display_get_default());
  switch (rotation) {
  // swap
  case LV_DISPLAY_ROTATION_90:
  case LV_DISPLAY_ROTATION_270:
    return display_height_;
  // as configured
  case LV_DISPLAY_ROTATION_0:
  case LV_DISPLAY_ROTATION_180:
  default:
    return display_width_;
  }
}

size_t M5StackTab5::rotated_display_height() const {
  auto rotation = lv_display_get_rotation(lv_display_get_default());
  switch (rotation) {
  // swap
  case LV_DISPLAY_ROTATION_90:
  case LV_DISPLAY_ROTATION_270:
    return display_width_;
  // as configured
  case LV_DISPLAY_ROTATION_0:
  case LV_DISPLAY_ROTATION_180:
  default:
    return display_height_;
  }
}

void M5StackTab5::write_lcd_lines(int xs, int ys, int xe, int ye, const uint8_t *data,
                                  uint32_t user_data) {
  (void)user_data;
  if (lcd_handles_.panel == nullptr || data == nullptr) {
    return;
  }
  if (xs < 0 || ys < 0 || xe < xs || ye < ys) {
    logger_.error("write_lcd_lines: Bad region: ({},{}) to ({},{})", xs, ys, xe, ye);
    return;
  }
  esp_lcd_panel_draw_bitmap(lcd_handles_.panel, xs, ys, xe + 1, ye + 1, data);
}

void M5StackTab5::brightness(float brightness) {
  brightness = std::clamp(brightness, 0.0f, 100.0f);
  if (backlight_) {
    backlight_->set_duty(LEDC_CHANNEL_0, brightness);
  } else {
    gpio_set_level(lcd_backlight_io, brightness > 0.0f ? 1 : 0);
  }
}

float M5StackTab5::brightness() const {
  if (backlight_) {
    auto maybe_duty = backlight_->get_duty(LEDC_CHANNEL_0);
    if (maybe_duty.has_value())
      return maybe_duty.value();
  }
  return gpio_get_level(lcd_backlight_io) ? 100.0f : 0.0f;
}

// -----------------
// DSI write helpers
// -----------------

void IRAM_ATTR M5StackTab5::flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  // Note: This function may be called from ISR context via DPI callback
  // Avoid using floating-point operations, logging, or other coprocessor functions

  if (lcd_handles_.panel == nullptr) {
    lv_display_flush_ready(disp);
    return;
  }

  int offsetx1 = area->x1;
  int offsetx2 = area->x2;
  int offsety1 = area->y1;
  int offsety2 = area->y2;

  auto rotation = lv_display_get_rotation(lv_display_get_default());
  if (rotation > LV_DISPLAY_ROTATION_0) {
    /* SW rotation */
    int32_t ww = lv_area_get_width(area);
    int32_t hh = lv_area_get_height(area);
    lv_color_format_t cf = lv_display_get_color_format(disp);
    uint32_t w_stride = lv_draw_buf_width_to_stride(ww, cf);
    uint32_t h_stride = lv_draw_buf_width_to_stride(hh, cf);
    if (rotation == LV_DISPLAY_ROTATION_180) {
      lv_draw_sw_rotate(px_map, third_buffer, hh, ww, h_stride, h_stride, LV_DISPLAY_ROTATION_180,
                        cf);
    } else if (rotation == LV_DISPLAY_ROTATION_90) {
      // printf("%ld %ld\n", w_stride, h_stride);
      lv_draw_sw_rotate(px_map, third_buffer, ww, hh, w_stride, h_stride, LV_DISPLAY_ROTATION_90,
                        cf);
      // // rotate_copy_pixel((uint16_t*)color_map, (uint16_t*)third_buffer, offsetx1, offsety1,
      // //                   offsetx2, offsety2, LV_HOR_RES, LV_VER_RES, 270);
      // rotate_copy_pixel((uint16_t*)color_map, (uint16_t*)third_buffer, 0, 0, offsetx2 - offsetx1,
      //                   offsety2 - offsety1, offsetx2 - offsetx1 + 1, offsety2 - offsety1 + 1,
      //                   270);
    } else if (rotation == LV_DISPLAY_ROTATION_270) {
      lv_draw_sw_rotate(px_map, third_buffer, ww, hh, w_stride, h_stride, LV_DISPLAY_ROTATION_270,
                        cf);
    }
    px_map = reinterpret_cast<uint8_t *>(third_buffer);
    lv_display_rotate_area(disp, const_cast<lv_area_t *>(area));
    offsetx1 = area->x1;
    offsetx2 = area->x2;
    offsety1 = area->y1;
    offsety2 = area->y2;
  }

  // pass the draw buffer to the DPI panel driver
  esp_lcd_panel_draw_bitmap(lcd_handles_.panel, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1,
                            px_map);
  // For DPI panels, the notification will come through the callback
}

bool IRAM_ATTR M5StackTab5::notify_lvgl_flush_ready(esp_lcd_panel_handle_t panel,
                                                    esp_lcd_dpi_panel_event_data_t *edata,
                                                    void *user_ctx) {
  espp::M5StackTab5 *tab5 = static_cast<espp::M5StackTab5 *>(user_ctx);
  if (tab5 == nullptr) {
    return false;
  }

  // This is called from ISR context, so we need to be careful about what we do
  // Just notify LVGL that the flush is ready - avoid logging or other complex operations
  if (tab5->display_) {
    tab5->display_->notify_flush_ready();
  }
  return false;
}

void M5StackTab5::dsi_write_command(uint8_t cmd, std::span<const uint8_t> params,
                                    uint32_t /*flags*/) {
  if (!lcd_handles_.io) {
    logger_.error("DSI write_command does not have a valid IO handle");
    return;
  }

  // logger_.debug("DSI write_command 0x{:02X} with {} bytes", cmd, params.size());

  esp_lcd_panel_io_handle_t io = lcd_handles_.io;
  const void *data_ptr = params.data();
  size_t data_size = params.size();
  esp_err_t err = esp_lcd_panel_io_tx_param(io, (int)cmd, data_ptr, data_size);
  if (err != ESP_OK) {
    logger_.error("DSI write_command 0x{:02X} failed: {}", cmd, esp_err_to_name(err));
  }
}

void M5StackTab5::dsi_read_command(uint8_t cmd, std::span<uint8_t> data, uint32_t /*flags*/) {
  if (!lcd_handles_.io) {
    logger_.error("DSI read_command does not have a valid IO handle");
    return;
  }

  // logger_.debug("DSI read_command 0x{:02X} with {} bytes", cmd, length);

  esp_lcd_panel_io_handle_t io = lcd_handles_.io;
  void *data_ptr = data.data();
  size_t data_size = data.size();
  esp_err_t err = esp_lcd_panel_io_rx_param(io, (int)cmd, data_ptr, data_size);
  if (err != ESP_OK) {
    logger_.error("DSI read_command 0x{:02X} failed: {}", cmd, esp_err_to_name(err));
  }
}

} // namespace espp
