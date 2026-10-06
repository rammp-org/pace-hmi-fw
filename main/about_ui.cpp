#include "about_ui.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "esp_app_desc.h"
#include "esp_mac.h"
#include "format.hpp"
#include "fw_info.hpp"
#include "hmi_format/about.hpp"
#include "lvgl.h"
#include "rtps_comms.hpp"
#include "ui.h"
#include "ui_themes.h"

#ifndef HMI_GIT_COMMIT
#define HMI_GIT_COMMIT "unknown"
#endif

namespace {

constexpr uint32_t kRefreshMs = 1000;

// What the mark shows. Kept so the themeable colour is only set on a change:
// each call registers the label with the theme again.
enum class Mark { NONE, RELEASE, NOT_RELEASE };
Mark shown_mark = Mark::NONE;

void set_mark(Mark mark) {
  if (mark == shown_mark) {
    return;
  }
  shown_mark = mark;
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

void show_verdict(const FwInfo &info) {
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
                      fmt::format("{} {}", info.release->prerelease ? "Pre-release" : "Release",
                                  info.release->tag)
                          .c_str());
    return;
  }
  set_mark(Mark::NOT_RELEASE);
  // A build that calls itself a release but has no matching line was either
  // never checked against GitHub (flashed without fw_verify.py) or is not the
  // published binary; one that does not is simply not a release.
  lv_label_set_text(ui_AboutVerdict, hmi::format::names_a_tag(esp_app_get_description()->version)
                                         ? "Not verified against GitHub"
                                         : "Not a published release");
}

void show_sha(const FwInfo &info) {
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
    std::array<char, hmi::format::SHA_LINE_TEXT_SIZE> line{};
    hmi::format::sha_line_text(hex, from, line);
    lv_label_set_text(label, line.data());
  }
}

const char *link_words(RtpsLinkState state) {
  switch (state) {
  case RtpsLinkState::NET_FAILED:
    return "hardware not responding";
  case RtpsLinkState::LINK_DOWN:
    return "not connected";
  case RtpsLinkState::NO_IP:
    return "waiting for an address";
  case RtpsLinkState::NO_PEER:
  case RtpsLinkState::CONNECTED:
    return "online";
  }
  return "--";
}

void refresh() {
  const FwInfo info = fw_info();
  show_verdict(info);
  show_sha(info);
  lv_label_set_text(ui_AboutLinkValue,
                    fmt::format("{}, {}", rtps_comms_net_link_name(rtps_comms_net_link()),
                                link_words(rtps_comms_link_state()))
                        .c_str());
  const std::string ip = rtps_comms_ip();
  lv_label_set_text(ui_AboutIpValue, ip.empty() ? "--" : ip.c_str());
}

void fill_static() {
  const esp_app_desc_t *desc = esp_app_get_description();
  lv_label_set_text(ui_AboutVersionValue, desc->version);
  lv_label_set_text(ui_AboutCommitValue, HMI_GIT_COMMIT);
  lv_label_set_text(ui_AboutBuiltValue, fmt::format("{} {}", desc->date, desc->time).c_str());
  // The base MAC: the USB serial number the PC sees, so a board on the bench
  // and a port in Device Manager can be matched up.
  std::array<uint8_t, 6> mac{};
  if (esp_efuse_mac_get_default(mac.data()) == ESP_OK) {
    std::array<char, hmi::format::MAC_TEXT_SIZE> text{};
    hmi::format::mac_text(mac, text);
    lv_label_set_text(ui_AboutDeviceValue, text.data());
  }
  lv_label_set_text(ui_AboutHostValue, rtps_comms_hostname());
}

} // namespace

void about_ui_init() {
  fill_static();
  lv_timer_create(
      [](lv_timer_t *) {
        if (lv_screen_active() == ui_AboutScreen) {
          refresh();
        }
      },
      kRefreshMs, nullptr);
}

void about_ui_on_load() { refresh(); }
