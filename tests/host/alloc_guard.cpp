// Host allocation guard: see alloc_guard.hpp. Linked only by apps with ALLOC_GUARD := 1, which
// also pass -Wl,--wrap=<allocator> for each __wrap_ function below.

#include "alloc_guard.hpp"

#include <execinfo.h>
#include <pthread.h>
#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

constinit thread_local int t_armed = 0;
constinit thread_local unsigned t_hits = 0;
constinit thread_local bool t_reporting = false;

/// Prints the thread and a backtrace of an allocation. Uses write(2) and backtrace_symbols_fd,
/// which do not allocate; t_reporting stops a recursion if backtrace() itself does.
void report(const char *what) {
  char name[32] = "?";
  (void)pthread_getname_np(pthread_self(), name, sizeof name);
  char line[128];
  const int n =
      std::snprintf(line, sizeof line, "alloc_guard: %s while armed, thread \"%s\"\n", what, name);
  if (n > 0) {
    (void)!write(2, line, static_cast<std::size_t>(n));
  }
  void *frames[24];
  const int depth = backtrace(frames, 24);
  backtrace_symbols_fd(frames, depth, 2);
}

void note(const char *what) noexcept {
  if (t_armed == 0 || t_reporting) {
    return;
  }
  t_reporting = true;
  ++t_hits;
  if (t_hits == 1) {
    report(what);
  }
  t_reporting = false;
}

} // namespace

namespace host_test {

NoAlloc::NoAlloc() noexcept
    : start_(t_hits) {
  t_armed = 1;
}

NoAlloc::~NoAlloc() { t_armed = 0; }

unsigned NoAlloc::count() const noexcept { return t_hits - start_; }

} // namespace host_test

extern "C" {

void *__real_malloc(std::size_t);
void *__real_calloc(std::size_t, std::size_t);
void *__real_realloc(void *, std::size_t);
void *__real_aligned_alloc(std::size_t, std::size_t);
int __real_posix_memalign(void **, std::size_t, std::size_t);
void *__real_memalign(std::size_t, std::size_t);
char *__real_strdup(const char *);
char *__real_strndup(const char *, std::size_t);

void *__wrap_malloc(std::size_t size) {
  note("malloc");
  return __real_malloc(size);
}
void *__wrap_calloc(std::size_t n, std::size_t size) {
  note("calloc");
  return __real_calloc(n, size);
}
void *__wrap_realloc(void *p, std::size_t size) {
  note("realloc");
  return __real_realloc(p, size);
}
void *__wrap_aligned_alloc(std::size_t align, std::size_t size) {
  note("aligned_alloc");
  return __real_aligned_alloc(align, size);
}
int __wrap_posix_memalign(void **out, std::size_t align, std::size_t size) {
  note("posix_memalign");
  return __real_posix_memalign(out, align, size);
}
void *__wrap_memalign(std::size_t align, std::size_t size) {
  note("memalign");
  return __real_memalign(align, size);
}
char *__wrap_strdup(const char *s) {
  note("strdup");
  return __real_strdup(s);
}
char *__wrap_strndup(const char *s, std::size_t n) {
  note("strndup");
  return __real_strndup(s, n);
}

} // extern "C"

// operator new: libstdc++.so's own call to malloc is not wrapped, so the replaceable forms are
// defined here. They count, then call the real allocator. The array forms and the nothrow-delete
// forms default to these.

namespace {

void *alloc_or_null(std::size_t size, const char *what) noexcept {
  note(what);
  return __real_malloc(size == 0 ? 1 : size);
}

void *aligned_or_null(std::size_t size, std::align_val_t align, const char *what) noexcept {
  note(what);
  std::size_t a = static_cast<std::size_t>(align);
  if (a < sizeof(void *)) {
    a = sizeof(void *);
  }
  void *p = nullptr;
  if (__real_posix_memalign(&p, a, size == 0 ? 1 : size) != 0) {
    return nullptr;
  }
  return p;
}

} // namespace

void *operator new(std::size_t size) {
  if (void *p = alloc_or_null(size, "operator new")) {
    return p;
  }
  throw std::bad_alloc();
}
void *operator new(std::size_t size, const std::nothrow_t &) noexcept {
  return alloc_or_null(size, "operator new");
}
void *operator new(std::size_t size, std::align_val_t align) {
  if (void *p = aligned_or_null(size, align, "operator new (aligned)")) {
    return p;
  }
  throw std::bad_alloc();
}
void *operator new(std::size_t size, std::align_val_t align, const std::nothrow_t &) noexcept {
  return aligned_or_null(size, align, "operator new (aligned)");
}

void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete(void *p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void *p, std::size_t, std::align_val_t) noexcept { std::free(p); }
