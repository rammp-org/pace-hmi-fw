#pragma once
// Facts for the POST tests: a healthy boot, and a setter that makes one check measure a chosen
// value without disturbing any other check.

#include <cstdint>

#include "post/post.hpp"

namespace post_test {

using hmi::post::Facts;
using hmi::post::Id;

/// Board 2-like numbers where every check passes (stack and heap from the 2026-10-06 profile).
inline Facts good_facts() {
  using namespace hmi::post;
  Facts f;
  const AxisRest still{.mean_mv = 1650, .min_mv = 1649, .max_mv = 1651, .samples = 30};
  const AxisRest twist{.mean_mv = 1650, .min_mv = 1620, .max_mv = 1680, .samples = 30};
  f.window = StickWindow{.cycles = 30,
                         .valid_cycles = 30,
                         .x = still,
                         .y = still,
                         .twist = twist,
                         .button_idle = true};
  const AxisCal axis{.min_mv = 50, .centre_mv = 1650, .max_mv = 3250};
  f.cal = Calibration{.saved = true, .x = axis, .y = axis, .twist = axis};
  I2cSet found;
  for (const uint8_t a : EXPECTED_I2C) {
    found.add(a);
  }
  f.i2c = found;
  f.reset = ResetReason::POWERON;
  f.image = ImageFacts{.verified = true, .state = OtaImageState::VALID};
  f.memory = MemoryFacts{.internal_free_min_b = 30000,
                         .internal_largest_b = 22000,
                         .dma_free_min_b = 2400,
                         .psram_free_b = 28'000'000};
  f.stacks = StackFacts{.adc_free_b = 1496, .ui_free_b = 11060};
  return f;
}

/// The window axis that a REST_* or STILL_* check reads.
inline hmi::post::AxisRest hmi::post::StickWindow::*rest_axis(Id id) {
  using hmi::post::StickWindow;
  if (id == Id::REST_X || id == Id::STILL_X) {
    return &StickWindow::x;
  }
  if (id == Id::REST_Y || id == Id::STILL_Y) {
    return &StickWindow::y;
  }
  return &StickWindow::twist;
}

/// @brief Changes `f` so that check `id` measures `value` and every other check is unchanged.
/// @return false when no facts measure that value for that check (e.g. a negative offset)
inline bool set_measured(Facts &f, Id id, int32_t value) {
  using namespace hmi::post;
  switch (id) {
  case Id::ADC_VALID: // over 1000 cycles, the share in 0.1 % is the valid count
    if (value < 0 || value > 1000) {
      return false;
    }
    f.window->cycles = 1000;
    f.window->valid_cycles = static_cast<uint32_t>(value);
    return true;
  case Id::CAL_SAVED:
    if (value != 0 && value != 1) {
      return false;
    }
    f.cal->saved = value == 1;
    return true;
  case Id::CAL_SPAN: // x's travel either side of 1650; y and twist keep 1600
    if (value < 0 || value > 1600) {
      return false;
    }
    f.cal->x = AxisCal{.min_mv = 1650 - value, .centre_mv = 1650, .max_mv = 1650 + value};
    return true;
  case Id::I2C_MISSING: {
    if (value < 0 || value > static_cast<int32_t>(EXPECTED_I2C.size())) {
      return false;
    }
    I2cSet found;
    for (std::size_t i = static_cast<std::size_t>(value); i < EXPECTED_I2C.size(); ++i) {
      found.add(EXPECTED_I2C[i]);
    }
    f.i2c = found;
    return true;
  }
  case Id::RESET_CLEAN:
    if (value != 0 && value != 1) {
      return false;
    }
    f.reset = value == 1 ? ResetReason::POWERON : ResetReason::PANIC;
    return true;
  case Id::IMAGE_OK:
    if (value != 0 && value != 1) {
      return false;
    }
    f.image->state = value == 1 ? OtaImageState::VALID : OtaImageState::INVALID;
    return true;
  case Id::MEM_INT_MIN:
    f.memory->internal_free_min_b = value;
    return true;
  case Id::MEM_INT_BLOCK:
    f.memory->internal_largest_b = value;
    return true;
  case Id::MEM_DMA_MIN:
    f.memory->dma_free_min_b = value;
    return true;
  case Id::MEM_PSRAM_FREE:
    f.memory->psram_free_b = value;
    return true;
  case Id::STK_ADC:
    f.stacks->adc_free_b = value;
    return true;
  case Id::STK_UI:
    f.stacks->ui_free_b = value;
    return true;
  case Id::REST_X:
  case Id::REST_Y:
  case Id::REST_TWIST: { // move the whole window: same stillness, new offset
    if (value < 0 || value > 1000) {
      return false;
    }
    AxisRest &a = (*f.window).*rest_axis(id);
    const int32_t shift = 1650 + value - a.mean_mv;
    a.mean_mv += shift;
    a.min_mv += shift;
    a.max_mv += shift;
    return true;
  }
  case Id::STILL_X:
  case Id::STILL_Y:
  case Id::STILL_TWIST: { // spread around the same mean: same offset, new stillness
    if (value < 0 || value > 1000) {
      return false;
    }
    AxisRest &a = (*f.window).*rest_axis(id);
    a.min_mv = a.mean_mv - value / 2;
    a.max_mv = a.min_mv + value;
    return true;
  }
  case Id::BUTTON_IDLE:
    if (value != 0 && value != 1) {
      return false;
    }
    f.window->button_idle = value == 1;
    return true;
  case Id::COUNT:
  default:
    return false;
  }
}

} // namespace post_test
