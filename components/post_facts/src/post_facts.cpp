/**
 * @file post_facts.cpp
 * @brief The quick POST's facts from ESP-IDF and FreeRTOS (post_facts.hpp). Moved from
 *        main.cpp's kPostPort and its helpers unchanged.
 */

#include "post_facts/post_facts.hpp"

#include <cstddef>
#include <cstdint>

#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace {

// post/facts.hpp mirrors the IDF enums value for value: the static_asserts here are POST-050,
// built against the IDF this firmware uses.
static_assert(static_cast<int>(hmi::post::ResetReason::UNKNOWN) == ESP_RST_UNKNOWN &&
                  static_cast<int>(hmi::post::ResetReason::POWERON) == ESP_RST_POWERON &&
                  static_cast<int>(hmi::post::ResetReason::EXT) == ESP_RST_EXT &&
                  static_cast<int>(hmi::post::ResetReason::SW) == ESP_RST_SW &&
                  static_cast<int>(hmi::post::ResetReason::PANIC) == ESP_RST_PANIC &&
                  static_cast<int>(hmi::post::ResetReason::INT_WDT) == ESP_RST_INT_WDT &&
                  static_cast<int>(hmi::post::ResetReason::TASK_WDT) == ESP_RST_TASK_WDT &&
                  static_cast<int>(hmi::post::ResetReason::WDT) == ESP_RST_WDT &&
                  static_cast<int>(hmi::post::ResetReason::DEEPSLEEP) == ESP_RST_DEEPSLEEP &&
                  static_cast<int>(hmi::post::ResetReason::BROWNOUT) == ESP_RST_BROWNOUT &&
                  static_cast<int>(hmi::post::ResetReason::SDIO) == ESP_RST_SDIO &&
                  static_cast<int>(hmi::post::ResetReason::USB) == ESP_RST_USB &&
                  static_cast<int>(hmi::post::ResetReason::JTAG) == ESP_RST_JTAG &&
                  static_cast<int>(hmi::post::ResetReason::EFUSE) == ESP_RST_EFUSE &&
                  static_cast<int>(hmi::post::ResetReason::PWR_GLITCH) == ESP_RST_PWR_GLITCH &&
                  static_cast<int>(hmi::post::ResetReason::CPU_LOCKUP) == ESP_RST_CPU_LOCKUP,
              "post::ResetReason must mirror esp_reset_reason_t (POST-050)");
static_assert(static_cast<uint32_t>(hmi::post::OtaImageState::NEW) == ESP_OTA_IMG_NEW &&
                  static_cast<uint32_t>(hmi::post::OtaImageState::PENDING_VERIFY) ==
                      ESP_OTA_IMG_PENDING_VERIFY &&
                  static_cast<uint32_t>(hmi::post::OtaImageState::VALID) == ESP_OTA_IMG_VALID &&
                  static_cast<uint32_t>(hmi::post::OtaImageState::INVALID) == ESP_OTA_IMG_INVALID &&
                  static_cast<uint32_t>(hmi::post::OtaImageState::ABORTED) == ESP_OTA_IMG_ABORTED &&
                  static_cast<uint32_t>(hmi::post::OtaImageState::UNDEFINED) ==
                      ESP_OTA_IMG_UNDEFINED,
              "post::OtaImageState must mirror esp_ota_img_states_t (POST-050)");
// img.ok's "verified" is the bootloader's own validation of the image (decision C7 a): true
// only because no BOOTLOADER_SKIP_VALIDATE_* option is set, which this proves.
static_assert(CONFIG_HMI_BOOTLOADER_SKIPS_VALIDATE_AS_INT == 0,
              "the bootloader must validate the app image: img.ok relies on it (C3 Q10)");

// A byte count as the POST's int32 facts carry it, saturated (REQ-POST-19).
int32_t saturated_b(size_t bytes) {
  return bytes > static_cast<size_t>(INT32_MAX) ? INT32_MAX : static_cast<int32_t>(bytes);
}

} // namespace

namespace hmi::post_facts {

hmi::post::ImageFacts image() {
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running == nullptr || esp_ota_get_state_partition(running, &state) != ESP_OK) {
    state = ESP_OTA_IMG_UNDEFINED; // a factory image or no OTA data: no OTA state
  }
  return {.verified = CONFIG_HMI_BOOTLOADER_SKIPS_VALIDATE_AS_INT == 0,
          .state = static_cast<hmi::post::OtaImageState>(state)};
}

// Heap headroom (C3 §2.2): read-only heap_caps_get_* queries (they allocate nothing; the
// ratchet exempts them, owner 2026-10-08).
std::optional<hmi::post::MemoryFacts> memory() {
  constexpr uint32_t kInternal8Bit = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  return hmi::post::MemoryFacts{
      .internal_free_min_b = saturated_b(heap_caps_get_minimum_free_size(kInternal8Bit)),
      .internal_largest_b = saturated_b(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
      .dma_free_min_b = saturated_b(heap_caps_get_minimum_free_size(MALLOC_CAP_DMA)),
      .psram_free_b = saturated_b(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
  };
}

hmi::post::StackFacts stacks() {
  TaskHandle_t adc = xTaskGetHandle("Read ADC");
  return {.adc_free_b = adc == nullptr ? 0 : saturated_b(uxTaskGetStackHighWaterMark(adc)),
          // the runner's own task: lv_task
          .ui_free_b = saturated_b(uxTaskGetStackHighWaterMark(nullptr))};
}

bool stick_task_running() { return xTaskGetHandle("Read ADC") != nullptr; }

std::uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

hmi::post::ResetReason reset_reason() {
  return static_cast<hmi::post::ResetReason>(esp_reset_reason());
}

} // namespace hmi::post_facts
