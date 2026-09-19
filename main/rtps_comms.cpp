#include "rtps_comms.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_eth_mac_w5500.h"
#include "esp_eth_netif_glue.h"
#include "esp_eth_phy_w5500.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"
#include "sdkconfig.h"

#include "logger.hpp"
#include "ota_update.hpp"
#include "rtps_participant.hpp"
#include "rtps_pubsub.hpp"
#include "task.hpp"

using namespace std::chrono_literals;
using Rtps = espp::RtpsParticipant;
template <class T> using Publisher = std::unique_ptr<espp::Publisher<T>>;

namespace {

// W5500 on the M5-Bus SPI lines, CS/INT on spare header GPIOs (GPIO52 stays free for the twist pot)
constexpr spi_host_device_t kSpiHost = SPI2_HOST;
constexpr gpio_num_t kPinSck = GPIO_NUM_5;
constexpr gpio_num_t kPinMosi = GPIO_NUM_18;
constexpr gpio_num_t kPinMiso = GPIO_NUM_19;
constexpr gpio_num_t kPinCs = GPIO_NUM_45;
constexpr gpio_num_t kPinInt = GPIO_NUM_4;
// The P4's SPI clock is 80 MHz / n (80, 40, 26.7, 20). 40 and 80 bring no link up
// on this PCB (these pins go through the GPIO matrix, not SPI2's IOMUX pins), nor
// does 40 in half-duplex with IDF's delay compensation (input_delay_ns 25 or 37);
// this v1.3 silicon has no later sample point to offer. 26.7 works but measured no
// faster than 20 at OTA (the flash write is the bottleneck), so 20 keeps the margin.
constexpr int kSpiClockHz = 80'000'000 / 4;
constexpr int kRxPollPeriodMs = 0; // 0 = RX on the INT line; N = poll every N ms (rules out INT)

constexpr int kHeartbeatEvery = 2; // bench counter on rammp::kHmiCounter, per kOtaInfoPeriod
constexpr int64_t kMibStatusTimeoutUs =
    std::chrono::duration_cast<std::chrono::microseconds>(rammp::kMibStatusTimeout).count();
constexpr int64_t kDiagRateWindowUs = 4'000'000;

espp::Logger logger({.tag = "rtps_comms", .level = espp::Logger::Verbosity::INFO});

// Link state: the event handlers, the RTPS task and the LVGL poll all touch these.
std::atomic<bool> eth_failed{false};
std::atomic<bool> link_up{false};
std::atomic<bool> got_ip{false};
std::atomic<bool> peer_matched{false};    // a latch: espp only reports "matched"
std::atomic<bool> endpoints_ready{false}; // every publisher below exists
std::atomic<int64_t> last_status_us{0};   // last MibStatus, esp_timer time; 0 = never
std::string ip_address;
esp_netif_ip_info_t ip_info{};

std::unique_ptr<Rtps> participant;
std::unique_ptr<espp::Task> heartbeat_task;

// HMI -> MCB / PC
Publisher<rammp::UInt32> counter_pub;
Publisher<rammp::XYTwist> joystick_pub;
Publisher<rammp::SeatCommand> seat_pub;
Publisher<rammp::DriveCommand> drive_pub;
Publisher<rammp::SelfTestReport> report_pub;
Publisher<rammp::OtaDeviceInfo> ota_info_pub;
Publisher<rammp::SerialData> serial_pub; // CONFIG_HMI_RTPS_SERIAL

std::function<void(float)> brightness_handler;
std::function<void(const MIB::MibStatus &)> mib_status_handler;
std::function<void(const rammp::Diagnostics &)> diagnostics_handler;
std::function<void(uint8_t)> selftest_run_handler;
std::function<void(uint16_t, int)> selftest_pong_handler;
std::function<void(const rammp::SerialData &)> serial_handler;

// Arrival statistics: written on the RTPS task, read by the LVGL and self-test tasks.
std::mutex stats_mutex;
RtpsMcbStats mcb_stats;
uint8_t mcb_last_seq = 0;
constexpr size_t kDiagArrivals = 8;
int64_t diag_arrival_us[kDiagArrivals] = {};
uint32_t diag_arrival_total = 0;

///////////////////////////////////////////////////////////////////////////////
// Typed endpoints: espp serializes every message (XCDR1)

// The topic fixes the message type: a publisher or handler for the wrong one does not compile.
template <class T> Publisher<T> make_publisher(const rammp::Topic<T> &topic) {
  auto pub = std::make_unique<espp::Publisher<T>>(
      *participant,
      typename espp::Publisher<T>::Config{.topic = topic.name, .type_name = topic.type});
  if (!pub->is_valid()) {
    logger.error("Could not add writer '{}' (CONFIG_RTPS_LIMIT_* in sdkconfig.defaults)",
                 topic.name);
    return nullptr;
  }
  return pub;
}

// The reader lives on the participant, so the Subscriber object itself can go.
template <class T> bool subscribe(const rammp::Topic<T> &topic, void (*on_msg)(const T &)) {
  const espp::Subscriber<T> sub(
      *participant, {.topic = topic.name, .type_name = topic.type, .on_message = on_msg});
  if (!sub.is_valid()) {
    logger.error("Could not add reader '{}' (CONFIG_RTPS_LIMIT_* in sdkconfig.defaults)",
                 topic.name);
  }
  return sub.is_valid();
}

// Quiet until the endpoints exist and a peer has matched: no one to send to yet.
template <class T> bool publish(const Publisher<T> &pub, const T &msg) {
  return endpoints_ready && peer_matched && pub->publish(msg);
}

// PC -> HMI bench command. The self test's run and pong ride it, tagged in the top nibble.
void on_command(const rammp::UInt32 &msg) {
  const uint32_t v = msg.data;
  const rammp::SelfTestTag tag = rammp::tag_of(v);
  if (tag == rammp::SelfTestTag::RUN) {
    logger.info("Self-test run {} requested", v & 0xFFu);
    if (selftest_run_handler) {
      selftest_run_handler(static_cast<uint8_t>(v & 0xFFu));
    }
  } else if (tag == rammp::SelfTestTag::PONG || tag == rammp::SelfTestTag::PING) {
    const int peer_rx = tag == rammp::SelfTestTag::PONG ? static_cast<int>((v >> 16) & 0xFFFu) : -1;
    if (selftest_pong_handler) {
      selftest_pong_handler(static_cast<uint16_t>(v & 0xFFFFu), peer_rx); // PING back = echo
    }
  } else {
    logger.info("Command/echo: {}", v);
  }
}

void on_brightness(const rammp::UInt32 &msg) {
  const float percent = std::min(static_cast<float>(msg.data), 100.0f);
  logger.info("Brightness command: {:.0f}%", percent);
  if (brightness_handler) {
    brightness_handler(percent);
  }
}

void note_mcb_status(int64_t now_us, uint8_t seq) {
  std::lock_guard<std::mutex> lock(stats_mutex);
  if (mcb_stats.samples > 0) {
    mcb_stats.max_gap_us = std::max(mcb_stats.max_gap_us, now_us - mcb_stats.last_us);
    const auto step = static_cast<uint8_t>(seq - mcb_last_seq);
    if (step > 1 && step < 64) { // a big jump is a restarted MCB, not a loss
      mcb_stats.lost += step - 1u;
    }
  } else {
    mcb_stats.first_us = now_us;
  }
  mcb_stats.samples++;
  mcb_stats.last_us = now_us;
  mcb_last_seq = seq;
}

void on_mib_status(const MIB::MibStatus &s) {
  const int64_t now = esp_timer_get_time();
  last_status_us = now; // liveness, stamped before a slow handler can age it
  note_mcb_status(now, s.seq);
  // It repeats every kMibStatusPeriod: log only what changed. The seat is deliberately
  // not in here - it moves while a button is held, and would bury everything else.
  auto shown = [](const MIB::MibStatus &m) {
    return std::tie(m.systemState, m.activeProfile, m.speed, m.status_text, m.error_message,
                    m.error_footer);
  };
  static std::optional<MIB::MibStatus> last;
  if (!last || shown(*last) != shown(s)) {
    logger.info("MIB: state={} '{}' profile={} speed={:.2f} m/s error='{}' / '{}'",
                rammp::to_string(s.systemState), s.status_text, rammp::to_string(s.activeProfile),
                s.speed, s.error_message, s.error_footer);
    last = s;
  }
  if (mib_status_handler) {
    mib_status_handler(s);
  }
}

void on_ota_command(const rammp::OtaCommand &c) { ota_handle_command(c); }

void on_serial(const rammp::SerialData &d) {
  if (serial_handler) {
    serial_handler(d);
  }
}

void on_diagnostics(const rammp::Diagnostics &d) {
  static size_t last_count = SIZE_MAX; // log the first sample, then only a changed item count
  if (d.items.size() != last_count) {
    last_count = d.items.size();
    logger.info("Diagnostics arriving: {} items", last_count);
  }
  {
    std::lock_guard<std::mutex> lock(stats_mutex);
    diag_arrival_us[diag_arrival_total++ % kDiagArrivals] = esp_timer_get_time();
  }
  if (diagnostics_handler) {
    diagnostics_handler(d);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Ethernet

void eth_event_handler(void *, esp_event_base_t, int32_t event_id, void *) {
  if (event_id == ETHERNET_EVENT_CONNECTED) {
    logger.info("Ethernet link up");
    link_up = true;
  } else if (event_id == ETHERNET_EVENT_DISCONNECTED) {
    logger.warn("Ethernet link down");
    link_up = false;
    got_ip = false; // the lease does not survive the link
  }
}

void got_ip_event_handler(void *, esp_event_base_t, int32_t, void *event_data) {
  ip_info = static_cast<ip_event_got_ip_t *>(event_data)->ip_info;
  ip_address = fmt::format("{}.{}.{}.{}", IP2STR(&ip_info.ip));
  logger.info("Got IP {} (gateway {}.{}.{}.{})", ip_address, IP2STR(&ip_info.gw));
  got_ip = true;
}

void lost_ip_event_handler(void *, esp_event_base_t, int32_t, void *) {
  logger.warn("Lost IP address");
  got_ip = false;
}

// Three pings to `target`, logged; true if any came back.
bool run_ping(const ip_addr_t &target, std::string_view label) {
  struct PingStats {
    std::atomic<uint32_t> received{0};
    std::atomic<bool> done{false};
  } stats;
  esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
  config.target_addr = target;
  config.count = 3;
  esp_ping_callbacks_t callbacks = {};
  callbacks.cb_args = &stats;
  callbacks.on_ping_success = [](esp_ping_handle_t, void *args) {
    static_cast<PingStats *>(args)->received++;
  };
  callbacks.on_ping_end = [](esp_ping_handle_t, void *args) {
    static_cast<PingStats *>(args)->done = true;
  };
  esp_ping_handle_t ping = nullptr;
  if (esp_ping_new_session(&config, &callbacks, &ping) != ESP_OK) {
    return false;
  }
  esp_ping_start(ping);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(config.count + 2);
  while (!stats.done && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(100ms);
  }
  esp_ping_stop(ping);
  esp_ping_delete_session(ping);
  logger.info("Ping {} ({}): {}/{} replies", label, ipaddr_ntoa(&target), stats.received.load(),
              config.count);
  return stats.received > 0;
}

bool check(esp_err_t err, const char *what) {
  if (err != ESP_OK) {
    logger.error("{} failed: {}", what, esp_err_to_name(err));
  }
  return err == ESP_OK;
}

// W5500 over SPI -> esp_eth -> esp_netif with a DHCP client
bool initialize_ethernet() {
  if (!check(esp_netif_init(), "esp_netif_init")) {
    return false;
  }
  if (esp_err_t err = esp_event_loop_create_default();
      err != ESP_ERR_INVALID_STATE && !check(err, "event loop")) {
    return false;
  }
  if (esp_err_t err = gpio_install_isr_service(0);
      err != ESP_ERR_INVALID_STATE && !check(err, "gpio_install_isr_service")) {
    return false; // the W5500's INT line needs it
  }

  spi_bus_config_t bus_config = {};
  bus_config.mosi_io_num = kPinMosi;
  bus_config.miso_io_num = kPinMiso;
  bus_config.sclk_io_num = kPinSck;
  bus_config.quadwp_io_num = -1;
  bus_config.quadhd_io_num = -1;
  if (!check(spi_bus_initialize(kSpiHost, &bus_config, SPI_DMA_CH_AUTO), "spi_bus_initialize")) {
    return false;
  }

  spi_device_interface_config_t dev_config = {};
  dev_config.command_bits = 16; // W5500 address phase
  dev_config.address_bits = 8;  // W5500 control phase
  dev_config.clock_speed_hz = kSpiClockHz;
  dev_config.spics_io_num = kPinCs;
  dev_config.queue_size = 20;
  eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(kSpiHost, &dev_config);
  // cppcheck-suppress knownConditionTrueFalse ; a build-time switch, both arms are real
  w5500_config.base.int_gpio_num = kRxPollPeriodMs > 0 ? -1 : kPinInt;
  w5500_config.base.poll_period_ms = kRxPollPeriodMs;

  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.reset_gpio_num = -1; // no reset line wired
  esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
  esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
  if (!mac || !phy) {
    logger.error("Failed to create W5500 MAC/PHY");
    return false;
  }
  esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
  esp_eth_handle_t eth_handle = nullptr;
  // the first SPI read of the chip: failing here means wiring, power, CS or a held reset
  if (!check(esp_eth_driver_install(&eth_config, &eth_handle), "W5500 driver install")) {
    return false;
  }

  uint8_t mac_addr[6] = {}; // the W5500 has no MAC of its own
  esp_read_mac(mac_addr, ESP_MAC_ETH);
  esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr);

  esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
  esp_netif_t *eth_netif = esp_netif_new(&netif_config);
  if (!eth_netif ||
      !check(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)), "netif attach")) {
    return false;
  }
  esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, &lost_ip_event_handler, nullptr);
  if (!check(esp_eth_start(eth_handle), "esp_eth_start")) {
    return false;
  }
  logger.info("W5500 up, waiting for link + DHCP");
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// RTPS

bool start_participant() {
  // the W5500's per-frame SPI bounce buffer comes from this pool, unchecked
  logger.info("DMA-capable heap: {} free", heap_caps_get_free_size(MALLOC_CAP_DMA));
  participant = std::make_unique<Rtps>(Rtps::Config{
      .interface_address = ip_address,
      .on_publisher_matched = [] { peer_matched = true; },
      .on_subscriber_matched = [] { peer_matched = true; },
      .log_level = espp::Logger::Verbosity::INFO, // DEBUG traces discovery
  });
  if (!participant->start()) {
    logger.error("Failed to start RTPS participant");
    participant.reset();
    return false;
  }

  // 7 writers + 6 readers, plus SPDP's pair: the budget set in sdkconfig.defaults
  counter_pub = make_publisher(rammp::kHmiCounter);
  joystick_pub = make_publisher(rammp::kJoystickXYTwist);
  seat_pub = make_publisher(rammp::kJoystickSeatCommand);
  drive_pub = make_publisher(rammp::kJoystickDriveCommand);
  report_pub = make_publisher(rammp::kSelfTestReport);
  ota_info_pub = make_publisher(rammp::kOtaDeviceInfo);
  const bool ok = counter_pub && joystick_pub && seat_pub && drive_pub && report_pub &&
                  ota_info_pub && subscribe(rammp::kHmiCommand, on_command) &&
                  subscribe(rammp::kHmiBrightness, on_brightness) &&
                  subscribe(MIB::kMibStatus, on_mib_status) &&
                  subscribe(rammp::kMcbDiagnostics, on_diagnostics) &&
                  subscribe(rammp::kOtaCommand, on_ota_command);
  if (!ok) {
    return false;
  }
#if CONFIG_HMI_RTPS_SERIAL
  serial_pub = make_publisher(rammp::kSerialTx);
  if (!serial_pub || !subscribe(rammp::kSerialRx, on_serial)) {
    return false;
  }
#endif
  endpoints_ready = true;
  logger.info("RTPS up on {}", ip_address);
  // Ethernet, DHCP and every endpoint: enough for an updated image to keep itself.
  ota_boot_confirm();

  // OtaDeviceInfo every kOtaInfoPeriod, for a fleet update host to find this device,
  // and the bench heartbeat counter every kHeartbeatEvery of them.
  heartbeat_task = std::make_unique<espp::Task>(espp::Task::Config{
      .callback = [](std::mutex &m, std::condition_variable &cv) -> bool {
        static uint32_t tick = 0;
        static uint32_t counter = 0;
        rammp::OtaDeviceInfo info = ota_device_info();
        info.seq = static_cast<uint8_t>(tick);
        info.ip = ip_address;
        publish(ota_info_pub, info);
        if (tick++ % kHeartbeatEvery == 0 && publish(counter_pub, rammp::UInt32{++counter}) &&
            counter % 10 == 1) {
          logger.info("Heartbeat {}", counter);
        }
        std::unique_lock<std::mutex> lock(m);
        cv.wait_for(lock, rammp::kOtaInfoPeriod);
        return false; // keep running
      },
      .task_config = {.name = "rtps_pub", .stack_size_bytes = 6 * 1024, .priority = 5}});
  return heartbeat_task->start();
}

} // namespace

///////////////////////////////////////////////////////////////////////////////
// Public API

void rtps_comms_on_brightness(std::function<void(float)> handler) {
  brightness_handler = std::move(handler);
}
void rtps_comms_on_mib_status(std::function<void(const MIB::MibStatus &)> handler) {
  mib_status_handler = std::move(handler);
}
void rtps_comms_on_diagnostics(std::function<void(const rammp::Diagnostics &)> handler) {
  diagnostics_handler = std::move(handler);
}
void rtps_comms_on_selftest_run(std::function<void(uint8_t)> handler) {
  selftest_run_handler = std::move(handler);
}
void rtps_comms_on_selftest_pong(std::function<void(uint16_t, int)> handler) {
  selftest_pong_handler = std::move(handler);
}

void rtps_comms_on_serial(std::function<void(const rammp::SerialData &)> handler) {
  serial_handler = std::move(handler);
}

bool rtps_comms_publish_serial(const rammp::SerialData &data) {
  return serial_pub && publish(serial_pub, data);
}

bool rtps_comms_publish_adc(float x, float y, float twist, rammp::Buttons buttons) {
  return publish(joystick_pub, rammp::XYTwist{x, y, twist, buttons});
}

bool rtps_comms_publish_drive(rammp::DriveRequest request, MIB::DriveProfile profile) {
  return publish(drive_pub, rammp::DriveCommand{request, profile});
}

bool rtps_comms_publish_seat(rammp::SeatAxis axis, float target) {
  return publish(seat_pub, rammp::SeatCommand{axis, target});
}

bool rtps_comms_publish_selftest_ping(uint16_t seq) {
  return publish(counter_pub, rammp::UInt32{rammp::tagged(rammp::SelfTestTag::PING, seq)});
}

bool rtps_comms_publish_selftest_report(const rammp::SelfTestReport &report) {
  return publish(report_pub, report);
}

RtpsDiagStats rtps_comms_diag_stats() {
  std::lock_guard<std::mutex> lock(stats_mutex);
  RtpsDiagStats stats;
  if (diag_arrival_total == 0) {
    return stats;
  }
  // Rate over the samples of the last kDiagRateWindowUs, so a gap does not drag it down.
  const uint32_t stored = std::min<uint32_t>(diag_arrival_total, kDiagArrivals);
  const int64_t newest = diag_arrival_us[(diag_arrival_total - 1) % kDiagArrivals];
  int64_t oldest = newest;
  uint32_t n = 1;
  for (; n < stored; n++) {
    const int64_t earlier = diag_arrival_us[(diag_arrival_total - 1 - n) % kDiagArrivals];
    if (newest - earlier > kDiagRateWindowUs) {
      break;
    }
    oldest = earlier;
  }
  stats.last_us = newest;
  if (n >= 2 && newest > oldest) {
    stats.rate_tenths_hz = static_cast<int32_t>((n - 1) * 10'000'000LL / (newest - oldest));
  }
  return stats;
}

void rtps_comms_mcb_stats_reset() {
  std::lock_guard<std::mutex> lock(stats_mutex);
  mcb_stats = RtpsMcbStats{};
}

RtpsMcbStats rtps_comms_mcb_stats() {
  std::lock_guard<std::mutex> lock(stats_mutex);
  return mcb_stats;
}

RtpsLinkState rtps_comms_link_state() {
  if (eth_failed) {
    return RtpsLinkState::ETH_FAILED;
  }
  if (!link_up) {
    return RtpsLinkState::LINK_DOWN;
  }
  if (!got_ip) {
    return RtpsLinkState::NO_IP;
  }
  const int64_t last = last_status_us.load();
  const bool fresh = last != 0 && esp_timer_get_time() - last < kMibStatusTimeoutUs;
  return fresh ? RtpsLinkState::CONNECTED : RtpsLinkState::NO_PEER;
}

const char *rtps_comms_link_state_name(RtpsLinkState state) {
  switch (state) {
  case RtpsLinkState::ETH_FAILED:
    return "ETH_FAILED";
  case RtpsLinkState::LINK_DOWN:
    return "LINK_DOWN";
  case RtpsLinkState::NO_IP:
    return "NO_IP";
  case RtpsLinkState::NO_PEER:
    return "NO_PEER";
  case RtpsLinkState::CONNECTED:
    return "CONNECTED";
  }
  return "?";
}

std::string rtps_comms_link_state_meaning(RtpsLinkState state) {
  switch (state) {
  case RtpsLinkState::ETH_FAILED:
    return "W5500 did not answer at boot";
  case RtpsLinkState::LINK_DOWN:
    return "no Ethernet link: cable unplugged?";
  case RtpsLinkState::NO_IP:
    return "link up, but no DHCP lease";
  case RtpsLinkState::NO_PEER:
    return fmt::format("MIB not answering: no MibStatus in {} ms",
                       rammp::kMibStatusTimeout.count());
  case RtpsLinkState::CONNECTED:
    return "MibStatus arriving";
  }
  return "?";
}

bool rtps_comms_start() {
  logger.info("W5500: SCK {}, MOSI {}, MISO {}, CS {}, INT {}", static_cast<int>(kPinSck),
              static_cast<int>(kPinMosi), static_cast<int>(kPinMiso), static_cast<int>(kPinCs),
              static_cast<int>(kPinInt));
  if (!initialize_ethernet()) {
    eth_failed = true;
    return false;
  }
  // DHCP can take tens of seconds (or a cable goes in later): wait in the background.
  static auto startup_task = std::make_unique<espp::Task>(espp::Task::Config{
      .callback = [](std::mutex &m, std::condition_variable &cv) -> bool {
        if (!got_ip) {
          std::unique_lock<std::mutex> lock(m);
          cv.wait_for(lock, 500ms);
          return false; // keep waiting
        }
        ip_addr_t gateway{};
        ipaddr_aton(fmt::format("{}.{}.{}.{}", IP2STR(&ip_info.gw)).c_str(), &gateway);
        if (!run_ping(gateway, "gateway")) {
          logger.warn("Gateway unreachable: discovery with LAN peers will likely fail");
        }
        ip_addr_t internet{};
        ipaddr_aton("8.8.8.8", &internet);
        run_ping(internet, "internet");
        if (!start_participant()) {
          logger.error("RTPS participant failed to start");
        }
        return true; // one-shot
      },
      .task_config = {.name = "rtps_start", .stack_size_bytes = 8 * 1024, .priority = 5}});
  return startup_task->start();
}
