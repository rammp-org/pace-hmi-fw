#include "internet_ui.hpp"

// The InternetView's one instance and what it does through main: the network (rtps_comms), the
// worker thread the scan and the join run on, the hand-back to the LVGL task, the restart.

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "esp_pthread.h"
#include "esp_system.h"
#include "format.hpp"
#include "hmi_ui/internet_view.hpp"
#include "logger.hpp"
#include "lvgl.h"
#include "rtps_comms.hpp"

///////////////////////////////////////////////////////////////////////////////
// What main does for the view

namespace {

espp::Logger logger({.tag = "internet", .level = espp::Logger::Verbosity::INFO});

InternetUiConfig cfg;

// The view's state that is not trivially destructible stays here, at namespace
// scope in this file, so its construction and destruction stay where G4's
// baseline (tools/guards/baselines/init_order.*.json) has them.
std::string chosen_ssid;                        // the network the password page is for
std::vector<hmi::ui::WifiNetwork> scan_results; // what the rows stand for, in row order

constexpr size_t kWorkerStackBytes = 8 * 1024;

static_assert(static_cast<int>(NetLink::ETHERNET) == static_cast<int>(hmi::format::Link::ETHERNET));
static_assert(static_cast<int>(NetLink::WIFI) == static_cast<int>(hmi::format::Link::WIFI));
static_assert(static_cast<int>(WifiJoin::JOINED) ==
              static_cast<int>(hmi::ui::WifiJoinResult::JOINED));
static_assert(static_cast<int>(WifiJoin::WRONG_PASSWORD) ==
              static_cast<int>(hmi::ui::WifiJoinResult::WRONG_PASSWORD));
static_assert(static_cast<int>(WifiJoin::NOT_FOUND) ==
              static_cast<int>(hmi::ui::WifiJoinResult::NOT_FOUND));
static_assert(static_cast<int>(WifiJoin::FAILED) ==
              static_cast<int>(hmi::ui::WifiJoinResult::FAILED));

NetLink net_link(hmi::format::Link link) { return static_cast<NetLink>(link); }

std::optional<std::vector<hmi::ui::WifiNetwork>> scan() {
  std::optional<std::vector<WifiNetworkFound>> found = rtps_comms_wifi_scan();
  if (!found) {
    return std::nullopt;
  }
  std::vector<hmi::ui::WifiNetwork> networks;
  networks.reserve(found->size());
  for (WifiNetworkFound &n : *found) {
    networks.push_back({.ssid = std::move(n.ssid), .rssi = n.rssi, .secured = n.secured});
  }
  return networks;
}

// The worker the scan and the join run on: one thread each, detached, with
// the stack a scan needs.
void spawn(std::function<void()> job) {
  esp_pthread_cfg_t thread_cfg = esp_pthread_get_default_config();
  thread_cfg.stack_size = kWorkerStackBytes;
  thread_cfg.thread_name = "internet_ui";
  esp_pthread_set_cfg(&thread_cfg);
  std::thread(std::move(job)).detach();
}

// Back to the LVGL task. The worker takes the LVGL lock to queue it: a
// finding against CS-UI-02 (no other task takes the LVGL lock), kept as it is.
void post(void (*fn)(void *), void *data) {
  std::lock_guard<std::recursive_mutex> lock(*cfg.lvgl_mutex);
  lv_async_call(fn, data);
}

void restart(hmi::format::Link link) {
  logger.warn("Connection set to {}: restarting to use it",
              rtps_comms_net_link_name(net_link(link)));
  esp_restart();
}

// The one InternetView.
constinit hmi::ui::InternetView view{{
    .wifi_configured = rtps_comms_wifi_configured,
    .wifi_ssid = rtps_comms_wifi_ssid,
    .link = [] { return static_cast<hmi::format::Link>(rtps_comms_net_link()); },
    .link_name = [](hmi::format::Link link) { return rtps_comms_net_link_name(net_link(link)); },
    .link_state =
        [] {
          return static_cast<hmi::ui::LinkState>(static_cast<int32_t>(rtps_comms_link_state()));
        },
    .ip = rtps_comms_ip,
    .rssi = rtps_comms_wifi_rssi,
    .scan = scan,
    .join =
        [](const std::string &ssid, const std::string &password) {
          return static_cast<hmi::ui::WifiJoinResult>(rtps_comms_wifi_join(ssid, password));
        },
    .spawn = spawn,
    .post = post,
    .restart = restart,
    .chosen_ssid = &chosen_ssid,
    .scan_results = &scan_results,
}};

} // namespace

void internet_ui_init(const InternetUiConfig &config) {
  cfg = config;
  view.init({
      .connection = config.connection,
      .use_group = config.use_group,
      .focus_ring = config.focus_ring,
      .mirror_states = config.mirror_states,
      .claim_clicks = config.claim_clicks,
      .refuse = config.refuse,
  });
}

void internet_ui_on_load() { view.on_load(); }
