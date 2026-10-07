// split_main.py fragment of main.cpp (frag_seat: original lines 1833-2046); one TU, see main.cpp.
#include "hmi_models/grid.hpp"
#include "hmi_ui/button_grid.hpp"
#include "hmi_ui/seat_view.hpp"
using hmi::ui::ButtonGrid;
using hmi::ui::grid_key_cb;
using hmi::ui::grid_sync_cursor;
// The subtree helpers, defined with the settings rows (frag_settings_ui).
#include "hmi_ui/widget_tree.hpp"
using hmi::ui::clear_click_focusable_recursive;
using hmi::ui::set_focused_recursive;

static_assert(kGridMaxRows == hmi::ui::GRID_MAX_ROWS && kGridMaxCols == hmi::ui::GRID_MAX_COLS,
              "ButtonGrid and the cursor model must agree on the grid's size");

// Both defined with the settings rows further down, which own the seat values
// and the path that asks the MCB to move one.
static void seat_step(size_t row, int direction);
static void seat_request(rammp::SeatAxis axis, int32_t target);

// Raw value per seat axis, in the table's units, as the MCB last reported it
// (seat_apply_state). Shared by the seat view and the DEBUG ACTUATORS rows;
// the command path (seat_step, seat_request) reads it too.
static lv_subject_t seat_axis_value[rammp::kSeatAxisCount];

// The one instance.
static constinit hmi::ui::SeatView seat_view{{
    .nav = &ui_nav_port,
    .values = seat_axis_value,
    .step = seat_step,
    .request = seat_request,
    .show_buttons_page = seat_show_buttons_page,
    .keep_overlay_fill = keep_overlay_fill,
    .page = &seat_page,
}};
// frag_nav resets the function buttons' cursor on arrival.
static constinit ButtonGrid &seat_buttons_grid = seat_view.buttons_grid();
static void seat_show_buttons_page() { seat_view.show_buttons_page(); }
