#pragma once
// Host allocation guard (TS-DET-08, CS-MEM-02): assert that a code path makes no heap allocation.
//
// An L1 app opts in with `ALLOC_GUARD := 1` in its Makefile (before including common.mk). That
// adds tests/host/alloc_guard.cpp and links with -Wl,--wrap=malloc (and calloc, realloc,
// aligned_alloc, posix_memalign, memalign, strdup, strndup); alloc_guard.cpp also replaces
// operator new. A test then arms the guard around the path under test:
//
//   TEST_ASSERT_NO_ALLOC(result = decode(bytes));          // one statement or block
//
//   {                                                      // or by hand
//     host_test::NoAlloc scope;
//     run_the_path();
//     hits = scope.count();
//   }
//   TEST_ASSERT_EQUAL_UINT(0, hits);
//
// While a scope is armed, every allocation by the arming thread is counted; the first one prints
// the thread and a backtrace to stderr. Other threads are not counted (a safety task arms its own
// scope). Put no TEST_ASSERT inside the scope: Unity leaves a failed case by longjmp, which would
// skip the scope's destructor and leave the thread armed for the next case. free() is not an
// allocation. Memory that libstdc++.so or the sanitizer runtime takes with their own internal
// calls is not seen; the code under test reaches operator new and malloc through the symbols above.
//
// spec-deviation(TS-DET-08): host-side `--wrap` on the allocators, as the standard says for linux;
// the on-chip hook (CONFIG_HEAP_USE_HOOKS) is not written yet.

#include "test_case.hpp"

namespace host_test {

/// Arms the allocation guard for the constructing thread until destruction.
class NoAlloc {
public:
  NoAlloc() noexcept;
  ~NoAlloc();
  NoAlloc(const NoAlloc &) = delete;
  NoAlloc &operator=(const NoAlloc &) = delete;

  /// Allocations made by this thread since construction.
  [[nodiscard]] unsigned count() const noexcept;

private:
  unsigned start_;
};

} // namespace host_test

/// Runs the statements and fails the case when they allocated.
#define TEST_ASSERT_NO_ALLOC(...)                                                                  \
  do {                                                                                             \
    unsigned host_test_alloc_hits_ = 0;                                                            \
    {                                                                                              \
      ::host_test::NoAlloc host_test_alloc_scope_;                                                 \
      __VA_ARGS__;                                                                                 \
      host_test_alloc_hits_ = host_test_alloc_scope_.count();                                      \
    }                                                                                              \
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0U, host_test_alloc_hits_,                                      \
                                   "allocation on a path declared allocation-free");               \
  } while (false)
