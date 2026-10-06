#pragma once
// TEST_CASE shim for host L1 apps (AGENT_BRIEF "host L1 app contract", TS-UNIT-02).
//
// Write a case as on ESP-IDF:
//
//   #include "test_case.hpp"
//   TEST_CASE("JOY-001 a centred stick reads zero", "[joystick]") { TEST_ASSERT_...; }
//
// Each case registers itself before main() and becomes one Unity test, run by the main in
// tests/host/test_main.cpp (linked in by tests/host/common.mk). The name starts with the
// test ID and then a sentence stating the behaviour. Registration uses a fixed-size table:
// no allocation (CS-MEM-01).

#include "unity.h"

namespace host_test {

using TestFn = void (*)();

/// Registers one case at static-initialisation time. Use TEST_CASE, not this directly.
struct Registrar {
  Registrar(const char *name, const char *tags, TestFn fn, const char *file, int line) noexcept;
};

} // namespace host_test

#define HOST_TEST_CAT_(a, b) a##b
#define HOST_TEST_CAT(a, b) HOST_TEST_CAT_(a, b)
#define HOST_TEST_CASE_(name, tags, fn)                                                            \
  static void fn();                                                                                \
  [[maybe_unused]] static const ::host_test::Registrar HOST_TEST_CAT(fn, _registrar){              \
      (name), (tags), &fn, __FILE__, __LINE__};                                                    \
  static void fn()
#define TEST_CASE(name, tags)                                                                      \
  HOST_TEST_CASE_(name, tags, HOST_TEST_CAT(host_test_case_, __COUNTER__))
