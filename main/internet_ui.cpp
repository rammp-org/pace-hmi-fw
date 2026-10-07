#include "internet_ui.hpp"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "components/ui_comp_netrow.h"
#include "esp_pthread.h"
#include "esp_system.h"
#include "format.hpp"
#include "hmi_format/net.hpp"
#include "hmi_ui/internet_view.hpp"
#include "logger.hpp"
#include "rtps_comms.hpp"
#include "ui.h"
#include "ui_themes.h"

namespace {

constexpr lv_style_selector_t sel(lv_part_t part, lv_state_t state) {
  return static_cast<lv_style_selector_t>(part) | static_cast<lv_style_selector_t>(state);
}

} // namespace

///////////////////////////////////////////////////////////////////////////////
// The worker: one blocking call off the LVGL task, its result posted back

template <class Result> struct hmi::ui::InternetView::Posted {
  InternetView *view;
  uint32_t generation;
  Result result;
  void (InternetView::*deliver)(Result &);
};

// `work` runs on its own thread; `deliver` then runs on the LVGL task with what
// it returned, unless the page was left meanwhile.
template <class Result, class Work>
void hmi::ui::InternetView::run_on_worker(Work work, void (InternetView::*deliver)(Result &)) {
  busy_ = true;
  const uint32_t started = generation_;
  config_.spawn([this, work = std::move(work), deliver, started]() mutable {
    auto posted = std::make_unique<Posted<Result>>(Posted<Result>{this, started, work(), deliver});
    config_.post(
        [](void *data) {
          std::unique_ptr<Posted<Result>> p(static_cast<Posted<Result> *>(data));
          p->view->busy_ = false;
          if (p->generation == p->view->generation_) {
            (p->view->*p->deliver)(p->result);
          }
        },
        posted.release());
  });
}

///////////////////////////////////////////////////////////////////////////////
// Main page

hmi::format::Link hmi::ui::InternetView::chosen_link() const {
  return static_cast<format::Link>(lv_subject_get_int(hooks_.connection));
}

// The link a restart would bring up: WiFi needs a network to join.
hmi::format::Link hmi::ui::InternetView::link_after_restart() const {
  return chosen_link() == format::Link::WIFI && config_.wifi_configured() ? format::Link::WIFI
                                                                          : format::Link::ETHERNET;
}

const char *hmi::ui::InternetView::state_text(LinkState state, format::Link link) {
  switch (state) {
  case LinkState::NET_FAILED:
    return link == format::Link::WIFI ? "WiFi hardware not responding"
                                      : "Ethernet hardware missing";
  case LinkState::LINK_DOWN:
    return link == format::Link::WIFI ? "Not connected" : "Cable unplugged";
  case LinkState::NO_IP:
    return "Connected, waiting for an address";
  case LinkState::NO_PEER:
    return "Online, no MCB";
  case LinkState::CONNECTED:
    return "Online, MCB connected";
  }
  return "--";
}

void hmi::ui::InternetView::main_refresh() {
  const format::Link chosen = chosen_link();
  lv_obj_set_state(ui_NetChoiceEthernet, LV_STATE_CHECKED, chosen == format::Link::ETHERNET);
  lv_obj_set_state(ui_NetChoiceWifi, LV_STATE_CHECKED, chosen == format::Link::WIFI);

  const std::string ssid = config_.wifi_ssid();
  lv_label_set_text(ui_NetWifiName, ssid.empty() ? "Not set" : ssid.c_str());

  const format::Link link = config_.link();
  lv_label_set_text(ui_NetUsingValue, config_.link_name(link));
  lv_label_set_text(ui_NetStateValue, state_text(config_.link_state(), link));
  const std::string ip = config_.ip();
  lv_label_set_text(ui_NetIpValue, ip.empty() ? "--" : ip.c_str());
  const std::optional<int> rssi = config_.rssi();
  std::array<char, format::SIGNAL_TEXT_SIZE> signal{};
  format::signal_text(rssi, signal);
  lv_label_set_text(ui_NetSignalValue, signal.data());

  // Offered only when a restart would change the link: WiFi chosen with no
  // network to join would come up on Ethernet again, so there is nothing to apply.
  const bool pending = link_after_restart() != link;
  const bool shown = !lv_obj_has_flag(ui_NetRestartButton, LV_OBJ_FLAG_HIDDEN);
  if (pending != shown) {
    lv_obj_set_flag(ui_NetRestartButton, LV_OBJ_FLAG_HIDDEN, !pending);
    if (!pending && lv_group_get_focused(main_group_) == ui_NetRestartButton) {
      lv_group_focus_obj(ui_NetWifiButton); // the cursor must not stay on a hidden button
    }
  }
}

void hmi::ui::InternetView::show_page(Page next) {
  generation_++; // whatever the worker is still doing belongs to the page being left
  page_ = next;
  lv_obj_set_flag(ui_NetPickPanel, LV_OBJ_FLAG_HIDDEN, next != Page::NETWORKS);
  lv_obj_set_flag(ui_NetPwPanel, LV_OBJ_FLAG_HIDDEN, next != Page::PASSWORD);
}

void hmi::ui::InternetView::show_main(lv_obj_t *focus) {
  show_page(Page::MAIN);
  main_refresh();
  hooks_.use_group(main_group_);
  lv_group_focus_obj(focus);
}

// Ethernet or WiFi, by which of the two buttons this callback is on.
void hmi::ui::InternetView::choice_cb(lv_event_t *e) {
  auto *view = static_cast<InternetView *>(lv_event_get_user_data(e));
  const format::Link link = lv_event_get_current_target_obj(e) == ui_NetChoiceWifi
                                ? format::Link::WIFI
                                : format::Link::ETHERNET;
  lv_subject_set_int(view->hooks_.connection, static_cast<int32_t>(link)); // saved by its observer
  view->main_refresh();
}

void hmi::ui::InternetView::restart_cb(lv_event_t *e) {
  auto *view = static_cast<InternetView *>(lv_event_get_user_data(e));
  view->config_.restart(view->link_after_restart());
}

// The stick on the main page: two choices side by side, then the network, then
// (when it is shown) the restart button. Down past the last one is the burger
// key, which the group holds after them.
void hmi::ui::InternetView::main_key_cb(lv_event_t *e) {
  auto *view = static_cast<InternetView *>(lv_event_get_user_data(e));
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
      lv_group_focus_obj(view->chosen_link() == format::Link::WIFI ? ui_NetChoiceWifi
                                                                   : ui_NetChoiceEthernet);
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
      lv_group_focus_obj(lv_group_get_obj_by_index(view->main_group_,
                                                   lv_group_get_obj_count(view->main_group_) - 1));
    }
    break;
  default:
    break;
  }
}

///////////////////////////////////////////////////////////////////////////////
// Networks page

void hmi::ui::InternetView::networks_clear() {
  // Backwards, and by index: deleting a row renumbers the ones after it.
  for (int32_t i = static_cast<int32_t>(lv_obj_get_child_count(ui_NetList)) - 1; i >= 0; i--) {
    lv_obj_delete(lv_obj_get_child(ui_NetList, i));
  }
  config_.scan_results->clear();
}

void hmi::ui::InternetView::networks_back() {
  networks_clear();
  show_main(ui_NetWifiButton);
}

// A row: its place in NetList is its place in the scan results (networks_fill
// adds one row per result, in order, to an emptied list).
void hmi::ui::InternetView::network_row_click_cb(lv_event_t *e) {
  auto *view = static_cast<InternetView *>(lv_event_get_user_data(e));
  if (view->busy_) {
    return;
  }
  const auto index = static_cast<size_t>(lv_obj_get_index(lv_event_get_current_target_obj(e)));
  if (index >= view->config_.scan_results->size()) {
    return;
  }
  const WifiNetwork network =
      (*view->config_.scan_results)[index]; // a copy: the rows are about to go
  if (network.secured) {
    view->show_password(network.ssid);
  } else {
    lv_label_set_text_fmt(ui_NetPickStatus, "Connecting to %s...", network.ssid.c_str());
    view->join(network.ssid, "");
  }
}

// Up and down walk the list; up from the first row is the back button, down
// from the last is the burger key. Left is "back", as on the seat page.
void hmi::ui::InternetView::networks_key_cb(lv_event_t *e) {
  auto *view = static_cast<InternetView *>(lv_event_get_user_data(e));
  switch (lv_event_get_key(e)) {
  case LV_KEY_UP:
    lv_group_focus_prev(view->networks_group_);
    break;
  case LV_KEY_DOWN:
    lv_group_focus_next(view->networks_group_);
    break;
  case LV_KEY_LEFT:
    view->networks_back();
    break;
  default:
    break;
  }
}

void hmi::ui::InternetView::networks_fill(std::optional<std::vector<WifiNetwork>> &found) {
  if (page_ != Page::NETWORKS) {
    return;
  }
  if (!found) {
    lv_label_set_text(ui_NetPickStatus, "WiFi is not responding.");
    hooks_.refuse();
    return;
  }
  std::vector<WifiNetwork> &scan_results = *config_.scan_results;
  scan_results = std::move(*found);
  lv_label_set_text(ui_NetPickStatus,
                    scan_results.empty() ? "No networks found." : "Pick a network.");
  for (size_t i = 0; i < scan_results.size(); i++) {
    const WifiNetwork &network = scan_results[i];
    lv_obj_t *row = ui_NetRow_create(ui_NetList);
    lv_label_set_text(ui_comp_get_child(row, UI_COMP_NETROW_NETROWGROUND_NETROWNAME),
                      network.ssid.c_str());
    std::array<char, format::ROW_SIGNAL_TEXT_SIZE> signal{};
    format::net_row_signal_text(network.secured, network.rssi, signal);
    lv_label_set_text(ui_comp_get_child(row, UI_COMP_NETROW_NETROWGROUND_NETROWSIGNAL),
                      signal.data());
    hooks_.claim_clicks(row);
    hooks_.mirror_states(row);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(row, network_row_click_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(row, networks_key_cb, LV_EVENT_KEY, this);
    lv_group_add_obj(networks_group_, row);
  }
  hooks_.use_group(networks_group_); // puts the burger key back after the rows
  if (!scan_results.empty()) {
    lv_group_focus_obj(lv_obj_get_child(ui_NetList, 0));
  }
}

void hmi::ui::InternetView::show_networks() {
  show_page(Page::NETWORKS);
  networks_clear();
  lv_label_set_text(ui_NetPickStatus, "Scanning...");
  hooks_.use_group(networks_group_);
  lv_group_focus_obj(ui_NetPickBack);
  run_on_worker<std::optional<std::vector<WifiNetwork>>>([scan = config_.scan] { return scan(); },
                                                         &InternetView::networks_fill);
}

///////////////////////////////////////////////////////////////////////////////
// Password page

void hmi::ui::InternetView::password_close() {
  if (keyboard_ != nullptr) {
    lv_obj_delete(keyboard_);
    keyboard_ = nullptr;
  }
  if (password_text_ != nullptr) {
    lv_obj_delete(password_text_);
    password_text_ = nullptr;
  }
}

void hmi::ui::InternetView::password_back() {
  password_close();
  show_networks();
}

void hmi::ui::InternetView::join_done(WifiJoinResult &result) {
  if (result == WifiJoinResult::JOINED) {
    password_close();
    networks_clear();
    show_main(ui_NetWifiButton);
    return;
  }
  const char *why = result == WifiJoinResult::WRONG_PASSWORD ? "Wrong password. Try again."
                    : result == WifiJoinResult::NOT_FOUND    ? "Network not found."
                                                             : "Could not connect.";
  hooks_.refuse();
  if (page_ == Page::PASSWORD) {
    lv_label_set_text(ui_NetPwStatus, why);
    lv_obj_remove_state(keyboard_, LV_STATE_DISABLED);
  } else {
    lv_label_set_text(ui_NetPickStatus, why);
  }
}

void hmi::ui::InternetView::join(const std::string &ssid, const std::string &password) {
  run_on_worker<WifiJoinResult>(
      [join = config_.join, ssid, password] { return join(ssid, password); },
      &InternetView::join_done);
}

void hmi::ui::InternetView::keyboard_cb(lv_event_t *e) {
  auto *view = static_cast<InternetView *>(lv_event_get_user_data(e));
  if (lv_event_get_code(e) == LV_EVENT_CANCEL) {
    view->password_back();
    return;
  }
  // LV_EVENT_READY: the OK key
  if (view->busy_) {
    return;
  }
  const std::string password = lv_textarea_get_text(view->password_text_);
  if (!password.empty() && password.size() < 8) {
    lv_label_set_text(ui_NetPwStatus, "A WiFi password has 8 characters or more.");
    view->hooks_.refuse();
    return;
  }
  lv_label_set_text(ui_NetPwStatus, "Connecting...");
  lv_obj_add_state(view->keyboard_, LV_STATE_DISABLED); // no typing under a join in flight
  view->join(*view->config_.chosen_ssid, password);
}

// A finger and the stick share the keyboard's one "selected key". A stick move
// while a finger is down would carry the selection off the key being pressed,
// and the keys that act on release (OK, shift, backspace) would do nothing.
void hmi::ui::InternetView::keyboard_key_guard_cb(lv_event_t *e) {
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
void hmi::ui::InternetView::keyboard_style(lv_obj_t *kb) {
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

void hmi::ui::InternetView::text_style(lv_obj_t *text) {
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

void hmi::ui::InternetView::show_password(const std::string &ssid) {
  networks_clear();
  show_page(Page::PASSWORD);
  *config_.chosen_ssid = ssid;
  lv_label_set_text(ui_NetPwTitle, ssid.c_str());
  lv_label_set_text(ui_NetPwStatus, "Type the password, then OK.");

  // Shown as typed: a typo on a 720 px keyboard is easier to see than to guess.
  password_text_ = lv_textarea_create(ui_NetPwBox);
  lv_textarea_set_one_line(password_text_, true);
  lv_textarea_set_max_length(password_text_, 63); // WPA2's longest passphrase
  lv_obj_set_width(password_text_, lv_pct(100));  // one line sets its own height
  lv_obj_center(password_text_);
  text_style(password_text_);
  lv_obj_add_state(password_text_, LV_STATE_FOCUSED); // its cursor blinks while it is

  keyboard_ = lv_keyboard_create(ui_NetPwKeyboard);
  lv_obj_set_size(keyboard_, lv_pct(100), lv_pct(100));
  lv_obj_align(keyboard_, LV_ALIGN_TOP_MID, 0, 0);
  lv_keyboard_set_textarea(keyboard_, password_text_);
  keyboard_style(keyboard_);
  lv_obj_add_event_cb(keyboard_, keyboard_cb, LV_EVENT_READY, this);
  lv_obj_add_event_cb(keyboard_, keyboard_cb, LV_EVENT_CANCEL, this);
  lv_obj_add_event_cb(keyboard_, keyboard_key_guard_cb,
                      static_cast<lv_event_code_t>(LV_EVENT_KEY | LV_EVENT_PREPROCESS), nullptr);

  // The stick walks the keys (the keyboard takes the arrows itself) and its
  // button presses one. OK joins; the keyboard key, bottom left, goes back.
  lv_group_remove_all_objs(password_group_);
  lv_group_add_obj(password_group_, keyboard_);
  hooks_.use_group(password_group_);
  lv_group_focus_obj(keyboard_);
}

void hmi::ui::InternetView::button_setup(lv_obj_t *button, lv_event_cb_t click, void *user_data) {
  hooks_.focus_ring(button);
  hooks_.mirror_states(button);
  lv_obj_add_event_cb(button, click, LV_EVENT_CLICKED, user_data);
}

void hmi::ui::InternetView::status_cb(lv_timer_t *timer) {
  auto *view = static_cast<InternetView *>(lv_timer_get_user_data(timer));
  if (lv_screen_active() == ui_InternetScreen && view->page_ == Page::MAIN) {
    view->main_refresh();
  }
}

void hmi::ui::InternetView::init(const Hooks &hooks) {
  hooks_ = hooks;

  main_group_ = lv_group_create();
  for (lv_obj_t *button :
       {ui_NetChoiceEthernet, ui_NetChoiceWifi, ui_NetWifiButton, ui_NetRestartButton}) {
    lv_group_add_obj(main_group_, button);
    lv_obj_add_event_cb(button, main_key_cb, LV_EVENT_KEY, this);
  }
  button_setup(ui_NetChoiceEthernet, choice_cb, this);
  button_setup(ui_NetChoiceWifi, choice_cb, this);
  button_setup(
      ui_NetWifiButton,
      [](lv_event_t *e) {
        static_cast<InternetView *>(lv_event_get_user_data(e))->show_networks();
      },
      this);
  hooks_.claim_clicks(ui_NetWifiButton); // its name and chevron are part of the button
  button_setup(ui_NetRestartButton, restart_cb, this);

  networks_group_ = lv_group_create();
  lv_group_add_obj(networks_group_, ui_NetPickBack);
  button_setup(
      ui_NetPickBack,
      [](lv_event_t *e) {
        static_cast<InternetView *>(lv_event_get_user_data(e))->networks_back();
      },
      this);
  lv_obj_add_event_cb(ui_NetPickBack, networks_key_cb, LV_EVENT_KEY, this);
  // NetRowTemplate is only there so SquareLine exports the component.
  lv_obj_delete(ui_NetRowTemplate);
  ui_NetRowTemplate = nullptr;

  password_group_ = lv_group_create();
  button_setup(
      ui_NetPwBack,
      [](lv_event_t *e) {
        static_cast<InternetView *>(lv_event_get_user_data(e))->password_back();
      },
      this);

  // The status lines follow the link while the main page is up.
  lv_timer_create(status_cb, STATUS_POLL_MS, this);
}

void hmi::ui::InternetView::on_load() {
  password_close();
  networks_clear();
  show_main(chosen_link() == format::Link::WIFI ? ui_NetChoiceWifi : ui_NetChoiceEthernet);
}

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
