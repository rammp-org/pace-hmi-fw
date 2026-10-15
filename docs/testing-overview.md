# Testing overview: one page, one diagram

Every test and check pace-hmi-fw has, what runs it, what it proves, and how its verdict decides
whether a change goes in. The firmware it tests is on [fw-overview.md](fw-overview.md).

Checked against `dev_ai_hazard_candidate` at `46f5719` (2026-10-11): the workflows in
`.github/workflows/`, `tests/run.py` and `tests/manifest.d/` (21 entries), the Makefiles of the
host apps, `tools/` and `scripts/`. Gates the owner asked about that are **not on this branch**
are marked as such; they are on `dev_ai_alloc_hook` (not merged): the coverage ratchet, the
Doxygen ratchet and the `alloc_guard` build variant.

## 1. The diagram

```text
                                     A CHANGE (a commit on a branch)
                                                   |
            locally: pre-commit (clang-format 14.0.6; not components/ui, m5stack-tab5, espp_adc
            upstream files, managed_components, external, boot_logo); host apps with `make test`
                                                   |
                                                   | git push (any branch but main), or a PR
                                                   v
+-------------------------------------------------------------------------------------------------------------+
| CI: GitHub Actions. Every job must pass. Docker-limit failures are rerun, never waived.                     |
|                                                                                                             |
|  l0.yml / checks        ubuntu, Python only, no toolchain                                                   |
|    ratchet.py selftest + check ......... per-file debt counts may only fall; new files start clean          |
|    include_boundary.py selftest + check  no #include of another component's src/ (CS-LAY-03)                |
|    pre-commit --all-files .............. clang-format on every first-party C/C++ file (CS-FMT-01)           |
|    scripts/ui_contract.py .............. the SquareLine export still has what the firmware binds            |
|    gen_diagrams.py selftest + check .... D2/D3 in docs/architecture.md match their sources (CS-ARC-02)      |
|    guard selftests G2 exports, G4 init_order, G10 task_dump, G11 observer_census (inline samples)           |
|    rtps_mcb_sim.py selftest (SIM-*) .... the simulated MCB's decisions and fault modes                      |
|    ui_client.py selftest (UI-*), run_bench.py selftest (BENCH-*, hazard graders BENCH-015..046)             |
|    compare_selftest.py selftest (CMP) .. B3's comparison with the self-test baseline                        |
|    tests/run.py selftest --matrix + check: manifest valid, requirement IDs not retired or reused            |
|    tests/run.py report ................. requirement -> test matrix to the job summary (informational)      |
|    sdkconfig.defaults keeps REMOTE_UI, OTA_TEST_URL, DA7280_TEST, DEBUG_FPS, STICK_INJECT off               |
|                                                                                                             |
|  l0.yml / host-l1       IDF v6.0 image: idf.py reconfigure (fetches espp headers), then                     |
|    tests/run.py run --all --seed 1 ..... 21 L1 host apps (Unity, g++, host-native): 20 run, 1 SKIP          |
|                                          (DRV-L1-FULL, on demand). Verdict = exit code + Unity summary      |
|                                                                                                             |
|  l0.yml / bench-build   matrix, each its own build dir and sdkconfig:                                       |
|    bench        = defaults + ci/sdkconfig.bench (remote UI)                                                 |
|    bench_inject = bench + ci/sdkconfig.stick_inject (stick injection, hazard bench verbs)                   |
|    each: -Werror build; sdkconfig.h asserts; G4 init_order.py check; G2 exports.py check;                   |
|    bench_inject: nm finds stick_inject and bench_verb symbols (so build.yml's absence check means something)|
|                                                                                                             |
|  build.yml / build      default variant (what ships): -Werror build; G4 + G2 on its objects;                |
|    release ELF has no stick_inject and no bench_verb symbol, sdkconfig.h has injection off; size delta      |
|                                                                                                             |
|  static_analysis.yml    cppcheck (esp-cpp/StaticAnalysis, pinned SHA): --enable=all, c++20,                 |
|                         suppressions.txt, components/ui excluded                                            |
|                                                                                                             |
|  package_main.yml       on main and on a release only: build and package the binaries                       |
|                                                                                                             |
|  NOT ON THIS BRANCH (dev_ai_alloc_hook): coverage ratchet (run.py --coverage + coverage_gate.py),           |
|    Doxygen warning ratchet (doxygen_gate.py), alloc_guard build variant (heap hook, TS-DET-08)              |
+-------------------------------------------------------------+-----------------------------------------------+
                                                              |
                     the L1 apps that carry the safety logic  |  host, every push
                                                              v
+-------------------------------------------------------------------------------------------------------------+
| THE DRIVE SESSION'S VERIFICATION STACK  (components/drive_session: 59 rows, 6 phases, 7 hidden bits)        |
|                                                                                                             |
|  drive_session_table.hpp  --static_assert-->  rows pairwise exclusive, a way out of every phase,            |
|                                               TABLE_FINGERPRINT (FNV-1a over every field): a row change     |
|                                               needs the owner's approval and a new fingerprint              |
|  DSO-001..024  oracle_selfcheck (CI) ........ the table's invariants by brute force                         |
|  DRV-L1        oracle (CI) .................. sequences, safe state, stick gate                             |
|  DRV-L1-BYINPUT oracle_by_input (CI) ........ per input: every value of every guard it reads, x all (or a   |
|                                               sampled corner set) of the rest, in every phase; < 30 s       |
|  DRV-L1-FULL   oracle_full (on demand) ...... every phase x hidden x env x input; `make full`, ~10 min      |
|  make equivalence (on demand) ............... by-input visits every class the full product visits, same     |
|                                               rows: the CI oracle is not weaker                             |
|  make mutants (on demand) ................... 22 table + 12 code one-line mutants; BOTH oracles must reject |
|  make mnc (on demand) ....................... must-not-compile: header mutations trip their static_assert   |
|  DAD-L1        drive_adapter (CI) ........... the adapter's contract: windows, re-entry, safe state         |
|  GLD-L1        drive_golden (CI) ............ the firmware's DrivePort + DriveAdapter over recording shims  |
|                                               = hand-written goldens GLD-101..130 (C1, C3, C2b); GLD-115    |
|                                               walks take every table row                                    |
|  other safety L1 apps (CI): L1-STK stick pipeline, monitor, permit; L1-CTL motion guard, ControlCycle;      |
|    L1-POST POST evaluator; L1-CAL calibration; L1-RTPS wire format, hostile decodes; FWC-L1 fw_core;        |
|    TOP-L1 topology. Not safety: L1-FMT, L1-MOD, L1-JOY, L1-OTA, L1-RUI, L1-SST, L1-SET, L1-AGD              |
+-------------------------------------------------------------+-----------------------------------------------+
                                                              |
    a bench image: ci/sdkconfig.bench (+ stick_inject)        |  on the bench PC, per change
                                                              v
+-------------------------------------------------------------------------------------------------------------+
| THE BENCH  tools/bench/run_bench.py: board 2 (Tab5) + bench PC; holds the board lease; scripts decide       |
|                                                                                                             |
|  B0 preflight: port by MAC, network, no stray RTPS peer, partition table == build's   (else INVALID)        |
|  B1 flash (backs up /storage once; never erases)       B2 boot markers in baseline order (serial)           |
|  B3 self test (rtps_selftest.py, 60 checks) vs baseline bands, never beside a sim                           |
|  B4 screen walk (13 names, static screens pixel-equal)  B4b PIN pad and seat grid (no seat command)         |
|  B5 drive: hold -> Drive, `e` -> Locked, XYTwist 0 while locked                                             |
|  B5a..B5e sim fault modes: exit hold, burger key, link loss, profile tap, ignored/dropped DISABLE (RECORD)  |
|  --hazard c1,c3,c4,c2 (stick-inject image only, after the SOLE-SIM PROOF):                                  |
|    c1  B5''-1..15  entry, neutral first, MCB stops, stale status, stop re-send, calibration, profile tap    |
|    c3  B5''-16..22b POST at boot (5 boots), bumped stick, button held, reset reason (CRASH), budget         |
|    c4  B5f..B5j    MCB state and stale status on the ADC side, G4 holds, STALL UI, STALL ADC                |
|    c2  C2-1..16    rails, bad cycles, failed read, stale X/Y, NaN, fault while driving (C2b), soak,         |
|                    STALL CADC                                                                               |
|    retired: B5''-15 (by c3: B5''-18b), B5''-12 (by c2)                                                      |
|                                                                                                             |
|   MCB = scripts/rtps_mcb_sim.py (sim_child.py)   a / i / z / e / ok states   x, s refuse ENABLE / DISABLE   |
|         ign N, drop N DISABLEs   ongone keep|idle   p / r pause MibStatus   mark   --event-log JSONL        |
|   stick = remote UI STICK every 100 ms (expires 300 ms after the last; fail_mask: failed read, NaN, frozen  |
|         X/Y) -> fw::Mailbox -> Read ADC, replacing the raw sample before the real pipeline                  |
|   bench verbs = STATE (one JSON line), PERMIT POST, CAL UNSAVED, POST RERUN, CRASH, STALL UI|ADC|CADC <ms>  |
|   evidence = sim event log, every XYTwist sample, STATE every 100 ms, serial log; one monotonic clock       |
+-------------------------------------------------------------+-----------------------------------------------+
                                                              |
                                                              v
      VERDICTS  PASS / FAIL: graded by the script from the evidence
                NOT_RUN: could not test (no IP, a verb missing, an injection lapse over 0.28 s): no verdict
                INVALID: the preflight failed (B0, the sole-sim proof)   SKIP: declared on the command line
                RECORD: a characterisation (B5e); passes only with clean graded set-up and clean-up
                last-good image saved only if every step is PASS or a clean RECORD
                                                              |
                                                              v
      MERGE     CI green  +  for firmware behaviour: the bench run on the change's image  +  for a safety
                behaviour change: the owner's approval of the spec first, code by another agent, a new
                table fingerprint (CS-SAF-05)  ->  merge to the integration branch (dev_refactor)
```

## 2. Legend

| Term | Meaning |
| --- | --- |
| L0 | static checks: no board, no test execution of firmware code (TESTING_SPEC TS-LVL) |
| L1 | host unit tests: one component (or the drive code with shims), compiled with host g++ in WSL or the IDF image, Unity, fake time. A spec deviation from the IDF `linux` target (TS-UNIT-01, owner 2026-10-06) |
| bench | L5-style system tests: the real board, the simulated MCB, the remote UI debug channel |
| G2 / G4 / G10 / G11 | `tools/guards`: exported symbols, static-init order, the task table (`baselines/tasks.json`, needs a board: `task_dump.py fetch`), the observer census (no firmware half yet) |
| sole-sim proof | before any stick injection: no other RTPS peer process on the PC and an RTPS sweep of the subnet finds only the board (`hazard_rig.sole_sim_proof`); else every hazard step is INVALID and nothing is injected |
| STALL | bench verb: `UI` or `ADC` makes that task busy-wait for the time (no yield, no heartbeat, no watchdog reset: a hang as the guard sees it); `CADC` makes Read ADC suspend the ContinuousAdc task (stale X/Y) |
| oracle | a test that drives `DriveSession::step` over a state space and compares every step with `find_row` on the table |
| mutant | a one-line change to the table or the code; a test suite that does not fail on it is too weak |
| golden | expected port calls written by hand from the spec; compared exactly (TS-DET-05) |
| RECORD | today's behaviour kept as data, so a later fix shows up as a diff, not a FAIL |

## 3. Life of a change

1. **Write it.** A behaviour change to safety code starts as a spec the owner approves; the
   code comes from another agent. A refactor and a behaviour change never share a commit.
2. **Locally.** pre-commit formats it; `make test` in the touched host apps (WSL) runs their
   Unity cases; for the drive table, `make mutants` and `make equivalence` too, since CI does not
   run them.
3. **Push.** CI runs every job above on the branch. L0 fails on a debt count that grew (ratchet),
   an include across a component's `src/`, a format diff, an export the firmware binds that
   SquareLine lost, a diagram that no longer matches, a new static initialiser or export (G4,
   G2), a manifest or requirement-ID error, or a debug feature on in the shipping defaults.
   host-l1 fails on any Unity failure. The builds fail on any warning and on a stick-injection
   or bench-verb symbol in the release ELF. Docker-limit failures are infrastructure: rerun.
4. **Bench.** For a change that reaches the board, build the bench image (and the
   stick-inject image for the hazard steps), then `run_bench.py --flash --hazard c1,c3,c4,c2`.
   Every step gets PASS, FAIL, NOT_RUN, INVALID, SKIP or RECORD in `summary.json`; a lapse or a
   missing verb is NOT_RUN and is rerun, never counted as a pass.
5. **Merge.** With CI green and the bench result read against the change (the candidate's
   results are written up, e.g. [plans/hazard-candidate-results.md](plans/hazard-candidate-results.md)),
   the change merges to `dev_refactor`. A release adds TESTING_SPEC's gate (TS-REL): L0-L2 on
   the frozen build, one full bench night on the same artifacts, sign-off by someone else.

## 4. Where each piece lives

| Piece | Path |
| --- | --- |
| Workflows | `.github/workflows/l0.yml`, `build.yml`, `static_analysis.yml`, `package_main.yml` |
| Test runner, manifests, requirement matrix | `tests/run.py`, `tests/manifest.d/*.yaml`, `tests/reqmatrix.py` |
| Host app contract | `tests/host/common.mk`, `tests/host/test_main.cpp`; each app's `Makefile` (`make test`, `make coverage`) |
| Drive oracles, mutants, equivalence | `components/drive_session/test/{oracle,oracle_by_input,oracle_full,oracle_selfcheck}` |
| Drive goldens | `tests/host/drive_golden` |
| L0 tools | `tools/l0/ratchet.py`, `tools/l0/include_boundary.py`, `tools/gen_diagrams`, `scripts/ui_contract.py` |
| Guards and baselines | `tools/guards/*.py`, `tools/guards/baselines/` |
| Bench runner and scenarios | `tools/bench/run_bench.py`, `hazard_steps.py`, `scenario_*.py`, `hazard_rig.py`, `hazard_grade.py` |
| MCB simulator | `scripts/rtps_mcb_sim.py`, `scripts/mcb_sim_logic.py` |
| Self-test peer, remote UI client | `scripts/rtps_selftest.py`, `scripts/hmi_ui.py`, `tools/bench/ui_client.py` |
| Bench baselines | `tests/characterisation/baseline-e2047a4/` (boot log and expected markers, self-test JSON, walk PNGs) |
| Desktop simulator | `sim/`: out of service (its README says so); not part of any gate |
