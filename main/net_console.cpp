#include "net_console.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include "esp_heap_caps.h"
#include "esp_pthread.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"

#include "log_capture.hpp"
#include "logger.hpp"
#include "ota_update.hpp"
#include "rtps_comms.hpp"
#include "selftest.hpp"

namespace {

espp::Logger logger({.tag = "console", .level = espp::Logger::Verbosity::INFO});

constexpr size_t kBufferSize = 16 * 1024; // output waiting for the socket, in PSRAM
constexpr size_t kChunk = 512;
constexpr size_t kMaxLine = 128;

StreamBufferHandle_t output = nullptr;
std::mutex output_mutex; // a stream buffer takes one writer at a time
std::atomic<bool> connected{false};
std::atomic<uint32_t> dropped{0};

// log_capture's sink: from any task that prints. Never blocks, never prints.
void forward(const char *data, size_t size) {
  if (!connected) {
    return;
  }
  std::lock_guard<std::mutex> lock(output_mutex);
  if (xStreamBufferSend(output, data, size, 0) != size) {
    dropped++;
  }
}

void send_all(int sock, std::string_view data) {
  while (!data.empty()) {
    const ssize_t sent = send(sock, data.data(), data.size(), 0);
    if (sent <= 0) {
      return;
    }
    data.remove_prefix(static_cast<size_t>(sent));
  }
}

void run_command(std::string_view line) {
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
    fmt::print("rebooting\n");
    std::fflush(stdout);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    esp_restart();
  } else {
    fmt::print("unknown command '{}' (help lists them)\n", line);
  }
}

void serve(int client) {
  // What the LogScreen still holds, then everything from here on. A line printed
  // between the two can show twice; none goes missing.
  connected = true;
  std::string history = "--- console: the last lines kept, then live. 'help' for commands ---\n";
  log_capture_visit([&history](LogLevel, std::string_view line) {
    history.append(line);
    history += '\n';
  });
  send_all(client, history);
  history = {};

  char chunk[kChunk];
  std::string line;
  while (true) {
    const size_t n = xStreamBufferReceive(output, chunk, sizeof(chunk), pdMS_TO_TICKS(50));
    if (n > 0) {
      send_all(client, std::string_view(chunk, n));
    }
    if (const uint32_t lost = dropped.exchange(0)) {
      send_all(client, fmt::format("\n--- console: {} writes dropped ---\n", lost));
    }
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(client, &fds);
    timeval now{};
    if (select(client + 1, &fds, nullptr, nullptr, &now) <= 0) {
      continue;
    }
    char in[64];
    const ssize_t got = recv(client, in, sizeof(in), 0);
    if (got <= 0) {
      return;
    }
    for (ssize_t i = 0; i < got; i++) {
      if (in[i] == '\n' || in[i] == '\r') {
        run_command(line);
        line.clear();
      } else if (line.size() < kMaxLine) {
        line += in[i];
      }
    }
  }
}

void run() {
  const int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  const int yes = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(rammp::kConsolePort);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (listener < 0 || bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
      listen(listener, 1) != 0) {
    logger.error("Could not listen on port {}", rammp::kConsolePort);
    return;
  }
  logger.info("Network console on port {}", rammp::kConsolePort);
  while (true) {
    sockaddr_in peer{};
    socklen_t len = sizeof(peer);
    const int client = accept(listener, reinterpret_cast<sockaddr *>(&peer), &len);
    if (client < 0) {
      continue;
    }
    const timeval send_timeout{.tv_sec = 2, .tv_usec = 0};
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
    logger.info("Console client {} connected", inet_ntoa(peer.sin_addr));
    serve(client);
    connected = false;
    close(client);
    xStreamBufferReset(output);
    logger.info("Console client left (stack never used: {} B)",
                uxTaskGetStackHighWaterMark(nullptr));
  }
}

} // namespace

void net_console_start() {
#if CONFIG_HMI_NET_CONSOLE
  output = xStreamBufferCreateWithCaps(kBufferSize, 1, MALLOC_CAP_SPIRAM);
  if (output == nullptr) {
    logger.error("No memory for the network console");
    return;
  }
  log_capture_set_sink(forward);
  esp_pthread_cfg_t previous = esp_pthread_get_default_config();
  esp_pthread_get_cfg(&previous);
  auto cfg = esp_pthread_get_default_config();
  cfg.stack_size = 12 * 1024; // the espp logger alone takes ~2.5 KB; 6 KB overflowed
  cfg.prio = 2;
  cfg.thread_name = "net_console";
  cfg.stack_alloc_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  esp_pthread_set_cfg(&cfg);
  std::thread(run).detach();
  esp_pthread_set_cfg(&previous);
#endif
}
