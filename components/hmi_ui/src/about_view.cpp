// AboutView: the AboutScreen (moved from main/about_ui.cpp). What it shows comes from main.

#include "hmi_ui/about_view.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <utility>

#include "hmi_format/about.hpp"
#include "hmi_rtps_spec.hpp"
#include "ui.h"

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
  add_last_reset_row();
}

// "Last reset: <name>" (hazard-c3-spec.md §2.9, REQ-UI-21): a row in code below Hostname, in
// the export's own row layout (key at x 30, value at x 250, rows 50 px apart) and styles.
void hmi::ui::AboutView::add_last_reset_row() {
  constexpr int32_t ROW_STEP = 50;
  const int32_t y = lv_obj_get_y(ui_AboutHostKey) + ROW_STEP;
  const auto main = static_cast<lv_style_selector_t>(LV_PART_MAIN) |
                    static_cast<lv_style_selector_t>(LV_STATE_DEFAULT);
  lv_obj_t *key = lv_label_create(ui_AboutContent);
  lv_obj_set_pos(key, lv_obj_get_x(ui_AboutHostKey), y);
  lv_label_set_text(key, rammp::kPostLastResetKey);
  ui_object_set_themeable_style_property(key, main, LV_STYLE_TEXT_COLOR,
                                         _ui_theme_color_text_muted);
  ui_object_set_themeable_style_property(key, main, LV_STYLE_TEXT_OPA, _ui_theme_alpha_text_muted);
  lv_obj_set_style_text_font(key, lv_obj_get_style_text_font(ui_AboutHostKey, LV_PART_MAIN), main);
  lv_obj_t *value = lv_label_create(ui_AboutContent);
  lv_obj_set_pos(value, lv_obj_get_x(ui_AboutHostValue), y);
  lv_obj_set_width(value, lv_obj_get_width(ui_AboutHostValue));
  lv_label_set_text(value, config_.last_reset());
  ui_object_set_themeable_style_property(value, main, LV_STYLE_TEXT_COLOR, _ui_theme_color_text);
  ui_object_set_themeable_style_property(value, main, LV_STYLE_TEXT_OPA, _ui_theme_alpha_text);
  lv_obj_set_style_text_font(value, lv_obj_get_style_text_font(ui_AboutHostValue, LV_PART_MAIN),
                             main);
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
