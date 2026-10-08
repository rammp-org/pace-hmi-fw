#include "hmi_ui/rtps_ui_bridge.hpp"

#include <algorithm>
#include <cstddef>

#include "hmi_format/speed.hpp"

namespace hmi::ui {

void RtpsUiBridge::apply_mib_status(const MIB::MibStatus &status) const {
  lv_subject_set_int(config_.mib_state, static_cast<int32_t>(status.systemState));
  // What the MIB is actually driving with: the three profile buttons highlight from
  // this, so they follow the chair even when something else changed it.
  lv_subject_set_int(config_.drive_band->profile_subject(),
                     static_cast<int32_t>(status.activeProfile));
  // m/s on the wire, mph on the dial: the shared spec carries the real
  // quantity and the unit on the label is ours to pick.
  lv_subject_set_int(config_.drive_band->speed_subject(),
                     hmi::format::speed_display_tenths(status.speed));
  // copy_string cuts each text to its subject's buffer (RAMMP_*_LEN). drive_text stays
  // empty: the MIB sends one wording, and the state label is where it belongs.
  lv_subject_copy_string(config_.status_band->state_text(), status.status_text.c_str());
  lv_subject_copy_string(config_.refusal->error_text(), status.error_message.c_str());
  lv_subject_copy_string(config_.refusal->error_footer(), status.error_footer.c_str());
  config_.seat_apply_state(status.currentSeatState);
}

void RtpsUiBridge::apply_diagnostics(const rammp::Diagnostics &diag) const {
  for (size_t i = 0; i < std::min<size_t>(diag.items.size(), rammp::kDiagCount); i++) {
    const auto &values = diag.items[i].values;
    for (size_t f = 0; f < std::min<size_t>(values.size(), rammp::kDiagFields); f++) {
      lv_subject_set_int(&config_.diag_values[i][f], values[f]);
    }
  }
}

} // namespace hmi::ui
