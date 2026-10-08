#pragma once
// The InternetScreen ("Internet Settings" in the burger menu).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "lvgl.h"

#include "drive_ui/link_state.hpp"
#include "hmi_format/topbar.hpp"

namespace hmi::ui {

/// One network a scan heard (main's WifiNetworkFound).
struct WifiNetwork {
  std::string ssid;
  int rssi = 0;         ///< dBm
  bool secured = false; ///< needs a password
};

/// How a join ended (main's WifiJoin, value for value).
enum class WifiJoinResult : uint8_t {
  JOINED,         ///< associated: the network and its password are saved
  WRONG_PASSWORD, ///< found, but the handshake failed
  NOT_FOUND,      ///< no such network in range
  FAILED,         ///< the WiFi hardware did not start, or no answer in time
};

/// Which link RTPS runs over, which WiFi network, and what the link is doing. Three pages on
/// one screen:
///   main      Ethernet / WiFi, the saved network, status, "Restart to apply"
///   networks  what a scan found, strongest first
///   password  a text box and an on-screen keyboard; OK tries the network
/// The link is chosen at boot, so picking the other one only saves the choice and offers a
/// restart. A network is saved only once it has been joined. Everything here runs on the UI
/// task except the scan and the join, which block for seconds: main runs them on a worker
/// thread (Config::spawn) that posts the result back (Config::post), and a result that comes
/// back after its page was left is dropped.
class InternetView {
public:
  static constexpr uint32_t STATUS_POLL_MS = 500;

  /// What main does for the view: the network (rtps_comms), the worker, the restart.
  struct Config {
    bool (*wifi_configured)();                   ///< a WiFi network is known
    std::string (*wifi_ssid)();                  ///< the network WiFi joins; "" = none
    format::Link (*link)();                      ///< the link in use
    const char *(*link_name)(format::Link link); ///< "Ethernet" / "WiFi"
    LinkState (*link_state)();                   ///< what the link is doing
    std::string (*ip)();                         ///< its address; "" without one
    std::optional<int> (*rssi)();                ///< dBm; nullopt on Ethernet or not associated
    /// Scans (blocking, a worker only): strongest first; nullopt if WiFi did not answer.
    std::optional<std::vector<WifiNetwork>> (*scan)();
    /// Joins (blocking, a worker only), saving the network only if that works.
    WifiJoinResult (*join)(const std::string &ssid, const std::string &password);
    /// Runs `job` on a new worker thread. Any task.
    void (*spawn)(std::function<void()> job);
    /// Hands `fn(data)` to the UI task (main: lv_async_call under lvgl_mutex). Worker thread.
    void (*post)(void (*fn)(void *data), void *data);
    /// Logs and restarts the HMI so `link` comes up. UI task (a click); does not return.
    void (*restart)(format::Link link);
    std::string *chosen_ssid;               ///< main's: the network the password page is for
    std::vector<WifiNetwork> *scan_results; ///< main's: what the rows stand for, in row order
  };

  /// What the screen borrows from the rest of the UI (main's nav and settings).
  struct Hooks {
    lv_subject_t *connection;             ///< NetLink as an int; setting it saves it
    void (*use_group)(lv_group_t *group); ///< hands the joystick a group (+ the burger key)
    void (*focus_ring)(lv_obj_t *button); ///< the cursor ring of a plain button
    void (*mirror_states)(lv_obj_t *obj); ///< PRESSED / CHECKED / FOCUSED to the children
    void (*claim_clicks)(lv_obj_t *obj);  ///< `obj`, and nothing inside it, takes clicks
    void (*refuse)();                     ///< "can't do that", heard and felt
  };

  constexpr explicit InternetView(const Config &config) noexcept
      : config_(config) {}

  /// @brief Wires the three pages and starts the status timer.
  /// @param hooks what the screen borrows from the rest of the UI
  /// app_main, once, after ui_init and before lv_task starts.
  void init(const Hooks &hooks);
  /// @brief The screen came up: the main page, the cursor on the link in use.
  /// UI task.
  void on_load();

private:
  enum class Page : uint8_t { MAIN, NETWORKS, PASSWORD };

  template <class Result> struct Posted;
  template <class Result, class Work>
  void run_on_worker(Work work, void (InternetView::*deliver)(Result &));

  format::Link chosen_link() const;
  format::Link link_after_restart() const;
  static const char *state_text(LinkState state, format::Link link);
  void main_refresh();
  void show_page(Page next);
  void show_main(lv_obj_t *focus);
  void networks_clear();
  void networks_back();
  void networks_fill(std::optional<std::vector<WifiNetwork>> &found);
  void show_networks();
  void password_close();
  void password_back();
  void join_done(WifiJoinResult &result);
  void join(const std::string &ssid, const std::string &password);
  void show_password(const std::string &ssid);
  void button_setup(lv_obj_t *button, lv_event_cb_t click, void *user_data);
  static void keyboard_style(lv_obj_t *kb);
  static void text_style(lv_obj_t *text);

  static void choice_cb(lv_event_t *e);
  static void restart_cb(lv_event_t *e);
  static void main_key_cb(lv_event_t *e);
  static void network_row_click_cb(lv_event_t *e);
  static void networks_key_cb(lv_event_t *e);
  static void keyboard_cb(lv_event_t *e);
  static void keyboard_key_guard_cb(lv_event_t *e);
  static void status_cb(lv_timer_t *timer);

  Config config_;
  Hooks hooks_{};
  Page page_ = Page::MAIN;
  lv_group_t *main_group_ = nullptr;
  lv_group_t *networks_group_ = nullptr;
  lv_group_t *password_group_ = nullptr;
  // Made for the length of a visit to their page.
  lv_obj_t *password_text_ = nullptr;
  lv_obj_t *keyboard_ = nullptr;
  // A scan or a join is running on the worker. One at a time, and a result that comes back
  // after its page was left (generation_ moved on) is dropped.
  bool busy_ = false;
  uint32_t generation_ = 0;
};

} // namespace hmi::ui
