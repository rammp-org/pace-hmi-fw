#pragma once

// pace-hmi-fw addition to the vendored espp adc (not upstream; components/espp_adc/README.md):
// the bookkeeping of one continuous-ADC window per channel, pulled out of
// ContinuousAdc::update_task so the host tests can run it (hazard fix C2, REQ-CTL-16, CTL-024).
// Plain C++: no ESP-IDF include.

#include <cstdint>

namespace espp {

/**
 * @brief One channel's conversions within one window: what update_task sums. The mean is
 *        espp's (the sum over the count, as floats); the maximum is new.
 */
struct AdcWindowAccumulator {
  uint32_t sum{0};     /**< Sum of the raw conversions. */
  uint32_t count{0};   /**< Number of raw conversions. */
  uint32_t max_raw{0}; /**< Largest raw conversion. */

  /// Starts a new window.
  void clear() {
    sum = 0;
    count = 0;
    max_raw = 0;
  }
  /// Adds one raw conversion.
  void add(uint32_t data) {
    sum += data;
    count++;
    if (data > max_raw) {
      max_raw = data;
    }
  }
};

/**
 * @brief A channel's newest window: espp's filtered value (the window mean) and, new, the
 *        window's largest conversion and a sequence number. Both values are in mV, or raw when
 *        the channel could not be calibrated (as get_mv()).
 */
struct AdcWindowReading {
  float mean_mv{0};     /**< The window's mean, as get_mv() returns it. */
  float max_mv{0};      /**< The window's largest conversion. */
  uint32_t sequence{0}; /**< +1 for each window holding at least one conversion; wraps. */
};

/**
 * @brief Closes one channel's window into @p reading. With at least one conversion: the mean
 *        exactly as espp computes it (`float(sum) / float(count)`, then @p to_mv), the maximum
 *        through the same @p to_mv, and the sequence + 1. Without any: @p reading is left as it
 *        was (the previous window's values and sequence).
 * @param acc The window's conversions.
 * @param to_mv Raw (as a float) to the reported unit: espp's calibration, or the raw value.
 * @param reading The channel's newest window, updated in place.
 * @return Whether the window held a conversion of this channel.
 */
template <typename ToMv>
bool close_window(const AdcWindowAccumulator &acc, ToMv &&to_mv, AdcWindowReading &reading) {
  if (acc.count == 0) {
    return false;
  }
  float num_samples = static_cast<float>(acc.count);
  float sum = static_cast<float>(acc.sum);
  float average = sum / num_samples;
  reading.mean_mv = to_mv(average);
  reading.max_mv = to_mv(static_cast<float>(acc.max_raw));
  reading.sequence++;
  return true;
}

} // namespace espp
