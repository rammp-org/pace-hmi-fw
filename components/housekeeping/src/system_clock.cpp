#include "housekeeping/system_clock.hpp"

#include <cstdlib>
#include <sys/time.h>

#include "hmi_format/topbar.hpp"
#include "m5stack-tab5.hpp"

namespace hmi::housekeeping {

SystemClock::SystemClock(const Config &config)
    : config_(config)
    , logger_({.tag = "clock", .level = espp::Logger::Verbosity::INFO}) {}

void SystemClock::set(const std::tm &local) {
  std::tm copy = local;
  const timeval tv{.tv_sec = mktime(&copy), .tv_usec = 0};
  settimeofday(&tv, nullptr);
  *config_.valid = true;
}

void SystemClock::note_mcb_time(const MIB::MibStatus &status) {
  if (status.epoch_s <= 0) {
    return; // 0: the MCB does not know the time
  }
  // epoch_s is UTC; the TopBar shows the wall clock where the chair is, so the
  // MIB's own offset comes off the wire with it. TZ is unset, which makes
  // localtime_r and mktime the identity, so the system clock holds local time.
  const time_t mcb =
      static_cast<time_t>(status.epoch_s + static_cast<int64_t>(status.utc_offset_min) * 60);
  std::tm t{};
  // An RTC that lost power reads 2000; anything before 2025 is not a real time
  // (components/hmi_format, REQ-FMT-06).
  if (localtime_r(&mcb, &t) == nullptr || !hmi::format::clock_plausible(t)) {
    return;
  }
  if (*config_.valid &&
      std::llabs(static_cast<int64_t>(mcb - time(nullptr))) <= config_.max_drift_s) {
    return;
  }
  set(t);
  if (espp::M5StackTab5::get().set_rtc_time(t)) {
    logger_.info("set from the MCB: {:%Y-%m-%d %H:%M:%S}", t);
  } else {
    logger_.warn("set from the MCB, but the RTC write failed");
  }
}

} // namespace hmi::housekeeping
