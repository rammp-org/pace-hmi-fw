#include "about_ui.hpp"

// The AboutView's one instance and what it reads, which is main's: the firmware hash
// (fw_info), the network (rtps_comms), the image's description and the MAC.

#include <array>
#include <cstdint>
#include <string>
#include <utility>

#include "esp_app_desc.h"
#include "esp_mac.h"
#include "fw_info.hpp"
#include "hmi_ui/about_view.hpp"
#include "rtps_comms.hpp"

#ifndef HMI_GIT_COMMIT
#define HMI_GIT_COMMIT "unknown"
#endif

namespace {

// rtps_comms' RtpsLinkState, read as the view's LinkState: the same values
// (static_asserted in main/frag_state.inc).
hmi::ui::LinkState link_state() {
  return static_cast<hmi::ui::LinkState>(static_cast<int32_t>(rtps_comms_link_state()));
}

hmi::ui::FirmwareInfo firmware() {
  FwInfo info = fw_info();
  hmi::ui::FirmwareInfo out{.done = info.done, .sha256 = std::move(info.sha256), .release = {}};
  if (info.release) {
    out.release = hmi::ui::FirmwareRelease{.tag = std::move(info.release->tag),
                                           .prerelease = info.release->prerelease};
  }
  return out;
}

hmi::ui::FirmwareIdentity identity() {
  const esp_app_desc_t *desc = esp_app_get_description();
  return {
      .version = desc->version, .date = desc->date, .time = desc->time, .commit = HMI_GIT_COMMIT};
}

// The one AboutView: everything it shows comes from these.
constinit hmi::ui::AboutView view{{
    .firmware = firmware,
    .identity = identity,
    .mac =
        [](std::array<uint8_t, 6> &mac) { return esp_efuse_mac_get_default(mac.data()) == ESP_OK; },
    .link_name = [] { return rtps_comms_net_link_name(rtps_comms_net_link()); },
    .link_state = link_state,
    .ip = rtps_comms_ip,
    .hostname = rtps_comms_hostname,
}};

} // namespace

void about_ui_init() { view.init(); }

void about_ui_on_load() { view.on_load(); }
