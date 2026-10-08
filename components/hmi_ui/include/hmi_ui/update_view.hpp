#pragma once
// The UpdateScreen (Settings > Firmware update in the burger menu).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "lvgl.h"

#include "hmi_ui/firmware_info.hpp"
#include "ota_parse/ota_parse.hpp"

namespace hmi::ui {

/// What a list request found (main's GithubReleases).
struct ReleaseList {
  bool ok = false;
  std::string error;                  ///< why not, when !ok, for the panel
  std::vector<ota::Release> releases; ///< newest first, drafts left out
};

/// Where an install is (main's OtaStage, value for value).
enum class InstallStage : uint8_t {
  IDLE,        ///< nothing started since boot
  CONNECTING,  ///< resolving, TLS, following GitHub's redirect to the file
  DOWNLOADING, ///< writing it to the other slot
  VERIFYING,   ///< checking the image and its digest
  DONE,        ///< the next boot runs it
  FAILED,      ///< nothing changed: the running image still boots
};

/// An install's progress (main's OtaStatus).
struct InstallStatus {
  InstallStage stage = InstallStage::IDLE;
  std::string tag;     ///< the release being installed
  size_t done = 0;     ///< bytes written
  size_t total = 0;    ///< bytes expected
  std::string message; ///< one line: what it is doing, or why it failed
  std::string log;     ///< every step so far, one per line
};

/// The releases published on GitHub, and installing one of them. Three pages on one screen:
///   list   the firmware installed, then every release, newest first; the one running is
///          marked "Installed"
///   pick   one release: date, kind, size, its notes, and Install
///   run    the install's progress and log
/// The list is fetched each time the screen opens, on main's worker thread (Config::spawn),
/// and handed back to the UI task (Config::post). The install runs on main's thread and
/// outlives the screen. When the HMI restarts after an install is main's decision (it keeps
/// the guard against restarting under a driving chair); the view only says it is waiting.
class UpdateView {
public:
  /// What main does for the view.
  struct Config {
    ReleaseList (*fetch)();                       ///< asks GitHub; blocking, a worker only
    bool (*install)(const ota::Release &release); ///< starts one; false if one is running
    InstallStatus (*status)();                    ///< where the install is; any task
    FirmwareInfo (*firmware)();                   ///< what main knows of the running image
    FirmwareIdentity (*identity)();               ///< the running image's description
    void (*spawn)(std::function<void()> job);     ///< runs `job` on a new worker thread
    /// Hands `fn(data)` to the UI task (main: lv_async_call under lvgl_mutex). Worker thread.
    void (*post)(void (*fn)(void *data), void *data);
    std::vector<ota::Release> *releases; ///< main's: what the rows stand for, in row order
  };

  /// What the screen borrows from the rest of the UI.
  struct Hooks {
    void (*use_group)(lv_group_t *group); ///< hands the joystick a group (+ the burger key)
    void (*focus_ring)(lv_obj_t *button); ///< the cursor ring of a plain button
    void (*mirror_states)(lv_obj_t *obj); ///< PRESSED / CHECKED / FOCUSED to the children
    void (*claim_clicks)(lv_obj_t *obj);  ///< `obj`, and nothing inside it, takes clicks
    void (*refuse)();                     ///< "can't do that", heard and felt
    bool (*may_restart)();                ///< false while the HMI must not restart (driving)
  };

  constexpr explicit UpdateView(const Config &config) noexcept
      : config_(config) {}

  /// @brief Wires the three pages.
  /// @param hooks what the screen borrows from the rest of the UI
  /// app_main, once, after ui_init and before lv_task starts.
  void init(const Hooks &hooks);
  /// @brief The screen came up: the list (fetched again), or the install running.
  /// UI task.
  void on_load();
  /// @brief The run page follows the install while it is up. main's poll timer calls this.
  /// UI task.
  void poll();

private:
  enum class Page : uint8_t { LIST, PICK, RUN };

  struct Fetched {
    UpdateView *view;
    ReleaseList result;
  };

  static std::string day(const std::string &iso);
  bool is_installed(const ota::Release &r) const;
  static std::string megabytes(size_t bytes);
  void show_page(Page next);
  void list_clear();
  void list_fill();
  void list_status(const ReleaseList &result);
  void fetch();
  void show_installed();
  void show_list();
  void show_pick(size_t index);
  void run_refresh();
  void show_run();
  void button_setup(lv_obj_t *button, lv_event_cb_t click);

  static void walk_key_cb(lv_event_t *e);
  static void row_click_cb(lv_event_t *e);
  static void fetch_done(void *data);
  static void install_cb(lv_event_t *e);

  Config config_;
  Hooks hooks_{};
  Page page_ = Page::LIST;
  lv_group_t *list_group_ = nullptr;
  lv_group_t *pick_group_ = nullptr;
  lv_group_t *run_group_ = nullptr;
  size_t picked_ = 0;     ///< the release on the pick page
  bool fetching_ = false; ///< a list request is on the worker
};

} // namespace hmi::ui
