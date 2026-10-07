#pragma once
// Test-only: the header name main/joystick_cal.cpp includes, found before LVGL's own lvgl.h
// because this folder is on the host app's include path. The fake is C++ and lives in
// fake_lvgl.hpp (C++ goes in .hpp, as selftest_spec.hpp); this file only forwards to it.
#ifdef __cplusplus
#include "fake_lvgl.hpp"
#endif
