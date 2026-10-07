// Spec-table tests for components/settings/include/settings_spec.hpp (TS-UNIT-04): the invariants
// of SETTINGS_PAGES and SETTINGS_PARAMS, and that the typed tables hold exactly what the X-macro
// tables they replaced held (plan step 8, the refactor commit). The golden rows below are the old
// SETTINGS_PAGE_TABLE / SETTINGS_PARAM_TABLE lines (dev_refactor fa3f13a), copied as data.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "settings.hpp"
#include "test_case.hpp"

namespace {

struct OldPage {
  int page;
  std::string_view title;
  std::string_view instructions;
};

// P(NAME, title, instructions), in table order.
constexpr std::array<OldPage, 3> OLD_PAGES{{
    {0, "Display & sound", "Left/right or -/+ to change. Saved automatically."},
    {1, "Joystick & driving", "Left/right or -/+ to change. Saved automatically."},
    {2, "Internet", "Shown on the InternetScreen, not as rows."},
}};

struct OldParam {
  int page;
  std::string_view name; // the X-macro NAME; settings.cpp stored it lowercase
  std::string_view short_name;
  std::string_view label;
  int min_value;
  int max_value;
  int step;
  int decimals;
  std::string_view unit;
  int default_value;
};

// X(PAGE, NAME, short, label, min, max, step, decimals, unit, default), in table order;
// PAGE as its index (DISPLAY 0, STICK 1, NET 2).
constexpr std::array<OldParam, 11> OLD_PARAMS{{
    {0, "BRIGHTNESS", "D1", "Brightness", 5, 100, 5, 0, "%", 75},
    {0, "THEME", "D2", "Theme", 0, 1, 1, 0, "", 0},
    {0, "MENU_SLIDE", "D3", "Menu slide", 0, 1, 1, 0, "", 0},
    {0, "FLIP", "D4", "Flip screen", 0, 1, 1, 0, "", 0},
    {1, "STICK_SENSITIVITY", "J1", "Stick sensitivity", 1, 10, 1, 0, "", 9},
    {1, "DRIVE_SPEED", "J2", "Speed sensitivity", 1, 10, 1, 1, "x", 10},
    {1, "STICK_INVERT_X", "J3", "Stick left/right", 0, 1, 1, 0, "", 0},
    {1, "STICK_INVERT_Y", "J4", "Stick fwd/back", 0, 1, 1, 0, "", 0},
    {1, "STICK_SWAP", "J5", "Stick axes", 0, 1, 1, 0, "", 0},
    {0, "SOUNDS", "D5", "Sounds", 0, 1, 1, 0, "", 1},
    {2, "NETWORK", "N1", "Connection", 0, 1, 1, 0, "", 0},
}};

std::string lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  });
  return out;
}

} // namespace

TEST_CASE("SET-060 the typed page table holds exactly the old X-macro pages", "[settings][spec]") {
  TEST_ASSERT_EQUAL_size_t(OLD_PAGES.size(), SETTINGS_PAGES.size());
  for (std::size_t i = 0; i < OLD_PAGES.size(); i++) {
    TEST_ASSERT_EQUAL_INT(OLD_PAGES[i].page, SETTINGS_PAGES[i].id);
    TEST_ASSERT_EQUAL_STRING(std::string(OLD_PAGES[i].title).c_str(), SETTINGS_PAGES[i].title);
    TEST_ASSERT_EQUAL_STRING(std::string(OLD_PAGES[i].instructions).c_str(),
                             SETTINGS_PAGES[i].instructions);
  }
}

TEST_CASE("SET-061 the typed parameter table holds exactly the old X-macro rows",
          "[settings][spec]") {
  TEST_ASSERT_EQUAL_size_t(OLD_PARAMS.size(), SETTINGS_PARAMS.size());
  for (std::size_t i = 0; i < OLD_PARAMS.size(); i++) {
    const OldParam &o = OLD_PARAMS[i];
    const SettingsParamSpec &p = SETTINGS_PARAMS[i];
    const std::string what = std::string(o.name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(i), p.id, what.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(o.page, p.page, what.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(lower(o.name).c_str(), p.key, what.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(std::string(o.short_name).c_str(), p.short_name, what.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(std::string(o.label).c_str(), p.label, what.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(o.min_value, p.min_value, what.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(o.max_value, p.max_value, what.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(o.step, p.step, what.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(o.decimals, p.decimals, what.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(std::string(o.unit).c_str(), p.unit, what.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(o.default_value, p.default_value, what.c_str());
  }
}

TEST_CASE("SET-062 the table invariants hold: ids in order, valid unique keys, valid ranges",
          "[settings][spec]") {
  // The same checks the header static_asserts, run as a test so a report shows them (TS-UNIT-04).
  TEST_ASSERT_TRUE(settings_spec_detail::rows_in_order());
  TEST_ASSERT_TRUE(settings_spec_detail::pages_valid());
  TEST_ASSERT_TRUE(settings_spec_detail::keys_valid());
  TEST_ASSERT_TRUE(settings_spec_detail::keys_unique());
  TEST_ASSERT_TRUE(settings_spec_detail::short_names_unique());
  TEST_ASSERT_TRUE(settings_spec_detail::ranges_valid());
  TEST_ASSERT_TRUE(settings_spec_detail::texts_present());
}

TEST_CASE("SET-063 the key check rejects what the codec could not read back", "[settings][spec]") {
  TEST_ASSERT_TRUE(settings_spec_detail::is_valid_key("stick_invert_x"));
  TEST_ASSERT_FALSE(settings_spec_detail::is_valid_key(""));
  TEST_ASSERT_FALSE(settings_spec_detail::is_valid_key("two words"));
  TEST_ASSERT_FALSE(settings_spec_detail::is_valid_key("tab\there"));
  TEST_ASSERT_FALSE(settings_spec_detail::is_valid_key("Brightness"));
  TEST_ASSERT_FALSE(settings_spec_detail::is_valid_key("line\n"));
}

TEST_CASE("SET-064 the range constants are the table's", "[settings][spec]") {
  TEST_ASSERT_EQUAL_INT(SETTINGS_PARAMS[SETTINGS_PARAM_BRIGHTNESS].min_value,
                        SETTINGS_BRIGHTNESS_MIN);
  TEST_ASSERT_EQUAL_INT(SETTINGS_PARAMS[SETTINGS_PARAM_BRIGHTNESS].max_value,
                        SETTINGS_BRIGHTNESS_MAX);
  TEST_ASSERT_EQUAL_INT(SETTINGS_PARAMS[SETTINGS_PARAM_STICK_SENSITIVITY].min_value,
                        SETTINGS_STICK_SENSITIVITY_MIN);
  TEST_ASSERT_EQUAL_INT(SETTINGS_PARAMS[SETTINGS_PARAM_STICK_SENSITIVITY].max_value,
                        SETTINGS_STICK_SENSITIVITY_MAX);
  TEST_ASSERT_EQUAL_INT(SETTINGS_PARAMS[SETTINGS_PARAM_DRIVE_SPEED].min_value,
                        SETTINGS_DRIVE_SPEED_MIN);
  TEST_ASSERT_EQUAL_INT(SETTINGS_PARAMS[SETTINGS_PARAM_DRIVE_SPEED].max_value,
                        SETTINGS_DRIVE_SPEED_MAX);
}
