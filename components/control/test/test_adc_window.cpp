// L1-CTL: the continuous ADC's window bookkeeping (components/adc_window; hazard fix C2,
// docs/plans/hazard-c2-spec.md REQ-CTL-16, CTL-024). The mean is checked against espp 1.3.2's own
// arithmetic, written out here from its update_task.

#include <array>
#include <cstddef>
#include <cstdint>

#include "adc_window/adc_window.hpp"
#include "test_case.hpp"

namespace hmi::adc {
// Holds a publish open, as the writer does while it stores (a reader then sees a torn board).
struct AdcWindowBoardTestPeer {
  static void hold_publish(AdcWindowBoard &b) { b.version_.fetch_add(1); }
  static void release_publish(AdcWindowBoard &b) { b.version_.fetch_add(1); }
};
} // namespace hmi::adc

namespace {

using hmi::adc::AdcWindowAccumulator;
using hmi::adc::AdcWindowBoard;
using hmi::adc::AdcWindowReading;

// espp 1.3.2 ContinuousAdc::update_task, the uncalibrated branch: values_ = sum / num_samples
// with both converted to float first.
float espp_mean(std::uint32_t sum, std::uint32_t count) {
  float num_samples = static_cast<float>(count);
  float s = static_cast<float>(sum);
  return s / num_samples;
}

// A stand-in for adc_cali_raw_to_voltage as the hook calls it: the raw value truncated to int
// first, then scaled; it records what it was given.
struct FakeCali {
  std::array<float, 4> seen{};
  std::size_t calls = 0;
  float operator()(float raw) {
    seen[calls % seen.size()] = raw;
    ++calls;
    return static_cast<float>(static_cast<int>(raw) * 3 / 4);
  }
};

float raw_mv(std::size_t, float raw) { return raw; }

} // namespace

TEST_CASE("CTL-024 window bookkeeping: conversions per channel give espp's mean, the max and the "
          "sequence; a window without a conversion of a channel leaves that channel unchanged",
          "[control][adc_window][REQ-CTL-16]") {
  // Channel 0 gets three conversions, channel 1 none, in the same window.
  AdcWindowBoard board;
  board.add(0, 1000);
  board.add(0, 3001);
  board.add(0, 2000);
  board.close_and_publish(2, raw_mv);
  std::array<AdcWindowReading, 2> got{};
  TEST_ASSERT_TRUE(board.read(std::array<std::size_t, 2>{0, 1}, got));
  TEST_ASSERT_EQUAL_FLOAT(espp_mean(6001, 3), got[0].mean_mv);
  TEST_ASSERT_EQUAL_FLOAT(3001.0f, got[0].max_mv);
  TEST_ASSERT_EQUAL_UINT32(1, got[0].sequence);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, got[1].mean_mv); // before its first window, as espp's 0
  TEST_ASSERT_EQUAL_UINT32(0, got[1].sequence);

  // Next window (the publish started it): channel 0 none, channel 1 one conversion. Channel 0
  // keeps its last values.
  board.add(1, 42);
  board.add(hmi::adc::kMaxChannels, 7); // past the board: ignored
  board.close_and_publish(2, raw_mv);
  TEST_ASSERT_TRUE(board.read(std::array<std::size_t, 2>{0, 1}, got));
  TEST_ASSERT_EQUAL_FLOAT(espp_mean(6001, 3), got[0].mean_mv);
  TEST_ASSERT_EQUAL_FLOAT(3001.0f, got[0].max_mv);
  TEST_ASSERT_EQUAL_UINT32(1, got[0].sequence);
  TEST_ASSERT_EQUAL_FLOAT(42.0f, got[1].mean_mv);
  TEST_ASSERT_EQUAL_FLOAT(42.0f, got[1].max_mv);
  TEST_ASSERT_EQUAL_UINT32(1, got[1].sequence);

  // Calibrated: the mean goes through the calibration as espp's (the float average, which the
  // calibration truncates), the max through the same calibration; an empty window calls
  // nothing and changes nothing.
  AdcWindowAccumulator acc;
  acc.add(1001);
  acc.add(1002);
  AdcWindowReading reading;
  FakeCali cali;
  TEST_ASSERT_TRUE(hmi::adc::close_window(acc, cali, reading));
  TEST_ASSERT_EQUAL_UINT(2, cali.calls);
  TEST_ASSERT_EQUAL_FLOAT(espp_mean(2003, 2), cali.seen[0]); // 1001.5
  TEST_ASSERT_EQUAL_FLOAT(1002.0f, cali.seen[1]);
  TEST_ASSERT_EQUAL_FLOAT(750.0f, reading.mean_mv); // int(1001.5) * 3 / 4
  TEST_ASSERT_EQUAL_FLOAT(751.0f, reading.max_mv);  // 1002 * 3 / 4
  acc.clear();
  TEST_ASSERT_FALSE(hmi::adc::close_window(acc, cali, reading));
  TEST_ASSERT_EQUAL_UINT(2, cali.calls);
  TEST_ASSERT_EQUAL_UINT32(1, reading.sequence);

  // The sequence wraps: 2^32 - 1 -> 0.
  acc.add(5);
  reading.sequence = 0xFFFF'FFFFU;
  TEST_ASSERT_TRUE(hmi::adc::close_window(acc, cali, reading));
  TEST_ASSERT_EQUAL_UINT32(0, reading.sequence);
}

// Not a spec case: the lock-free publish that replaces the spec's "one locked read" (a lock in
// our code trips the ratchet; owner's decision of 2026-10-08: fix the code, not the check).
TEST_CASE("CTL-027 window snapshot: all channels from one publish; a read during a publish "
          "retries and gives up after kReadTries; an index past the board is refused",
          "[control][adc_window][REQ-CTL-16]") {
  AdcWindowBoard board;
  board.add(0, 100);
  board.add(1, 200);
  board.close_and_publish(2, raw_mv);
  std::array<AdcWindowReading, 2> got{};
  hmi::adc::AdcWindowBoardTestPeer::hold_publish(board);
  TEST_ASSERT_FALSE(board.read(std::array<std::size_t, 2>{0, 1}, got)); // torn every time
  hmi::adc::AdcWindowBoardTestPeer::release_publish(board);
  TEST_ASSERT_TRUE(board.read(std::array<std::size_t, 2>{1, 0}, got));
  TEST_ASSERT_EQUAL_FLOAT(200.0f, got[0].mean_mv); // in the order asked
  TEST_ASSERT_EQUAL_FLOAT(100.0f, got[1].mean_mv);
  std::array<AdcWindowReading, 1> one{};
  TEST_ASSERT_FALSE(board.read(std::array<std::size_t, 1>{hmi::adc::kMaxChannels}, one));
  TEST_ASSERT_FALSE(board.read(std::array<std::size_t, 2>{0, hmi::adc::kMaxChannels}, got));
  hmi::adc::AdcWindowBoardTestPeer::hold_publish(board);
  TEST_ASSERT_FALSE(board.read(std::array<std::size_t, 1>{0}, one));
  hmi::adc::AdcWindowBoardTestPeer::release_publish(board);
  // More channels than the board holds: the first kMaxChannels are closed, no more.
  for (std::size_t i = 0; i <= hmi::adc::kMaxChannels; ++i) {
    board.add(i, 9);
  }
  board.close_and_publish(hmi::adc::kMaxChannels + 4, raw_mv);
  std::array<AdcWindowReading, 1> last{};
  TEST_ASSERT_TRUE(board.read(std::array<std::size_t, 1>{hmi::adc::kMaxChannels - 1}, last));
  TEST_ASSERT_EQUAL_UINT32(1, last[0].sequence);
}
