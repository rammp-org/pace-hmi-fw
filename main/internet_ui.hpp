#pragma once

/**
 * @file internet_ui.hpp
 * @brief The InternetScreen ("Internet Settings" in the burger menu): which
 *        link RTPS runs over, which WiFi network, and what the link is doing.
 *
 * Three pages on one screen:
 *   main      Ethernet / WiFi, the saved network, status, "Restart to apply"
 *   networks  what a scan found, strongest first
 *   password  a text box and an on-screen keyboard; OK tries the network
 *
 * The link is chosen at boot (rtps_comms_start), so picking the other one only
 * saves the choice and offers a restart. A network is saved only once it has
 * been joined: rtps_comms_wifi_join.
 *
 * Everything here runs on the LVGL task except the scan and the join, which
 * block for seconds and so run on a worker thread that posts its result back.
 */

#include <mutex>

#include "lvgl.h"

struct InternetUiConfig {
  std::recursive_mutex *lvgl_mutex;
  /// NetLink as an int (0 Ethernet, 1 WiFi). Setting it saves it.
  lv_subject_t *connection;
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
};

/// Once, after ui_init and before the LVGL task starts.
void internet_ui_init(const InternetUiConfig &config);

/// The InternetScreen came up: the main page, the cursor on the link in use.
void internet_ui_on_load();
