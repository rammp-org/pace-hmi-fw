#include "console.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "format.hpp"
#include "ota_update.hpp"
#include "rtps_comms.hpp"
#include "selftest.hpp"

namespace {

std::function<std::string()> reboot_guard;

} // namespace

void console_set_reboot_guard(std::function<std::string()> guard) {
  reboot_guard = std::move(guard);
}

bool console_reboot() {
  if (const std::string refused = reboot_guard ? reboot_guard() : ""; !refused.empty()) {
    fmt::print("reboot {}\n", refused);
    return false;
  }
  fmt::print("rebooting\n");
  std::fflush(stdout);
  std::this_thread::sleep_for(std::chrono::milliseconds(200)); // let the line reach the hosts
  esp_restart();
  return true;
}

void console_run(std::string_view line) {
  while (!line.empty() && (line.front() == ' ' || line.front() == '\r')) {
    line.remove_prefix(1);
  }
  while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) {
    line.remove_suffix(1);
  }
  if (line.empty()) {
    return;
  }
  if (line == "help") {
    fmt::print("commands: info, mem, selftest, reboot\n");
  } else if (line == "info") {
    const rammp::OtaDeviceInfo ota = ota_device_info();
    const RtpsLinkState link = rtps_comms_link_state();
    fmt::print("{} {} on {} ({}), image {}, update {}{}\nmac {}, link {}: {}, up {} s\n",
               ota.project, ota.version, ota.slot, ota.hw_rev,
               ota.image_state == rammp::OtaImageState::CONFIRMED        ? "confirmed"
               : ota.image_state == rammp::OtaImageState::PENDING_VERIFY ? "pending verify"
                                                                         : "not from an update",
               rammp::to_string(ota.state),
               ota.last_error.empty() ? "" : " (last: " + ota.last_error + ")", ota.mac,
               rtps_comms_link_state_name(link), rtps_comms_link_state_meaning(link),
               esp_timer_get_time() / 1'000'000);
  } else if (line == "mem") {
    fmt::print("internal {} B free (min {}), DMA {} B free (min {}), PSRAM {} B free\n",
               heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
               heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
               heap_caps_get_free_size(MALLOC_CAP_DMA),
               heap_caps_get_minimum_free_size(MALLOC_CAP_DMA),
               heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  } else if (line == "selftest") {
    std::fputs(selftest_request(SelfTestTrigger::LOCAL, 0) ? "self test started\n"
                                                           : "a self test is already running\n",
               stdout);
  } else if (line == "reboot") {
    console_reboot();
  } else {
    fmt::print("unknown command '{}' (help lists them)\n", line);
  }
}
