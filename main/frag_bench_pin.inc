// split_main.py fragment of main.cpp (frag_bench_pin: original lines 2047-2151); one TU, see main.cpp.
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
static_assert(kRdPinLen == 4, "the bench gate draws exactly four PIN dots");

// The keypad, in the order it is drawn. The last row starts at the middle
// column, so its left cell is a hole. kRdBack is the backspace key's marker.
static constexpr int kRdBack = -1;
static ButtonGrid rd_grid;

// The digits typed so far, NUL-terminated so a full entry is one comparison.
// The length lives in the subject rather than here, because that is what the
// dots are bound to; this array is only ever read to judge a full entry.
static char rd_pin_entry[kRdPinLen + 1];
static lv_subject_t rd_pin_len_subject;

// The line above the dots, which doubles as the rejection notice. A
// subject rather than an lv_label_set_text from the keypad handler, for the
// reason in CLAUDE.md; the buffers are static because the subject stores its
// copy in them and both have to outlive the label bound to them.
// Arrays rather than pointers, so the static_assert below measures the string
// and not the pointer to it.
static constexpr char kRdPinPromptText[] = "Enter PIN to proceed";
static constexpr char kRdPinWrongText[] = "Incorrect PIN - try again";
static char rd_pin_message_buf[32];
static char rd_pin_message_prev_buf[32];
static_assert(sizeof(rd_pin_message_buf) > sizeof(kRdPinWrongText), "label buffer too small");
static lv_subject_t rd_pin_message_subject;

// Empties the entry and puts the prompt back. Runs on every BenchGateScreen
// load, so the PIN is asked again on every visit rather than latching once per
// boot.
static void rd_pin_reset() {
  rd_pin_entry[0] = '\0';
  lv_subject_set_int(&rd_pin_len_subject, 0);
  lv_subject_copy_string(&rd_pin_message_subject, kRdPinPromptText);
}

// Puts the cursor and the focus on one key, by grid index. Used on load so the
// first joystick nudge moves between keys instead of being spent picking one.
static void rd_focus(int index) {
  rd_grid.row = index / kGridMaxCols;
  rd_grid.col = index % kGridMaxCols;
  if (rd_grid.cell[rd_grid.row][rd_grid.col] != nullptr) {
    lv_group_focus_obj(rd_grid.cell[rd_grid.row][rd_grid.col]);
  }
}

// A key on the PIN pad. user_data carries the digit, or kRdBack for backspace;
// nothing else can reach here, so there is no key this has to ignore.
static void rd_keypad_cb(lv_event_t *e) {
  const int digit = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  // Touch moves the cursor too, so the joystick carries on from where a finger
  // last landed rather than from where the stick left off.
  grid_sync_cursor(&rd_grid, lv_event_get_target_obj(e));
  int len = lv_subject_get_int(&rd_pin_len_subject);

  if (digit == kRdBack) {
    if (len > 0) {
      rd_pin_entry[--len] = '\0';
      lv_subject_set_int(&rd_pin_len_subject, len);
    }
    return;
  }

  rd_pin_entry[len++] = static_cast<char>('0' + digit);
  rd_pin_entry[len] = '\0';
  lv_subject_set_int(&rd_pin_len_subject, len);
  // Typing clears a previous rejection, so the line reads as a prompt for the
  // entry in progress rather than a verdict on the last one.
  lv_subject_copy_string(&rd_pin_message_subject, kRdPinPromptText);
  if (len < kRdPinLen) {
    return;
  }

  // Judged on the fourth digit rather than on an OK key: the row of dots makes
  // the length obvious, so a confirm step would add nothing. Either branch
  // empties the entry, which is what keeps `len` below kRdPinLen above.
  const bool correct = lv_strcmp(rd_pin_entry, kRdPin) == 0;
  rd_pin_reset();
  if (correct) {
    setting_page_open(kActuatorsPage);
    return;
  }
  lv_subject_copy_string(&rd_pin_message_subject, kRdPinWrongText);
}

