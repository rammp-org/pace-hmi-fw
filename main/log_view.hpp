#pragma once

/**
 * @file log_view.hpp
 * @brief The LogScreen: TextArea1 shows what log_capture keeps of the serial
 *        output, newest at the bottom, errors red and warnings amber.
 *
 * Everything here runs on the LVGL task. Lines reach the widget the way all
 * firmware state does (CLAUDE.md): a poll timer bumps a subject and an observer
 * bound to the text area does the widget work.
 */

#include "lvgl.h"

/// Once, after ui_init and before the LVGL task starts.
void log_view_init();

/// The joystick's group while the LogScreen is up: up/down scroll a page.
lv_group_t *log_view_group();

/// For LV_EVENT_SCREEN_LOADED on ui_LogScreen: brings the text up to date and
/// jumps to the newest line.
void log_view_on_load();
