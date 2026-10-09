#pragma once
// espp's ContinuousAdc (vendored, components/espp_adc) with the window reads hazard fix C2 adds
// (docs/plans/hazard-c2-spec.md REQ-CTL-16, §2.1): each X/Y channel's newest window (mean, max,
// sequence) in one consistent snapshot, without a lock. Our code: the upstream class stays as
// it is apart from its hook lines (components/espp_adc/README.md).
//
// Not used by the firmware yet: C2's island commit reads both X/Y windows through it.

#include <array>
#include <cstddef>
#include <optional>

#include "adc_window/adc_window.hpp"
#include "continuous_adc.hpp"

namespace hmi::control {

/// ContinuousAdc plus get_windows(). Built and run as ContinuousAdc.
class WindowedContinuousAdc : public espp::ContinuousAdc {
public:
  using espp::ContinuousAdc::ContinuousAdc;

  /// The newest window of each channel in @p configs, all from one publish, read without a
  /// lock. A channel's entry is empty when it was not configured, the ADC is not running, or
  /// the snapshot was torn kReadTries times (then every entry is empty). A sequence of 0 means
  /// no window yet: espp's mean then reads 0.
  template <std::size_t N>
  [[nodiscard]] std::array<std::optional<hmi::adc::AdcWindowReading>, N>
  get_windows(const std::array<espp::AdcConfig, N> &configs) const noexcept {
    std::array<std::optional<hmi::adc::AdcWindowReading>, N> out{};
    if (!running_) {
      return out;
    }
    std::array<std::size_t, N> indices{};
    std::array<bool, N> configured{};
    for (std::size_t k = 0; k < N; ++k) {
      const auto index = get_index(configs[k]);
      configured[k] = index != INVALID_INDEX;
      indices[k] = configured[k] ? index : 0;
    }
    std::array<hmi::adc::AdcWindowReading, N> snapshot{};
    if (!windows_.read(indices, snapshot)) {
      return out;
    }
    for (std::size_t k = 0; k < N; ++k) {
      if (configured[k]) {
        out[k] = snapshot[k];
      }
    }
    return out;
  }
};

} // namespace hmi::control
