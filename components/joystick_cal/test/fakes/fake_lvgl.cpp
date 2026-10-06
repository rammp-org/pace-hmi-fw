// The fake LVGL behind fakes/lvgl.h (test only). See that header for what it copies from LVGL 9.

#include <cstdio>
#include <cstdlib>

#include "lvgl.h"

namespace {

constexpr std::size_t kMaxTimers = 4;
lv_timer_t g_timers[kMaxTimers];
std::size_t g_timer_count = 0;

[[noreturn]] void fake_fail(const char *what) {
  std::fprintf(stderr, "fake_lvgl: %s\n", what);
  std::abort(); // test-only fake: a misuse is a broken test, not a verdict to recover from
}

void notify(lv_subject_t *subject) {
  for (std::size_t i = 0; i < subject->observer_count; ++i) {
    subject->observers[i].cb(&subject->observers[i], subject);
  }
}

lv_observer_t *add_observer(lv_subject_t *subject, lv_observer_cb_t cb, lv_obj_t *obj) {
  if (subject->observer_count == lv_subject_t::kMaxObservers) {
    fake_fail("too many observers on one subject");
  }
  lv_observer_t &o = subject->observers[subject->observer_count++];
  o = {.subject = subject, .cb = cb, .target = obj};
  cb(&o, subject); // LVGL calls a new observer once at once
  return &o;
}

void label_follow_subject(lv_observer_t *observer, lv_subject_t *subject) {
  observer->target->text = subject->text;
}

} // namespace

void lv_subject_init_string(lv_subject_t *subject, char * /*buf*/, char * /*prev_buf*/,
                            size_t /*size*/, const char *value) {
  *subject = lv_subject_t{};
  subject->text = value;
}

void lv_subject_init_int(lv_subject_t *subject, int32_t value) {
  *subject = lv_subject_t{};
  subject->value = value;
}

void lv_subject_copy_string(lv_subject_t *subject, const char *buf) {
  subject->text = buf;
  notify(subject);
}

void lv_subject_set_int(lv_subject_t *subject, int32_t value) {
  subject->value = value;
  notify(subject);
}

int32_t lv_subject_get_int(lv_subject_t *subject) { return subject->value; }

lv_observer_t *lv_subject_add_observer_obj(lv_subject_t *subject, lv_observer_cb_t cb,
                                           lv_obj_t *obj, void * /*user_data*/) {
  return add_observer(subject, cb, obj);
}

lv_obj_t *lv_observer_get_target_obj(lv_observer_t *observer) { return observer->target; }

lv_observer_t *lv_label_bind_text(lv_obj_t *obj, lv_subject_t *subject, const char * /*fmt*/) {
  obj->bound = subject;
  return add_observer(subject, label_follow_subject, obj);
}

char *lv_label_get_text(const lv_obj_t *obj) { return const_cast<char *>(obj->text.c_str()); }

void lv_label_set_text(lv_obj_t *obj, const char *text) { obj->text = text; }

void lv_obj_set_style_text_opa(lv_obj_t *obj, lv_opa_t value, lv_style_selector_t /*selector*/) {
  obj->text_opa = value;
}

void *lv_obj_add_event_cb(lv_obj_t *obj, lv_event_cb_t event_cb, lv_event_code_t filter,
                          void * /*user_data*/) {
  if (obj->event_count == lv_obj_t::kMaxEvents) {
    fake_fail("too many event callbacks on one object");
  }
  obj->events[obj->event_count++] = {.cb = event_cb, .code = filter};
  return &obj->events[obj->event_count - 1];
}

lv_timer_t *lv_timer_create(lv_timer_cb_t timer_xcb, uint32_t period, void * /*user_data*/) {
  if (g_timer_count == kMaxTimers) {
    fake_fail("too many timers");
  }
  lv_timer_t &t = g_timers[g_timer_count++];
  t = {.cb = timer_xcb, .period_ms = period, .paused = false};
  return &t;
}

void lv_timer_pause(lv_timer_t *timer) { timer->paused = true; }

void lv_timer_resume(lv_timer_t *timer) { timer->paused = false; }

namespace fake_lvgl {

lv_timer_t *last_timer() { return g_timer_count == 0 ? nullptr : &g_timers[g_timer_count - 1]; }

bool tick(lv_timer_t *timer) {
  if (timer == nullptr || timer->paused) {
    return false;
  }
  timer->cb(timer);
  return true;
}

void send_event(lv_obj_t *obj, lv_event_code_t code) {
  for (std::size_t i = 0; i < obj->event_count; ++i) {
    if (obj->events[i].code == code) {
      lv_event_t e{.code = code};
      obj->events[i].cb(&e);
    }
  }
}

std::string label_text(const lv_obj_t *obj) { return obj->text; }

} // namespace fake_lvgl
