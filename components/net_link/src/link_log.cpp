/**
 * @file link_log.cpp
 * @brief rtps_comms_log_link_change: each change of the link state, to the serial log (and so
 *        to the LogScreen). Moved from main.cpp's log_link_change unchanged.
 */

#include <optional>
#include <string>

#include "logger.hpp"
#include "rtps_comms.hpp"

// Every change of the link state goes to the serial log, and so to the
// LogScreen: the TopBar shows where the link is now, the log keeps when it
// changed. Warnings on the way down, info on the way up.
void rtps_comms_log_link_change(RtpsLinkState state) {
  static espp::Logger link_logger({.tag = "rtps_link", .level = espp::Logger::Verbosity::INFO});
  static std::optional<RtpsLinkState> last;
  if (last == state) {
    return;
  }
  const std::string meaning = rtps_comms_link_state_meaning(state);
  if (!last) {
    link_logger.info("{} ({})", rtps_comms_link_state_name(state), meaning);
  } else if (state > *last) {
    link_logger.info("{} -> {} ({})", rtps_comms_link_state_name(*last),
                     rtps_comms_link_state_name(state), meaning);
  } else {
    link_logger.warn("{} -> {} ({})", rtps_comms_link_state_name(*last),
                     rtps_comms_link_state_name(state), meaning);
  }
  last = state;
}
