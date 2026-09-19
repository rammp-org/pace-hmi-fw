#include "rtps_serial.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_pthread.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "console.hpp"
#include "log_capture.hpp"
#include "logger.hpp"
#include "ota_update.hpp"
#include "rtps_comms.hpp"

#if CONFIG_HMI_RTPS_SERIAL
namespace {

using namespace std::chrono_literals;

espp::Logger logger({.tag = "rtps_serial", .level = espp::Logger::Verbosity::INFO});

constexpr size_t kBufferSize = 16 * 1024; // output waiting to be sent, in PSRAM
constexpr size_t kMaxHosts = 4;
constexpr size_t kMaxLine = 128;
// A best-effort writer keeps RTPS_CFG_HISTORY_SIZE_STATELESS (2) samples and sends
// them from the RTPS worker pool: a publish that outruns the pool overwrites one that
// was never sent. Each one gets this long to go out before the next goes in.
constexpr auto kPublishGap = 4ms;
constexpr int64_t kIdleTimeoutUs =
    std::chrono::duration_cast<std::chrono::microseconds>(rammp::kSerialIdleTimeout).count();

// The loggers of the network stack itself (espp rtps/socket/thread_pool, the W5500
// driver, esp_eth, esp_netif, lwIP). Sending the stream is what makes them print, so
// what they print stays off it: see "Serial over RTPS" in hmi_rtps_spec.hpp.
constexpr std::string_view kHeldBackTags[] = {"Rtps",   "rtps_reactor", "ThreadPool", "UdpSocket",
                                              "w5500.", "esp_eth",      "esp_netif",  "lwip"};

// Output: the sink fills it from any task, the pump drains it.
StreamBufferHandle_t output = nullptr;
std::mutex output_mutex;            // one writer at a time, and the line state below
std::atomic<bool> attached{false};  // any host: nothing is gathered without one
std::atomic<uint32_t> dropped{0};   // bytes the buffer had no room for
std::atomic<uint32_t> held_back{0}; // network-stack lines kept off the link
std::atomic<TaskHandle_t> pump_task{nullptr};
bool line_start = true; // the sink is at the start of a line (output_mutex)
bool line_held = false; // and the current line is not forwarded (output_mutex)

// Hosts, by session. The RX handler adds them, the pump expires them.
struct Host {
  uint32_t session = 0; // 0 = free slot
  int64_t seen_us = 0;
  bool wants_history = false;
};
std::mutex hosts_mutex;
std::array<Host, kMaxHosts> hosts;

// Input: one line being typed (the RX handler only, but it may run on either
// RTPS worker).
std::mutex input_mutex;
std::string input_line;
bool input_after_cr = false;
QueueHandle_t commands = nullptr; // finished lines, for the console task (in PSRAM)
std::atomic<TaskHandle_t> console_task{nullptr};

// The pump's own.
uint32_t own_session = 0;
std::string own_mac;
uint32_t tx_seq = 0;
rammp::SerialData message;
char chunk[rammp::kSerialChunk];

// The tag a log line was printed under: "[Tag/I][..]: ..." (espp) or
// "I (123) tag: ..." (IDF), after any colour codes; "" when it is neither.
std::string_view tag_of(std::string_view line) {
  while (line.size() >= 2 && line[0] == '\x1b' && line[1] == '[') {
    size_t end = 2;
    while (end < line.size() && !(line[end] >= '@' && line[end] <= '~')) {
      end++;
    }
    line.remove_prefix(std::min(end + 1, line.size()));
  }
  if (line.starts_with('[')) {
    const size_t end = line.find_first_of("/]");
    return end == std::string_view::npos ? std::string_view{} : line.substr(1, end - 1);
  }
  if (line.size() > 3 && line[1] == ' ' && line[2] == '(') {
    const size_t open = line.find(") ");
    const size_t colon = open == std::string_view::npos ? open : line.find(':', open);
    return colon == std::string_view::npos ? std::string_view{}
                                           : line.substr(open + 2, colon - open - 2);
  }
  return {};
}

bool network_stack_line(std::string_view line) {
  const std::string_view tag = tag_of(line);
  return !tag.empty() &&
         std::any_of(std::begin(kHeldBackTags), std::end(kHeldBackTags),
                     [tag](std::string_view held) { return tag.starts_with(held); });
}

// Into the output buffer, under output_mutex.
void put(std::string_view data) {
  const size_t sent = xStreamBufferSend(output, data.data(), data.size(), 0);
  if (sent != data.size()) {
    dropped += data.size() - sent;
  }
}

// log_capture's sink: from any task that prints. Never blocks, never prints. A line
// is judged by its start, which is all of it but for one longer than stdout's
// 128-byte buffer.
void forward(const char *data, size_t size) {
  if (!attached || xTaskGetCurrentTaskHandle() == pump_task.load()) {
    return;
  }
  std::lock_guard<std::mutex> lock(output_mutex);
  std::string_view rest(data, size);
  while (!rest.empty()) {
    const size_t newline = rest.find('\n');
    const size_t length = newline == std::string_view::npos ? rest.size() : newline + 1;
    const std::string_view piece = rest.substr(0, length);
    if (line_start) {
      line_held = network_stack_line(piece);
      held_back += line_held ? 1 : 0;
    }
    if (!line_held) {
      put(piece);
    }
    line_start = newline != std::string_view::npos;
    rest.remove_prefix(length);
  }
}

void echo(std::string_view text) {
  std::lock_guard<std::mutex> lock(output_mutex);
  put(text);
}

// A finished line, for the console task: commands do not run on the RTPS worker that
// received them (6 KB of stack, and `reboot` sleeps 200 ms).
void run_later(std::string_view line) {
  std::array<char, kMaxLine + 1> item{};
  line.copy(item.data(), kMaxLine);
  if (xQueueSend(commands, item.data(), 0) != pdTRUE) {
    echo("busy: line dropped\r\n");
  }
}

// The console task: runs typed commands; they print their answers, which are forwarded
// like any output (this is not the pump's task).
void run_commands() {
  console_task = xTaskGetCurrentTaskHandle();
  std::array<char, kMaxLine + 1> line{};
  while (true) {
    if (xQueueReceive(commands, line.data(), portMAX_DELAY) == pdTRUE) {
      console_run(line.data());
    }
  }
}

// Typed bytes: echoed as a terminal would, and run as a command at the end of a line.
void type(const std::vector<uint8_t> &data) {
  std::lock_guard<std::mutex> lock(input_mutex);
  for (const uint8_t byte : data) {
    const char c = static_cast<char>(byte);
    const bool after_cr = input_after_cr;
    input_after_cr = c == '\r';
    if (c == '\r' || c == '\n') {
      if (c == '\n' && after_cr) {
        continue; // the LF of a CR LF
      }
      echo("\r\n");
      run_later(input_line);
      input_line.clear();
    } else if (c == '\b' || c == 0x7f) {
      if (!input_line.empty()) {
        input_line.pop_back();
        echo("\b \b");
      }
    } else if (byte >= 0x20 && byte < 0x7f && input_line.size() < kMaxLine) {
      input_line += c;
      echo(std::string_view(&c, 1));
    }
  }
}

// A host said something: it is (still) attached. A new one gets the history. False
// for a host that is not attached (anything but HELLO from a session never seen).
bool note_host(uint32_t session, bool hello) {
  std::lock_guard<std::mutex> lock(hosts_mutex);
  const int64_t now = esp_timer_get_time();
  auto host = std::find_if(hosts.begin(), hosts.end(),
                           [session](const Host &h) { return h.session == session; });
  if (host == hosts.end()) {
    if (!hello) {
      return false; // HELLO comes first (this device may have rebooted since)
    }
    // a free slot, else the host heard from least recently
    host = std::min_element(hosts.begin(), hosts.end(),
                            [](const Host &a, const Host &b) { return a.seen_us < b.seen_us; });
    *host = Host{.session = session, .seen_us = now, .wants_history = true};
  }
  host->seen_us = now;
  attached = true;
  return true;
}

void forget_host(uint32_t session) {
  std::lock_guard<std::mutex> lock(hosts_mutex);
  for (auto &host : hosts) {
    if (host.session == session) {
      host = Host{};
    }
  }
}

void on_rx(const rammp::SerialData &m) {
  if (m.session == 0 || (!m.mac.empty() && m.mac != own_mac)) {
    return;
  }
  switch (m.kind) {
  case rammp::SerialKind::HELLO:
    note_host(m.session, true);
    break;
  case rammp::SerialKind::DATA:
    if (note_host(m.session, false)) {
      type(m.data);
    }
    break;
  case rammp::SerialKind::BYE:
    forget_host(m.session);
    break;
  case rammp::SerialKind::RESET:
    if (note_host(m.session, false)) {
      run_later("reboot"); // says why not, when it will not
    }
    break;
  default:
    break;
  }
}

///////////////////////////////////////////////////////////////////////////////
// The pump: the only task that publishes

bool send(rammp::SerialKind kind, uint32_t to, std::string_view data) {
  message.session = own_session;
  message.seq = tx_seq + 1;
  message.kind = kind;
  message.to = to;
  message.data.assign(data.begin(), data.end());
  const bool sent = rtps_comms_publish_serial(message);
  if (sent) {
    tx_seq++; // a message nobody could be sent is not a gap
  }
  std::this_thread::sleep_for(kPublishGap);
  return sent;
}

// Drops hosts gone quiet; returns the session of one that still wants its history (0 = none).
// Logs only after letting go of the locks: a print takes stdout's lock, and forward()
// takes output_mutex under it, so printing while holding output_mutex can deadlock.
uint32_t tend_hosts() {
  uint32_t wants = 0;
  std::array<uint32_t, kMaxHosts> quiet{};
  bool detached = false;
  {
    std::lock_guard<std::mutex> lock(hosts_mutex);
    const int64_t now = esp_timer_get_time();
    bool any = false;
    for (size_t i = 0; i < hosts.size(); i++) {
      Host &host = hosts[i];
      if (host.session != 0 && now - host.seen_us > kIdleTimeoutUs) {
        quiet[i] = host.session;
        host = Host{};
      }
      if (host.session != 0) {
        any = true;
        if (host.wants_history && wants == 0) {
          wants = host.session;
          host.wants_history = false;
        }
      }
    }
    if (!any && attached) {
      attached = false;
      std::lock_guard<std::mutex> out_lock(output_mutex);
      xStreamBufferReset(output);
      line_start = true;
      detached = true;
    }
  }
  for (const uint32_t session : quiet) {
    if (session != 0) {
      logger.info("Serial host {:08x} gone quiet", session);
    }
  }
  if (detached) {
    logger.info("Serial: no host attached ({} network-stack lines were kept off it; stack "
                "never used: {} B, console's {} B)",
                held_back.exchange(0), uxTaskGetStackHighWaterMark(nullptr),
                console_task.load() ? uxTaskGetStackHighWaterMark(console_task.load()) : 0);
  }
  return wants;
}

// The lines the LogScreen holds, coloured as they were printed, then an empty
// HISTORY to say that was all.
void send_history(uint32_t to) {
  constexpr size_t kCapacity = kLogCaptureLines * (kLogCaptureLineLen + 12) + 256;
  auto *text = static_cast<char *>(heap_caps_malloc(kCapacity, MALLOC_CAP_SPIRAM));
  if (text == nullptr) {
    send(rammp::SerialKind::HISTORY, to, "--- serial: no memory for the history ---\n");
    send(rammp::SerialKind::HISTORY, to, {});
    return;
  }
  const esp_app_desc_t *app = esp_app_get_description();
  size_t length = static_cast<size_t>(std::snprintf(
      text, kCapacity,
      "--- serial over RTPS: %s %s, mac %s. The last lines kept, then live; 'help' for "
      "commands ---\n",
      app->project_name, app->version, own_mac.c_str()));
  const uint32_t lines = log_capture_visit([&](LogLevel level, std::string_view line) {
    const std::string_view colour = level == LogLevel::Error     ? "\x1b[0;31m"
                                    : level == LogLevel::Warning ? "\x1b[0;33m"
                                                                 : "";
    const std::string_view reset = colour.empty() ? "" : "\x1b[0m";
    if (length + colour.size() + line.size() + reset.size() + 1 > kCapacity) {
      return;
    }
    for (const std::string_view part : {colour, line, reset, std::string_view("\n")}) {
      std::memcpy(text + length, part.data(), part.size());
      length += part.size();
    }
  });
  logger.info("Serial host {:08x} attached: sending {} lines ({} B) kept", to,
              std::min<uint32_t>(lines, kLogCaptureLines), length);
  for (size_t at = 0; at < length; at += rammp::kSerialChunk) {
    send(rammp::SerialKind::HISTORY, to,
         std::string_view(text + at, std::min(rammp::kSerialChunk, length - at)));
  }
  send(rammp::SerialKind::HISTORY, to, {});
  heap_caps_free(text);
}

void pump() {
  pump_task = xTaskGetCurrentTaskHandle();
  while (true) {
    if (const uint32_t to = tend_hosts()) {
      send_history(to);
      continue;
    }
    size_t n = xStreamBufferReceive(output, chunk, sizeof(chunk), pdMS_TO_TICKS(100));
    if (n > 0 && n < sizeof(chunk)) {
      // Gather what follows for a moment: one message per line would be ~100 each.
      std::this_thread::sleep_for(rammp::kSerialFlushPeriod);
      n += xStreamBufferReceive(output, chunk + n, sizeof(chunk) - n, 0);
    }
    if (n > 0) {
      send(rammp::SerialKind::DATA, 0, std::string_view(chunk, n));
    }
    if (const uint32_t lost = dropped.exchange(0)) {
      const int len = std::snprintf(chunk, sizeof(chunk),
                                    "\n--- serial: %lu bytes dropped (output faster than the "
                                    "link) ---\n",
                                    static_cast<unsigned long>(lost));
      send(rammp::SerialKind::DATA, 0, std::string_view(chunk, static_cast<size_t>(len)));
    }
  }
}

} // namespace
#endif

void rtps_serial_start() {
#if CONFIG_HMI_RTPS_SERIAL
  output = xStreamBufferCreateWithCaps(kBufferSize, 1, MALLOC_CAP_SPIRAM);
  commands = xQueueCreateWithCaps(4, kMaxLine + 1, MALLOC_CAP_SPIRAM);
  if (output == nullptr || commands == nullptr) {
    logger.error("No memory for the serial over RTPS");
    return;
  }
  own_mac = ota_mac_string(); // the host tells devices apart by OtaDeviceInfo.mac
  do {
    own_session = esp_random();
  } while (own_session == 0);
  message.mac = own_mac;
  message.data.reserve(rammp::kSerialChunk); // grows once, here
  if (!log_capture_add_sink(forward)) {
    logger.error("No log_capture sink left for the serial over RTPS");
    return;
  }
  rtps_comms_on_serial(on_rx);

  esp_pthread_cfg_t previous = esp_pthread_get_default_config();
  esp_pthread_get_cfg(&previous);
  auto cfg = esp_pthread_get_default_config();
  cfg.stack_size = 8 * 1024;
  cfg.prio = 3;
  cfg.thread_name = "rtps_serial";
  cfg.stack_alloc_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  esp_pthread_set_cfg(&cfg);
  std::thread(pump).detach();
  cfg.stack_size = 12 * 1024; // as the network console's: console_run's `info` needs it
  cfg.thread_name = "serial_console";
  esp_pthread_set_cfg(&cfg);
  std::thread(run_commands).detach();
  esp_pthread_set_cfg(&previous);
  logger.info("Serial over RTPS on '{}' / '{}' as {}", rammp::kSerialTx.name, rammp::kSerialRx.name,
              own_mac);
#endif
}
