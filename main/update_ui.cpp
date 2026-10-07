#include "update_ui.hpp"

// The UpdateView's one instance and what it does through main: the release list and the install
// (github_ota), the firmware info, the worker thread, the hand-back to the LVGL task, and the
// restart once an install is done (never under a driving chair: cfg.may_restart).

#include <algorithm>
#include <functional>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "esp_app_desc.h"
#include "esp_pthread.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "format.hpp"
#include "fw_info.hpp"
#include "github_ota.hpp"
#include "hmi_ui/update_view.hpp"
#include "logger.hpp"
#include "lvgl.h"

///////////////////////////////////////////////////////////////////////////////
// What main does for the view

namespace {

espp::Logger logger({.tag = "update_ui", .level = espp::Logger::Verbosity::INFO});

UpdateUiConfig cfg;

// The view's list, kept at namespace scope here so its construction and
// destruction stay where G4's baseline has them: what the rows stand for, in
// row order.
std::vector<hmi::ota::Release> releases;

// When an install that has finished restarts the HMI; 0 until it has.
int64_t restart_at_us = 0;

constexpr uint32_t kPollMs = 250;
constexpr int64_t kRestartDelayUs = 3'000'000;  // long enough to read "Installed"
constexpr size_t kWorkerStackBytes = 12 * 1024; // TLS with certificate checks

static_assert(static_cast<int>(OtaStage::IDLE) == static_cast<int>(hmi::ui::InstallStage::IDLE));
static_assert(static_cast<int>(OtaStage::CONNECTING) ==
              static_cast<int>(hmi::ui::InstallStage::CONNECTING));
static_assert(static_cast<int>(OtaStage::DOWNLOADING) ==
              static_cast<int>(hmi::ui::InstallStage::DOWNLOADING));
static_assert(static_cast<int>(OtaStage::VERIFYING) ==
              static_cast<int>(hmi::ui::InstallStage::VERIFYING));
static_assert(static_cast<int>(OtaStage::DONE) == static_cast<int>(hmi::ui::InstallStage::DONE));
static_assert(static_cast<int>(OtaStage::FAILED) ==
              static_cast<int>(hmi::ui::InstallStage::FAILED));

hmi::ota::Release to_view(GithubRelease &&r) {
  return {.tag = std::move(r.tag),
          .prerelease = r.prerelease,
          .published = std::move(r.published),
          .notes = std::move(r.notes),
          .url = std::move(r.url),
          .size = r.size,
          .sha256 = std::move(r.sha256)};
}

hmi::ui::ReleaseList fetch() {
  GithubReleases found = github_releases_fetch();
  hmi::ui::ReleaseList list{.ok = found.ok, .error = std::move(found.error), .releases = {}};
  list.releases.reserve(found.releases.size());
  std::transform(found.releases.begin(), found.releases.end(), std::back_inserter(list.releases),
                 [](GithubRelease &r) { return to_view(std::move(r)); });
  return list;
}

bool install(const hmi::ota::Release &r) {
  const GithubRelease release{.tag = r.tag,
                              .prerelease = r.prerelease,
                              .published = r.published,
                              .notes = r.notes,
                              .url = r.url,
                              .size = r.size,
                              .sha256 = r.sha256};
  if (!github_ota_start(release)) {
    return false; // one is running already
  }
  logger.info("Installing {} from GitHub", release.tag);
  return true;
}

hmi::ui::InstallStatus status() {
  OtaStatus s = github_ota_status();
  return {.stage = static_cast<hmi::ui::InstallStage>(s.stage),
          .tag = std::move(s.tag),
          .done = s.done,
          .total = s.total,
          .message = std::move(s.message),
          .log = std::move(s.log)};
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
  return {.version = desc->version, .date = desc->date, .time = desc->time, .commit = ""};
}

// The worker the list request runs on: a detached thread with the stack TLS
// with certificate checks needs.
void spawn(std::function<void()> job) {
  esp_pthread_cfg_t thread_cfg = esp_pthread_get_default_config();
  thread_cfg.stack_size = kWorkerStackBytes;
  thread_cfg.thread_name = "update_ui";
  esp_pthread_set_cfg(&thread_cfg);
  std::thread(std::move(job)).detach();
}

// Back to the LVGL task. The worker takes the LVGL lock to queue it: a
// finding against CS-UI-02 (no other task takes the LVGL lock), kept as it is.
void post(void (*fn)(void *), void *data) {
  std::lock_guard<std::recursive_mutex> lock(*cfg.lvgl_mutex);
  lv_async_call(fn, data);
}

// The one UpdateView.
constinit hmi::ui::UpdateView view{{
    .fetch = fetch,
    .install = install,
    .status = status,
    .firmware = firmware,
    .identity = identity,
    .spawn = spawn,
    .post = post,
    .releases = &releases,
}};

// On the LVGL task, whatever screen is up: the run page follows the install,
// and a finished install restarts the HMI.
void poll_cb(lv_timer_t *) {
  view.poll();
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

} // namespace

void update_ui_init(const UpdateUiConfig &config) {
  cfg = config;
  view.init({
      .use_group = config.use_group,
      .focus_ring = config.focus_ring,
      .mirror_states = config.mirror_states,
      .claim_clicks = config.claim_clicks,
      .refuse = config.refuse,
      .may_restart = config.may_restart,
  });
  lv_timer_create(poll_cb, kPollMs, nullptr);
}

void update_ui_on_load() { view.on_load(); }
