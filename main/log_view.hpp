#pragma once

/**
 * @file log_view.hpp
 * @brief The LogScreen: TextArea1 shows what log_capture keeps of the serial
 *        output, one line per log line, newest at the bottom, errors red and
 *        warnings amber. The Oldest/Newest buttons jump to either end. The
 *        UpdateScreen's UpdateLog shows the same, following the newest line.
 *
 * Everything here runs on the LVGL task. Lines reach the widget the way all
 * firmware state does (CLAUDE.md): a poll timer bumps a subject and an observer
 * bound to the text area does the widget work.
 */

#include "lvgl.h"

/// Once, after ui_init and before the LVGL task starts.
void log_view_init();

/// The joystick's group while the LogScreen is up: up/down scroll a page,
/// left/right a page sideways.
lv_group_t *log_view_group();

/// For LV_EVENT_SCREEN_LOADED on ui_LogScreen: brings the text up to date and
/// jumps to the newest line.
void log_view_on_load();

/// The UpdateScreen's log: each time that screen is built (it is built on
/// demand), with its UpdateLog textarea. The pane goes when the screen does.
void log_view_attach_update(lv_obj_t *frame);

/// For LV_EVENT_SCREEN_LOADED on ui_UpdateScreen.
void log_view_update_on_load();
