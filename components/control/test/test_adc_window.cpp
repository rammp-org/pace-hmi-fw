// L1-CTL: the vendored ContinuousAdc's window bookkeeping (components/espp_adc/include/
// adc_window.hpp; hazard fix C2, docs/plans/hazard-c2-spec.md REQ-CTL-16, CTL-024). The mean is
// checked against espp 1.3.2's own arithmetic, written out here from its update_task.

#include <array>
#include <cstdint>

#include "adc_window.hpp"
#include "test_case.hpp"

namespace {

using espp::AdcWindowAccumulator;
using espp::AdcWindowReading;

// espp 1.3.2 ContinuousAdc::update_task, the uncalibrated branch: values_ = sum / num_samples
// with both converted to float first.
float espp_mean(std::uint32_t sum, std::uint32_t count) {
  float num_samples = static_cast<float>(count);
  float s = static_cast<float>(sum);
  return s / num_samples;
}

// A stand-in for adc_cali_raw_to_voltage, as the vendored code calls it: the raw value
// truncated to int first, then scaled; it records what it was given.
struct FakeCali {
  std::array<float, 4> seen{};
  std::size_t calls = 0;
  float operator()(float raw) {
    seen[calls % seen.size()] = raw;
    ++calls;
    return static_cast<float>(static_cast<int>(raw) * 3 / 4);
  }
};

} // namespace

TEST_CASE("CTL-024 window bookkeeping: conversions per channel give espp's mean, the max and the "
          "sequence; a window without a conversion of a channel leaves that channel unchanged",
          "[control][espp_adc][REQ-CTL-16]") {
  // Channel 0 gets three conversions, channel 1 none, in the same window.
  std::array<AdcWindowAccumulator, 2> window{};
  std::array<AdcWindowReading, 2> reading{};
  window[0].add(1000);
  window[0].add(3001);
  window[0].add(2000);
  auto raw = [](float r) { return r; };
  TEST_ASSERT_TRUE(espp::close_window(window[0], raw, reading[0]));
  TEST_ASSERT_FALSE(espp::close_window(window[1], raw, reading[1]));
  TEST_ASSERT_EQUAL_FLOAT(espp_mean(6001, 3), reading[0].mean_mv);
  TEST_ASSERT_EQUAL_FLOAT(3001.0f, reading[0].max_mv);
  TEST_ASSERT_EQUAL_UINT32(1, reading[0].sequence);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, reading[1].mean_mv); // before its first window, as espp's 0
  TEST_ASSERT_EQUAL_UINT32(0, reading[1].sequence);

  // Next window: channel 0 none, channel 1 one conversion. Channel 0 keeps its last values.
  for (auto &w : window) {
    w.clear();
  }
  TEST_ASSERT_EQUAL_UINT32(0, window[0].count);
  TEST_ASSERT_EQUAL_UINT32(0, window[0].max_raw);
  window[1].add(42);
  TEST_ASSERT_FALSE(espp::close_window(window[0], raw, reading[0]));
  TEST_ASSERT_TRUE(espp::close_window(window[1], raw, reading[1]));
  TEST_ASSERT_EQUAL_FLOAT(espp_mean(6001, 3), reading[0].mean_mv);
  TEST_ASSERT_EQUAL_FLOAT(3001.0f, reading[0].max_mv);
  TEST_ASSERT_EQUAL_UINT32(1, reading[0].sequence);
  TEST_ASSERT_EQUAL_FLOAT(42.0f, reading[1].mean_mv);
  TEST_ASSERT_EQUAL_FLOAT(42.0f, reading[1].max_mv);
  TEST_ASSERT_EQUAL_UINT32(1, reading[1].sequence);

  // Calibrated: the mean goes through the calibration as espp's (the float average, which
  // the calibration truncates), the max through the same calibration.
  AdcWindowAccumulator cal_window;
  cal_window.add(1001);
  cal_window.add(1002);
  AdcWindowReading cal_reading;
  FakeCali cali;
  TEST_ASSERT_TRUE(espp::close_window(cal_window, cali, cal_reading));
  TEST_ASSERT_EQUAL_UINT(2, cali.calls);
  TEST_ASSERT_EQUAL_FLOAT(espp_mean(2003, 2), cali.seen[0]); // 1001.5
  TEST_ASSERT_EQUAL_FLOAT(1002.0f, cali.seen[1]);
  TEST_ASSERT_EQUAL_FLOAT(750.0f, cal_reading.mean_mv); // int(1001.5) * 3 / 4
  TEST_ASSERT_EQUAL_FLOAT(751.0f, cal_reading.max_mv);  // 1002 * 3 / 4
  // An empty window does not touch the calibration or the reading.
  cal_window.clear();
  TEST_ASSERT_FALSE(espp::close_window(cal_window, cali, cal_reading));
  TEST_ASSERT_EQUAL_UINT(2, cali.calls);
  TEST_ASSERT_EQUAL_FLOAT(750.0f, cal_reading.mean_mv);
  TEST_ASSERT_EQUAL_UINT32(1, cal_reading.sequence);
  cal_window.add(1001);
  cal_window.add(1002);

  // The sequence wraps: 2^32 - 1 -> 0.
  AdcWindowReading wrap;
  wrap.sequence = 0xFFFF'FFFFU;
  TEST_ASSERT_TRUE(espp::close_window(cal_window, raw, wrap));
  TEST_ASSERT_EQUAL_UINT32(0, wrap.sequence);
}
