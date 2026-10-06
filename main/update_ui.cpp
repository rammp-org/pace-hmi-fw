#include "update_ui.hpp"

#include <array>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "components/ui_comp_releaserow.h"
#include "esp_app_desc.h"
#include "esp_pthread.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "format.hpp"
#include "fw_info.hpp"
#include "github_ota.hpp"
#include "hmi_format/update.hpp"
#include "logger.hpp"
#include "ui.h"

namespace {

espp::Logger logger({.tag = "update_ui", .level = espp::Logger::Verbosity::INFO});

UpdateUiConfig cfg;

enum class Page { LIST, PICK, RUN };
Page page = Page::LIST;

lv_group_t *list_group = nullptr;
lv_group_t *pick_group = nullptr;
lv_group_t *run_group = nullptr;

std::vector<GithubRelease> releases; // what the rows stand for, in row order
size_t picked = 0;                   // the release on the pick page
bool fetching = false;               // a list request is on the worker

// When an install that has finished restarts the HMI; 0 until it has.
int64_t restart_at_us = 0;

constexpr uint32_t kPollMs = 250;
constexpr int64_t kRestartDelayUs = 3'000'000;  // long enough to read "Installed"
constexpr size_t kWorkerStackBytes = 12 * 1024; // TLS with certificate checks

///////////////////////////////////////////////////////////////////////////////
// Words

// "2026-09-30" -> "30 Sep 2026"; the text itself when it is not a date
std::string day(const std::string &iso) {
  std::array<char, hmi::format::DAY_TEXT_SIZE> text{};
  return hmi::format::day_text(iso, text) ? std::string(text.data()) : iso;
}

bool is_installed(const GithubRelease &r) {
  const FwInfo info = fw_info();
  return r.tag == esp_app_get_description()->version ||
         (info.release && info.release->tag == r.tag);
}

std::string megabytes(size_t bytes) {
  std::array<char, hmi::format::MEGABYTES_TEXT_SIZE> text{};
  hmi::format::megabytes_text(bytes, text);
  return text.data();
}

///////////////////////////////////////////////////////////////////////////////
// Pages

void show_page(Page next) {
  page = next;
  lv_obj_set_flag(ui_UpdatePickPanel, LV_OBJ_FLAG_HIDDEN, next != Page::PICK);
  lv_obj_set_flag(ui_UpdateRunPanel, LV_OBJ_FLAG_HIDDEN, next != Page::RUN);
}

void show_list();
void show_pick(size_t index);
void show_run();

// Up and down walk the page's group; left is "back", as on the seat page.
void walk_key_cb(lv_event_t *e) {
  auto *group = static_cast<lv_group_t *>(lv_event_get_user_data(e));
  switch (lv_event_get_key(e)) {
  case LV_KEY_UP:
    lv_group_focus_prev(group);
    break;
  case LV_KEY_DOWN:
    lv_group_focus_next(group);
    break;
  case LV_KEY_LEFT:
    if (page == Page::PICK) {
      show_list();
    }
    break;
  default:
    break;
  }
}

///////////////////////////////////////////////////////////////////////////////
// List page

void list_clear() {
  // Backwards, and by index: deleting a row renumbers the ones after it.
  for (int32_t i = static_cast<int32_t>(lv_obj_get_child_count(ui_UpdateList)) - 1; i >= 0; i--) {
    lv_obj_delete(lv_obj_get_child(ui_UpdateList, i));
  }
}

void row_click_cb(lv_event_t *e) {
  const auto index = static_cast<size_t>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (index < releases.size()) {
    show_pick(index);
  }
}

void list_fill() {
  list_clear();
  for (size_t i = 0; i < releases.size(); i++) {
    const GithubRelease &r = releases[i];
    lv_obj_t *row = ui_ReleaseRow_create(ui_UpdateList);
    lv_label_set_text(ui_comp_get_child(row, UI_COMP_RELEASEROW_RELEASEROWGROUND_RELEASEROWTAG),
                      r.tag.c_str());
    std::string note = r.prerelease ? "Pre-release" : "Release";
    if (!r.published.empty()) {
      note += " · " + day(r.published);
    }
    if (r.url.empty()) {
      note += " · no firmware file";
    }
    lv_label_set_text(ui_comp_get_child(row, UI_COMP_RELEASEROW_RELEASEROWGROUND_RELEASEROWNOTE),
                      note.c_str());
    lv_label_set_text(ui_comp_get_child(row, UI_COMP_RELEASEROW_RELEASEROWGROUND_RELEASEROWMARK),
                      is_installed(r) ? "Installed" : "");
    cfg.claim_clicks(row);
    cfg.mirror_states(row);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_add_event_cb(row, walk_key_cb, LV_EVENT_KEY, list_group);
    lv_group_add_obj(list_group, row);
  }
  cfg.use_group(list_group); // puts the burger key back after the rows
  if (!releases.empty()) {
    lv_group_focus_obj(lv_obj_get_child(ui_UpdateList, 0));
  }
}

void list_status(const GithubReleases &result) {
  if (!result.ok) {
    lv_label_set_text(ui_UpdateStatus, result.error.c_str());
  } else if (result.releases.empty()) {
    lv_label_set_text(ui_UpdateStatus, "No releases published yet.");
  } else {
    std::array<char, hmi::format::RELEASE_COUNT_TEXT_SIZE> text{};
    hmi::format::release_count_text(result.releases.size(), text);
    lv_label_set_text(ui_UpdateStatus, text.data());
  }
}

struct Fetched {
  GithubReleases result;
};

void fetch_done(void *data) {
  std::unique_ptr<Fetched> fetched(static_cast<Fetched *>(data));
  GithubReleases &result = fetched->result;
  fetching = false;
  const bool shown = lv_screen_active() == ui_UpdateScreen && page == Page::LIST;
  if (shown) {
    list_status(result);
  }
  if (result.ok) {
    releases = std::move(result.releases); // a failed fetch keeps the last list
  }
  if (shown) {
    if (!result.ok) {
      cfg.refuse();
    }
    list_fill();
  }
}

void fetch() {
  if (fetching) {
    return; // the one already out answers this visit too
  }
  fetching = true;
  esp_pthread_cfg_t thread_cfg = esp_pthread_get_default_config();
  thread_cfg.stack_size = kWorkerStackBytes;
  thread_cfg.thread_name = "update_ui";
  esp_pthread_set_cfg(&thread_cfg);
  std::thread([] {
    auto *fetched = new Fetched{github_releases_fetch()};
    std::lock_guard<std::recursive_mutex> lock(*cfg.lvgl_mutex);
    lv_async_call(fetch_done, fetched);
  }).detach();
}

void show_installed() {
  const FwInfo info = fw_info();
  std::string text = esp_app_get_description()->version;
  if (info.release) {
    text += info.release->prerelease ? "  (pre-release)" : "  (release)";
  }
  lv_label_set_text(ui_UpdateInstalled, text.c_str());
}

void show_list() {
  show_page(Page::LIST);
  show_installed();
  list_fill(); // what the last fetch found, while this one runs
  lv_label_set_text(ui_UpdateStatus, "Checking GitHub...");
  fetch();
}

///////////////////////////////////////////////////////////////////////////////
// Pick page

void show_pick(size_t index) {
  picked = index;
  const GithubRelease &r = releases[index];
  show_page(Page::PICK);
  lv_label_set_text(ui_UpdatePickTitle, r.tag.c_str());
  lv_label_set_text(ui_UpdatePickDateValue, day(r.published).c_str());
  lv_label_set_text(ui_UpdatePickKindValue, r.prerelease ? "Pre-release" : "Release");
  lv_label_set_text(ui_UpdatePickSizeValue,
                    r.url.empty() || r.size == 0 ? "--" : megabytes(r.size).c_str());
  lv_label_set_text(ui_UpdatePickNotes, r.notes.empty() ? "No notes." : r.notes.c_str());
  lv_obj_scroll_to_y(ui_UpdateNotesBox, 0, LV_ANIM_OFF);
  const char *status = r.url.empty() ? "This release has no firmware file to install."
                       : is_installed(r)
                           ? "Installed already. Installing it again restarts the HMI."
                           : "Installing restarts the HMI.";
  lv_label_set_text(ui_UpdatePickStatus, status);
  cfg.use_group(pick_group);
  lv_group_focus_obj(r.url.empty() ? ui_UpdatePickBack : ui_UpdateInstallButton);
}

void install_cb(lv_event_t *) {
  if (picked >= releases.size() || releases[picked].url.empty()) {
    cfg.refuse();
    return;
  }
  if (!github_ota_start(releases[picked])) {
    cfg.refuse(); // one is running already
    return;
  }
  logger.info("Installing {} from GitHub", releases[picked].tag);
  show_run();
}

///////////////////////////////////////////////////////////////////////////////
// Run page

void run_refresh() {
  const OtaStatus s = github_ota_status();
  const bool failed = s.stage == OtaStage::FAILED;
  const bool done = s.stage == OtaStage::DONE;
  lv_label_set_text(ui_UpdateRunTitle, failed ? "Update failed"
                                       : done ? fmt::format("Installed {}", s.tag).c_str()
                                              : fmt::format("Installing {}", s.tag).c_str());
  std::string message = s.message;
  if (done && !cfg.may_restart()) {
    message = "Installed. The HMI restarts when the chair stops driving.";
  }
  lv_label_set_text(ui_UpdateRunStatus, message.c_str());

  const int32_t pct = hmi::format::progress_pct(s.done, s.total);
  lv_bar_set_value(ui_UpdateProgressBar, done ? 100 : pct, LV_ANIM_OFF);
  std::array<char, hmi::format::PROGRESS_TEXT_SIZE> progress{}; // "" while total is 0
  if (s.total > 0) {
    hmi::format::progress_text(s.done, s.total, done ? 100 : pct, progress);
  }
  lv_label_set_text(ui_UpdateProgressLabel, progress.data());
  if (s.log != lv_label_get_text(ui_UpdateRunLog)) {
    lv_label_set_text(ui_UpdateRunLog, s.log.c_str());
    lv_obj_update_layout(ui_UpdateLogBox);
    lv_obj_scroll_to_y(ui_UpdateLogBox, LV_COORD_MAX, LV_ANIM_OFF); // the newest line
  }

  // "Back" once there is nothing left to wait for.
  const bool shown = !lv_obj_has_flag(ui_UpdateRunButton, LV_OBJ_FLAG_HIDDEN);
  if (failed != shown) {
    lv_obj_set_flag(ui_UpdateRunButton, LV_OBJ_FLAG_HIDDEN, !failed);
    lv_group_remove_all_objs(run_group);
    if (failed) {
      lv_group_add_obj(run_group, ui_UpdateRunButton);
    }
    cfg.use_group(run_group);
    if (failed) {
      lv_group_focus_obj(ui_UpdateRunButton);
    }
  }
}

void show_run() {
  show_page(Page::RUN);
  lv_obj_add_flag(ui_UpdateRunButton, LV_OBJ_FLAG_HIDDEN);
  lv_group_remove_all_objs(run_group);
  cfg.use_group(run_group);
  run_refresh();
}

// On the LVGL task, whatever screen is up: the run page follows the install,
// and a finished install restarts the HMI.
void poll_cb(lv_timer_t *) {
  if (lv_screen_active() == ui_UpdateScreen && page == Page::RUN) {
    run_refresh();
  }
  if (github_ota_status().stage != OtaStage::DONE) {
    return;
  }
  const int64_t now = esp_timer_get_time();
  if (restart_at_us == 0) {
    restart_at_us = now + kRestartDelayUs;
  }
  if (now >= restart_at_us && cfg.may_restart()) {
    logger.warn("Update installed: restarting into it");
    esp_restart();
  }
}

void button_setup(lv_obj_t *button, lv_event_cb_t click, lv_group_t *group) {
  cfg.focus_ring(button);
  cfg.mirror_states(button);
  lv_obj_add_event_cb(button, click, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(button, walk_key_cb, LV_EVENT_KEY, group);
}

} // namespace

void update_ui_init(const UpdateUiConfig &config) {
  cfg = config;
  list_group = lv_group_create();
  pick_group = lv_group_create();
  run_group = lv_group_create();

  lv_group_add_obj(pick_group, ui_UpdatePickBack);
  lv_group_add_obj(pick_group, ui_UpdateInstallButton);
  button_setup(
      ui_UpdatePickBack, [](lv_event_t *) { show_list(); }, pick_group);
  button_setup(ui_UpdateInstallButton, install_cb, pick_group);
  button_setup(
      ui_UpdateRunButton, [](lv_event_t *) { show_list(); }, run_group);

  // One line each, whatever GitHub or the network answers: the run page's log
  // has the whole text.
  for (lv_obj_t *status : {ui_UpdateStatus, ui_UpdatePickStatus, ui_UpdateRunStatus}) {
    lv_label_set_long_mode(status, LV_LABEL_LONG_MODE_DOTS);
  }

  // ReleaseRowTemplate is only there so SquareLine exports the component.
  lv_obj_delete(ui_ReleaseRowTemplate);
  ui_ReleaseRowTemplate = nullptr;

  lv_timer_create(poll_cb, kPollMs, nullptr);
}

void update_ui_on_load() {
  const OtaStage stage = github_ota_status().stage;
  if (stage == OtaStage::CONNECTING || stage == OtaStage::DOWNLOADING ||
      stage == OtaStage::VERIFYING || stage == OtaStage::DONE) {
    show_run();
  } else {
    show_list();
  }
}
