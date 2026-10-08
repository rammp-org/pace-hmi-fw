// StatusBandView: the DriveBand's DRIVE and STATE cells (moved from main/frag_status_band.inc).

#include "hmi_ui/status_band_view.hpp"

#include "messages/mib_message.hpp"
#include "ui.h"

#include "components/ui_comp_driveband.h"
#include "drive_ui/link_state.hpp"

void hmi::ui::StatusBandView::drive_observer_cb(lv_observer_t *observer, lv_subject_t *) {
  static_cast<StatusBandView *>(lv_observer_get_user_data(observer))
      ->show(lv_observer_get_target_obj(observer), Kind::DRIVE_STATUS);
}

void hmi::ui::StatusBandView::state_observer_cb(lv_observer_t *observer, lv_subject_t *) {
  static_cast<StatusBandView *>(lv_observer_get_user_data(observer))
      ->show(lv_observer_get_target_obj(observer), Kind::STATE);
}

// Re-run either when the MCB says something new or when the link or the lock
// changes: whichever subject fired, the answer depends on all of them.
void hmi::ui::StatusBandView::show(lv_obj_t *label, Kind kind) {
  // Theme colours, not literals: the day theme's green and red are darker, so
  // they still read on white. Re-run on a theme switch (rtps_poll_cb notifies).
  const lv_color_t ok = lv_color_hex(static_cast<uint32_t>(ui_get_theme_value(_ui_theme_color_ok)));
  const lv_color_t alert =
      lv_color_hex(static_cast<uint32_t>(ui_get_theme_value(_ui_theme_color_alert)));
  const lv_color_t muted =
      lv_color_hex(static_cast<uint32_t>(ui_get_theme_value(_ui_theme_color_text_muted)));
  auto show = [label](const char *text, lv_color_t colour) {
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, colour, LV_PART_MAIN);
  };

  const bool connected =
      static_cast<LinkState>(lv_subject_get_int(config_.shared->rtps_link)) == LinkState::CONNECTED;
  const auto state =
      static_cast<MIB::MibSystemState>(lv_subject_get_int(config_.shared->mib_state));

  // DRIVE is two words, per spec: ACTIVE in green while the chair is driving,
  // LOCKED in red whenever it is not -- locked here, a fault, the link down, or
  // the MIB simply not enabled. It is the one thing the user has to act on
  // before anything moves, and the spec gives it the alert colour for that.
  // Locked outranks everything: the chair does not drive until it is asked to,
  // whatever the MIB says.
  if (kind == Kind::DRIVE_STATUS) {
    const bool active = lv_subject_get_int(config_.shared->locked) == 0 && connected &&
                        state == MIB::MibSystemState::ENABLED;
    show(active ? "ACTIVE" : "LOCKED", active ? ok : alert);
    return;
  }

  // STATE: no live link means no current answer, whatever arrived last.
  if (!connected) {
    show(UNKNOWN_TEXT, muted);
    return;
  }
  // The MIB's own wording wins when it sends one ("M2 ERROR"); the colour still
  // comes from the state, so it can say anything and still read as a fault.
  const char *override_text = lv_subject_get_string(&state_text_);
  const bool overridden = override_text != nullptr && override_text[0] != '\0';
  switch (state) {
  case MIB::MibSystemState::IDLE:
  case MIB::MibSystemState::ENABLED:
    show(overridden ? override_text : "OK", ok);
    return;
  case MIB::MibSystemState::INITIALIZING:
    // Not a fault and not yet OK. "STARTING" rather than the enum's name, which
    // does not fit the cell.
    show(overridden ? override_text : "STARTING", muted);
    return;
  default:
    // ERROR, and any value this firmware does not know -- what a MIB running
    // ahead of it sends.
    show(overridden ? override_text : "ERROR", alert);
    return;
  }
}

void hmi::ui::StatusBandView::bind(lv_obj_t *panel) {
  if (panel == nullptr) {
    return;
  }
  const SharedSubjects &s = *config_.shared;
  lv_obj_t *drive_label = ui_comp_get_child(panel, UI_COMP_DRIVEBAND_DRIVECELL_DRIVEVALUE);
  lv_obj_t *state_label = ui_comp_get_child(panel, UI_COMP_DRIVEBAND_STATECELL_STATEVALUE);
  lv_subject_add_observer_obj(s.mib_state, drive_observer_cb, drive_label, this);
  lv_subject_add_observer_obj(s.mib_state, state_observer_cb, state_label, this);
  // Further observers, so losing the link or receiving a new override repaints
  // them even though the enum said nothing new. All are object-bound and die
  // with the label.
  lv_subject_add_observer_obj(s.rtps_link, drive_observer_cb, drive_label, this);
  lv_subject_add_observer_obj(s.rtps_link, state_observer_cb, state_label, this);
  lv_subject_add_observer_obj(&drive_text_, drive_observer_cb, drive_label, this);
  lv_subject_add_observer_obj(&state_text_, state_observer_cb, state_label, this);
  // The drive cell reads LOCKED while locked, so it re-runs on that too.
  lv_subject_add_observer_obj(s.locked, drive_observer_cb, drive_label, this);
}

void hmi::ui::StatusBandView::init_texts() {
  lv_subject_init_string(&drive_text_, drive_text_buf_.data(), drive_text_prev_buf_.data(),
                         drive_text_buf_.size(), "");
  lv_subject_init_string(&state_text_, state_text_buf_.data(), state_text_prev_buf_.data(),
                         state_text_buf_.size(), "");
}
