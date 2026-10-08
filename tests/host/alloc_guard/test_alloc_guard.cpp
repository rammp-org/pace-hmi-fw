// Cases for the host allocation guard (tests/host/alloc_guard.{hpp,cpp}, TS-DET-08): the guard
// counts the allocation forms while armed and nothing else. The "counted" cases are the proof
// that the guard fails a test whose path allocates (TEST_ASSERT_NO_ALLOC reads the same count).

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "alloc_guard.hpp"
#include "test_case.hpp"

namespace {

// Keeps the compiler from removing an allocation whose result is unused, then frees it.
void sink(void *p) {
  asm volatile("" : : "r"(p) : "memory");
  std::free(p);
}

void release(void *p) { std::free(p); }

unsigned count_of(void (*body)()) {
  unsigned hits = 0;
  {
    host_test::NoAlloc scope;
    body();
    hits = scope.count();
  }
  return hits;
}

} // namespace

TEST_CASE("AGD-001 a scope with no allocation in it counts zero", "[alloc_guard]") {
  unsigned hits = 99;
  {
    host_test::NoAlloc scope;
    hits = scope.count();
  }
  TEST_ASSERT_EQUAL_UINT(0U, hits);
}

TEST_CASE("AGD-002 malloc inside the scope is counted", "[alloc_guard]") {
  TEST_ASSERT_EQUAL_UINT(1U, count_of([] { sink(std::malloc(16)); }));
}

TEST_CASE("AGD-003 calloc, realloc and strdup inside the scope are counted", "[alloc_guard]") {
  TEST_ASSERT_EQUAL_UINT(1U, count_of([] { sink(std::calloc(4, 4)); }));
  TEST_ASSERT_EQUAL_UINT(2U, count_of([] { sink(std::realloc(std::malloc(8), 64)); }));
  TEST_ASSERT_EQUAL_UINT(1U, count_of([] { sink(strdup("guard")); }));
}

TEST_CASE("AGD-004 operator new, a growing vector and a long string are counted", "[alloc_guard]") {
  TEST_ASSERT_EQUAL_UINT(1U, count_of([] { delete new int(7); }));
  TEST_ASSERT_TRUE(count_of([] {
                     std::vector<int> v;
                     v.push_back(1);
                   }) >= 1U);
  TEST_ASSERT_TRUE(count_of([] { std::string s(200, 'x'); }) >= 1U);
}

TEST_CASE("AGD-005 aligned allocation forms are counted", "[alloc_guard]") {
  TEST_ASSERT_EQUAL_UINT(1U, count_of([] { sink(std::aligned_alloc(64, 64)); }));
  TEST_ASSERT_EQUAL_UINT(1U, count_of([] {
                           void *p = nullptr;
                           (void)!posix_memalign(&p, 32, 32);
                           sink(p);
                         }));
}

TEST_CASE("AGD-006 a path that only uses the stack is not counted, and free is not an allocation",
          "[alloc_guard]") {
  void *outside = std::malloc(8);
  unsigned char buf[64];
  TEST_ASSERT_NO_ALLOC({
    std::memset(buf, 0x5a, sizeof buf);
    release(outside);
  });
  TEST_ASSERT_EQUAL_UINT8(0x5a, buf[63]);
}

TEST_CASE("AGD-007 the count is per scope: a later scope starts from zero", "[alloc_guard]") {
  unsigned first = 0;
  {
    host_test::NoAlloc scope;
    sink(std::malloc(4));
    sink(std::malloc(4));
    first = scope.count();
  }
  TEST_ASSERT_EQUAL_UINT(2U, first);
  TEST_ASSERT_NO_ALLOC(static_cast<void>(first));
}

TEST_CASE("AGD-008 allocation after the scope has ended is not counted", "[alloc_guard]") {
  unsigned inside = 0;
  {
    host_test::NoAlloc scope;
    inside = scope.count();
  }
  host_test::NoAlloc later;
  const unsigned before = later.count();
  TEST_ASSERT_EQUAL_UINT(0U, inside);
  TEST_ASSERT_EQUAL_UINT(0U, before);
}

TEST_CASE("AGD-009 another thread's allocation is not counted against this thread's scope",
          "[alloc_guard]") {
  std::atomic<int> state{0};
  std::thread worker([&state] {
    while (state.load() == 0) {
    }
    sink(std::malloc(32));
    state.store(2);
  });
  unsigned hits = 0;
  {
    host_test::NoAlloc scope;
    state.store(1);
    while (state.load() != 2) {
    }
    hits = scope.count();
  }
  worker.join();
  TEST_ASSERT_EQUAL_UINT(0U, hits);
}
