#pragma once
// Characterisation oracle: the screen formatters exactly as main/ had them before they moved to
// components/hmi_format (dev_refactor fa3f13a). legacy_format.cpp holds verbatim copies of
// their bodies, compiled against LVGL's own lv_snprintf (lv_sprintf_builtin.c, linked by the
// Makefile), so the golden tables in goldens.hpp record what the firmware drew. Test-only code:
// nothing in the firmware links it.

#include <cstddef>
#include <cstdint>
#include <ctime>

namespace legacy {

// Stand-in for StepperSpec, main/frag_settings_ui.inc:26-39 (verbatim layout).
struct StepperSpec {
  const char *short_name; // "S1", "M1"
  const char *label;      // "Brightness"
  int32_t min_value;
  int32_t max_value;
  int32_t step;
  uint8_t decimals; // display only: 126 with 1 decimal shows "12.6"
  const char *unit; // appended to the value; nullptr = none
  // What each value reads, min_value first, for a row that picks between
  // named options rather than a number; nullptr = a number.
  const char *const *names = nullptr;
  // Refused while the chair is driving (see setting_step).
  bool locked_only = false;
};

// Stand-in for NetLink, main/rtps_comms.hpp:16-19.
enum class NetLink : uint8_t {
  ETHERNET,
  WIFI,
};

// main/frag_settings_ui.inc:135
inline constexpr int32_t kValueUnknown = INT32_MIN;

// main/frag_drive_band.inc:56-62
int32_t speed_display_tenths(float mps);
// main/frag_drive_band.inc:66-72 (speed_label_observer: the text it hands the label)
void speed_label_text(int32_t subject_value, char *out, size_t out_size);
// main/frag_settings_ui.inc:152-180
void stepper_format(const StepperSpec &spec, int32_t raw, char *out, size_t out_size);
// main/frag_settings_ui.inc:227-237
void seat_format(const StepperSpec &spec, int32_t raw, char *out, size_t out_size);
// main/frag_settings_ui.inc:249-267 (seat_angle_refresh: its two texts, before any lv_*)
void seat_angle_texts(const StepperSpec &spec, int32_t raw, char (&text)[32], char (&footer)[40]);
// main/frag_clock.inc:8
bool clock_plausible(const std::tm &t);
// main/frag_clock.inc:46-51 (clock_poll_cb: the text, when the clock is valid)
void clock_text(const std::tm &t, char (&text)[8]);
// main/frag_clock.inc:65-67
const char *link_text(NetLink link);
// main/frag_diag.inc:123-124 (diag_freq_observer: the live text)
void diag_rate_text(int32_t rate, char *out, size_t out_size);

} // namespace legacy
