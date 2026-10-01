#pragma once

/**
 * @file update_ui.hpp
 * @brief The UpdateScreen (Settings > Firmware update in the burger menu):
 *        the releases published on GitHub, and installing one of them.
 *
 * Three pages on one screen:
 *   list   the firmware installed, then every release, newest first; the one
 *          running is marked "Installed"
 *   pick   one release: date, kind, size, its notes, and Install
 *   run    the install's progress and log; the HMI restarts when it is done
 *
 * The list is fetched each time the screen opens, on a worker thread
 * (github_ota.hpp does the talking). An install runs on its own thread and
 * outlives the screen: leave it and the restart still comes, though never
 * while the chair is driving.
 */

#include <mutex>

#include "lvgl.h"

struct UpdateUiConfig {
  std::recursive_mutex *lvgl_mutex;
  /// Hands the joystick a group on this screen (the burger key is appended).
  void (*use_group)(lv_group_t *group);
  /// The cursor ring of a plain button.
  void (*focus_ring)(lv_obj_t *button);
  /// Carries PRESSED / CHECKED / FOCUSED down to a widget's children.
  void (*mirror_states)(lv_obj_t *obj);
  /// Makes `obj`, and nothing inside it, the click target.
  void (*claim_clicks)(lv_obj_t *obj);
  /// "Can't do that": the double click, heard and felt.
  void (*refuse)();
  /// False while the HMI must not restart (the chair is driving).
  bool (*may_restart)();
};

/// Once, after ui_init and before the LVGL task starts.
void update_ui_init(const UpdateUiConfig &config);

/// The UpdateScreen came up: the list (fetched again), or the install running.
void update_ui_on_load();
