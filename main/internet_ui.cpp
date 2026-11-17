#include "internet_ui.hpp"

#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "components/ui_comp_netrow.h"
#include "esp_pthread.h"
#include "esp_system.h"
#include "format.hpp"
#include "logger.hpp"
#include "rtps_comms.hpp"
#include "ui.h"
#include "ui_themes.h"

namespace {

espp::Logger logger({.tag = "internet", .level = espp::Logger::Verbosity::INFO});

InternetUiConfig cfg;

enum class Page { MAIN, NETWORKS, PASSWORD };
Page page = Page::MAIN;

lv_group_t *main_group = nullptr;
lv_group_t *networks_group = nullptr;
lv_group_t *password_group = nullptr;

// Made for the length of a visit to their page.
lv_obj_t *password_text = nullptr;
lv_obj_t *keyboard = nullptr;
std::string chosen_ssid;                    // the network the password page is for
std::vector<WifiNetworkFound> scan_results; // what the rows stand for, in row order

// A scan or a join is running on the worker. One at a time, and a result that
// comes back after its page was left (`generation` moved on) is dropped.
bool busy = false;
uint32_t generation = 0;

constexpr uint32_t kStatusPollMs = 500;
constexpr size_t kWorkerStackBytes = 8 * 1024;

constexpr lv_style_selector_t sel(lv_part_t part, lv_state_t state) {
  return static_cast<lv_style_selector_t>(part) | static_cast<lv_style_selector_t>(state);
}

///////////////////////////////////////////////////////////////////////////////
// The worker: one blocking call off the LVGL task, its result posted back

template <class Result> struct Posted {
  uint32_t generation;
  Result result;
  void (*deliver)(Result &);
};

// `work` runs on its own thread; `deliver` then runs on the LVGL task with what
// it returned, unless the page was left meanwhile.
template <class Result, class Work> void run_on_worker(Work work, void (*deliver)(Result &)) {
  busy = true;
  const uint32_t started = generation;
  esp_pthread_cfg_t thread_cfg = esp_pthread_get_default_config();
  thread_cfg.stack_size = kWorkerStackBytes;
  thread_cfg.thread_name = "internet_ui";
  esp_pthread_set_cfg(&thread_cfg);
  std::thread([work = std::move(work), deliver, started]() mutable {
    auto *posted = new Posted<Result>{started, work(), deliver};
    std::lock_guard<std::recursive_mutex> lock(*cfg.lvgl_mutex);
    lv_async_call(
        [](void *data) {
          std::unique_ptr<Posted<Result>> p(static_cast<Posted<Result> *>(data));
          busy = false;
          if (p->generation == generation) {
            p->deliver(p->result);
          }
        },
        posted);
  }).detach();
}

///////////////////////////////////////////////////////////////////////////////
// Main page

NetLink chosen_link() { return static_cast<NetLink>(lv_subject_get_int(cfg.connection)); }

// The link a restart would bring up: WiFi needs a network to join.
NetLink link_after_restart() {
  return chosen_link() == NetLink::WIFI && rtps_comms_wifi_configured() ? NetLink::WIFI
                                                                        : NetLink::ETHERNET;
}

const char *state_text(RtpsLinkState state, NetLink link) {
  switch (state) {
  case RtpsLinkState::NET_FAILED:
    return link == NetLink::WIFI ? "WiFi hardware not responding" : "Ethernet hardware missing";
  case RtpsLinkState::LINK_DOWN:
    return link == NetLink::WIFI ? "Not connected" : "Cable unplugged";
  case RtpsLinkState::NO_IP:
    return "Connected, waiting for an address";
  case RtpsLinkState::NO_PEER:
    return "Online, no MCB";
  case RtpsLinkState::CONNECTED:
    return "Online, MCB connected";
  }
  return "--";
}

void main_refresh() {
  const NetLink chosen = chosen_link();
  lv_obj_set_state(ui_NetChoiceEthernet, LV_STATE_CHECKED, chosen == NetLink::ETHERNET);
  lv_obj_set_state(ui_NetChoiceWifi, LV_STATE_CHECKED, chosen == NetLink::WIFI);

  const std::string ssid = rtps_comms_wifi_ssid();
  lv_label_set_text(ui_NetWifiName, ssid.empty() ? "Not set" : ssid.c_str());

  const NetLink link = rtps_comms_net_link();
  lv_label_set_text(ui_NetUsingValue, rtps_comms_net_link_name(link));
  lv_label_set_text(ui_NetStateValue, state_text(rtps_comms_link_state(), link));
  const std::string ip = rtps_comms_ip();
  lv_label_set_text(ui_NetIpValue, ip.empty() ? "--" : ip.c_str());
  const std::optional<int> rssi = rtps_comms_wifi_rssi();
  lv_label_set_text(ui_NetSignalValue, rssi ? fmt::format("{} dBm", *rssi).c_str() : "--");

  // Offered only when a restart would change the link: WiFi chosen with no
  // network to join would come up on Ethernet again, so there is nothing to apply.
  const bool pending = link_after_restart() != link;
  const bool shown = !lv_obj_has_flag(ui_NetRestartButton, LV_OBJ_FLAG_HIDDEN);
  if (pending != shown) {
    lv_obj_set_flag(ui_NetRestartButton, LV_OBJ_FLAG_HIDDEN, !pending);
    if (!pending && lv_group_get_focused(main_group) == ui_NetRestartButton) {
      lv_group_focus_obj(ui_NetWifiButton); // the cursor must not stay on a hidden button
    }
  }
}

void show_page(Page next) {
  generation++; // whatever the worker is still doing belongs to the page being left
  page = next;
  lv_obj_set_flag(ui_NetPickPanel, LV_OBJ_FLAG_HIDDEN, next != Page::NETWORKS);
  lv_obj_set_flag(ui_NetPwPanel, LV_OBJ_FLAG_HIDDEN, next != Page::PASSWORD);
}

void show_main(lv_obj_t *focus) {
  show_page(Page::MAIN);
  main_refresh();
  cfg.use_group(main_group);
  lv_group_focus_obj(focus);
}

void choice_cb(lv_event_t *e) {
  const auto link = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  lv_subject_set_int(cfg.connection, link); // saved by the subject's observer
  main_refresh();
}

void restart_cb(lv_event_t *) {
  logger.warn("Connection set to {}: restarting to use it",
              rtps_comms_net_link_name(link_after_restart()));
  esp_restart();
}

// The stick on the main page: two choices side by side, then the network, then
// (when it is shown) the restart button. Down past the last one is the burger
// key, which the group holds after them.
void main_key_cb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target_obj(e);
  const bool restart_shown = !lv_obj_has_flag(ui_NetRestartButton, LV_OBJ_FLAG_HIDDEN);
  switch (lv_event_get_key(e)) {
  case LV_KEY_LEFT:
    if (obj == ui_NetChoiceWifi) {
      lv_group_focus_obj(ui_NetChoiceEthernet);
    }
    break;
  case LV_KEY_RIGHT:
    if (obj == ui_NetChoiceEthernet) {
      lv_group_focus_obj(ui_NetChoiceWifi);
    }
    break;
  case LV_KEY_UP:
    if (obj == ui_NetWifiButton) {
      lv_group_focus_obj(chosen_link() == NetLink::WIFI ? ui_NetChoiceWifi : ui_NetChoiceEthernet);
    } else if (obj == ui_NetRestartButton) {
      lv_group_focus_obj(ui_NetWifiButton);
    }
    break;
  case LV_KEY_DOWN:
    if (obj == ui_NetChoiceEthernet || obj == ui_NetChoiceWifi) {
      lv_group_focus_obj(ui_NetWifiButton);
    } else if (obj == ui_NetWifiButton && restart_shown) {
      lv_group_focus_obj(ui_NetRestartButton);
    } else {
      // the burger key: the group's last member, after a restart button that
      // is skipped while it is hidden
      lv_group_focus_obj(
          lv_group_get_obj_by_index(main_group, lv_group_get_obj_count(main_group) - 1));
    }
    break;
  default:
    break;
  }
}

///////////////////////////////////////////////////////////////////////////////
// Networks page

void show_password(const std::string &ssid);
void join(const std::string &ssid, const std::string &password);

void networks_clear() {
  // Backwards, and by index: deleting a row renumbers the ones after it.
  for (int32_t i = static_cast<int32_t>(lv_obj_get_child_count(ui_NetList)) - 1; i >= 0; i--) {
    lv_obj_delete(lv_obj_get_child(ui_NetList, i));
  }
  scan_results.clear();
}

void networks_back() {
  networks_clear();
  show_main(ui_NetWifiButton);
}

void network_row_click_cb(lv_event_t *e) {
  if (busy) {
    return;
  }
  const auto index = static_cast<size_t>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (index >= scan_results.size()) {
    return;
  }
  const WifiNetworkFound network = scan_results[index]; // a copy: the rows are about to go
  if (network.secured) {
    show_password(network.ssid);
  } else {
    lv_label_set_text_fmt(ui_NetPickStatus, "Connecting to %s...", network.ssid.c_str());
    join(network.ssid, "");
  }
}

// Up and down walk the list; up from the first row is the back button, down
// from the last is the burger key. Left is "back", as on the seat page.
void networks_key_cb(lv_event_t *e) {
  switch (lv_event_get_key(e)) {
  case LV_KEY_UP:
    lv_group_focus_prev(networks_group);
    break;
  case LV_KEY_DOWN:
    lv_group_focus_next(networks_group);
    break;
  case LV_KEY_LEFT:
    networks_back();
    break;
  default:
    break;
  }
}

void networks_fill(std::optional<std::vector<WifiNetworkFound>> &found) {
  if (page != Page::NETWORKS) {
    return;
  }
  if (!found) {
    lv_label_set_text(ui_NetPickStatus, "WiFi is not responding.");
    cfg.refuse();
    return;
  }
  scan_results = std::move(*found);
  lv_label_set_text(ui_NetPickStatus,
                    scan_results.empty() ? "No networks found." : "Pick a network.");
  for (size_t i = 0; i < scan_results.size(); i++) {
    const WifiNetworkFound &network = scan_results[i];
    lv_obj_t *row = ui_NetRow_create(ui_NetList);
    lv_label_set_text(ui_comp_get_child(row, UI_COMP_NETROW_NETROWGROUND_NETROWNAME),
                      network.ssid.c_str());
    lv_label_set_text(
        ui_comp_get_child(row, UI_COMP_NETROW_NETROWGROUND_NETROWSIGNAL),
        fmt::format("{}{} dBm", network.secured ? "" : "open  ", network.rssi).c_str());
    cfg.claim_clicks(row);
    cfg.mirror_states(row);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(row, network_row_click_cb, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_add_event_cb(row, networks_key_cb, LV_EVENT_KEY, nullptr);
    lv_group_add_obj(networks_group, row);
  }
  cfg.use_group(networks_group); // puts the burger key back after the rows
  if (!scan_results.empty()) {
    lv_group_focus_obj(lv_obj_get_child(ui_NetList, 0));
  }
}

void show_networks() {
  show_page(Page::NETWORKS);
  networks_clear();
  lv_label_set_text(ui_NetPickStatus, "Scanning...");
  cfg.use_group(networks_group);
  lv_group_focus_obj(ui_NetPickBack);
  run_on_worker<std::optional<std::vector<WifiNetworkFound>>>([] { return rtps_comms_wifi_scan(); },
                                                              networks_fill);
}

///////////////////////////////////////////////////////////////////////////////
// Password page

void password_close() {
  if (keyboard != nullptr) {
    lv_obj_delete(keyboard);
    keyboard = nullptr;
  }
  if (password_text != nullptr) {
    lv_obj_delete(password_text);
    password_text = nullptr;
  }
}

void password_back() {
  password_close();
  show_networks();
}

void join_done(WifiJoin &result) {
  if (result == WifiJoin::JOINED) {
    password_close();
    networks_clear();
    show_main(ui_NetWifiButton);
    return;
  }
  const char *why = result == WifiJoin::WRONG_PASSWORD ? "Wrong password. Try again."
                    : result == WifiJoin::NOT_FOUND    ? "Network not found."
                                                       : "Could not connect.";
  cfg.refuse();
  if (page == Page::PASSWORD) {
    lv_label_set_text(ui_NetPwStatus, why);
    lv_obj_remove_state(keyboard, LV_STATE_DISABLED);
  } else {
    lv_label_set_text(ui_NetPickStatus, why);
  }
}

void join(const std::string &ssid, const std::string &password) {
  run_on_worker<WifiJoin>([ssid, password] { return rtps_comms_wifi_join(ssid, password); },
                          join_done);
}

void keyboard_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CANCEL) {
    password_back();
    return;
  }
  // LV_EVENT_READY: the OK key
  if (busy) {
    return;
  }
  const std::string password = lv_textarea_get_text(password_text);
  if (!password.empty() && password.size() < 8) {
    lv_label_set_text(ui_NetPwStatus, "A WiFi password has 8 characters or more.");
    cfg.refuse();
    return;
  }
  lv_label_set_text(ui_NetPwStatus, "Connecting...");
  lv_obj_add_state(keyboard, LV_STATE_DISABLED); // no typing under a join in flight
  join(chosen_ssid, password);
}

// A finger and the stick share the keyboard's one "selected key". A stick move
// while a finger is down would carry the selection off the key being pressed,
// and the keys that act on release (OK, shift, backspace) would do nothing.
void keyboard_key_guard_cb(lv_event_t *e) {
  for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev != nullptr;
       indev = lv_indev_get_next(indev)) {
    if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER &&
        lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
      lv_event_stop_processing(e);
      return;
    }
  }
}

// LVGL's keyboard in the spec's look: keys outlined on the ground colour, the
// key under the stick's cursor or a finger drawn negative.
void keyboard_style(lv_obj_t *kb) {
  const lv_style_selector_t main = sel(LV_PART_MAIN, LV_STATE_DEFAULT);
  ui_object_set_themeable_style_property(kb, main, LV_STYLE_BG_COLOR, _ui_theme_color_background);
  ui_object_set_themeable_style_property(kb, main, LV_STYLE_BG_OPA, _ui_theme_alpha_background);
  lv_obj_set_style_border_width(kb, 0, main);
  lv_obj_set_style_radius(kb, 0, main);
  lv_obj_set_style_pad_all(kb, 8, main);
  lv_obj_set_style_pad_gap(kb, 8, main);
  lv_obj_set_style_text_font(kb, &lv_font_montserrat_32, main);
  // the theme's focus outline round the whole keyboard would only be noise
  for (lv_state_t state : {LV_STATE_FOCUSED, LV_STATE_FOCUS_KEY}) {
    lv_obj_set_style_outline_width(kb, 0, sel(LV_PART_MAIN, state));
  }

  const lv_style_selector_t key = sel(LV_PART_ITEMS, LV_STATE_DEFAULT);
  ui_object_set_themeable_style_property(kb, key, LV_STYLE_BG_COLOR, _ui_theme_color_background);
  ui_object_set_themeable_style_property(kb, key, LV_STYLE_BG_OPA, _ui_theme_alpha_background);
  ui_object_set_themeable_style_property(kb, key, LV_STYLE_TEXT_COLOR, _ui_theme_color_text);
  ui_object_set_themeable_style_property(kb, key, LV_STYLE_TEXT_OPA, _ui_theme_alpha_text);
  ui_object_set_themeable_style_property(kb, key, LV_STYLE_BORDER_COLOR,
                                         _ui_theme_color_text_muted);
  ui_object_set_themeable_style_property(kb, key, LV_STYLE_BORDER_OPA, _ui_theme_alpha_text_muted);
  lv_obj_set_style_border_width(kb, 2, key);
  lv_obj_set_style_radius(kb, 8, key);
  lv_obj_set_style_shadow_width(kb, 0, key);
  // LVGL marks the keys that are not letters (shift, backspace, OK...) CHECKED:
  // a quiet fill sets them apart, and leaves the negative to the cursor.
  const lv_style_selector_t special = sel(LV_PART_ITEMS, LV_STATE_CHECKED);
  ui_object_set_themeable_style_property(kb, special, LV_STYLE_BG_COLOR,
                                         _ui_theme_color_text_muted);
  lv_obj_set_style_bg_opa(kb, LV_OPA_30, special);
  ui_object_set_themeable_style_property(kb, special, LV_STYLE_TEXT_COLOR, _ui_theme_color_text);
  ui_object_set_themeable_style_property(kb, special, LV_STYLE_TEXT_OPA, _ui_theme_alpha_text);
  for (lv_state_t state : {LV_STATE_PRESSED, LV_STATE_FOCUS_KEY,
                           static_cast<lv_state_t>(LV_STATE_FOCUS_KEY | LV_STATE_CHECKED),
                           static_cast<lv_state_t>(LV_STATE_PRESSED | LV_STATE_CHECKED)}) {
    const lv_style_selector_t on = sel(LV_PART_ITEMS, state);
    ui_object_set_themeable_style_property(kb, on, LV_STYLE_BG_COLOR, _ui_theme_color_text);
    ui_object_set_themeable_style_property(kb, on, LV_STYLE_BG_OPA, _ui_theme_alpha_text);
    ui_object_set_themeable_style_property(kb, on, LV_STYLE_TEXT_COLOR, _ui_theme_color_background);
    ui_object_set_themeable_style_property(kb, on, LV_STYLE_TEXT_OPA, _ui_theme_alpha_background);
    lv_obj_set_style_outline_width(kb, 0, on);
  }
}

void text_style(lv_obj_t *text) {
  const lv_style_selector_t main = sel(LV_PART_MAIN, LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(text, LV_OPA_TRANSP, main); // NetPwBox draws the box
  lv_obj_set_style_border_width(text, 0, main);
  lv_obj_set_style_pad_hor(text, 20, main);
  lv_obj_set_style_text_font(text, &ui_font_IBMPlexSansRegular34, main);
  ui_object_set_themeable_style_property(text, main, LV_STYLE_TEXT_COLOR, _ui_theme_color_text);
  ui_object_set_themeable_style_property(text, main, LV_STYLE_TEXT_OPA, _ui_theme_alpha_text);
  const lv_style_selector_t cursor = sel(LV_PART_CURSOR, LV_STATE_FOCUSED);
  ui_object_set_themeable_style_property(text, cursor, LV_STYLE_BORDER_COLOR, _ui_theme_color_text);
  ui_object_set_themeable_style_property(text, cursor, LV_STYLE_BORDER_OPA, _ui_theme_alpha_text);
  for (lv_state_t state : {LV_STATE_FOCUSED, LV_STATE_FOCUS_KEY}) {
    lv_obj_set_style_outline_width(text, 0, sel(LV_PART_MAIN, state));
  }
}

void show_password(const std::string &ssid) {
  networks_clear();
  show_page(Page::PASSWORD);
  chosen_ssid = ssid;
  lv_label_set_text(ui_NetPwTitle, ssid.c_str());
  lv_label_set_text(ui_NetPwStatus, "Type the password, then OK.");

  // Shown as typed: a typo on a 720 px keyboard is easier to see than to guess.
  password_text = lv_textarea_create(ui_NetPwBox);
  lv_textarea_set_one_line(password_text, true);
  lv_textarea_set_max_length(password_text, 63); // WPA2's longest passphrase
  lv_obj_set_width(password_text, lv_pct(100));  // one line sets its own height
  lv_obj_center(password_text);
  text_style(password_text);
  lv_obj_add_state(password_text, LV_STATE_FOCUSED); // its cursor blinks while it is

  keyboard = lv_keyboard_create(ui_NetPwKeyboard);
  lv_obj_set_size(keyboard, lv_pct(100), lv_pct(100));
  lv_obj_align(keyboard, LV_ALIGN_TOP_MID, 0, 0);
  lv_keyboard_set_textarea(keyboard, password_text);
  keyboard_style(keyboard);
  lv_obj_add_event_cb(keyboard, keyboard_cb, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(keyboard, keyboard_cb, LV_EVENT_CANCEL, nullptr);
  lv_obj_add_event_cb(keyboard, keyboard_key_guard_cb,
                      static_cast<lv_event_code_t>(LV_EVENT_KEY | LV_EVENT_PREPROCESS), nullptr);

  // The stick walks the keys (the keyboard takes the arrows itself) and its
  // button presses one. OK joins; the keyboard key, bottom left, goes back.
  lv_group_remove_all_objs(password_group);
  lv_group_add_obj(password_group, keyboard);
  cfg.use_group(password_group);
  lv_group_focus_obj(keyboard);
}

void button_setup(lv_obj_t *button, lv_event_cb_t click, void *user_data = nullptr) {
  cfg.focus_ring(button);
  cfg.mirror_states(button);
  lv_obj_add_event_cb(button, click, LV_EVENT_CLICKED, user_data);
}

} // namespace

void internet_ui_init(const InternetUiConfig &config) {
  cfg = config;

  main_group = lv_group_create();
  for (lv_obj_t *button :
       {ui_NetChoiceEthernet, ui_NetChoiceWifi, ui_NetWifiButton, ui_NetRestartButton}) {
    lv_group_add_obj(main_group, button);
    lv_obj_add_event_cb(button, main_key_cb, LV_EVENT_KEY, nullptr);
  }
  button_setup(ui_NetChoiceEthernet, choice_cb,
               reinterpret_cast<void *>(static_cast<intptr_t>(NetLink::ETHERNET)));
  button_setup(ui_NetChoiceWifi, choice_cb,
               reinterpret_cast<void *>(static_cast<intptr_t>(NetLink::WIFI)));
  button_setup(ui_NetWifiButton, [](lv_event_t *) { show_networks(); });
  cfg.claim_clicks(ui_NetWifiButton); // its name and chevron are part of the button
  button_setup(ui_NetRestartButton, restart_cb);

  networks_group = lv_group_create();
  lv_group_add_obj(networks_group, ui_NetPickBack);
  button_setup(ui_NetPickBack, [](lv_event_t *) { networks_back(); });
  lv_obj_add_event_cb(ui_NetPickBack, networks_key_cb, LV_EVENT_KEY, nullptr);
  // NetRowTemplate is only there so SquareLine exports the component.
  lv_obj_delete(ui_NetRowTemplate);
  ui_NetRowTemplate = nullptr;

  password_group = lv_group_create();
  button_setup(ui_NetPwBack, [](lv_event_t *) { password_back(); });

  // The status lines follow the link while the main page is up.
  lv_timer_create(
      [](lv_timer_t *) {
        if (lv_screen_active() == ui_InternetScreen && page == Page::MAIN) {
          main_refresh();
        }
      },
      kStatusPollMs, nullptr);
}

void internet_ui_on_load() {
  password_close();
  networks_clear();
  show_main(chosen_link() == NetLink::WIFI ? ui_NetChoiceWifi : ui_NetChoiceEthernet);
}
