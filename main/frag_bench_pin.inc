// split_main.py fragment of main.cpp (frag_bench_pin: original lines 2047-2151); one TU, see main.cpp.
#include "hmi_models/pin.hpp"
#include "hmi_ui/bench_pin_view.hpp"
/////////////////////////////////////////////////////////////////////////////
// BenchGateScreen: the 4-digit PIN
//
// Spec V2 draws the pad as eleven discrete buttons (1-9, 0, backspace) rather
// than the one lv_keyboard the old screen used, so LVGL brings neither the key
// text nor the 2D arrow walk with it: each button carries its digit in
// user_data, and the joystick walks them through the same ButtonGrid the seat
// pages use. The bottom row has no left-hand key, which is the hole
// grid_key_cb steps over.
//
// The four dots above are the only readout. They are not clickable, they just
// show how many digits are in; the export gives them no CHECKED look, so the
// wiring adds one in the theme's text colour.
//
// The PIN is a build-time constant. It gates a bench screen, not anything
// safety-related, so it is a "not by accident" barrier rather than a secret:
// anyone holding the firmware image has it either way.
/////////////////////////////////////////////////////////////////////////////

static constexpr char kRdPin[] = "1234";
static constexpr int kRdPinLen = sizeof(kRdPin) - 1;
static_assert(kRdPinLen == hmi::ui::BenchPinView::PIN_LEN, "the model judges a four-digit PIN");

// The one instance.
static constinit hmi::ui::BenchPinView bench_pin_view{{
    .pin = std::string_view(kRdPin, kRdPinLen),
    .accepted = [] { setting_page_open(kActuatorsPage); },
    .off_bottom = nav_to_key,
    .style_key =
        [](lv_obj_t *key) {
          nav_focus_ring(key);
          nav_mirror_states(key);
          clear_click_focusable_recursive(key);
        },
}};
static void rd_pin_reset() { bench_pin_view.reset(); }
static void rd_focus(int index) { bench_pin_view.focus(index); }
