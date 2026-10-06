#pragma once
// The rows the firmware formats, copied as test inputs (not as goldens):
//   settings rows   main/settings_spec.h:32-43 with the names of main/frag_settings_ui.inc:89-110
//   seat axes       RAMMP_SEAT_AXIS_TABLE, external/rammp-rtps/.../joystick_message.hpp:101-105
//                   (with its unit: seat buttons and the adjustment page; with unit nullptr:
//                   the DEBUG ACTUATORS rows, main/frag_settings_ui.inc:608-611)
//   diagnostics     the decimals of RAMMP_DIAG_TABLE (joystick_message.hpp:108-111), formatted
//                   through a StepperSpec of {nullptr, nullptr, 0, 0, 0, decimals, nullptr}
//                   (main/frag_diag.inc:86)
// A row here is spec-neutral; make_spec() builds either StepperSpec (the legacy copy or the
// component's) from it, so both are fed the same thing.

#include <array>
#include <cstdint>

namespace fmt_test {

struct Row {
  const char *short_name;
  const char *label;
  int32_t min_value;
  int32_t max_value;
  int32_t step;
  uint8_t decimals;
  const char *unit;
  const char *const *names;
};

template <class Spec> constexpr Spec make_spec(const Row &r) {
  Spec spec{r.short_name, r.label, r.min_value, r.max_value, r.step, r.decimals, r.unit};
  spec.names = r.names;
  return spec;
}

inline constexpr const char *THEME_NAMES[] = {"Dark", "Day"};
inline constexpr const char *ON_OFF_NAMES[] = {"Off", "On"};
inline constexpr const char *MIRROR_NAMES[] = {"Normal", "Mirror"};
inline constexpr const char *SWAP_NAMES[] = {"Normal", "Swap"};
inline constexpr const char *NETWORK_NAMES[] = {"Ethernet", "WiFi"};

inline constexpr std::array SETTINGS_ROWS{
    Row{"D1", "Brightness", 5, 100, 5, 0, "%", nullptr},
    Row{"D2", "Theme", 0, 1, 1, 0, "", THEME_NAMES},
    Row{"D3", "Menu slide", 0, 1, 1, 0, "", ON_OFF_NAMES},
    Row{"D4", "Flip screen", 0, 1, 1, 0, "", ON_OFF_NAMES},
    Row{"J1", "Stick sensitivity", 1, 10, 1, 0, "", nullptr},
    Row{"J2", "Speed sensitivity", 1, 10, 1, 1, "x", nullptr},
    Row{"J3", "Stick left/right", 0, 1, 1, 0, "", MIRROR_NAMES},
    Row{"J4", "Stick fwd/back", 0, 1, 1, 0, "", MIRROR_NAMES},
    Row{"J5", "Stick axes", 0, 1, 1, 0, "", SWAP_NAMES},
    Row{"D5", "Sounds", 0, 1, 1, 0, "", ON_OFF_NAMES},
    Row{"N1", "Connection", 0, 1, 1, 0, "", NETWORK_NAMES},
};

inline constexpr std::array SEAT_ROWS{
    Row{"M1", "FB Tilt", -450, 900, 25, 1, "deg", nullptr},
    Row{"M2", "Side Tilt", -300, 300, 25, 1, "deg", nullptr},
    Row{"M3", "Elevation", 0, 2500, 50, 1, "mm", nullptr},
    Row{"M4", "Translation", 0, 500, 25, 1, "mm", nullptr},
};

inline constexpr std::array ACTUATOR_ROWS{
    Row{"M1", "FB Tilt", -450, 900, 25, 1, nullptr, nullptr},
    Row{"M2", "Side Tilt", -300, 300, 25, 1, nullptr, nullptr},
    Row{"M3", "Elevation", 0, 2500, 50, 1, nullptr, nullptr},
    Row{"M4", "Translation", 0, 500, 25, 1, nullptr, nullptr},
};

// One row per decimals value the diagnostics table uses (1 and 2), plus 0.
inline constexpr std::array DIAG_ROWS{
    Row{nullptr, nullptr, 0, 0, 0, 0, nullptr, nullptr},
    Row{nullptr, nullptr, 0, 0, 0, 1, nullptr, nullptr},
    Row{nullptr, nullptr, 0, 0, 0, 2, nullptr, nullptr},
};

// The buffer each caller hands the formatter (main/frag_*.inc), so truncation is the real one.
inline constexpr std::size_t SETTING_TEXT_SIZE = 24; // setting_value_observer
inline constexpr std::size_t SEAT_TEXT_SIZE = 32;    // seat_button_value_observer
inline constexpr std::size_t DIAG_TEXT_SIZE = 24;    // diag_value_observer

} // namespace fmt_test
