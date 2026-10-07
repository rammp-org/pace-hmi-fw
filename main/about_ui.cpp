#include "about_ui.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "esp_app_desc.h"
#include "esp_mac.h"
#include "fw_info.hpp"
#include "hmi_format/about.hpp"
#include "hmi_ui/about_view.hpp"
#include "lvgl.h"
#include "rtps_comms.hpp"
#include "ui.h"
#include "ui_themes.h"

#ifndef HMI_GIT_COMMIT
#define HMI_GIT_COMMIT "unknown"
#endif

void hmi::ui::AboutView::set_mark(Mark mark) {
  if (mark == shown_mark_) {
    return;
  }
  shown_mark_ = mark;
  const auto main = static_cast<lv_style_selector_t>(LV_PART_MAIN) |
                    static_cast<lv_style_selector_t>(LV_STATE_DEFAULT);
  if (mark == Mark::RELEASE) {
    lv_label_set_text(ui_AboutMark, LV_SYMBOL_OK);
    ui_object_set_themeable_style_property(ui_AboutMark, main, LV_STYLE_TEXT_COLOR,
                                           _ui_theme_color_ok);
    ui_object_set_themeable_style_property(ui_AboutMark, main, LV_STYLE_TEXT_OPA,
                                           _ui_theme_alpha_ok);
  } else if (mark == Mark::NOT_RELEASE) {
    lv_label_set_text(ui_AboutMark, LV_SYMBOL_CLOSE);
    ui_object_set_themeable_style_property(ui_AboutMark, main, LV_STYLE_TEXT_COLOR,
                                           _ui_theme_color_alert);
    ui_object_set_themeable_style_property(ui_AboutMark, main, LV_STYLE_TEXT_OPA,
                                           _ui_theme_alpha_alert);
  } else {
    lv_label_set_text(ui_AboutMark, "");
  }
}

void hmi::ui::AboutView::show_verdict(const FirmwareInfo &info) {
  if (!info.done) {
    set_mark(Mark::NONE);
    lv_label_set_text(ui_AboutVerdict, "Checking...");
    return;
  }
  if (!info.sha256) {
    set_mark(Mark::NOT_RELEASE);
    lv_label_set_text(ui_AboutVerdict, "Could not read the firmware");
    return;
  }
  if (info.release) {
    set_mark(Mark::RELEASE);
    lv_label_set_text(ui_AboutVerdict,
                      (std::string(info.release->prerelease ? "Pre-release" : "Release") + " " +
                       info.release->tag)
                          .c_str());
    return;
  }
  set_mark(Mark::NOT_RELEASE);
  // A build that calls itself a release but has no matching line was either
  // never checked against GitHub (flashed without fw_verify.py) or is not the
  // published binary; one that does not is simply not a release.
  lv_label_set_text(ui_AboutVerdict, format::names_a_tag(config_.identity().version)
                                         ? "Not verified against GitHub"
                                         : "Not a published release");
}

void hmi::ui::AboutView::show_sha(const FirmwareInfo &info) {
  if (!info.sha256) {
    lv_label_set_text(ui_AboutSha1, info.done ? "--" : "Computing...");
    lv_label_set_text(ui_AboutSha2, "");
    return;
  }
  // Four groups of eight a line, the way it is easiest to read off against a
  // release page.
  const std::string &hex = *info.sha256;
  for (auto [label, from] :
       {std::pair{ui_AboutSha1, size_t{0}}, std::pair{ui_AboutSha2, size_t{32}}}) {
    std::array<char, format::SHA_LINE_TEXT_SIZE> line{};
    format::sha_line_text(hex, from, line);
    lv_label_set_text(label, line.data());
  }
}

const char *hmi::ui::AboutView::link_words(LinkState state) {
  switch (state) {
  case LinkState::NET_FAILED:
    return "hardware not responding";
  case LinkState::LINK_DOWN:
    return "not connected";
  case LinkState::NO_IP:
    return "waiting for an address";
  case LinkState::NO_PEER:
  case LinkState::CONNECTED:
    return "online";
  }
  return "--";
}

void hmi::ui::AboutView::refresh() {
  const FirmwareInfo info = config_.firmware();
  show_verdict(info);
  show_sha(info);
  lv_label_set_text(
      ui_AboutLinkValue,
      (std::string(config_.link_name()) + ", " + link_words(config_.link_state())).c_str());
  const std::string ip = config_.ip();
  lv_label_set_text(ui_AboutIpValue, ip.empty() ? "--" : ip.c_str());
}

void hmi::ui::AboutView::fill_static() {
  const FirmwareIdentity id = config_.identity();
  lv_label_set_text(ui_AboutVersionValue, id.version);
  lv_label_set_text(ui_AboutCommitValue, id.commit);
  lv_label_set_text(ui_AboutBuiltValue, (std::string(id.date) + " " + id.time).c_str());
  // The base MAC: the USB serial number the PC sees, so a board on the bench
  // and a port in Device Manager can be matched up.
  std::array<uint8_t, 6> mac{};
  if (config_.mac(mac)) {
    std::array<char, format::MAC_TEXT_SIZE> text{};
    format::mac_text(mac, text);
    lv_label_set_text(ui_AboutDeviceValue, text.data());
  }
  lv_label_set_text(ui_AboutHostValue, config_.hostname());
}

void hmi::ui::AboutView::refresh_cb(lv_timer_t *timer) {
  if (lv_screen_active() == ui_AboutScreen) {
    static_cast<AboutView *>(lv_timer_get_user_data(timer))->refresh();
  }
}

void hmi::ui::AboutView::init() {
  fill_static();
  lv_timer_create(refresh_cb, REFRESH_MS, this);
}

void hmi::ui::AboutView::on_load() { refresh(); }

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
