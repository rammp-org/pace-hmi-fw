// The one LVGL symbol lv_sprintf_builtin.c needs besides itself: lv_strnlen, copied from
// managed_components/lvgl__lvgl/src/stdlib/builtin/lv_string_builtin.c:194-200 (LVGL 9.5.0),
// so the test links LVGL's real printf without the rest of LVGL.

#include <cstddef>

extern "C" size_t lv_strnlen(const char *str, size_t max_len) {
  size_t i = 0;
  while (i < max_len && str[i] != '\0') {
    i++;
  }
  return i;
}
