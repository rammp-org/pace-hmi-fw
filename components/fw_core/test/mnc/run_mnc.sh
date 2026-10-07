#!/usr/bin/env bash
# Must-not-compile runner (TS-UNIT-06).
# Each case is built twice: without MNC_FAULT it must compile (the twin); with -DMNC_FAULT it must
# fail, and the compiler output must contain the text on each of the case's "// expect-error:"
# lines, so a typo cannot pass as the expected failure.
# Usage: run_mnc.sh "<compiler and flags>" <case dir> <build dir>
set -u
export LC_ALL=C # ASCII quotes in compiler messages, so the expected text matches
compile="$1"
case_dir="$2"
build_dir="$3"
mkdir -p "$build_dir"
pass=0
fail=0
for src in "$case_dir"/mnc_*.cpp; do
  name="$(basename "$src" .cpp)"
  # Every "// expect-error:" line must appear in the output (long texts are split over lines).
  expect="$(sed -n 's|^// expect-error: ||p' "$src")"
  if [ -z "$expect" ]; then
    echo "MNC $name FAIL: no '// expect-error:' line"
    fail=$((fail + 1))
    continue
  fi
  if ! $compile "$src" >"$build_dir/$name.twin.log" 2>&1; then
    echo "MNC $name FAIL: the twin (no MNC_FAULT) does not compile; see $build_dir/$name.twin.log"
    fail=$((fail + 1))
    continue
  fi
  if $compile -DMNC_FAULT "$src" >"$build_dir/$name.fault.log" 2>&1; then
    echo "MNC $name FAIL: compiled with MNC_FAULT"
    fail=$((fail + 1))
    continue
  fi
  missing=""
  while IFS= read -r text; do
    grep -qF -- "$text" "$build_dir/$name.fault.log" || missing="$text"
  done <<<"$expect"
  if [ -n "$missing" ]; then
    echo "MNC $name FAIL: failed, but without '$missing'; see $build_dir/$name.fault.log"
    fail=$((fail + 1))
    continue
  fi
  echo "MNC $name PASS: $(echo "$expect" | paste -sd ' ' -)"
  pass=$((pass + 1))
done
echo "MNC summary: $pass passed, $fail failed"
[ "$fail" -eq 0 ] && [ "$pass" -gt 0 ]
