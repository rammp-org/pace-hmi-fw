#!/usr/bin/env bash
# `make mutants` (docs/plans/hazard-fixes.md B3): one-line mutations of the drive session
# table and of its code. Each mutant is built into BOTH oracles, the full product (../oracle_full,
# DRV-001..012) and the by-input one (this folder, DRV-101..113), and each oracle must reject
# it: Unity reports failures. A mutant that does not compile is a bad mutant (the table's own
# static_asserts are `make mnc`'s job in ../oracle_selfcheck), and fails this script.
#
# The table and the code in the repo are never touched: each mutant is a copy under the build
# folder, first on the include path. A table mutant changes the table's data, so its copy of
# drive_session_fingerprint.hpp has the fingerprint's static_assert switched off; the real
# fingerprint and its check are untouched.
#
# Usage: mutants.sh <repo> <build dir> <unity dir>. Exit 0 = the unmutated build passes both
# oracles and every mutant is rejected by both.

set -u
REPO=$1
OUT=$2
UNITY=$3
INC=$REPO/components/drive_session/include
SRC=$REPO/components/drive_session/src/drive_session.cpp
OLD=$REPO/components/drive_session/test/oracle_full
NEW=$REPO/components/drive_session/test/oracle_by_input

# name|table or code|sed expression (applied to drive_session_table.hpp or drive_session.cpp)
MUTANTS=(
  # Table rows (TABLE.md section 2 row numbers).
  'T01-row1-no-boot-guard|table|0,/                    {Guard::CALIBRATING, Guard::ON_BOOT_SCREEN}),/s//                    {Guard::CALIBRATING}),/'
  'T02-row3-banner-lost|table|0,/Phase::LOCKED, kF2LockStopped,/s//Phase::LOCKED, kF2LockLost,/'
  'T03-row8-exit-banner|table|0,/Phase::LOCKED, kF2LockAsked,/s//Phase::LOCKED, kF2LockStopped,/'
  'T04-row9-needs-link|table|s/Transition{Phase::EXIT_REFUSED, Input::TICK_FOLLOW, when({}, {Guard::DRIVING_OK}),/Transition{Phase::EXIT_REFUSED, Input::TICK_FOLLOW, when({Guard::LINK_CONNECTED}, {Guard::DRIVING_OK}),/'
  'T05-row14-always|table|s/Phase::EXITING, Input::TICK_EXIT_DUE, when({Guard::EXIT_ELAPSED}), Phase::EXIT_REFUSED,/Phase::EXITING, Input::TICK_EXIT_DUE, kAlways, Phase::EXIT_REFUSED,/'
  'T06-row15-no-warn-armed|table|s/when({Guard::WARN_ARMED, Guard::WARN_ELAPSED}),/when({Guard::WARN_ELAPSED}),/'
  'T07-row16-no-disable|table|0,/acts({Action::CLEAR_GIVEUP, Action::SEND_DISABLE})/s//acts({Action::CLEAR_GIVEUP})/'
  'T08-row18-order|table|s/acts({Action::RING_WAIT, Action::SEND_ENABLE, Action::ARM_WARN, Action::ARM_GIVEUP})/acts({Action::RING_WAIT, Action::SEND_ENABLE, Action::ARM_GIVEUP, Action::ARM_WARN})/'
  'T09-row21-then-menu|table|s/Transition{Phase::UNLOCKING, Input::EXIT_HOLD_DONE, kAlways, Phase::EXITING, kFirstExitAskHold,/Transition{Phase::UNLOCKING, Input::EXIT_HOLD_DONE, kAlways, Phase::EXITING, kFirstExitAskMenu,/'
  'T10-row27-asks-again|table|s/Transition{Phase::EXITING, Input::MENU_KEY_DRIVE, kAlways, Phase::EXITING, kNoActions,/Transition{Phase::EXITING, Input::MENU_KEY_DRIVE, kAlways, Phase::EXITING, kExitAskMenu,/'
  'T11-row29-needs-link|table|s/Transition{Phase::LOCKED, Input::PROFILE_CLICK, kAlways, Phase::LOCKED,/Transition{Phase::LOCKED, Input::PROFILE_CLICK, when({Guard::LINK_CONNECTED}), Phase::LOCKED,/'
  'T12-row38-menu-ignored|table|0,/when({Guard::ON_LOCKED_SCREEN}, {Guard::MENU_OPEN, Guard::MCB_READY}), Phase::LOCKED,/s//when({Guard::ON_LOCKED_SCREEN}, {Guard::MCB_READY}), Phase::LOCKED,/'
  'T13-row40-link-not-mcb|table|s/Transition{Phase::LOCKED, Input::MENU_ROW_DRIVE, when({}, {Guard::MCB_READY}), Phase::LOCKED,/Transition{Phase::LOCKED, Input::MENU_ROW_DRIVE, when({}, {Guard::LINK_CONNECTED}), Phase::LOCKED,/'
  'T14-effect-enable-sets-nothing|table|s/ActionEffect{Action::SEND_ENABLE, bit(Guard::REQUEST_ENABLE), 0},/ActionEffect{Action::SEND_ENABLE, 0, 0},/'
  # The hazard fix C1's rows (hazard-c1-spec.md §2.3).
  'T15-row46-waits-for-slow|table|0,/when({Guard::RESEND_FAST_DUE}, {Guard::STOP_FAULT}), Phase::EXITING,/s//when({Guard::RESEND_SLOW_DUE}, {Guard::STOP_FAULT}), Phase::EXITING,/'
  'T16-effect-fault-sets-nothing|table|s/ActionEffect{Action::RAISE_STOP_FAULT, bit(Guard::STOP_FAULT), 0},/ActionEffect{Action::RAISE_STOP_FAULT, 0, 0},/'
  # The hazard fix C3's rows (hazard-c3-spec.md §4).
  'T17-row50-without-link|table|0,/when({Guard::LINK_CONNECTED}, {Guard::BOOT_STOP_DONE}), Phase::LOCKED,/s//when({}, {Guard::BOOT_STOP_DONE}), Phase::LOCKED,/'
  # Code that reads a guard bit the table does not read for that input: the by-input oracle
  # only samples those (oracle_space.hpp), so these test the sample.
  'C01-unlock-reads-menu|code|0,/^  if (on(g, Guard::DRIVING_OK)) {/s//  if (on(g, Guard::DRIVING_OK) \&\& !on(g, Guard::MENU_OPEN)) {/'
  'C02-publish-reads-giveup-elapsed|code|s|    return go(Phase::LOCKED, PUBLISH); // row 29 (H5)|    return on(g, Guard::GIVEUP_ELAPSED) ? stay() : go(Phase::LOCKED, PUBLISH); // row 29|'
  'C03-exit-reads-two-set|code|s|    return go(Phase::EXITING, FIRST_ASK_EXIT); // row 22|    return (on(g, Guard::MENU_OPEN) \&\& on(g, Guard::WARN_ELAPSED)) ? stay() : go(Phase::EXITING, FIRST_ASK_EXIT); // row 22|'
  'C04-menu-key-reads-two-clear|code|s|    return go(Phase::EXITING, FIRST_ASK_EXIT_THEN_MENU); // row 26|    return (!on(g, Guard::THEN_MENU) \&\& !on(g, Guard::GIVEUP_ARMED)) ? stay() : go(Phase::EXITING, FIRST_ASK_EXIT_THEN_MENU); // row 26|'
  'C05-publish-reads-three-set|code|s|      return go(Phase::UNLOCKING, ENABLE_PROFILE); // row 31|      return (on(g, Guard::MENU_OPEN) \&\& on(g, Guard::EXIT_ELAPSED) \&\& on(g, Guard::GIVEUP_ARMED)) ? stay() : go(Phase::UNLOCKING, ENABLE_PROFILE); // row 31|'
  'C06-publish-reads-three-set-three-clear|code|s|      return go(Phase::DRIVING, ENABLE_PROFILE); // row 32|      return (on(g, Guard::MENU_OPEN) \&\& on(g, Guard::EXIT_ELAPSED) \&\& on(g, Guard::REQUEST_ENABLE) \&\& !on(g, Guard::WARN_ELAPSED) \&\& !on(g, Guard::GIVEUP_ELAPSED) \&\& !on(g, Guard::CALIBRATING)) ? stay() : go(Phase::DRIVING, ENABLE_PROFILE); // row 32|'
  # Code that gets a row wrong where the table reads the bit.
  'C07-giveup-ignores-elapsed|code|0,/    if (on(g, Guard::GIVEUP_ARMED) && on(g, Guard::GIVEUP_ELAPSED)) {/s//    if (on(g, Guard::GIVEUP_ARMED)) {/'
  'C08-effect-cancel-keeps-timer|code|s/    {Action::CANCEL_UNLOCK_TIMER, clears(Guard::UNLOCK_TIMER_ARMED)},/    {Action::CANCEL_UNLOCK_TIMER, NO_EFFECT},/'
  'C10-row51-sends-disable|code|s|BOOT_STOP_MOOT); // row 51|BOOT_STOP); // row 51|'
  'C09-resend-fast-after-fault|code|s/      on(g, Guard::STOP_FAULT) ? on(g, Guard::RESEND_SLOW_DUE) :/      on(g, Guard::STOP_FAULT) ? on(g, Guard::RESEND_FAST_DUE) :/'
)

# run_app <app dir> <include dir> <source> <build dir>: prints PASS, REJECTED, CRASHED or
# NO-BUILD, and leaves the log in <build dir>.log
run_app() {
  local log=$4.log
  make --no-print-directory -j2 -C "$1" test REPO="$REPO" UNITY_DIR="$UNITY" INCLUDES="$2" \
    SRCS_UNDER_TEST="$3" BUILD_DIR="$4" > "$log" 2>&1
  local rc=$?
  local summary
  summary=$(grep -E '^[0-9]+ Tests [0-9]+ Failures [0-9]+ Ignored' "$log" | tail -1)
  if [ -n "$summary" ]; then
    local failures
    failures=$(echo "$summary" | awk '{print $3}')
    if [ "$rc" -eq 0 ] && [ "$failures" -eq 0 ]; then echo PASS; else echo REJECTED; fi
  elif grep -q 'error:' "$log"; then
    echo NO-BUILD
  else
    echo CRASHED
  fi
}

mkdir -p "$OUT"
fail=0
base_old=$(run_app "$OLD" "$INC" "$SRC" "$OUT/unmutated-old")
base_new=$(run_app "$NEW" "$INC" "$SRC" "$OUT/unmutated-new")
echo "MUTANT unmutated: full product $base_old, by input $base_new"
if [ "$base_old" != PASS ] || [ "$base_new" != PASS ]; then
  fail=1
fi

for m in "${MUTANTS[@]}"; do
  name=${m%%|*}
  rest=${m#*|}
  kind=${rest%%|*}
  expr=${rest#*|}
  dir=$OUT/$name
  rm -rf "$dir"
  mkdir -p "$dir/include"
  cp "$INC"/*.hpp "$dir/include/"
  cp "$SRC" "$dir/drive_session.cpp"
  if [ "$kind" = table ]; then
    target=$dir/include/drive_session_table.hpp
    orig=$INC/drive_session_table.hpp
    sed -i 's/^static_assert(table_fingerprint() == TABLE_FINGERPRINT,/static_assert(true || table_fingerprint() == TABLE_FINGERPRINT,/' \
      "$dir/include/drive_session_fingerprint.hpp"
    if cmp -s "$INC/drive_session_fingerprint.hpp" "$dir/include/drive_session_fingerprint.hpp"; then
      echo "MUTANT $name: the fingerprint check could not be switched off (FAIL)"
      fail=1
      continue
    fi
  else
    target=$dir/drive_session.cpp
    orig=$SRC
  fi
  sed -i "$expr" "$target"
  if cmp -s "$orig" "$target"; then
    echo "MUTANT $name: the mutation did not apply (FAIL)"
    fail=1
    continue
  fi
  changed=$(diff "$orig" "$target" | grep -c '^>')
  old=$(run_app "$OLD" "$dir/include" "$dir/drive_session.cpp" "$dir/old")
  new=$(run_app "$NEW" "$dir/include" "$dir/drive_session.cpp" "$dir/new")
  old_fail=$(grep -cE '^[^ ]+:[0-9]+:DRV-[0-9]{3} .*:FAIL(:|$)' "$dir/old.log")
  new_fail=$(grep -cE '^[^ ]+:[0-9]+:DRV-[0-9]{3} .*:FAIL(:|$)' "$dir/new.log")
  verdict=ok
  if [ "$changed" -ne 1 ] || [ "$old" != REJECTED ] || [ "$new" != REJECTED ]; then
    verdict=FAIL
    fail=1
  fi
  echo "MUTANT $name ($kind, $changed line): full product $old ($old_fail cases), by input $new ($new_fail cases) -> $verdict"
done
echo "MUTANTS: ${#MUTANTS[@]} mutants -> $([ $fail -eq 0 ] && echo PASS || echo FAIL)"
exit $fail
