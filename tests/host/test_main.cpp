// main() for host L1 apps: runs every TEST_CASE registered through test_case.hpp as a Unity
// test and prints Unity's summary line. Exit code 0 = PASS (AGENT_BRIEF host L1 contract).
//
// Usage: test_<name> [--list] [<case-name prefix>...]
//   no prefix   run every case
//   prefixes    run only the cases whose name starts with one of them (e.g. JOY-007);
//               a prefix that matches no case fails the run, so a typo cannot pass
//   --list      print the case names and exit
// HOST_TEST_SEED (environment): 0 or unset keeps the declared order; any other value
// shuffles the cases with that seed, which is printed (TS-DET-02).

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "test_case.hpp"

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

namespace {

struct Case {
  const char *name;
  const char *tags;
  host_test::TestFn fn;
  const char *file;
  int line;
};

constexpr std::size_t MAX_CASES = 512;
constinit std::array<Case, MAX_CASES> g_cases{};
constinit std::size_t g_count = 0;
constinit bool g_overflow = false;

std::uint64_t splitmix64(std::uint64_t &state) {
  state += 0x9E3779B97F4A7C15ULL;
  std::uint64_t z = state;
  z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31U);
}

std::uint64_t read_seed() {
  const char *text = std::getenv("HOST_TEST_SEED");
  if (text == nullptr) {
    return 0;
  }
  std::uint64_t seed = 0;
  const std::string_view view{text};
  const auto [ptr, ec] = std::from_chars(view.data(), view.data() + view.size(), seed);
  if (ec != std::errc{} || ptr != view.data() + view.size()) {
    std::printf("HOST_TEST bad HOST_TEST_SEED '%s'\n", text);
    std::exit(2);
  }
  return seed;
}

bool matches(const char *name, int argc, char **argv, int first_filter) {
  if (first_filter >= argc) {
    return true;
  }
  const std::string_view case_name{name};
  for (int i = first_filter; i < argc; ++i) {
    if (case_name.starts_with(std::string_view{argv[i]})) {
      return true;
    }
  }
  return false;
}

} // namespace

host_test::Registrar::Registrar(const char *name, const char *tags, TestFn fn, const char *file,
                                int line) noexcept {
  if (g_count >= MAX_CASES) {
    g_overflow = true;
    return;
  }
  g_cases[g_count] = Case{name, tags, fn, file, line};
  ++g_count;
}

int main(int argc, char **argv) {
  if (g_overflow) {
    std::printf("HOST_TEST more than %zu cases: raise MAX_CASES\n", MAX_CASES);
    return 2;
  }
  int first_filter = 1;
  if (argc > 1 && std::strcmp(argv[1], "--list") == 0) {
    for (std::size_t i = 0; i < g_count; ++i) {
      std::printf("%s %s\n", g_cases[i].name, g_cases[i].tags);
    }
    return 0;
  }

  std::array<std::size_t, MAX_CASES> order{};
  for (std::size_t i = 0; i < g_count; ++i) {
    order[i] = i;
  }
  const std::uint64_t seed = read_seed();
  if (seed != 0 && g_count > 1) {
    std::uint64_t state = seed;
    for (std::size_t i = g_count - 1; i > 0; --i) { // Fisher-Yates, bounded by g_count
      const auto j = static_cast<std::size_t>(splitmix64(state) % (i + 1U));
      const std::size_t tmp = order[i];
      order[i] = order[j];
      order[j] = tmp;
    }
  }
  std::printf("HOST_TEST seed %llu cases %zu\n", static_cast<unsigned long long>(seed), g_count);

  // a filter that selects nothing is an error, never a vacuous pass
  for (int i = first_filter; i < argc; ++i) {
    bool found = false;
    for (std::size_t c = 0; c < g_count && !found; ++c) {
      found = std::string_view{g_cases[c].name}.starts_with(std::string_view{argv[i]});
    }
    if (!found) {
      std::printf("HOST_TEST no case matches '%s'\n", argv[i]);
      return 2;
    }
  }

  UnityBegin("host_test");
  for (std::size_t i = 0; i < g_count; ++i) {
    const Case &c = g_cases[order[i]];
    if (!matches(c.name, argc, argv, first_filter)) {
      continue;
    }
    UnitySetTestFile(c.file);
    UnityDefaultTestRun(c.fn, c.name, c.line);
  }
  const int failures = UnityEnd();
  if (Unity.NumberOfTests == 0U) {
    std::printf("HOST_TEST no case ran\n");
    return 1;
  }
  if (Unity.TestIgnores != 0U) {
    std::printf("HOST_TEST %u ignored: SKIP is declared in the manifest, never at run time\n",
                static_cast<unsigned>(Unity.TestIgnores));
    return 1;
  }
  return failures == 0 ? 0 : 1;
}
