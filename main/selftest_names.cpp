#include "selftest_names.hpp"

const char *reset_reason_name(esp_reset_reason_t reason) {
  switch (reason) {
  case ESP_RST_POWERON:
    return "power-on";
  case ESP_RST_EXT:
    return "external pin";
  case ESP_RST_SW:
    return "software";
  case ESP_RST_PANIC:
    return "PANIC";
  case ESP_RST_INT_WDT:
    return "INTERRUPT WDT";
  case ESP_RST_TASK_WDT:
    return "TASK WDT";
  case ESP_RST_WDT:
    return "WDT";
  case ESP_RST_DEEPSLEEP:
    return "deep sleep";
  case ESP_RST_BROWNOUT:
    return "BROWNOUT";
  case ESP_RST_USB:
    return "USB";
  case ESP_RST_JTAG:
    return "JTAG";
  case ESP_RST_CPU_LOCKUP:
    return "CPU LOCKUP";
  case ESP_RST_PWR_GLITCH:
    return "POWER GLITCH";
  default:
    return "other";
  }
}
