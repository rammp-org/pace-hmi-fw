#include "rtps_comms.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

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
#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"

#include "cdr.hpp"
#include "logger.hpp"
#include "rtps_participant.hpp"
#include "task.hpp"

using namespace std::chrono_literals;

namespace {

// W5500 on the M5-Bus SPI lines; CS/INT on the spare header GPIOs. INT is on
// GPIO4 (digital, routed via the GPIO matrix) so that GPIO52 — one of the few
// ADC2-capable pins on the headers — stays free for the twist pot (see
// main.cpp). GPIO18 (bus MOSI) was the twist channel before the W5500 took it.
constexpr spi_host_device_t kSpiHost = SPI2_HOST;
constexpr gpio_num_t kPinSck = GPIO_NUM_5;
constexpr gpio_num_t kPinMosi = GPIO_NUM_18;
constexpr gpio_num_t kPinMiso = GPIO_NUM_19;
constexpr gpio_num_t kPinCs = GPIO_NUM_45;
constexpr gpio_num_t kPinInt = GPIO_NUM_4;
// How the driver learns a frame arrived: 0 uses the INT line, anything else
// polls the chip over SPI every N ms and ignores INT. Set non-zero to rule out
// INT wiring — with a floating INT the driver never reads RX, so DHCP hangs
// while TX still works.
constexpr int kRxPollPeriodMs = 0;
// 20 MHz is comfortably within the W5500's 33 MHz limit and tolerant of
// jumper-wire runs to the module; raise once the wiring is proven.
constexpr int kSpiClockMhz = 20;

// RTPS settings. The domain is fixed at build time by the engine
// (RtpsParticipant::Config::DOMAIN_ID, default 0 = the ROS_DOMAIN_ID default)
// and is no longer settable per participant.
//
// Every topic and type name comes from rammp_rtps_spec.h so the MCB and the
// python test tools cannot drift from us. A writer only has send destinations
// once a remote reader on the same topic is discovered, so a name mismatch
// shows up as a "No send destinations" warning rather than an error.
//
// For a ROS 2 peer instead, use pre-mangled ROS 2 wire names — e.g. type
// "std_msgs::msg::dds_::UInt32_" — since the espp rtps component emits names
// verbatim.
//
// The counter/command pair still matches the espp python host harness
// (rtps_host.py --publish-topic/--subscribe-topic): it echoes every value
// back, giving a full publish->echo->receive round trip for bring-up.
constexpr std::string_view kNodeName = "rammp_hmi";
constexpr std::string_view kCounterTopic = RAMMP_TOPIC_HMI_COUNTER;
constexpr std::string_view kCmdTopic = RAMMP_TOPIC_HMI_COMMAND;
constexpr std::string_view kBrightnessTopic = RAMMP_TOPIC_HMI_BRIGHTNESS;
constexpr std::string_view kUInt32TypeName = RAMMP_TYPE_UINT32;
constexpr std::string_view kAdcTopic = RAMMP_TOPIC_JOYSTICK_ADC;
constexpr std::string_view kAdcTypeName = RAMMP_TYPE_ADC_XY_TWIST;
constexpr std::string_view kMcbStatusTopic = RAMMP_TOPIC_MCB_STATUS;
constexpr std::string_view kMcbStatusTypeName = RAMMP_TYPE_MCB_STATUS;

constexpr auto kPublishPeriod = 2s;

espp::Logger logger({.tag = "rtps_comms", .level = espp::Logger::Verbosity::INFO});

std::atomic<bool> got_ip{false};
// 1.2.0 dropped the discovered-endpoint accessors, so a matched-callback is the
// only signal that a peer exists: participant-wide, not per-topic. Both are
// wired because the facade's two callbacks are crossed relative to what its
// header documents — a remote reader matching our writer arrives on
// on_subscriber_matched, not on_publisher_matched (espp/rtps 1.2.0,
// rtps_participant.cpp:168-171 vs the Config doc comments).
std::atomic<bool> peer_matched{false};
std::string ip_address;
esp_netif_ip_info_t ip_info{}; // valid once got_ip is true (gateway used for the ping test)

std::unique_ptr<espp::RtpsParticipant> participant;
std::unique_ptr<espp::Task> publish_task;

// set by rtps_comms_on_brightness() / rtps_comms_on_mcb_status() before the
// participant starts; called from the RTPS receive task when a sample arrives
std::function<void(float)> brightness_handler;
std::function<void(const rammp_mcb_status_t &)> mcb_status_handler;

// UInt32 CDR helpers (little-endian CDR with the 4-byte encapsulation header,
// the on-the-wire format DDS expects for user data). espp/cdr derives the
// layout from the struct by reflection, so the wire shape lives in these
// definitions rather than in an explicit write sequence. xcdr1 is classic CDR,
// which is what ROS 2 / DDS peers expect.
struct UInt32Sample {
  uint32_t value;
};

std::vector<uint8_t> to_uint8(std::span<const std::byte> bytes) {
  const auto *begin = reinterpret_cast<const uint8_t *>(bytes.data());
  return {begin, begin + bytes.size()};
}

std::vector<uint8_t> serialize_uint32(uint32_t value) {
  auto bytes = cdr::serialize<cdr::xcdr1>(UInt32Sample{value});
  return bytes ? to_uint8(*bytes) : std::vector<uint8_t>{};
}

std::vector<uint8_t> serialize_adc(uint32_t x_mv, uint32_t y_mv, uint32_t twist_mv) {
  auto bytes = cdr::serialize<cdr::xcdr1>(rammp_adc_xy_twist_t{x_mv, y_mv, twist_mv});
  return bytes ? to_uint8(*bytes) : std::vector<uint8_t>{};
}

std::optional<rammp_mcb_status_t> deserialize_mcb_status(std::span<const uint8_t> cdr_payload) {
  auto sample = cdr::deserialize<rammp_mcb_status_t>(std::as_bytes(cdr_payload));
  if (!sample) {
    return std::nullopt;
  }
  return *sample;
}

std::optional<uint32_t> deserialize_uint32(std::span<const uint8_t> cdr_payload) {
  auto sample = cdr::deserialize<UInt32Sample>(std::as_bytes(cdr_payload));
  if (!sample) {
    return std::nullopt;
  }
  return sample->value;
}

void eth_event_handler(void *, esp_event_base_t, int32_t event_id, void *) {
  switch (event_id) {
  case ETHERNET_EVENT_CONNECTED:
    logger.info("Ethernet link up");
    break;
  case ETHERNET_EVENT_DISCONNECTED:
    logger.warn("Ethernet link down");
    break;
  case ETHERNET_EVENT_START:
    logger.info("Ethernet started");
    break;
  case ETHERNET_EVENT_STOP:
    logger.info("Ethernet stopped");
    break;
  default:
    break;
  }
}

void got_ip_event_handler(void *, esp_event_base_t, int32_t, void *event_data) {
  auto *event = static_cast<ip_event_got_ip_t *>(event_data);
  ip_info = event->ip_info;
  ip_address = fmt::format("{}.{}.{}.{}", IP2STR(&event->ip_info.ip));
  logger.info("Got IP: {} netmask {}.{}.{}.{} gateway {}.{}.{}.{}", ip_address,
              IP2STR(&event->ip_info.netmask), IP2STR(&event->ip_info.gw));
  got_ip = true;
}

// Fire a short ICMP ping session at `target` and log every reply/timeout.
// Returns true if at least one reply came back.
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
  callbacks.on_ping_success = [](esp_ping_handle_t hdl, void *args) {
    uint16_t seqno = 0;
    uint32_t elapsed_ms = 0;
    ip_addr_t addr{};
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_ms, sizeof(elapsed_ms));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &addr, sizeof(addr));
    logger.info("  ping reply from {}: seq={} time={} ms", ipaddr_ntoa(&addr), seqno, elapsed_ms);
    static_cast<PingStats *>(args)->received++;
  };
  callbacks.on_ping_timeout = [](esp_ping_handle_t hdl, void *) {
    uint16_t seqno = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    logger.warn("  ping timeout (seq={})", seqno);
  };
  callbacks.on_ping_end = [](esp_ping_handle_t, void *args) {
    static_cast<PingStats *>(args)->done = true;
  };

  logger.info("Pinging {} ({})...", label, ipaddr_ntoa(&target));
  esp_ping_handle_t ping = nullptr;
  esp_err_t err = esp_ping_new_session(&config, &callbacks, &ping);
  if (err != ESP_OK) {
    logger.warn("Failed to create ping session: {}", esp_err_to_name(err));
    return false;
  }
  esp_ping_start(ping);
  // count pings at 1 s interval + 1 s timeout slack
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(config.count + 2);
  while (!stats.done && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(100ms);
  }
  esp_ping_stop(ping);
  esp_ping_delete_session(ping);

  bool reachable = stats.received > 0;
  if (reachable) {
    logger.info("Ping {}: {}/{} replies", label, stats.received.load(), config.count);
  } else {
    logger.warn("Ping {}: no replies", label);
  }
  return reachable;
}

// W5500 over SPI -> esp_eth driver -> esp_netif with DHCP client
bool initialize_ethernet() {
  esp_err_t err = esp_netif_init();
  if (err != ESP_OK) {
    logger.error("esp_netif_init failed: {}", esp_err_to_name(err));
    return false;
  }
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    logger.error("esp_event_loop_create_default failed: {}", esp_err_to_name(err));
    return false;
  }

  // the W5500 driver signals RX via the INT line; it needs the GPIO ISR
  // service, which another driver may have installed already
  err = gpio_install_isr_service(0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    logger.error("gpio_install_isr_service failed: {}", esp_err_to_name(err));
    return false;
  }

  spi_bus_config_t bus_config = {};
  bus_config.mosi_io_num = kPinMosi;
  bus_config.miso_io_num = kPinMiso;
  bus_config.sclk_io_num = kPinSck;
  bus_config.quadwp_io_num = -1;
  bus_config.quadhd_io_num = -1;
  err = spi_bus_initialize(kSpiHost, &bus_config, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    logger.error("spi_bus_initialize failed: {}", esp_err_to_name(err));
    return false;
  }
  logger.info("SPI bus initialized (host {}, {} MHz)", static_cast<int>(kSpiHost), kSpiClockMhz);

  spi_device_interface_config_t dev_config = {};
  dev_config.command_bits = 16; // W5500 address phase
  dev_config.address_bits = 8;  // W5500 control phase
  dev_config.mode = 0;
  dev_config.clock_speed_hz = kSpiClockMhz * 1000 * 1000;
  dev_config.spics_io_num = kPinCs;
  dev_config.queue_size = 20;

  eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(kSpiHost, &dev_config);
  if (kRxPollPeriodMs > 0) {
    w5500_config.base.int_gpio_num = -1;
    w5500_config.base.poll_period_ms = kRxPollPeriodMs;
  } else {
    w5500_config.base.int_gpio_num = kPinInt;
  }

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
  // this probes the chip over SPI (version register), so it's the first point
  // that actually proves the wiring: a failure here almost always means
  // SPI wiring/power, a wrong CS pin, or a held reset
  err = esp_eth_driver_install(&eth_config, &eth_handle);
  if (err != ESP_OK) {
    logger.error("esp_eth_driver_install failed: {} — W5500 not responding on SPI "
                 "(check 3V3/GND, SCK/MOSI/MISO/CS wiring and that RSTn is not held low)",
                 esp_err_to_name(err));
    return false;
  }
  logger.info("W5500 detected, Ethernet driver installed");

  // the W5500 has no burned-in MAC; derive one from the chip's base MAC
  uint8_t mac_addr[6] = {};
  esp_read_mac(mac_addr, ESP_MAC_ETH);
  esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr);
  logger.info("MAC address: {:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}", mac_addr[0], mac_addr[1],
              mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

  esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
  esp_netif_t *eth_netif = esp_netif_new(&netif_config);
  if (!eth_netif) {
    logger.error("esp_netif_new failed");
    return false;
  }
  err = esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle));
  if (err != ESP_OK) {
    logger.error("esp_netif_attach failed: {}", esp_err_to_name(err));
    return false;
  }

  esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, nullptr);

  err = esp_eth_start(eth_handle);
  if (err != ESP_OK) {
    logger.error("esp_eth_start failed: {}", esp_err_to_name(err));
    return false;
  }
  logger.info("Ethernet started, waiting for link + DHCP...");
  return true;
}

bool start_participant() {
  logger.info("Creating RTPS participant '{}' (interface {})", kNodeName, ip_address);
  // lwIP allocates TX pbufs from internal DRAM; if this is low, sends to a
  // not-yet-ARP-resolved peer fail with ENOMEM
  logger.info("Internal heap: {} free, {} largest block",
              heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
              heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  participant = std::make_unique<espp::RtpsParticipant>(espp::RtpsParticipant::Config{
      .interface_address = ip_address,
      .on_publisher_matched =
          [] {
            if (!peer_matched.exchange(true)) {
              logger.info("Endpoint matched (publisher callback)");
            }
          },
      .on_subscriber_matched =
          [] {
            if (!peer_matched.exchange(true)) {
              logger.info("Endpoint matched (subscriber callback)");
            }
          },
      // raise to DEBUG to trace a failed discovery exchange: that surfaces the
      // "SPDP parsed"/"SEDP parsed" lines plus every send, showing what we
      // transmit and what actually reaches us
      .log_level = espp::Logger::Verbosity::INFO,
  });

  // endpoints can only be added once the transport is up
  if (!participant->start()) {
    logger.error("Failed to start RTPS participant");
    participant.reset();
    return false;
  }

  if (!participant->add_writer({
          .topic = std::string(kCounterTopic),
          .type_name = std::string(kUInt32TypeName),
          .reliability = espp::RtpsParticipant::Reliability::BEST_EFFORT,
      })) {
    logger.error("Failed to add writer for '{}'", kCounterTopic);
    return false;
  }
  logger.info("Added writer '{}' [{}]", kCounterTopic, kUInt32TypeName);
  if (!participant->add_writer({
          .topic = std::string(kAdcTopic),
          .type_name = std::string(kAdcTypeName),
          .reliability = espp::RtpsParticipant::Reliability::BEST_EFFORT,
      })) {
    logger.error("Failed to add writer for '{}'", kAdcTopic);
    return false;
  }
  logger.info("Added writer '{}' [{}]", kAdcTopic, kAdcTypeName);
  if (!participant->add_reader({
          .topic = std::string(kCmdTopic),
          .type_name = std::string(kUInt32TypeName),
          .reliability = espp::RtpsParticipant::Reliability::BEST_EFFORT,
          .on_sample =
              [](std::span<const uint8_t> cdr) {
                auto value = deserialize_uint32(cdr);
                if (!value) {
                  logger.warn("Received sample on '{}' that failed CDR decode", kCmdTopic);
                  return;
                }
                logger.info("Received echo/cmd on '{}': {}", kCmdTopic, *value);
              },
      })) {
    logger.error("Failed to add reader for '{}'", kCmdTopic);
    return false;
  }
  logger.info("Added reader '{}' [{}]", kCmdTopic, kUInt32TypeName);

  if (!participant->add_reader({
          .topic = std::string(kBrightnessTopic),
          .type_name = std::string(kUInt32TypeName),
          .reliability = espp::RtpsParticipant::Reliability::BEST_EFFORT,
          .on_sample =
              [](std::span<const uint8_t> cdr) {
                auto value = deserialize_uint32(cdr);
                if (!value) {
                  logger.warn("Received sample on '{}' that failed CDR decode", kBrightnessTopic);
                  return;
                }
                float percent = std::min<float>(static_cast<float>(*value), 100.0f);
                logger.info("Received brightness command: {} -> {:.0f}%", *value, percent);
                if (brightness_handler) {
                  brightness_handler(percent);
                } else {
                  logger.warn("No brightness handler registered; command ignored");
                }
              },
      })) {
    logger.error("Failed to add reader for '{}'", kBrightnessTopic);
    return false;
  }
  logger.info("Added reader '{}' [{}]", kBrightnessTopic, kUInt32TypeName);

  // The MCB's status broadcast. This is the one topic the HMI is a slave to:
  // the drive-status and state labels show whatever arrives here, so a decode
  // failure is left visible in the log rather than silently displaying a
  // stale or zeroed state.
  if (!participant->add_reader({
          .topic = std::string(kMcbStatusTopic),
          .type_name = std::string(kMcbStatusTypeName),
          .reliability = espp::RtpsParticipant::Reliability::BEST_EFFORT,
          .on_sample =
              [](std::span<const uint8_t> cdr) {
                auto status = deserialize_mcb_status(cdr);
                if (!status) {
                  logger.warn("Received sample on '{}' that failed CDR decode", kMcbStatusTopic);
                  return;
                }
                // the MCB republishes on a period, so log only what changes —
                // otherwise this floods at the status rate
                static std::optional<rammp_mcb_status_t> last;
                if (!last || last->drive_status != status->drive_status ||
                    last->system_state != status->system_state || last->flags != status->flags) {
                  logger.info("MCB status: drive={} state={} flags=0x{:02x} (seq {})",
                              rammp_drive_status_name(status->drive_status),
                              rammp_state_name(status->system_state), status->flags, status->seq);
                  last = status;
                }
                if (mcb_status_handler) {
                  mcb_status_handler(*status);
                } else {
                  logger.warn("No MCB status handler registered; sample ignored");
                }
              },
      })) {
    logger.error("Failed to add reader for '{}'", kMcbStatusTopic);
    return false;
  }
  logger.info("Added reader '{}' [{}]", kMcbStatusTopic, kMcbStatusTypeName);

  logger.info("RTPS participant '{}' up on {} (domain fixed at build time)", kNodeName, ip_address);
  logger.info("Publishing '{}' every {} s, listening on '{}'", kCounterTopic,
              std::chrono::duration_cast<std::chrono::seconds>(kPublishPeriod).count(), kCmdTopic);

  publish_task = std::make_unique<espp::Task>(espp::Task::Config{
      .callback = [](std::mutex &m, std::condition_variable &cv) -> bool {
        static uint32_t counter = 0;
        // a best-effort writer has no send destinations until a
        // remote reader on the topic is discovered — hold off
        // instead of warning every period
        if (!peer_matched) {
          static uint32_t skips = 0;
          if (skips++ % 10 == 0) {
            logger.info("No subscriber for '{}' yet; not publishing", kCounterTopic);
          }
          std::unique_lock<std::mutex> lock(m);
          cv.wait_for(lock, kPublishPeriod);
          return false; // keep running
        }
        counter++;
        if (participant->publish(kCounterTopic, serialize_uint32(counter))) {
          // heartbeat log on the first and every 10th sample so a
          // working publish loop is visible without log spam
          if (counter % 10 == 1) {
            logger.info("Published counter {} on '{}'", counter, kCounterTopic);
          } else {
            logger.debug("Published counter {}", counter);
          }
        } else {
          logger.warn("Failed to publish counter {}", counter);
        }
        // interruptible sleep so task teardown isn't blocked
        std::unique_lock<std::mutex> lock(m);
        cv.wait_for(lock, kPublishPeriod);
        return false; // keep running
      },
      .task_config = {
          .name = "rtps_pub",
          .stack_size_bytes = 6 * 1024,
          .priority = 5,
      }});
  return publish_task->start();
}

} // namespace

void rtps_comms_on_brightness(std::function<void(float)> handler) {
  brightness_handler = std::move(handler);
}

void rtps_comms_on_mcb_status(std::function<void(const rammp_mcb_status_t &)> handler) {
  mcb_status_handler = std::move(handler);
}

bool rtps_comms_publish_adc(uint32_t x_mv, uint32_t y_mv, uint32_t twist_mv) {
  // called from the ADC task at 30 Hz; quiet no-op until RTPS is up and
  // someone subscribes, so a missing cable or absent plot script costs
  // nothing and logs nothing
  if (!participant || !participant->is_started()) {
    return false;
  }
  if (!peer_matched) {
    return false;
  }
  // publish() is internally mutex-guarded, safe alongside the counter task
  return participant->publish(kAdcTopic, serialize_adc(x_mv, y_mv, twist_mv));
}

bool rtps_comms_start() {
  logger.info("Bringing up W5500 Ethernet (SCK={}, MOSI={}, MISO={}, CS={})",
              static_cast<int>(kPinSck), static_cast<int>(kPinMosi), static_cast<int>(kPinMiso),
              static_cast<int>(kPinCs));
  if (kRxPollPeriodMs > 0) {
    logger.info("RX serviced by polling every {} ms (INT line unused)", kRxPollPeriodMs);
  } else {
    logger.info("RX serviced by INT on GPIO{}", static_cast<int>(kPinInt));
  }
  if (!initialize_ethernet()) {
    return false;
  }

  // DHCP timing varies per network (one router here took ~21 s after link
  // up), so don't block app_main or give up on a fixed deadline: a one-shot
  // background task waits for the got-IP event — however long it takes — then
  // runs the ping smoke tests and starts the participant. This also covers
  // plugging in the cable minutes after boot.
  static auto startup_task = std::make_unique<espp::Task>(espp::Task::Config{
      .callback = [](std::mutex &m, std::condition_variable &cv) -> bool {
        if (!got_ip) {
          static int waited_ms = 0;
          waited_ms += 500;
          if (waited_ms % 15000 == 0) {
            logger.info("Still waiting for an IP address (link/DHCP)... {} s elapsed",
                        waited_ms / 1000);
          }
          std::unique_lock<std::mutex> lock(m);
          cv.wait_for(lock, 500ms);
          return false; // not yet — keep waiting
        }

        // connectivity smoke tests (non-fatal): the gateway proves the
        // local link works — that's what RTPS needs; 8.8.8.8 additionally
        // proves the route to the internet, pinged by IP so no DNS is
        // involved
        ip_addr_t gateway{};
        ipaddr_aton(fmt::format("{}.{}.{}.{}", IP2STR(&ip_info.gw)).c_str(), &gateway);
        bool gateway_ok = run_ping(gateway, "gateway");
        ip_addr_t google_dns{};
        ipaddr_aton("8.8.8.8", &google_dns);
        run_ping(google_dns, "internet (Google DNS)");
        if (!gateway_ok) {
          logger.warn("Gateway unreachable — RTPS discovery with LAN peers will likely fail; "
                      "starting the participant anyway");
        }

        if (!start_participant()) {
          logger.error("RTPS participant failed to start");
        }
        return true; // one-shot: stop this task
      },
      .task_config = {
          .name = "rtps_start",
          .stack_size_bytes = 8 * 1024,
          .priority = 5,
      }});
  logger.info("Ethernet up; RTPS will start automatically once an IP address is assigned");
  return startup_task->start();
}
