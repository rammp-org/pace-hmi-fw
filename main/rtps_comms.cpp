#include "rtps_comms.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_eth_mac_w5500.h"
#include "esp_eth_netif_glue.h"
#include "esp_eth_phy_w5500.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_hosted.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"

#include "logger.hpp"
#include "rtps_participant.hpp"
#include "rtps_pubsub.hpp"
#include "storage.hpp"
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
constexpr int kSpiClockMhz = 20;   // W5500 max is 33; 20 tolerates jumper wires
constexpr int kRxPollPeriodMs = 0; // 0 = RX on the INT line; N = poll every N ms (rules out INT)

// WiFi: the Tab5's ESP32-C6 does the radio (esp_hosted over SDIO, pins in sdkconfig.defaults).
// The network is the one saved from the Internet Settings screen; until there is one, the
// one built in from the local sdkconfig. Neither = no WiFi, see rtps_comms_wifi_configured.
constexpr char kBuiltInWifiSsid[] = CONFIG_HMI_WIFI_SSID;
constexpr char kBuiltInWifiPassword[] = CONFIG_HMI_WIFI_PASSWORD;
constexpr char kWifiFile[] = "wifi.txt";  // "ssid=...\npassword=...\n", beside settings.txt
constexpr char kHostname[] = "rammp-hmi"; // what the access point's client list shows

constexpr auto kHeartbeatPeriod = 2s; // bench counter on rammp::kHmiCounter
constexpr int64_t kMibStatusTimeoutUs =
    std::chrono::duration_cast<std::chrono::microseconds>(rammp::kMibStatusTimeout).count();
constexpr int64_t kDiagRateWindowUs = 4'000'000;

espp::Logger logger({.tag = "rtps_comms", .level = espp::Logger::Verbosity::INFO});

// Link state: the event handlers, the RTPS task and the LVGL poll all touch these.
std::atomic<NetLink> net_link{NetLink::ETHERNET}; // set once, before any task starts
std::atomic<bool> net_failed{false};
std::atomic<bool> link_up{false};
std::atomic<bool> got_ip{false};
std::atomic<bool> peer_matched{false};    // a latch: espp only reports "matched"
std::atomic<bool> endpoints_ready{false}; // every publisher below exists
std::atomic<int64_t> last_status_us{0};   // last MibStatus, esp_timer time; 0 = never
std::atomic<uint32_t> lease_ip{0};        // the DHCP lease, IPv4 in network order
std::atomic<uint32_t> lease_gw{0};
std::atomic<uint32_t> participant_ip{0}; // the address RTPS is bound to; 0 = none

// The WiFi network to join, and the radio's other jobs (Internet Settings: scan, join test).
std::mutex wifi_network_mutex; // wifi_ssid / wifi_password
std::string wifi_ssid;
std::string wifi_password;
std::mutex wifi_op_mutex;                 // the stack's bring-up, a scan, a join: one at a time
bool wifi_stack_up = false;               // under wifi_op_mutex
std::atomic<bool> wifi_hold{false};       // a scan or a join owns the radio: no auto-reconnect
std::atomic<int> wifi_join_state{0};      // 0 idle, 1 waiting, 2 joined, 3 refused
std::atomic<uint8_t> wifi_join_reason{0}; // why the last attempt was refused

// The participant and the publishers are made and dropped by the rtps_start / rtps_pub
// tasks, one after the other (start_ / stop_participant). Every publish() holds
// endpoints_mutex, so a publisher is never dropped under a task that is using it.
std::unique_ptr<Rtps> participant;
std::mutex endpoints_mutex;
std::unique_ptr<espp::Task> heartbeat_task;

// HMI -> MCB / PC
Publisher<rammp::UInt32> counter_pub;
Publisher<rammp::XYTwist> joystick_pub;
Publisher<rammp::SeatCommand> seat_pub;
Publisher<rammp::DriveCommand> drive_pub;
Publisher<rammp::SelfTestReport> report_pub;

std::function<void(float)> brightness_handler;
std::function<void(const MIB::MibStatus &)> mib_status_handler;
std::function<void(const rammp::Diagnostics &)> diagnostics_handler;
std::function<void(uint8_t)> selftest_run_handler;
std::function<void(uint16_t, int)> selftest_pong_handler;

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
  std::lock_guard<std::mutex> lock(endpoints_mutex);
  return endpoints_ready && peer_matched && pub->publish(msg);
}

std::string ip_string(uint32_t addr) {
  const esp_ip4_addr_t ip{addr};
  return fmt::format("{}.{}.{}.{}", IP2STR(&ip));
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
// The link: Ethernet or WiFi, whichever rtps_comms_start was given. Only one is
// brought up, so RTPS and its multicast have one interface to choose from.

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
  const esp_netif_ip_info_t &info = static_cast<ip_event_got_ip_t *>(event_data)->ip_info;
  lease_gw = info.gw.addr;
  lease_ip = info.ip.addr;
  logger.info("Got IP {} (gateway {})", ip_string(info.ip.addr), ip_string(info.gw.addr));
  got_ip = true; // an address other than RTPS's rebinds it: heartbeat_tick
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

bool initialize_netif() {
  if (!check(esp_netif_init(), "esp_netif_init")) {
    return false;
  }
  esp_err_t err = esp_event_loop_create_default();
  return err == ESP_ERR_INVALID_STATE || check(err, "event loop");
}

// W5500 over SPI -> esp_eth -> esp_netif with a DHCP client
bool initialize_ethernet() {
  if (!initialize_netif()) {
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
  dev_config.clock_speed_hz = kSpiClockMhz * 1000 * 1000;
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

// Why the last attempt to join failed, so a network that stays away logs once, not
// every few seconds. Event task only.
uint8_t wifi_last_reason = 0;

// `live`: WiFi is the link RTPS runs over, so this handler owns link_up / got_ip and keeps
// the network joined. With Ethernet as the link the radio is only up for the Internet
// Settings screen (a scan, a join test) and must leave the link's state alone.
void wifi_event_handler(void *, esp_event_base_t, int32_t event_id, void *event_data) {
  const bool live = net_link == NetLink::WIFI;
  if (event_id == WIFI_EVENT_STA_START) {
    if (live && !wifi_hold) {
      esp_wifi_connect();
    }
  } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
    const auto *event = static_cast<wifi_event_sta_connected_t *>(event_data);
    int expected = 1;
    wifi_join_state.compare_exchange_strong(expected, 2);
    if (live) {
      wifi_last_reason = 0;
      link_up = true; // before the RSSI, which reads nothing without it
      logger.info("WiFi joined '{}' on channel {} ({} dBm)", rtps_comms_wifi_ssid(), event->channel,
                  rtps_comms_wifi_rssi().value_or(0));
    }
  } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
    const auto *event = static_cast<wifi_event_sta_disconnected_t *>(event_data);
    // ASSOC_LEAVE is our own esp_wifi_disconnect, not an answer to a join.
    if (event->reason != WIFI_REASON_ASSOC_LEAVE) {
      wifi_join_reason = event->reason;
      int expected = 1;
      wifi_join_state.compare_exchange_strong(expected, 3);
    }
    if (live) {
      if (!wifi_hold && (link_up || event->reason != wifi_last_reason)) {
        logger.warn("WiFi: not connected to '{}' (reason {}), retrying", rtps_comms_wifi_ssid(),
                    static_cast<int>(event->reason));
      }
      wifi_last_reason = event->reason;
      link_up = false;
      got_ip = false; // the lease does not survive the association
      if (!wifi_hold) {
        esp_wifi_connect(); // the C6 scans and retries; this only asks it to
      }
    }
  }
}

// The saved network, else the built-in one. Before the link is chosen.
void wifi_load() {
  std::string ssid;
  std::string password;
  std::ifstream in(storage_path(kWifiFile));
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.rfind("ssid=", 0) == 0) {
      ssid = line.substr(5);
    } else if (line.rfind("password=", 0) == 0) {
      password = line.substr(9);
    }
  }
  const bool saved = !ssid.empty();
  if (!saved) {
    ssid = kBuiltInWifiSsid;
    password = kBuiltInWifiPassword;
  }
  if (!ssid.empty()) {
    logger.info("WiFi network: '{}' ({})", ssid, saved ? "saved" : "built in");
  }
  std::lock_guard<std::mutex> lock(wifi_network_mutex);
  wifi_ssid = ssid;
  wifi_password = password;
}

bool wifi_set_network(const std::string &ssid, const std::string &password) {
  wifi_config_t config = {};
  // Both fields may be full with no terminator (a 32-byte SSID is legal): copy by length.
  std::memcpy(config.sta.ssid, ssid.data(), std::min(ssid.size(), sizeof(config.sta.ssid)));
  std::memcpy(config.sta.password, password.data(),
              std::min(password.size(), sizeof(config.sta.password)));
  // the weakest security accepted: WPA2 or better when there is a password (WPA3 too)
  config.sta.threshold.authmode = password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  return check(esp_wifi_set_config(WIFI_IF_STA, &config), "esp_wifi_set_config");
}

// The ESP32-C6 over SDIO (esp_hosted) -> esp_wifi_remote -> esp_netif with a DHCP client.
// esp_wifi_init resets the C6 and waits for it, several seconds: not on app_main's time.
//
// With WiFi as the link this joins the known network and keeps it joined. With Ethernet as
// the link it only brings the radio up, for the Internet Settings screen to scan and to
// test a password: that station never takes an address (its DHCP client is stopped) and
// ranks under Ethernet for routing, so it cannot pull RTPS or its multicast off the cable.
// Under wifi_op_mutex; once.
bool wifi_stack_start() {
  if (wifi_stack_up) {
    return true;
  }
  const bool live = net_link == NetLink::WIFI;
  if (!initialize_netif()) {
    return false;
  }
  esp_netif_inherent_config_t netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
  if (!live) {
    netif_config.route_prio = 10; // Ethernet's is 50
  }
  esp_netif_t *netif = esp_netif_create_wifi(WIFI_IF_STA, &netif_config);
  if (!netif || !check(esp_wifi_set_default_wifi_sta_handlers(), "WiFi netif handlers")) {
    logger.error("Failed to create the WiFi netif");
    return false;
  }
  esp_netif_set_hostname(netif, kHostname);
  if (!live) {
    esp_netif_dhcpc_stop(netif);
  }
  const wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  if (!check(esp_wifi_init(&init_config), "esp_wifi_init (ESP32-C6 over SDIO)")) {
    return false;
  }
  esp_hosted_coprocessor_fwver_t version{};
  if (esp_hosted_get_coprocessor_fwversion(&version) == ESP_OK) {
    logger.info("ESP32-C6 runs esp_hosted {}.{}.{}", version.major1, version.minor1,
                version.patch1);
  }
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr);
  if (live) {
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &got_ip_event_handler, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &lost_ip_event_handler, nullptr);
  }

  if (!check(esp_wifi_set_mode(WIFI_MODE_STA), "esp_wifi_set_mode")) {
    return false;
  }
  // Before the start: the handler joins on STA_START.
  if (live) {
    std::lock_guard<std::mutex> lock(wifi_network_mutex);
    if (!wifi_set_network(wifi_ssid, wifi_password)) {
      return false;
    }
  }
  if (!check(esp_wifi_start(), "esp_wifi_start")) {
    return false;
  }
  // Power save holds frames to the next beacon (100 ms and up): the joystick stream
  // and MibStatus both need the radio awake.
  check(esp_wifi_set_ps(WIFI_PS_NONE), "esp_wifi_set_ps");
  wifi_ps_type_t ps = WIFI_PS_MAX_MODEM;
  esp_wifi_get_ps(&ps);
  if (live) {
    logger.info("WiFi up, joining '{}' (power save {})", rtps_comms_wifi_ssid(),
                static_cast<int>(ps));
  } else {
    logger.info("WiFi radio up for Internet Settings; Ethernet stays the link");
  }
  wifi_stack_up = true;
  return true;
}

bool initialize_wifi() {
  std::lock_guard<std::mutex> lock(wifi_op_mutex);
  return wifi_stack_start();
}

///////////////////////////////////////////////////////////////////////////////
// RTPS

// On the current lease.
bool start_participant() {
  // the W5500's per-frame SPI bounce buffer comes from this pool, unchecked
  logger.info("DMA-capable heap: {} free", heap_caps_get_free_size(MALLOC_CAP_DMA));
  participant_ip = lease_ip.load();
  const std::string address = ip_string(participant_ip);
  participant = std::make_unique<Rtps>(Rtps::Config{
      .interface_address = address,
      .on_publisher_matched = [] { peer_matched = true; },
      .on_subscriber_matched = [] { peer_matched = true; },
      .log_level = espp::Logger::Verbosity::INFO, // DEBUG traces discovery
  });
  if (!participant->start()) {
    logger.error("Failed to start RTPS participant");
    participant.reset();
    return false;
  }

  // 5 writers + 5 readers, plus SPDP's pair: the budget set in sdkconfig.defaults
  bool ok = false;
  {
    std::lock_guard<std::mutex> lock(endpoints_mutex);
    counter_pub = make_publisher(rammp::kHmiCounter);
    joystick_pub = make_publisher(rammp::kJoystickXYTwist);
    seat_pub = make_publisher(rammp::kJoystickSeatCommand);
    drive_pub = make_publisher(rammp::kJoystickDriveCommand);
    report_pub = make_publisher(rammp::kSelfTestReport);
    ok = counter_pub && joystick_pub && seat_pub && drive_pub && report_pub;
  }
  ok = ok && subscribe(rammp::kHmiCommand, on_command) &&
       subscribe(rammp::kHmiBrightness, on_brightness) &&
       subscribe(MIB::kMibStatus, on_mib_status) &&
       subscribe(rammp::kMcbDiagnostics, on_diagnostics);
  if (!ok) {
    return false;
  }
  endpoints_ready = true;
  logger.info("RTPS up on {} ({})", address, rtps_comms_net_link_name(net_link));
  return true;
}

// Drops the participant and its endpoints, for start_participant to bind anew. The
// publishers leave under endpoints_mutex, so no publish() is inside one, and are destroyed
// after it: stop() waits for reader callbacks, and one of those may be publishing.
void stop_participant() {
  Publisher<rammp::UInt32> counter;
  Publisher<rammp::XYTwist> joystick;
  Publisher<rammp::SeatCommand> seat;
  Publisher<rammp::DriveCommand> drive;
  Publisher<rammp::SelfTestReport> report;
  {
    std::lock_guard<std::mutex> lock(endpoints_mutex);
    endpoints_ready = false;
    counter = std::move(counter_pub);
    joystick = std::move(joystick_pub);
    seat = std::move(seat_pub);
    drive = std::move(drive_pub);
    report = std::move(report_pub);
  }
  counter.reset(); // the writers before the participant they are registered on
  joystick.reset();
  seat.reset();
  drive.reset();
  report.reset();
  participant.reset();  // stops it: sockets, threads and endpoint pools
  peer_matched = false; // the new participant is discovered afresh
  participant_ip = 0;
}

// The bench heartbeat (a counter on rammp::kHmiCounter every kHeartbeatPeriod), and the
// watch on the lease. RTPS binds to one address, so a lease that comes back different
// (an access point or DHCP server that forgot the last one) takes a new participant.
// Here, not in the IP event handler, which must not block.
bool heartbeat_tick(std::mutex &m, std::condition_variable &cv) {
  const uint32_t bound = participant_ip;
  if (got_ip && bound != 0 && lease_ip != bound) {
    logger.warn("IP changed from {} to {}: rebinding RTPS", ip_string(bound), ip_string(lease_ip));
    stop_participant();
    if (!start_participant()) {
      logger.error("RTPS participant failed to restart");
    }
  }
  static uint32_t counter = 0;
  if (publish(counter_pub, rammp::UInt32{++counter}) && counter % 10 == 1) {
    logger.info("Heartbeat {}", counter);
  }
  std::unique_lock<std::mutex> lock(m);
  cv.wait_for(lock, kHeartbeatPeriod);
  return false; // keep running
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

const char *rtps_comms_net_link_name(NetLink link) {
  return link == NetLink::WIFI ? "WiFi" : "Ethernet";
}

bool rtps_comms_wifi_configured() { return !rtps_comms_wifi_ssid().empty(); }

std::string rtps_comms_wifi_ssid() {
  std::lock_guard<std::mutex> lock(wifi_network_mutex);
  return wifi_ssid;
}

std::string rtps_comms_ip() { return got_ip ? ip_string(lease_ip) : std::string(); }

const char *rtps_comms_hostname() { return kHostname; }

std::optional<std::vector<WifiNetworkFound>> rtps_comms_wifi_scan() {
  std::lock_guard<std::mutex> lock(wifi_op_mutex);
  if (!wifi_stack_start()) {
    return std::nullopt;
  }
  const bool live = net_link == NetLink::WIFI;
  // The C6 refuses a scan while it is mid-connect, which is where a live link that
  // cannot reach its network sits: hold the retries for the length of the scan.
  wifi_hold = true;
  if (live && !link_up) {
    esp_wifi_disconnect();
    std::this_thread::sleep_for(200ms);
  }
  wifi_scan_config_t config = {};
  const bool scanned = check(esp_wifi_scan_start(&config, true), "esp_wifi_scan_start");
  uint16_t count = 24; // more than the list can usefully show
  std::vector<wifi_ap_record_t> records(count);
  const bool read =
      scanned && check(esp_wifi_scan_get_ap_records(&count, records.data()), "scan results");
  wifi_hold = false;
  if (live && !link_up) {
    esp_wifi_connect();
  }
  if (!read) {
    return std::nullopt;
  }
  std::vector<WifiNetworkFound> found;
  for (uint16_t i = 0; i < count; i++) {
    const std::string ssid(reinterpret_cast<const char *>(records[i].ssid));
    const bool known = std::any_of(found.begin(), found.end(),
                                   [&ssid](const WifiNetworkFound &n) { return n.ssid == ssid; });
    if (!ssid.empty() && !known) { // hidden networks have no name to show
      found.push_back({ssid, records[i].rssi, records[i].authmode != WIFI_AUTH_OPEN});
    }
  }
  std::sort(found.begin(), found.end(),
            [](const WifiNetworkFound &a, const WifiNetworkFound &b) { return a.rssi > b.rssi; });
  logger.info("WiFi scan: {} networks", found.size());
  return found;
}

WifiJoin rtps_comms_wifi_join(const std::string &ssid, const std::string &password) {
  std::lock_guard<std::mutex> lock(wifi_op_mutex);
  if (!wifi_stack_start()) {
    return WifiJoin::FAILED;
  }
  const bool live = net_link == NetLink::WIFI;
  logger.info("WiFi: trying '{}'", ssid);
  wifi_hold = true;
  esp_wifi_disconnect(); // leave the current network, or stop a connect in progress
  std::this_thread::sleep_for(300ms);

  WifiJoin result = WifiJoin::FAILED;
  if (wifi_set_network(ssid, password)) {
    // Two attempts: one missed beacon must not read as "not found". A wrong
    // password is final: asked again, the AP only answers "connection failed".
    for (int attempt = 0;
         attempt < 2 && result != WifiJoin::JOINED && result != WifiJoin::WRONG_PASSWORD;
         attempt++) {
      wifi_join_reason = 0;
      wifi_join_state = 1;
      if (!check(esp_wifi_connect(), "esp_wifi_connect")) {
        break;
      }
      for (int waited = 0; waited < 12000 && wifi_join_state == 1; waited += 100) {
        std::this_thread::sleep_for(100ms);
      }
      if (wifi_join_state == 2) {
        result = WifiJoin::JOINED;
        break;
      }
      switch (wifi_join_reason.load()) {
      case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
      case WIFI_REASON_HANDSHAKE_TIMEOUT:
      case WIFI_REASON_AUTH_FAIL:
      case WIFI_REASON_MIC_FAILURE:
        result = WifiJoin::WRONG_PASSWORD;
        break;
      case WIFI_REASON_NO_AP_FOUND:
      case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
      case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
      case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        result = WifiJoin::NOT_FOUND;
        break;
      default:
        result = WifiJoin::FAILED;
        break;
      }
      logger.warn("WiFi: '{}' refused (reason {})", ssid, static_cast<int>(wifi_join_reason));
      esp_wifi_disconnect();
      std::this_thread::sleep_for(300ms);
    }
  }
  wifi_join_state = 0;

  if (result == WifiJoin::JOINED) {
    std::string text = "ssid=" + ssid + "\npassword=" + password + "\n";
    storage_write(kWifiFile, text);
    {
      std::lock_guard<std::mutex> network_lock(wifi_network_mutex);
      wifi_ssid = ssid;
      wifi_password = password;
    }
    logger.info("WiFi: '{}' joined and saved", ssid);
    if (!live) {
      esp_wifi_disconnect(); // proved; Ethernet stays the link until a restart picks WiFi
    }
    wifi_hold = false;
  } else {
    // Back to the network that was in force, if any.
    std::string old_ssid;
    std::string old_password;
    {
      std::lock_guard<std::mutex> network_lock(wifi_network_mutex);
      old_ssid = wifi_ssid;
      old_password = wifi_password;
    }
    if (!old_ssid.empty()) {
      wifi_set_network(old_ssid, old_password);
    }
    wifi_hold = false;
    if (live) {
      esp_wifi_connect();
    }
  }
  return result;
}

NetLink rtps_comms_net_link() { return net_link; }

std::optional<int> rtps_comms_wifi_rssi() {
  wifi_ap_record_t ap{};
  if (net_link != NetLink::WIFI || !link_up || esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
    return std::nullopt;
  }
  return ap.rssi;
}

RtpsLinkState rtps_comms_link_state() {
  if (net_failed) {
    return RtpsLinkState::NET_FAILED;
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
  case RtpsLinkState::NET_FAILED:
    return "NET_FAILED";
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
  case RtpsLinkState::NET_FAILED:
    return net_link == NetLink::WIFI ? "the ESP32-C6 (WiFi) did not start"
                                     : "W5500 did not answer at boot";
  case RtpsLinkState::LINK_DOWN:
    return net_link == NetLink::WIFI
               ? fmt::format("not connected to WiFi '{}': out of range, or wrong password?",
                             rtps_comms_wifi_ssid())
               : "no Ethernet link: cable unplugged?";
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

bool rtps_comms_start(NetLink wanted) {
  wifi_load();
  const bool wifi = wanted == NetLink::WIFI && rtps_comms_wifi_configured();
  if (wanted == NetLink::WIFI && !wifi) {
    logger.warn("Connection is WiFi, but no network is known (Internet Settings, or "
                "CONFIG_HMI_WIFI_SSID): using Ethernet");
  }
  net_link = wifi ? NetLink::WIFI : NetLink::ETHERNET;
  logger.info("Network: {}", rtps_comms_net_link_name(net_link));
  if (!wifi) {
    logger.info("W5500: SCK {}, MOSI {}, MISO {}, CS {}, INT {}", static_cast<int>(kPinSck),
                static_cast<int>(kPinMosi), static_cast<int>(kPinMiso), static_cast<int>(kPinCs),
                static_cast<int>(kPinInt));
    if (!initialize_ethernet()) {
      net_failed = true;
      return false;
    }
  }
  // WiFi's bring-up takes seconds and DHCP tens of them (or a cable goes in later): wait
  // in the background.
  static auto startup_task = std::make_unique<espp::Task>(espp::Task::Config{
      .callback = [](std::mutex &m, std::condition_variable &cv) -> bool {
        static bool wifi_started = false;
        if (net_link == NetLink::WIFI && !wifi_started) {
          wifi_started = true;
          if (!initialize_wifi()) {
            net_failed = true;
            return true; // nothing to wait for
          }
        }
        if (!got_ip) {
          std::unique_lock<std::mutex> lock(m);
          cv.wait_for(lock, 500ms);
          return false; // keep waiting
        }
        ip_addr_t gateway{};
        ipaddr_aton(ip_string(lease_gw).c_str(), &gateway);
        if (!run_ping(gateway, "gateway")) {
          logger.warn("Gateway unreachable: discovery with LAN peers will likely fail");
        }
        ip_addr_t internet{};
        ipaddr_aton("8.8.8.8", &internet);
        run_ping(internet, "internet");
        if (!start_participant()) {
          logger.error("RTPS participant failed to start");
          return true; // the pools are sized at build time: a retry fails the same way
        }
        // 8 KB where a heartbeat needs 2: a rebind runs start_participant on it
        heartbeat_task = std::make_unique<espp::Task>(espp::Task::Config{
            .callback = heartbeat_tick,
            .task_config = {.name = "rtps_pub", .stack_size_bytes = 8 * 1024, .priority = 5}});
        heartbeat_task->start();
        return true; // one-shot
      },
      .task_config = {.name = "rtps_start", .stack_size_bytes = 8 * 1024, .priority = 5}});
  return startup_task->start();
}
