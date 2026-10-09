// Playing the hand-written goldens (goldens.hpp).

#include "goldens.hpp"

#include <cstdio>
#include <string>
#include <vector>

#include "world.hpp"

namespace golden {

namespace {

std::string joined(const std::vector<std::string> &tokens) {
  std::string out;
  for (const std::string &t : tokens) {
    out += out.empty() ? t : ", " + t;
  }
  return out.empty() ? "(nothing)" : out;
}

} // namespace

Golden golden_by_id(const std::string &id) {
  for (std::vector<Golden> set : {c1_goldens(), c3_goldens()}) {
    for (Golden &g : set) {
      if (g.name.starts_with(id + " ")) {
        return std::move(g);
      }
    }
  }
  return Golden{};
}

std::size_t run_golden(const Golden &g, const Target &target) {
  reset_world();
  target.reset();
  clear_logs();
  std::size_t differences = 0;
  for (std::size_t i = 0; i < g.steps.size(); ++i) {
    const GoldenStep &s = g.steps[i];
    filtered_log().clear();
    const unsigned errors_before = world().errors;
    play_step(s.step, target);
    if (!s.checked) {
      continue;
    }
    const bool same = filtered_log() == s.expect;
    const bool fault_ok = !s.fault || world().errors - errors_before == 1U;
    if (!same || !fault_ok) {
      ++differences;
      std::printf("%s, step %zu:\n  expected: %s%s\n  actual:   %s%s\n", g.name.c_str(), i + 1,
                  joined(s.expect).c_str(), s.fault ? " + a reported fault" : "",
                  joined(filtered_log()).c_str(),
                  world().errors != errors_before ? " + a reported fault" : "");
    }
  }
  return differences;
}

} // namespace golden
