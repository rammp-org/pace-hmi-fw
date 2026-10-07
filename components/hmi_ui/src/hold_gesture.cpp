// HoldEngine: runs the push-and-hold gestures (moved from main/frag_hold.inc).

#include "hmi_ui/hold_gesture.hpp"

void hmi::ui::HoldEngine::anim_exec_cb(void *var, int32_t value) {
  lv_subject_set_int(&static_cast<HoldGesture *>(var)->progress, value);
}

void hmi::ui::HoldEngine::reset(HoldGesture *g) {
  lv_anim_delete(g, anim_exec_cb);
  lv_subject_set_int(&g->progress, 0);
}

void hmi::ui::HoldEngine::anim_completed_cb(lv_anim_t *a) {
  auto *g = static_cast<HoldGesture *>(lv_anim_get_user_data(a));
  // Disarm before running the action: `completed` navigates, and the input is
  // still held at this instant, so whatever gesture watches that input on the
  // far side must not read the same unbroken hold as a fresh one.
  *g->armed = false;
  g->holding = false;
  // Deleting the animation from inside its own completed_cb (via reset) is
  // safe and expected: lv_anim.c removes it from the list before calling us,
  // and anim_timer explicitly re-reads the list afterwards.
  reset(g);
  g->engine->config_.confirm();
  g->completed();
}

// Runs on the LVGL task rather than the ADC/button tasks that produce the
// inputs: everything here is LVGL state, and this way each animation is created
// and destroyed on the task that runs it.
void hmi::ui::HoldEngine::poll(HoldGesture *g) const {
  const bool held = g->is_held();
  if (!held) {
    *g->armed = true;
  }
  const bool hold = held && *g->armed && g->applies();
  if (hold == g->holding) {
    return;
  }
  g->holding = hold;
  if (!hold) {
    reset(g);
    return;
  }
  g->engine = this;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, g);
  lv_anim_set_user_data(&a, g);
  lv_anim_set_exec_cb(&a, anim_exec_cb);
  lv_anim_set_values(&a, 0, HOLD_MAX);
  lv_anim_set_duration(&a, HOLD_MS);
  // The delay holds act_time negative, so the exec callback does not run and the
  // widget stays empty until it elapses. Completion lands at grace + duration,
  // which is what a gesture with a grace period costs in total.
  lv_anim_set_delay(&a, g->grace_ms);
  lv_anim_set_completed_cb(&a, anim_completed_cb);
  lv_anim_start(&a);
}
