#pragma once
// Test-only stand-in for LVGL: just the calls main/joystick_cal.cpp makes, so the host L1 app
// can compile that file unmodified and drive its calibration run (characterisation, step 10).
// Found before the real lvgl.h because this folder is on the app's include path and LVGL's is
// not. Behaviour copied from LVGL 9 where the run depends on it:
// - adding an observer calls it once at once;
// - every lv_subject_set_int / lv_subject_copy_string notifies every observer;
// - a label bound to a string subject shows the subject's string;
// - a paused timer is not called (fake_lvgl::tick returns false).
// Fixed-size tables, no allocation in the fake itself (std::string holds the texts).

#include <cstddef>
#include <cstdint>
#include <string>

using lv_opa_t = uint8_t;
using lv_style_selector_t = uint32_t;
enum : lv_opa_t { LV_OPA_TRANSP = 0, LV_OPA_COVER = 255 };
enum : lv_style_selector_t { LV_PART_MAIN = 0 };
enum lv_event_code_t { LV_EVENT_CLICKED = 10, LV_EVENT_SCREEN_UNLOAD_START = 40 };

struct lv_obj_t;
struct lv_subject_t;
struct lv_timer_t;
struct lv_event_t {
  lv_event_code_t code;
};
struct lv_observer_t;

using lv_timer_cb_t = void (*)(lv_timer_t *);
using lv_event_cb_t = void (*)(lv_event_t *);
using lv_observer_cb_t = void (*)(lv_observer_t *, lv_subject_t *);

struct lv_observer_t {
  lv_subject_t *subject = nullptr;
  lv_observer_cb_t cb = nullptr;
  lv_obj_t *target = nullptr;
};

struct lv_subject_t {
  static constexpr std::size_t kMaxObservers = 4;
  int32_t value = 0;
  std::string text;
  lv_observer_t observers[kMaxObservers]{};
  std::size_t observer_count = 0;
};

struct lv_obj_t {
  static constexpr std::size_t kMaxEvents = 4;
  std::string text; ///< a label's text
  lv_opa_t text_opa = LV_OPA_COVER;
  lv_subject_t *bound = nullptr; ///< lv_label_bind_text
  struct Event {
    lv_event_cb_t cb;
    lv_event_code_t code;
  } events[kMaxEvents]{};
  std::size_t event_count = 0;
};

struct lv_timer_t {
  lv_timer_cb_t cb = nullptr;
  uint32_t period_ms = 0;
  bool paused = false;
};

void lv_subject_init_string(lv_subject_t *subject, char *buf, char *prev_buf, size_t size,
                            const char *value);
void lv_subject_init_int(lv_subject_t *subject, int32_t value);
void lv_subject_copy_string(lv_subject_t *subject, const char *buf);
void lv_subject_set_int(lv_subject_t *subject, int32_t value);
int32_t lv_subject_get_int(const lv_subject_t *subject);
lv_observer_t *lv_subject_add_observer_obj(lv_subject_t *subject, lv_observer_cb_t cb,
                                           lv_obj_t *obj, void *user_data);
lv_obj_t *lv_observer_get_target_obj(const lv_observer_t *observer);
lv_observer_t *lv_label_bind_text(lv_obj_t *obj, lv_subject_t *subject, const char *fmt);
char *lv_label_get_text(const lv_obj_t *obj);
void lv_label_set_text(lv_obj_t *obj, const char *text);
void lv_obj_set_style_text_opa(lv_obj_t *obj, lv_opa_t value, lv_style_selector_t selector);
void *lv_obj_add_event_cb(lv_obj_t *obj, lv_event_cb_t event_cb, lv_event_code_t filter,
                          void *user_data);
lv_timer_t *lv_timer_create(lv_timer_cb_t timer_xcb, uint32_t period, void *user_data);
void lv_timer_pause(lv_timer_t *timer);
void lv_timer_resume(lv_timer_t *timer);

/// Test-side controls of the fake.
namespace fake_lvgl {
/// The timer lv_timer_create made most recently, or nullptr.
lv_timer_t *last_timer();
/// One LVGL timer period: calls the timer's callback unless it is paused. Returns whether it ran.
bool tick(lv_timer_t *timer);
/// Sends `code` to every callback `obj` registered for it.
void send_event(lv_obj_t *obj, lv_event_code_t code);
/// What a label shows: its bound subject's string, else its own text.
std::string label_text(const lv_obj_t *obj);
} // namespace fake_lvgl
