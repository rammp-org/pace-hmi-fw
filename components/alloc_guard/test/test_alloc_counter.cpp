// L1 cases for the on-chip allocation guard's counting (components/alloc_guard,
// alloc_counter.hpp; REQ-AGC-xx in its README): an allocation counts for the watched slot whose
// stack range holds the stack pointer, once armed, and nowhere else; the count itself is
// lock-free and allocates nothing. One behaviour per case.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include "alloc_guard.hpp"
#include "alloc_guard/alloc_counter.hpp"
#include "test_case.hpp"

namespace {

using hmi::alloc_guard::AllocCounter;
using hmi::alloc_guard::Count;

// Two made-up task stacks, as the firmware watches them: [lo, hi).
constexpr std::uintptr_t ADC_LO = 0x4ff10000;
constexpr std::uintptr_t ADC_HI = 0x4ff11000; // 4096 B
constexpr std::uintptr_t UI_LO = 0x48000000;
constexpr std::uintptr_t UI_HI = 0x48004000; // 16 KB
// An interrupt's stack: in no watched range.
constexpr std::uintptr_t ISR_SP = 0x4ff20800;

// Constant-initialised, as the firmware's is (no constructor runs before main).
constinit AllocCounter<4> g_constinit_counter;

// note() is always inlined (the firmware's hook must make no call), so every call site would
// be a copy of its branches of its own. The cases note through this one site, so its coverage
// reads as one function's, as in the hook.
[[gnu::noinline]] void note(AllocCounter<4> &c, std::uintptr_t sp, std::size_t size) {
  c.note(sp, size);
}

void expect_count(const Count &got, std::uint32_t allocations, std::uint32_t first_size) {
  TEST_ASSERT_EQUAL_UINT32(allocations, got.allocations);
  TEST_ASSERT_EQUAL_UINT32(first_size, got.first_size);
}

} // namespace

TEST_CASE("AGC-001 nothing counts before the counter is armed", "[alloc_guard][REQ-AGC-01]") {
  AllocCounter<4> c;
  c.watch(0, ADC_LO, ADC_HI);
  note(c, ADC_LO + 100, 32);
  TEST_ASSERT_FALSE(c.armed());
  expect_count(c.count(0), 0, 0);
  c.arm();
  TEST_ASSERT_TRUE(c.armed());
  note(c, ADC_LO + 100, 48);
  expect_count(c.count(0), 1, 48);
}

TEST_CASE("AGC-002 once armed, an allocation counts for the slot whose stack holds the stack "
          "pointer, with its size as the first size",
          "[alloc_guard][REQ-AGC-01]") {
  AllocCounter<4> c;
  c.watch(1, UI_LO, UI_HI);
  c.arm();
  note(c, UI_LO + 0x2000, 120);
  expect_count(c.count(1), 1, 120);
  expect_count(c.count(0), 0, 0);
}

TEST_CASE("AGC-003 a stack range includes its low end and excludes its high end",
          "[alloc_guard][REQ-AGC-01]") {
  AllocCounter<4> c;
  c.watch(0, ADC_LO, ADC_HI);
  c.arm();
  note(c, ADC_LO, 1);
  note(c, ADC_HI - 1, 2);
  note(c, ADC_HI, 3);
  note(c, ADC_LO - 1, 4);
  expect_count(c.count(0), 2, 1);
}

TEST_CASE("AGC-004 an allocation on a stack no slot watches (an interrupt's, another task's) "
          "counts nowhere",
          "[alloc_guard][REQ-AGC-02]") {
  AllocCounter<4> c;
  c.watch(0, ADC_LO, ADC_HI);
  c.watch(1, UI_LO, UI_HI);
  c.arm();
  note(c, ISR_SP, 64);
  note(c, 0, 64);
  note(c, UINTPTR_MAX, 64);
  for (std::size_t slot = 0; slot < c.capacity(); ++slot) {
    expect_count(c.count(slot), 0, 0);
  }
}

TEST_CASE("AGC-005 slots count independently and each keeps the size of its first allocation",
          "[alloc_guard][REQ-AGC-03]") {
  AllocCounter<4> c;
  c.watch(0, ADC_LO, ADC_HI);
  c.watch(1, UI_LO, UI_HI);
  c.arm();
  note(c, UI_LO + 8, 200);
  note(c, UI_LO + 16, 300);
  note(c, ADC_LO + 8, 24);
  note(c, UI_LO + 24, 400);
  expect_count(c.count(0), 1, 24);
  expect_count(c.count(1), 3, 200);
}

TEST_CASE("AGC-006 a slot moved to a new stack (its task made again) keeps its count, and the "
          "old stack no longer counts there",
          "[alloc_guard][REQ-AGC-03]") {
  AllocCounter<4> c;
  c.watch(2, ADC_LO, ADC_HI);
  c.arm();
  note(c, ADC_LO + 4, 10);
  c.watch(2, UI_LO, UI_HI);
  TEST_ASSERT_TRUE(c.watched(2));
  note(c, ADC_LO + 4, 11);
  note(c, UI_LO + 4, 12);
  expect_count(c.count(2), 2, 10);
  c.watch(2, UI_LO, UI_HI); // the same range again: no change
  note(c, UI_LO + 8, 13);
  expect_count(c.count(2), 3, 10);
}

TEST_CASE("AGC-007 an empty range or a slot out of range is ignored, and reads as not watched "
          "and nothing counted",
          "[alloc_guard][REQ-AGC-03]") {
  AllocCounter<4> c;
  TEST_ASSERT_EQUAL_UINT32(4U, c.capacity());
  c.watch(0, ADC_HI, ADC_LO); // lo > hi
  c.watch(1, ADC_LO, ADC_LO); // empty
  c.watch(4, ADC_LO, ADC_HI); // no slot 4
  c.arm();
  note(c, ADC_LO + 4, 8);
  TEST_ASSERT_FALSE(c.watched(0));
  TEST_ASSERT_FALSE(c.watched(1));
  TEST_ASSERT_FALSE(c.watched(4));
  expect_count(c.count(0), 0, 0);
  expect_count(c.count(1), 0, 0);
  expect_count(c.count(4), 0, 0);
}

TEST_CASE("AGC-008 allocations noted from several threads at once are all counted",
          "[alloc_guard][REQ-AGC-04]") {
  AllocCounter<4> c;
  constexpr std::size_t THREADS = 4;
  constexpr std::uint32_t PER_THREAD = 20000;
  constexpr std::uintptr_t STACK = 0x1000;
  for (std::size_t t = 0; t < THREADS; ++t) {
    c.watch(t, ADC_LO + t * STACK, ADC_LO + (t + 1) * STACK);
  }
  c.arm();
  std::atomic<bool> go{false};
  std::vector<std::thread> threads;
  threads.reserve(THREADS);
  for (std::size_t t = 0; t < THREADS; ++t) {
    // every thread notes into its own slot and into slot 0, so slot 0 is contended
    threads.emplace_back([&c, &go, t] {
      while (!go.load()) {
      }
      for (std::uint32_t i = 0; i < PER_THREAD; ++i) {
        note(c, ADC_LO + t * STACK + 64, 16);
        note(c, ADC_LO + 64, 16);
      }
    });
  }
  go.store(true);
  for (std::thread &th : threads) {
    th.join();
  }
  TEST_ASSERT_EQUAL_UINT32(PER_THREAD * (THREADS + 1), c.count(0).allocations);
  for (std::size_t t = 1; t < THREADS; ++t) {
    TEST_ASSERT_EQUAL_UINT32(PER_THREAD, c.count(t).allocations);
  }
}

TEST_CASE("AGC-009 noting, watching, arming and reading allocate nothing",
          "[alloc_guard][REQ-AGC-04]") {
  AllocCounter<4> c;
  TEST_ASSERT_NO_ALLOC({
    c.watch(0, ADC_LO, ADC_HI);
    c.arm();
    note(c, ADC_LO + 4, 8);
    note(c, ISR_SP, 8);
    static_cast<void>(c.count(0));
    static_cast<void>(c.watched(0));
  });
  expect_count(c.count(0), 1, 8);
}

TEST_CASE("AGC-010 a constant-initialised counter starts empty and disarmed",
          "[alloc_guard][REQ-AGC-01]") {
  TEST_ASSERT_FALSE(g_constinit_counter.armed());
  for (std::size_t slot = 0; slot < g_constinit_counter.capacity(); ++slot) {
    TEST_ASSERT_FALSE(g_constinit_counter.watched(slot));
    expect_count(g_constinit_counter.count(slot), 0, 0);
  }
}
