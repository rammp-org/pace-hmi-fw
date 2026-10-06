// Fixture for `run.py selftest`: each case drives one verdict path of the runner. Run one
// case at a time (TEST_FILTER=FIX-00n); only FIX-001 is meant to pass.

#include <climits>
#include <vector>

#include "test_case.hpp"

TEST_CASE("FIX-001 a passing case passes", "[fixture]") { TEST_ASSERT_EQUAL_INT(2, 1 + 1); }

TEST_CASE("FIX-002 a failed assertion fails", "[fixture]") { TEST_ASSERT_EQUAL_INT(3, 1 + 1); }

TEST_CASE("FIX-003 signed overflow is caught by UBSan", "[fixture]") {
  volatile int big = INT_MAX;
  const int sum = big + 1;
  TEST_ASSERT_TRUE(sum != 0);
}

TEST_CASE("FIX-004 a heap overrun is caught by ASan", "[fixture]") {
  std::vector<int> values(2, 0);
  volatile std::size_t index = 2;
  TEST_ASSERT_EQUAL_INT(0, values.data()[index]);
}

TEST_CASE("FIX-005 an ignored case is not a pass", "[fixture]") { TEST_IGNORE(); }
