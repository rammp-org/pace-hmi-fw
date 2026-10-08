#pragma once
// The system clock and the RTC, kept to the MCB's time (moved from main/frag_clock.inc).

#include <atomic>
#include <cstdint>
#include <ctime>

#include "logger.hpp"
#include "messages/mib_message.hpp"

namespace hmi::housekeeping {

/// @brief The MIB sends Unix time plus its UTC offset in every MibStatus. It sets the system
/// clock and the RTC, so the time keeps running through a lost link, and across a reboot
/// without the MIB. No TZ is set, so the system clock simply holds the local wall time the MIB
/// reported and nothing converts again. Built once, an app_main local (app-main-shrink V6),
/// before the RTC is read at boot.
class SystemClock {
public:
  struct Config {
    /// Set once the system clock holds a real time (main's clock_valid; the TopBar reads it).
    std::atomic<bool> *valid;
    /// The MCB's time and ours may disagree by this much before the MCB's wins.
    int64_t max_drift_s;
  };

  /// @brief Stores the config; touches no clock.
  /// @param config See Config.
  explicit SystemClock(const Config &config);

  /// @brief Sets the system clock to @p local and marks it valid.
  /// @param local The local wall time.
  /// Any task.
  void set(const std::tm &local);

  /// @brief Takes the MCB's time from @p status when it knows one, it is plausible, and ours
  /// is unset or off by more than max_drift_s: the system clock, then the RTC.
  /// @param status The MIB's status as received.
  /// RTPS receive task.
  void note_mcb_time(const MIB::MibStatus &status);

private:
  Config config_;
  espp::Logger logger_;
};

} // namespace hmi::housekeeping
