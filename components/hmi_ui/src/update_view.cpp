// UpdateView: the UpdateScreen (moved from main/update_ui.cpp). The release list, the install,
// the worker thread and the restart are main's, through the Config.

#include "hmi_ui/update_view.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "hmi_format/update.hpp"
#include "ui.h"

#include "components/ui_comp_releaserow.h"

///////////////////////////////////////////////////////////////////////////////
// Words

// "2026-09-30" -> "30 Sep 2026"; the text itself when it is not a date
std::string hmi::ui::UpdateView::day(const std::string &iso) {
  std::array<char, format::DAY_TEXT_SIZE> text{};
  return format::day_text(iso, text) ? std::string(text.data()) : iso;
}

bool hmi::ui::UpdateView::is_installed(const ota::Release &r) const {
  const FirmwareInfo info = config_.firmware();
  return r.tag == config_.identity().version || (info.release && info.release->tag == r.tag);
}

std::string hmi::ui::UpdateView::megabytes(size_t bytes) {
  std::array<char, format::MEGABYTES_TEXT_SIZE> text{};
  format::megabytes_text(bytes, text);
  return text.data();
}

///////////////////////////////////////////////////////////////////////////////
// Pages

void hmi::ui::UpdateView::show_page(Page next) {
  page_ = next;
  lv_obj_set_flag(ui_UpdatePickPanel, LV_OBJ_FLAG_HIDDEN, next != Page::PICK);
  lv_obj_set_flag(ui_UpdateRunPanel, LV_OBJ_FLAG_HIDDEN, next != Page::RUN);
}

// Up and down walk the page's group (the one the focused object is in); left is
// "back", as on the seat page.
void hmi::ui::UpdateView::walk_key_cb(lv_event_t *e) {
  auto *view = static_cast<UpdateView *>(lv_event_get_user_data(e));
  lv_group_t *group = lv_obj_get_group(lv_event_get_current_target_obj(e));
  switch (lv_event_get_key(e)) {
  case LV_KEY_UP:
    lv_group_focus_prev(group);
    break;
  case LV_KEY_DOWN:
    lv_group_focus_next(group);
    break;
  case LV_KEY_LEFT:
    if (view->page_ == Page::PICK) {
      view->show_list();
    }
    break;
  default:
    break;
  }
}

///////////////////////////////////////////////////////////////////////////////
// List page

void hmi::ui::UpdateView::list_clear() {
  // Backwards, and by index: deleting a row renumbers the ones after it.
  for (int32_t i = static_cast<int32_t>(lv_obj_get_child_count(ui_UpdateList)) - 1; i >= 0; i--) {
    lv_obj_delete(lv_obj_get_child(ui_UpdateList, i));
  }
}

// A row: its place in UpdateList is its place in the releases (list_fill adds
// one row per release, in order, to an emptied list).
void hmi::ui::UpdateView::row_click_cb(lv_event_t *e) {
  auto *view = static_cast<UpdateView *>(lv_event_get_user_data(e));
  const auto index = static_cast<size_t>(lv_obj_get_index(lv_event_get_current_target_obj(e)));
  if (index < view->config_.releases->size()) {
    view->show_pick(index);
  }
}

void hmi::ui::UpdateView::list_fill() {
  list_clear();
  const std::vector<ota::Release> &releases = *config_.releases;
  for (size_t i = 0; i < releases.size(); i++) {
    const ota::Release &r = releases[i];
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
    hooks_.claim_clicks(row);
    hooks_.mirror_states(row);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(row, walk_key_cb, LV_EVENT_KEY, this);
    lv_group_add_obj(list_group_, row);
  }
  hooks_.use_group(list_group_); // puts the burger key back after the rows
  if (!releases.empty()) {
    lv_group_focus_obj(lv_obj_get_child(ui_UpdateList, 0));
  }
}

void hmi::ui::UpdateView::list_status(const ReleaseList &result) {
  if (!result.ok) {
    lv_label_set_text(ui_UpdateStatus, result.error.c_str());
  } else if (result.releases.empty()) {
    lv_label_set_text(ui_UpdateStatus, "No releases published yet.");
  } else {
    std::array<char, format::RELEASE_COUNT_TEXT_SIZE> text{};
    format::release_count_text(result.releases.size(), text);
    lv_label_set_text(ui_UpdateStatus, text.data());
  }
}

void hmi::ui::UpdateView::fetch_done(void *data) {
  std::unique_ptr<Fetched> fetched(static_cast<Fetched *>(data));
  UpdateView *view = fetched->view;
  ReleaseList &result = fetched->result;
  view->fetching_ = false;
  const bool shown = lv_screen_active() == ui_UpdateScreen && view->page_ == Page::LIST;
  if (shown) {
    view->list_status(result);
  }
  if (result.ok) {
    *view->config_.releases = std::move(result.releases); // a failed fetch keeps the last list
  }
  if (shown) {
    if (!result.ok) {
      view->hooks_.refuse();
    }
    view->list_fill();
  }
}

void hmi::ui::UpdateView::fetch() {
  if (fetching_) {
    return; // the one already out answers this visit too
  }
  fetching_ = true;
  config_.spawn([this] {
    auto fetched = std::make_unique<Fetched>(Fetched{this, config_.fetch()});
    config_.post(fetch_done, fetched.release());
  });
}

void hmi::ui::UpdateView::show_installed() {
  const FirmwareInfo info = config_.firmware();
  std::string text = config_.identity().version;
  if (info.release) {
    text += info.release->prerelease ? "  (pre-release)" : "  (release)";
  }
  lv_label_set_text(ui_UpdateInstalled, text.c_str());
}

void hmi::ui::UpdateView::show_list() {
  show_page(Page::LIST);
  show_installed();
  list_fill(); // what the last fetch found, while this one runs
  lv_label_set_text(ui_UpdateStatus, "Checking GitHub...");
  fetch();
}

///////////////////////////////////////////////////////////////////////////////
// Pick page

void hmi::ui::UpdateView::show_pick(size_t index) {
  picked_ = index;
  const ota::Release &r = (*config_.releases)[index];
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
  hooks_.use_group(pick_group_);
  lv_group_focus_obj(r.url.empty() ? ui_UpdatePickBack : ui_UpdateInstallButton);
}

void hmi::ui::UpdateView::install_cb(lv_event_t *e) {
  auto *view = static_cast<UpdateView *>(lv_event_get_user_data(e));
  const std::vector<ota::Release> &releases = *view->config_.releases;
  if (view->picked_ >= releases.size() || releases[view->picked_].url.empty()) {
    view->hooks_.refuse();
    return;
  }
  if (!view->config_.install(releases[view->picked_])) {
    view->hooks_.refuse(); // one is running already
    return;
  }
  view->show_run();
}

///////////////////////////////////////////////////////////////////////////////
// Run page

void hmi::ui::UpdateView::run_refresh() {
  const InstallStatus s = config_.status();
  const bool failed = s.stage == InstallStage::FAILED;
  const bool done = s.stage == InstallStage::DONE;
  lv_label_set_text(ui_UpdateRunTitle, failed ? "Update failed"
                                       : done ? ("Installed " + s.tag).c_str()
                                              : ("Installing " + s.tag).c_str());
  std::string message = s.message;
  if (done && !hooks_.may_restart()) {
    message = "Installed. The HMI restarts when the chair stops driving.";
  }
  lv_label_set_text(ui_UpdateRunStatus, message.c_str());

  const int32_t pct = format::progress_pct(s.done, s.total);
  lv_bar_set_value(ui_UpdateProgressBar, done ? 100 : pct, LV_ANIM_OFF);
  std::array<char, format::PROGRESS_TEXT_SIZE> progress{}; // "" while total is 0
  if (s.total > 0) {
    format::progress_text(s.done, s.total, done ? 100 : pct, progress);
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
    lv_group_remove_all_objs(run_group_);
    if (failed) {
      lv_group_add_obj(run_group_, ui_UpdateRunButton);
    }
    hooks_.use_group(run_group_);
    if (failed) {
      lv_group_focus_obj(ui_UpdateRunButton);
    }
  }
}

void hmi::ui::UpdateView::show_run() {
  show_page(Page::RUN);
  lv_obj_add_flag(ui_UpdateRunButton, LV_OBJ_FLAG_HIDDEN);
  lv_group_remove_all_objs(run_group_);
  hooks_.use_group(run_group_);
  run_refresh();
}

// On the LVGL task, whatever screen is up: the run page follows the install.
void hmi::ui::UpdateView::poll() {
  if (lv_screen_active() == ui_UpdateScreen && page_ == Page::RUN) {
    run_refresh();
  }
}

// The key callback finds the button's group itself (walk_key_cb).
void hmi::ui::UpdateView::button_setup(lv_obj_t *button, lv_event_cb_t click) {
  hooks_.focus_ring(button);
  hooks_.mirror_states(button);
  lv_obj_add_event_cb(button, click, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(button, walk_key_cb, LV_EVENT_KEY, this);
}

void hmi::ui::UpdateView::init(const Hooks &hooks) {
  hooks_ = hooks;
  list_group_ = lv_group_create();
  pick_group_ = lv_group_create();
  run_group_ = lv_group_create();

  lv_group_add_obj(pick_group_, ui_UpdatePickBack);
  lv_group_add_obj(pick_group_, ui_UpdateInstallButton);
  button_setup(ui_UpdatePickBack, [](lv_event_t *e) {
    static_cast<UpdateView *>(lv_event_get_user_data(e))->show_list();
  });
  button_setup(ui_UpdateInstallButton, install_cb);
  button_setup(ui_UpdateRunButton, [](lv_event_t *e) {
    static_cast<UpdateView *>(lv_event_get_user_data(e))->show_list();
  });

  // One line each, whatever GitHub or the network answers: the run page's log
  // has the whole text.
  for (lv_obj_t *status : {ui_UpdateStatus, ui_UpdatePickStatus, ui_UpdateRunStatus}) {
    lv_label_set_long_mode(status, LV_LABEL_LONG_MODE_DOTS);
  }

  // ReleaseRowTemplate is only there so SquareLine exports the component.
  lv_obj_delete(ui_ReleaseRowTemplate);
  ui_ReleaseRowTemplate = nullptr;
}

void hmi::ui::UpdateView::on_load() {
  const InstallStage stage = config_.status().stage;
  if (stage == InstallStage::CONNECTING || stage == InstallStage::DOWNLOADING ||
      stage == InstallStage::VERIFYING || stage == InstallStage::DONE) {
    show_run();
  } else {
    show_list();
  }
}
