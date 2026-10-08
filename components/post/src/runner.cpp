// The POST runner's lines (TS-POST-05) and the reset reason's name (runner.hpp,
// REQ-POST-17). Each line is built with snprintf into a fixed buffer: no allocation.

#include "post/runner.hpp"

#include <algorithm>
#include <cstdio>

namespace hmi::post {
namespace {

/// The line's prefix for a check's verdict: a LATCHED check without a measurement is a FAIL,
/// a LIVE one is skipped.
std::string_view printed_verdict(const Check &row, const Result &result) {
  switch (result.verdict) {
  case Verdict::PASS:
    return "PASS";
  case Verdict::FAIL:
    return "FAIL";
  case Verdict::PENDING:
    return row.kind == Kind::LIVE ? "SKIP" : "FAIL";
  }
  return "FAIL";
}

/// Copies what snprintf wrote into `line`, clipped to the buffer.
Line finish(Line line, int written) {
  const auto most = static_cast<int>(line.text.size()) - 1;
  line.len = static_cast<std::size_t>(std::clamp(written, 0, most));
  return line;
}

/// " <unit>", or nothing for a row without one.
struct UnitPart {
  const char *space;
  int len;
  const char *text;
};
UnitPart unit_part(const Check &row) {
  return row.unit.empty() ? UnitPart{"", 0, ""}
                          : UnitPart{" ", static_cast<int>(row.unit.size()), row.unit.data()};
}

} // namespace

std::string_view reset_reason_name(ResetReason reason) noexcept {
  switch (reason) {
  case ResetReason::POWERON:
    return "power-on";
  case ResetReason::EXT:
    return "external pin";
  case ResetReason::SW:
    return "software";
  case ResetReason::PANIC:
    return "PANIC";
  case ResetReason::INT_WDT:
    return "INTERRUPT WDT";
  case ResetReason::TASK_WDT:
    return "TASK WDT";
  case ResetReason::WDT:
    return "WDT";
  case ResetReason::DEEPSLEEP:
    return "deep sleep";
  case ResetReason::BROWNOUT:
    return "BROWNOUT";
  case ResetReason::USB:
    return "USB";
  case ResetReason::JTAG:
    return "JTAG";
  case ResetReason::CPU_LOCKUP:
    return "CPU LOCKUP";
  case ResetReason::PWR_GLITCH:
    return "POWER GLITCH";
  case ResetReason::UNKNOWN:
  case ResetReason::SDIO:
  case ResetReason::EFUSE:
  default:
    return "other";
  }
}

Line check_line(const Check &row, const Result &result) noexcept {
  Line line;
  const std::string_view verdict = printed_verdict(row, result);
  const UnitPart unit = unit_part(row);
  const int written = std::snprintf(
      line.text.data(), line.text.size(), "POST %.*s %.*s %ld%s%.*s [%ld,%ld]",
      static_cast<int>(row.name.size()), row.name.data(), static_cast<int>(verdict.size()),
      verdict.data(), static_cast<long>(result.value), unit.space, unit.len, unit.text,
      static_cast<long>(row.lo), static_cast<long>(row.hi));
  return finish(line, written);
}

Line result_line(const Report &report, std::uint32_t ms) noexcept {
  Line line;
  const auto passed = std::count_if(report.results.begin(), report.results.end(),
                                    [](const Result &r) { return r.verdict == Verdict::PASS; });
  const std::string_view overall = report.overall == Overall::PASS ? "PASS" : "FAIL";
  const int written =
      std::snprintf(line.text.data(), line.text.size(), "POST RESULT %.*s %ld/%lu %lu",
                    static_cast<int>(overall.size()), overall.data(), static_cast<long>(passed),
                    static_cast<unsigned long>(CHECK_COUNT), static_cast<unsigned long>(ms));
  return finish(line, written);
}

Line reset_line(ResetReason reason) noexcept {
  Line line;
  const std::string_view name = reset_reason_name(reason);
  const int written =
      std::snprintf(line.text.data(), line.text.size(), "POST reset reason: %.*s (%ld)",
                    static_cast<int>(name.size()), name.data(), static_cast<long>(reason));
  return finish(line, written);
}

Line waiting_line(const Check &row, const Result &result) noexcept {
  Line line;
  const UnitPart unit = unit_part(row);
  const int written =
      std::snprintf(line.text.data(), line.text.size(), "POST waiting: %.*s %ld%s%.*s",
                    static_cast<int>(row.name.size()), row.name.data(),
                    static_cast<long>(result.value), unit.space, unit.len, unit.text);
  return finish(line, written);
}

bool latched_pending(const Report &report) noexcept {
  for (std::size_t i = 0; i < CHECK_COUNT; ++i) {
    if (CHECKS[i].kind == Kind::LATCHED && report.results[i].verdict == Verdict::PENDING) {
      return true;
    }
  }
  return false;
}

} // namespace hmi::post
