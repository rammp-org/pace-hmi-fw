#pragma once

/// @file bench_inject.hpp
/// @brief Bench stick injection: the three raw reads replaced by values a bench script sends.
/// @details BENCH ONLY (hazard-fixes.md §3 B1). The firmware uses this only behind
///          CONFIG_HMI_BENCH_STICK_INJECT, in `if constexpr` arms, so a release build compiles
///          none of it. It lets the bench drive the stick path (the gate, and the stick fault
///          handling, C2) without touching the pots:
///          - the remote UI's STICK verb parses a StickInjectMsg (parse_stick_inject) and writes
///            it to a one-slot mailbox;
///          - the ADC task drains the mailbox and swaps its three raw reads for the injected
///            ones (StickInjector::apply) before StickPipeline::cycle, so everything after the
///            reads (calibration, mapping, keys, gate, XYTwist) is the real code;
///          - an injection holds only while it is refreshed: INJECT_EXPIRY after the last
///            message, the real reads come back;
///          - fail_mask makes a read fail (no value), as a failed ADC read does (H9).
///          Pure: no ESP-IDF, no LVGL, no allocation; the clock is passed in (CS-HAL-03).

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

#include "stick/stick_pipeline.hpp"

namespace hmi::stick {

/// @brief StickInjectMsg::fail_mask bits: which read fails.
enum class InjectFail : std::uint8_t {
  HORIZONTAL = 1U << 0U, ///< ADC1_CH1, RawReadsMv::horizontal_mv
  VERTICAL = 1U << 1U,   ///< ADC1_CH0, RawReadsMv::vertical_mv
  TWIST = 1U << 2U,      ///< ADC2_CH3, RawReadsMv::twist_mv
};

/// @brief Every fail_mask bit.
inline constexpr std::uint8_t INJECT_FAIL_ALL = 0x07U;

/// @brief The largest injected read, in mV: the ADC's full scale as the firmware assumes it
///        (the uncalibrated ideal axis is 0..3300 mV).
inline constexpr std::uint32_t INJECT_MAX_MV = 3300U;

/// @brief How long an injection holds without a refresh. After it, the real reads come back.
inline constexpr std::chrono::milliseconds INJECT_EXPIRY{300};

/// @brief One injection: the three raw reads, which of them fail, and the sender's sequence
///        number (carried for the bench's logs; the firmware does not act on it).
/// @details A Message for an fw_core channel (CS-OWN-05): trivially copyable, 20 bytes,
///          marked with IS_MESSAGE.
struct StickInjectMsg {
  static constexpr bool IS_MESSAGE = true; ///< fw::Message marker (CS-OWN-05)
  float horizontal_mv;                     ///< stands in for ADC1_CH1 (GPIO17)
  float vertical_mv;                       ///< stands in for ADC1_CH0 (GPIO16); lower is up
  float twist_mv;                          ///< stands in for the averaged ADC2_CH3 (GPIO52)
  std::uint8_t fail_mask;                  ///< InjectFail bits: these reads fail
  std::uint32_t seq;                       ///< the sender's sequence number
};

/// @brief Parses the STICK verb's arguments: `<h_mv> <v_mv> <twist_mv> <fail_mask> <seq>`.
/// @details Outside input (TCP), so validated, never asserted (CS-ERR-04): exactly five
///          unsigned decimal integers separated by spaces; each mV 0..INJECT_MAX_MV, fail_mask
///          0..INJECT_FAIL_ALL, seq 0..2^32-1. Anything else (a sign, a fraction, a sixth
///          field, trailing text, an overflow) is rejected. Bounded by the text's length.
/// @param args The text after the verb.
/// @return The message, or nothing when the text is not exactly that.
[[nodiscard]] inline std::optional<StickInjectMsg> parse_stick_inject(std::string_view args) {
  constexpr std::size_t FIELDS = 5;
  std::array<std::uint32_t, FIELDS> values{};
  std::size_t count = 0;
  std::size_t i = 0;
  while (i < args.size()) {
    if (args[i] == ' ') {
      ++i;
      continue;
    }
    if (count == FIELDS) {
      return std::nullopt; // a sixth field
    }
    const char *first = args.data() + i;
    const char *last = args.data() + args.size();
    const auto [end, ec] = std::from_chars(first, last, values.at(count));
    if (ec != std::errc{} || end == first || (end != last && *end != ' ')) {
      return std::nullopt; // not a number, out of range, or a number with text stuck to it
    }
    i = static_cast<std::size_t>(end - args.data());
    ++count;
  }
  const auto [h_mv, v_mv, twist_mv, mask, seq] = values;
  if (count != FIELDS || h_mv > INJECT_MAX_MV || v_mv > INJECT_MAX_MV || twist_mv > INJECT_MAX_MV ||
      mask > INJECT_FAIL_ALL) {
    return std::nullopt;
  }
  return StickInjectMsg{.horizontal_mv = static_cast<float>(h_mv),
                        .vertical_mv = static_cast<float>(v_mv),
                        .twist_mv = static_cast<float>(twist_mv),
                        .fail_mask = static_cast<std::uint8_t>(mask),
                        .seq = seq};
}

/// @brief The injection's state on the ADC task: the last message and when it was taken.
/// @details One instance, owned by the ADC task. It only decides; the caller drains the channel
///          and passes the time, so this is pure and host-tested.
class StickInjector {
public:
  using TimePoint = std::chrono::steady_clock::time_point; ///< the ADC task's clock

  /// @brief A message was taken from the channel at @p now: it holds until INJECT_EXPIRY later.
  /// @param msg The message.
  /// @param now When the ADC task took it.
  void note(const StickInjectMsg &msg, TimePoint now) noexcept {
    last_ = msg;
    noted_at_ = now;
  }

  /// @brief Ends the injection now: the real reads come back.
  void cancel() noexcept { last_.reset(); }

  /// @brief Whether an injection holds at @p now: a message was taken less than INJECT_EXPIRY
  ///        ago. A clock that reads earlier than the message ends it too.
  /// @param now The ADC task's time.
  /// @return true while the injected reads replace the real ones.
  [[nodiscard]] bool active(TimePoint now) const noexcept {
    if (!last_) {
      return false;
    }
    const auto age = now - noted_at_;
    return age >= TimePoint::duration::zero() && age < INJECT_EXPIRY;
  }

  /// @brief The reads the stick pipeline gets at @p now: the injected ones while active (a
  ///        fail_mask bit makes that read fail), else @p real unchanged.
  /// @param real The ADC's reads this cycle.
  /// @param now The ADC task's time.
  /// @return The reads to pass to StickPipeline::cycle.
  [[nodiscard]] RawReadsMv apply(const RawReadsMv &real, TimePoint now) const noexcept {
    if (!active(now)) {
      return real;
    }
    const auto read = [mask = last_->fail_mask](InjectFail bit, float mv) {
      return (mask & static_cast<std::uint8_t>(bit)) != 0U ? std::nullopt
                                                           : std::optional<float>{mv};
    };
    return {.horizontal_mv = read(InjectFail::HORIZONTAL, last_->horizontal_mv),
            .vertical_mv = read(InjectFail::VERTICAL, last_->vertical_mv),
            .twist_mv = read(InjectFail::TWIST, last_->twist_mv)};
  }

private:
  std::optional<StickInjectMsg> last_;
  TimePoint noted_at_{};
};

} // namespace hmi::stick
