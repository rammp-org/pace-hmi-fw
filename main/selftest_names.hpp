#pragma once
/// @file selftest_names.hpp
/// @brief The self test's names for the things it reports: the reset reason. Moved from
///        selftest.cpp unchanged (the self test's own file has no room left under the L0
///        ratchet's line count for the checks hazard fix C4 adds).

#include "esp_system.h"

/// The reset reason as the self test's sys.clean_reset detail names it ("TASK WDT").
const char *reset_reason_name(esp_reset_reason_t reason);
