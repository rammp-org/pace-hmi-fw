#pragma once
// What the quick POST is judged on: plain facts gathered at boot (README, REQ-POST-01..03).
// No ESP-IDF here: the enums below mirror ESP-IDF's values, and the L1 app checks them against
// the IDF headers it is built with (POST-030, POST-031).

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace hmi::post {

/// Why the chip last reset. Value for value the same as ESP-IDF v6.0's `esp_reset_reason_t`
/// (esp_system.h), so the caller converts with one `static_cast` from the IDF's int. A value
/// this enum does not name is judged as an unknown reset (REQ-POST-05).
enum class ResetReason : int32_t {
  UNKNOWN = 0,     ///< ESP_RST_UNKNOWN: can not be determined
  POWERON = 1,     ///< ESP_RST_POWERON
  EXT = 2,         ///< ESP_RST_EXT: external pin
  SW = 3,          ///< ESP_RST_SW: esp_restart
  PANIC = 4,       ///< ESP_RST_PANIC: exception or panic
  INT_WDT = 5,     ///< ESP_RST_INT_WDT: interrupt watchdog
  TASK_WDT = 6,    ///< ESP_RST_TASK_WDT: task watchdog
  WDT = 7,         ///< ESP_RST_WDT: other watchdogs
  DEEPSLEEP = 8,   ///< ESP_RST_DEEPSLEEP: wake from deep sleep
  BROWNOUT = 9,    ///< ESP_RST_BROWNOUT
  SDIO = 10,       ///< ESP_RST_SDIO
  USB = 11,        ///< ESP_RST_USB: the USB peripheral
  JTAG = 12,       ///< ESP_RST_JTAG
  EFUSE = 13,      ///< ESP_RST_EFUSE: efuse error
  PWR_GLITCH = 14, ///< ESP_RST_PWR_GLITCH
  CPU_LOCKUP = 15, ///< ESP_RST_CPU_LOCKUP: double exception
};

/// The running image's OTA state. Value for value the same as ESP-IDF v6.0's
/// `esp_ota_img_states_t` (esp_flash_partitions.h).
enum class OtaImageState : uint32_t {
  NEW = 0x0U,              ///< ESP_OTA_IMG_NEW: the bootloader turns it into PENDING_VERIFY
  PENDING_VERIFY = 0x1U,   ///< ESP_OTA_IMG_PENDING_VERIFY: first boot of an update, unconfirmed
  VALID = 0x2U,            ///< ESP_OTA_IMG_VALID: confirmed
  INVALID = 0x3U,          ///< ESP_OTA_IMG_INVALID: confirmed bad; should never run
  ABORTED = 0x4U,          ///< ESP_OTA_IMG_ABORTED: never confirmed; should never run
  UNDEFINED = 0xFFFFFFFFU, ///< ESP_OTA_IMG_UNDEFINED: no OTA data (factory or serial flash)
};

/// One stick axis over the rest window, in raw ADC mV as the ADC task reads it (twist after
/// its 8-read average, every axis before any lowpass). Peak-to-peak is `max_mv - min_mv`.
struct AxisRest {
  int32_t mean_mv = 0;  ///< mean of the samples
  int32_t min_mv = 0;   ///< lowest sample
  int32_t max_mv = 0;   ///< highest sample
  uint32_t samples = 0; ///< samples behind the three numbers above
};

/// One rest window from the ADC task (about 1 s): how often it read all three axes, what each
/// axis did, and the stick button. Small and trivially copyable, so it can cross tasks as a
/// message (CS-OWN-05).
struct StickWindow {
  uint32_t cycles = 0;       ///< ADC task cycles in the window
  uint32_t valid_cycles = 0; ///< of those, cycles with all three axes read
  AxisRest x{};              ///< horizontal axis
  AxisRest y{};              ///< vertical axis
  AxisRest twist{};          ///< twist axis
  bool button_idle = false;  ///< the stick button read released on every cycle of the window
};
static_assert(std::is_trivially_copyable_v<StickWindow>, "StickWindow crosses tasks by value");
static_assert(sizeof(StickWindow) <= 128, "a message is at most 128 bytes (CS-OWN-05)");

/// One axis of the calibration in use, rounded to whole mV.
struct AxisCal {
  int32_t min_mv = 0;    ///< one end of travel
  int32_t centre_mv = 0; ///< rest
  int32_t max_mv = 0;    ///< the other end of travel
};

/// The calibration in use, and whether it is the one saved in flash (rather than the
/// compiled-in defaults).
struct Calibration {
  bool saved = false; ///< loaded from flash and accepted
  AxisCal x{};        ///< horizontal axis
  AxisCal y{};        ///< vertical axis
  AxisCal twist{};    ///< twist axis
};

/// The 7-bit I2C addresses that answered the boot scan, as a 128-bit set. A set has no
/// count to overrun (CS-FLW-02).
class I2cSet {
public:
  /// @brief Marks an address as found.
  /// @param address 7-bit address; bit 7 is ignored
  constexpr void add(uint8_t address) noexcept {
    const auto a = static_cast<uint8_t>(address & 0x7FU);
    words_[a / 32U] |= 1U << (a % 32U);
  }
  /// @brief Whether an address was found.
  /// @param address 7-bit address; bit 7 is ignored
  /// @return true when add() was called with it
  [[nodiscard]] constexpr bool has(uint8_t address) const noexcept {
    const auto a = static_cast<uint8_t>(address & 0x7FU);
    return ((words_[a / 32U] >> (a % 32U)) & 1U) != 0U;
  }

private:
  std::array<uint32_t, 4> words_{};
};

/// The running firmware image.
struct ImageFacts {
  bool verified = false;                          ///< the image's own SHA-256 check passed
  OtaImageState state = OtaImageState::UNDEFINED; ///< its OTA state
};

/// Heap headroom, in bytes. The caller saturates anything above INT32_MAX.
struct MemoryFacts {
  int32_t internal_free_min_b = 0; ///< least internal RAM free since boot
  int32_t internal_largest_b = 0;  ///< largest free internal block now
  int32_t dma_free_min_b = 0;      ///< least DMA-capable RAM free since boot
  int32_t psram_free_b = 0;        ///< PSRAM free now
};

/// Stack headroom of the safety-relevant tasks: the high-water mark, in bytes never used.
struct StackFacts {
  int32_t adc_free_b = 0; ///< the ADC (stick) task
  int32_t ui_free_b = 0;  ///< the LVGL task
};

/// Everything the quick POST is judged on. A group that has not been gathered yet is
/// `std::nullopt`, and its checks are PENDING (REQ-POST-03).
struct Facts {
  std::optional<StickWindow> window; ///< the latest rest window
  std::optional<Calibration> cal;    ///< the calibration in use
  std::optional<I2cSet> i2c;         ///< the boot scan of the internal bus
  std::optional<ResetReason> reset;  ///< the last reset's reason
  std::optional<ImageFacts> image;   ///< the running image
  std::optional<MemoryFacts> memory; ///< heap headroom
  std::optional<StackFacts> stacks;  ///< stack headroom
};

} // namespace hmi::post
