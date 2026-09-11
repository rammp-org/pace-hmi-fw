#pragma once

/**
 * @file log_view.hpp
 * @brief The LogScreen: TextArea1 shows what log_capture keeps of the serial
 *        output, one line per log line, newest at the bottom, errors red and
 *        warnings amber. The Oldest/Newest buttons jump to either end.
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
