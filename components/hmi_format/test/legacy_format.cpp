// Verbatim copies of the pre-move formatters (see legacy_format.hpp). Each body is the source
// named above it, character for character, except where a comment says what stands in for an
// LVGL call: the label setters are replaced by lv_snprintf into `out`, which is what
// lv_label_set_text_fmt does (lv_text_set_text_vfmt sizes the text with lv_vsnprintf and prints
// it with lv_vsnprintf), so the bytes are the ones the label got.

#include "legacy_format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// LVGL's printf (managed_components/lvgl__lvgl/src/stdlib/builtin/lv_sprintf_builtin.c, the
// one the firmware uses: CONFIG_LV_USE_BUILTIN_SPRINTF=1), linked in by the Makefile.
extern "C" int lv_snprintf(char *buffer, size_t count, const char *format, ...);

namespace {
// Stand-ins for main/hmi_rtps_spec.hpp:118-119.
namespace rammp {
inline constexpr float kMphPerMps = 2.236936f;
inline constexpr int32_t kSpeedMaxTenths = 99; // 9.9 mph, the widest the label fits
} // namespace rammp
} // namespace

namespace legacy {

// main/frag_drive_band.inc:56-62
int32_t speed_display_tenths(float mps) {
  if (!std::isfinite(mps) || mps <= 0.0f) {
    return 0;
  }
  const auto tenths = std::lround(mps * rammp::kMphPerMps * 10.0f);
  return static_cast<int32_t>(std::clamp<long>(tenths, 0, rammp::kSpeedMaxTenths));
}

// main/frag_drive_band.inc:66-72; lv_label_set_text_fmt(label, ...) -> lv_snprintf(out, ...)
void speed_label_text(int32_t subject_value, char *out, size_t out_size) {
  // int32_t is long on this target, so narrow explicitly rather than hand a
  // long to a "%d" that -Werror=format would reject
  const int tenths =
      static_cast<int>(std::clamp<int32_t>(subject_value, 0, rammp::kSpeedMaxTenths));
  lv_snprintf(out, out_size, "%d.%d", tenths / 10, tenths % 10);
}

// main/frag_settings_ui.inc:152-180
void stepper_format(const StepperSpec &spec, int32_t raw, char *out, size_t out_size) {
  if (spec.names != nullptr && raw >= spec.min_value && raw <= spec.max_value) {
    lv_snprintf(out, out_size, "%s", spec.names[raw - spec.min_value]);
    return;
  }
  char number[16];
  if (spec.decimals == 0) {
    lv_snprintf(number, sizeof(number), "%d", static_cast<int>(raw));
  } else {
    int32_t scale = 1;
    for (uint8_t i = 0; i < spec.decimals; i++) {
      scale *= 10;
    }
    // Split on the magnitude and put the sign back by hand, so -5 with one
    // decimal reads "-0.5" rather than the "0.5" an integer division would give.
    const bool negative = raw < 0;
    const int32_t magnitude = negative ? -raw : raw;
    char digits[12];
    const uint8_t count =
        spec.decimals < sizeof(digits) - 1 ? spec.decimals : (uint8_t)(sizeof(digits) - 1);
    int32_t frac = magnitude % scale;
    for (int i = count - 1; i >= 0; i--) {
      digits[i] = static_cast<char>('0' + frac % 10);
      frac /= 10;
    }
    digits[count] = '\0';
    lv_snprintf(number, sizeof(number), "%s%d.%s", negative ? "-" : "",
                static_cast<int>(magnitude / scale), digits);
  }
  lv_snprintf(out, out_size, "%s%s", number, spec.unit != nullptr ? spec.unit : "");
}

// main/frag_settings_ui.inc:227-237
void seat_format(const StepperSpec &spec, int32_t raw, char *out, size_t out_size) {
  if (raw == kValueUnknown) {
    lv_snprintf(out, out_size, "--");
    return;
  }
  char number[16];
  const StepperSpec bare{spec.short_name, spec.label,    spec.min_value, spec.max_value,
                         spec.step,       spec.decimals, nullptr};
  stepper_format(bare, raw, number, sizeof(number));
  lv_snprintf(out, out_size, "%s %s", number, spec.unit != nullptr ? spec.unit : "");
}

// main/frag_settings_ui.inc:249-267, the part of seat_angle_refresh before its first lv_*
// call that is not a label setter: `spec` is seat_axis_format[seat_selected_axis] and `raw`
// the selected axis' subject; the two lv_label_set_text calls are left out, the texts kept.
void seat_angle_texts(const StepperSpec &spec, int32_t raw, char (&text)[32], char (&footer)[40]) {
  const bool degrees = spec.unit != nullptr && strcmp(spec.unit, "deg") == 0;
  const StepperSpec bare{spec.short_name, spec.label,    spec.min_value, spec.max_value,
                         spec.step,       spec.decimals, nullptr};
  char number[16];
  if (raw == kValueUnknown) {
    lv_snprintf(text, sizeof(text), "--");
  } else {
    stepper_format(bare, raw, number, sizeof(number));
    lv_snprintf(text, sizeof(text), "%s%s", number, degrees ? "°" : "");
  }

  stepper_format(bare, spec.max_value, number, sizeof(number));
  lv_snprintf(footer, sizeof(footer), degrees ? "of %s°" : "of %s %s", number,
              spec.unit != nullptr ? spec.unit : "");
}

// main/frag_clock.inc:8
bool clock_plausible(const std::tm &t) { return t.tm_year >= 125; }

// main/frag_clock.inc:46-51, the clock_valid branch of clock_poll_cb after gmtime_r
void clock_text(const std::tm &t, char (&text)[8]) {
  snprintf(text, sizeof(text), "%02d:%02d", t.tm_hour, t.tm_min);
}

// main/frag_clock.inc:65-67
const char *link_text(NetLink link) { return link == NetLink::WIFI ? "BT · WI-FI" : "BT · ETH"; }

// main/frag_diag.inc:123-124; lv_label_set_text_fmt(label, ...) -> lv_snprintf(out, ...)
void diag_rate_text(int32_t rate, char *out, size_t out_size) {
  lv_snprintf(out, out_size, "%d.%d Hz - Live", static_cast<int>(rate / 10),
              static_cast<int>(rate % 10));
}

} // namespace legacy
