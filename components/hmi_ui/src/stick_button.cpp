// StickButton: what one edge of the stick button does to the UI (moved from
// main/frag_stick_button.inc, stick_button_edge; the LVGL lock stays with main's caller).

#include "hmi_ui/stick_button.hpp"

#include <cstdint>

#include "esp_timer.h"

void hmi::ui::StickButton::edge(bool active) {
  // the panel follows every edge, so contact bounce can't strand it blue: the
  // last edge always wins
  lv_subject_set_int(config_.view->pressed(), active);
  // undebounced on purpose: the MCB wants the raw line state, and this is the
  // same every-edge signal the panel follows
  config_.level->store(active);

  // What the edge means (hmi::stick::ButtonEdges): a release that ends a short
  // press selects; only the counter is debounced.
  const int64_t now = esp_timer_get_time();
  const hmi::stick::ButtonEdges::Effect effect = edges_.edge(active, now);
  if (effect.select) {
    config_.select->store(true);
  }
  if (effect.count) {
    lv_subject_set_int(config_.view->count(), lv_subject_get_int(config_.view->count()) + 1);
  }
}
