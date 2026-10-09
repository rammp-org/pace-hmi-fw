#pragma once
// The hazard fixes' hand-written goldens (hazard-c1-spec.md §5.2, hazard-c3-spec.md §6.2): each a
// list of steps, the checked ones with the filtered log (world.hpp) they must produce. Played
// against the firmware's drive code (main_unit.cpp) by run_golden.

#include <cstddef>
#include <string>
#include <vector>

#include "scripts.hpp"

namespace golden {

struct GoldenStep {
  Step step;
  bool checked;                    // compared at all (openings are not)
  std::vector<std::string> expect; // the step's filtered log, exactly
  bool fault;                      // the step must report one fault (the adapter's error log)
};

struct Golden {
  std::string name; // "GLD-1xx <sentence>"
  std::vector<GoldenStep> steps;
};

// C1's GLD-101..114 (hazard-c1-spec.md §5.2), after C3's "booted" preamble.
std::vector<Golden> c1_goldens();
// C3's GLD-117..124 (hazard-c3-spec.md §6.2).
std::vector<Golden> c3_goldens();

// The golden named by its ID ("GLD-104b"); an empty name when there is none.
Golden golden_by_id(const std::string &id);

// Plays a golden from a fresh world and drive state; returns the number of checked steps whose
// log (or fault report) differs, and prints each difference.
std::size_t run_golden(const Golden &g, const Target &target);

} // namespace golden
