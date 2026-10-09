// PostStage: the quick POST on the UI task (post_stage.hpp, hazard-c3-spec.md §2.3-2.9).

#include "hmi_ui/post_stage.hpp"

#include <algorithm>
#include <string_view>

#include "hmi_rtps_spec.hpp"

namespace {

using hmi::post::IndicatorKind;

/// Every check of the POST table has its words (hazard-c3-spec.md §2.8).
consteval bool every_check_has_text() {
  return std::all_of(
      hmi::post::CHECKS.begin(), hmi::post::CHECKS.end(),
      [](const hmi::post::Check &c) { return rammp::post_check_text(c.name) != nullptr; });
}
static_assert(every_check_has_text(), "a POST check has no text in hmi_rtps_spec");

static_assert(hmi::post::POST_TICK_MS == 250, "the runner rides the 250 ms UI poll");

constexpr std::uint32_t NO_CHECK = 0xFFU;

std::uint32_t pack(const hmi::ui::PostStage::Shown &shown) {
  const std::uint32_t check = shown.check ? static_cast<std::uint32_t>(*shown.check) : NO_CHECK;
  return static_cast<std::uint32_t>(shown.kind) | (check << 8U) |
         ((shown.timed_out ? 1U : 0U) << 16U) | ((shown.stick_task_missing ? 1U : 0U) << 17U);
}

/// Appends @p text to @p out at @p at; returns the new end (clipped to the buffer).
std::size_t append(hmi::ui::PostText &out, std::size_t at, std::string_view text) {
  const std::size_t room = out.text.size() - 1 - at;
  const std::size_t n = text.size() < room ? text.size() : room;
  text.copy(out.text.data() + at, n);
  out.text[at + n] = '\0';
  return at + n;
}

} // namespace

void hmi::ui::PostStage::attach(hmi::fw::Reader<RestWindowMailbox> windows,
                                const hmi::post::I2cSet &i2c, espp::Logger &log) {
  windows_.emplace(windows);
  i2c_ = i2c;
  log_ = &log;
}

void hmi::ui::PostStage::apply_request() {
  const std::uint32_t request = request_.read();
  const std::uint32_t seq = request >> 8U;
  if (seq == request_seen_) {
    return;
  }
  request_seen_ = seq;
  const std::uint32_t what = request & 0xFFU;
  if (what == 1U) {
    runner_.rerun();
  } else if (what >= 2U && what <= 5U) {
    runner_.force(static_cast<hmi::stick::PostGate>(what - 2U));
  }
}

void hmi::ui::PostStage::tick() {
  apply_request();
  std::optional<hmi::post::StickWindow> window;
  if (windows_) {
    hmi::post::RestWindowMsg msg{};
    if (windows_->read(msg) == hmi::fw::ReadStatus::CHANGED) {
      window = msg.window;
    }
  }
  const std::uint32_t now = config_.port->now_ms();
  runner_.tick(now, window);
  publish_shown(now);
}

void hmi::ui::PostStage::publish_shown(std::uint32_t now_ms) {
  const hmi::stick::PostGate gate = runner_.gate();
  if (gate != hmi::stick::PostGate::NOT_RUN) {
    not_run_seen_ = false;
  } else if (!not_run_seen_) {
    not_run_seen_ = true;
    not_run_since_ms_ = now_ms;
  }
  const hmi::post::Indicator shown = hmi::post::post_indicator(
      gate, runner_.report(), runner_.timed_out(), now_ms - not_run_since_ms_);
  shown_.write(pack({.kind = shown.kind,
                     .check = shown.check,
                     .timed_out = shown.timed_out,
                     .stick_task_missing = runner_.stick_task_missing()}));
}

hmi::ui::PostStage::Shown hmi::ui::PostStage::shown() const {
  const std::uint32_t packed = shown_.read();
  const std::uint32_t check = (packed >> 8U) & 0xFFU;
  Shown out;
  out.kind = static_cast<IndicatorKind>(packed & 0xFFU);
  if (check < hmi::post::CHECK_COUNT) {
    out.check = static_cast<hmi::post::Id>(check);
  }
  out.timed_out = ((packed >> 16U) & 1U) != 0U;
  out.stick_task_missing = ((packed >> 17U) & 1U) != 0U;
  return out;
}

hmi::ui::PostText hmi::ui::PostStage::text(const Shown &shown, hmi::post::ResetReason reset,
                                           bool calibration_saved_now) {
  PostText out;
  switch (shown.kind) {
  case IndicatorKind::NONE:
    return out;
  case IndicatorKind::CHECKING:
    (void)append(out, 0, rammp::kPostChecking);
    return out;
  case IndicatorKind::NOT_RUN:
    (void)append(out, 0, rammp::kPostNotRun);
    return out;
  case IndicatorKind::WAITING:
  case IndicatorKind::FAILED:
    break;
  }
  const rammp::PostCheckText *words =
      shown.check ? rammp::post_check_text(hmi::post::check(*shown.check).name) : nullptr;
  if (words == nullptr) { // a FAIL that names no check: a gate byte outside the enum
    (void)append(out, append(out, 0, rammp::kPostFailedPrefix), "?");
    return out;
  }
  if (shown.kind == IndicatorKind::WAITING) {
    (void)append(out, 0, words->text);
    return out;
  }
  if (*shown.check == hmi::post::Id::CAL_SAVED && calibration_saved_now) {
    (void)append(out, 0, rammp::kPostCalSavedRestart); // C3 §2.12, decision C2 a
    return out;
  }
  std::size_t at =
      append(out, 0, shown.timed_out ? rammp::kPostTimedOutPrefix : rammp::kPostFailedPrefix);
  at = append(out, at, words->text);
  if (shown.stick_task_missing) {
    at = append(out, at, rammp::kPostStickTaskMissing); // the cause of the adc.valid FAIL
  }
  if (words->names_reset) {
    at = append(out, at, hmi::post::reset_reason_name(reset));
  }
  if (words->turn_off_and_on) {
    (void)append(out, at, rammp::kPostTurnOffAndOn);
  }
  return out;
}

hmi::ui::PostText hmi::ui::PostStage::text_now() const {
  return text(shown(), config_.port->reset_reason(), config_.port->calibration_saved_now());
}

void hmi::ui::PostStage::request_rerun() {
  request_.write((((request_.read() >> 8U) + 1U) << 8U) | 1U);
}

void hmi::ui::PostStage::request_gate(hmi::stick::PostGate gate) {
  request_.write((((request_.read() >> 8U) + 1U) << 8U) | (2U + static_cast<std::uint32_t>(gate)));
}
