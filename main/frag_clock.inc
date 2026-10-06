// split_main.py fragment of main.cpp (frag_clock: original lines 767-852); one TU, see main.cpp.
static std::atomic<bool> clock_valid{false};
static lv_subject_t clock_subject;
static char clock_buf[8];
static char clock_prev_buf[8];

// An RTC that lost power reads 2000; anything before 2025 is not a real time.
static bool clock_plausible(const std::tm &t) { return t.tm_year >= 125; }

// Any task.
static void clock_set(const std::tm &local) {
  std::tm copy = local;
  const timeval tv{.tv_sec = mktime(&copy), .tv_usec = 0};
  settimeofday(&tv, nullptr);
  clock_valid = true;
}

// RTPS receive task.
static void clock_note_mcb_time(const MIB::MibStatus &status) {
  if (status.epoch_s <= 0) {
    return; // 0: the MCB does not know the time
  }
  // epoch_s is UTC; the TopBar shows the wall clock where the chair is, so the
  // MIB's own offset comes off the wire with it. TZ is unset, which makes
  // localtime_r and mktime the identity, so the system clock holds local time.
  const time_t mcb =
      static_cast<time_t>(status.epoch_s + static_cast<int64_t>(status.utc_offset_min) * 60);
  std::tm t{};
  if (localtime_r(&mcb, &t) == nullptr || !clock_plausible(t)) {
    return;
  }
  if (clock_valid && std::llabs(static_cast<int64_t>(mcb - time(nullptr))) <= kClockMaxDriftS) {
    return;
  }
  clock_set(t);
  static espp::Logger clock_logger({.tag = "clock", .level = espp::Logger::Verbosity::INFO});
  if (espp::M5StackTab5::get().set_rtc_time(t)) {
    clock_logger.info("set from the MCB: {:%Y-%m-%d %H:%M:%S}", t);
  } else {
    clock_logger.warn("set from the MCB, but the RTC write failed");
  }
}

// LVGL task.
static void clock_poll_cb(lv_timer_t *) {
  char text[sizeof(clock_buf)] = "--:--";
  if (clock_valid) {
    const time_t now = time(nullptr);
    std::tm t{};
    gmtime_r(&now, &t);
    snprintf(text, sizeof(text), "%02d:%02d", t.tm_hour, t.tm_min);
  }
  if (strcmp(lv_subject_get_string(&clock_subject), text) != 0) {
    lv_subject_copy_string(&clock_subject, text);
  }
}

// The TopBar's link label: what RTPS runs over. The design's "BT · WI-FI" is only
// a placeholder; it is set from the setting before rtps_comms_start, then from the
// link it brought up (WiFi with no network known runs Ethernet).
static lv_subject_t link_subject;
static char link_buf[16];
static char link_prev_buf[16];

static const char *link_text(NetLink link) {
  return link == NetLink::WIFI ? "BT · WI-FI" : "BT · ETH";
}

static void bind_topbar_labels(lv_obj_t *bar) {
  if (bar != nullptr) {
    lv_label_bind_text(ui_comp_get_child(bar, UI_COMP_TOPBAR_CLOCK), &clock_subject, nullptr);
    lv_label_bind_text(ui_comp_get_child(bar, UI_COMP_TOPBAR_LINK), &link_subject, nullptr);
  }
}

// One edge of the stick button, from the GPIO or from the remote UI channel --
// one path, so what a script presses is the real handling. Touches subjects,
// never widgets directly. Any task.
//
// "Select" -- the keypad's ENTER, which clicks whatever the joystick has
// focused -- fires on RELEASE, and only for a press shorter than
// kSelectMaxUs. The same button is held to stop driving and to start a
// calibration, and a select on the press edge would have clicked the focused
// control (on the Drive screen: opened the menu) at the start of every hold.
// kSelectMaxUs is a hold's grace period, so a press short enough to select
// never starts a hold filling, and one long enough to fill never selects.
static constexpr int64_t kSelectMaxUs = 500 * 1000; // == kBarGraceMs
