# Testing overview: one page

Every test and check pace-hmi-fw has, what runs it, what it catches, and how its verdict decides
whether a change goes in. The firmware it tests is on [fw-overview.md](fw-overview.md).

Checked against the release candidate (RC) branch `dev_ai_rc` on 2026-10-15: the workflows in
`.github/workflows/`, `tests/run.py` and `tests/manifest.d/` (22 files, 23 entries), `tools/`,
`scripts/` and `tools/bench/`. The RC is **TEST-ONLY**: it builds against local espp branches,
so CI can run its Python checks but not its builds ([§3](#3-ci-on-the-rc)).

## 0. Words used

| Term | Meaning |
| --- | --- |
| L0 | static checks: no board, no firmware code executed (TESTING_SPEC TS-LVL) |
| L1 | host unit tests: one component (or the drive code with shims) compiled with host g++, Unity test framework, fake time. A spec deviation from the IDF `linux` target (TS-UNIT-01, owner 2026-10-06) |
| bench | system tests on the real board (board 2, a Tab5), the simulated MCB (main control board) and the remote UI debug channel (TCP 3333) |
| B0..B5e | the bench's base steps: B0 preflight, B1 flash, B2 boot, B3 self test, B4 screen walk, B4b PIN pad and seat grid, B5 drive, B5a..B5e the simulator's fault modes |
| C1, C3, C4, C2, seat | the hazard-step groups, one per hazard fix: C1 output permit and drive table (steps B5''-1..15), C3 POST, power-on self test (B5''-16..22b), C4 motion guard and watchdogs (B5f..B5j), C2 stick monitor (C2-1..16), seat path (B5s-1..17) |
| G2, G4, G10, G11 | the guards in `tools/guards`: exported symbols, static-init order, the task table, the observer census |
| MCB / MIB | the chair's controller (main control board / Meebot interface board); on the bench, `scripts/rtps_mcb_sim.py` |
| RTPS | Real-Time Publish-Subscribe, the wire protocol between the HMI and the MCB |
| sole-sim proof | before any stick injection: no other RTPS peer process on the PC, and an RTPS sweep of the subnet finds only the board (`hazard_rig.sole_sim_proof`); else every hazard step is INVALID and nothing is injected |
| STALL | bench verb: `UI` or `ADC` makes that task busy-wait (no yield, no heartbeat, no watchdog reset: a hang as the guard sees it); `CADC` makes Read ADC suspend the ContinuousAdc task (stale X/Y) |
| oracle | a test that drives `DriveSession::step` over a state space and compares every step with the table's row |
| mutant | a one-line change to the table or the code; a test suite that does not fail on it is too weak |
| golden | expected port calls written by hand from the spec; compared exactly (TS-DET-05) |
| RECORD | today's behaviour kept as data, so a later fix shows up as a diff, not a FAIL |

## 1. The test layers in one diagram

```text
                                   A CHANGE (a commit on a branch)
                                                 |
   locally: pre-commit (clang-format 14.0.6; not components/ui, managed_components, external, boot_logo);
            the touched host apps' `make test`; for the drive table also `make mutants` and `make equivalence`
                                                 |
                                                 v
+----------------------------------------------------------------------------------------------------------+
| L0  STATIC CHECKS  (l0.yml / checks: Python only, no toolchain)                     CATCHES                |
|  ratchet.py selftest + check ............ per-file debt (lines, fn size, statics,    debt that grew; lv_* or|
|                                          globals, locks, lv_* outside the UI, ...)  locks outside the UI   |
|  include_boundary.py .................... no #include of another component's src/   a reach into internals |
|  pre-commit --all-files ................. clang-format on first-party C/C++          a format diff          |
|  scripts/ui_contract.py ................. the SquareLine export still has what the   a widget the firmware  |
|                                          firmware binds (147 assumptions)           binds, renamed or gone |
|  gen_diagrams.py selftest + check ....... D2/D3 in architecture.md match sources     a stale diagram        |
|  guard selftests G2, G4, G10, G11 ....... the guards' own logic on inline samples                          |
|  rtps_mcb_sim.py selftest (SIM-*) ....... the simulated MCB's decisions              a sim that lies        |
|  ui_client, run_bench, compare_selftest . the bench runner, its graders (BENCH-*),   a grader that passes a |
|    selftests (UI-*, BENCH-001..117, CMP)  B3's comparison (CMP-001..013)             bad run                |
|  tests/run.py selftest --matrix + check . manifests valid; requirement IDs not       a test citing a retired|
|                                          retired, reused or deleted                 requirement            |
|  release-defaults grep .................. sdkconfig.defaults keeps REMOTE_UI,         a bench feature in the |
|                                          OTA_TEST_URL, DA7280_TEST, DEBUG_FPS,       shipping build         |
|                                          STICK_INJECT, ALLOC_GUARD off                                     |
|  doxygen job: doxygen_gate.py check ..... Doxygen warnings per header may only fall  an undocumented API    |
+----------------------------------------------------------------------------------------------------------+
| L1  HOST TESTS  (l0.yml / host-l1: IDF v6.0 image, tests/run.py run --all --seed 1 --coverage)            |
|  22 entries in CI + DRV-L1-FULL on demand; 651 cases. Verdict = exit code + Unity summary                 |
|  safety: L1-STK stick pipeline, monitor, permit, mapping (J2) | L1-CTL motion guard, ControlCycle         |
|          L1-POST POST evaluator | L1-CAL calibration | L1-RTPS wire format, hostile decodes                |
|          L1-SEAT seat gate and reading | FWC-L1 fw_core | TOP-L1 topology | DAD-L1 drive adapter           |
|  other:  L1-FMT, L1-MOD, L1-JOY (espp Joystick + our twist mapper), L1-OTA, L1-RUI, L1-SST, L1-SET,       |
|          L1-AGD, L1-AGC                                                                                    |
|  coverage_gate.py check ................. per file, line and branch coverage may     coverage that fell;    |
|                                          only rise; new files meet spec (80 % lines, a new safety file    |
|                                          70 % branches; safety 100 % branches)      below 100 % branches   |
+----------------------------------------------------------------------------------------------------------+
| THE DRIVE SESSION'S VERIFICATION STACK  (components/drive_session: 59 rows, 6 phases, 7 hidden bits)     |
|  drive_session_table.hpp static_asserts . rows pairwise exclusive, a way out of      an ambiguous or       |
|                                          every phase; TABLE_FINGERPRINT pins data   dead-end row; an       |
|                                                                                     unapproved row change  |
|  DSO-001..024 oracle_selfcheck (CI) ..... the table's invariants by brute force                            |
|  DRV-L1 oracle (CI) ..................... sequences, safe state, stick gate          code != table          |
|  DRV-L1-BYINPUT oracle_by_input (CI) .... per input, every value of every guard it   code != table on a    |
|                                          reads, in every phase; < 30 s              rare guard value       |
|  DRV-L1-FULL oracle_full (on demand) .... every phase x hidden x env x input; ~10 min                      |
|  make equivalence (on demand) ........... by-input visits every class the full       a CI oracle weaker    |
|                                          product does, same rows                    than the full one     |
|  make mutants (on demand) ............... 22 table + 12 code one-line mutants;       a test suite too weak |
|                                          BOTH oracles must reject each               to notice a change    |
|  make mnc (on demand) ................... must-not-compile: header mutations trip     a static_assert that  |
|                                          their static_assert                         guards nothing        |
|  GLD-L1 drive_golden (CI) ............... DrivePort + DriveAdapter over recording     wrong port calls,     |
|                                          shims = hand-written goldens GLD-101..133   wrong order           |
+----------------------------------------------------------------------------------------------------------+
| BUILDS AND GUARDS  (build.yml: default; l0.yml / bench-build: bench, bench_inject, alloc_guard)          |
|  -Werror build per variant; sdkconfig.h asserts                          any warning; a wrong variant     |
|  G4 init_order.py check ................. no new static initialiser       a global built before app_main |
|  G2 exports.py check .................... no duplicate or undeclared export                                |
|  release ELF: no stick_inject, bench_verb or alloc_guard symbol          a bench backdoor in the release  |
|  static_analysis.yml: cppcheck --enable=all, c++20 (components/ui excluded)    zero findings required     |
|  G10 task_dump.py (bench, TASKS verb) ... running tasks == baselines/tasks.json    a task's prio/core/stack |
+----------------------------------------------------------------------------------------------------------+
                                                 |  bench image: ci/sdkconfig.bench (+ stick_inject)
                                                 v
+----------------------------------------------------------------------------------------------------------+
| THE BENCH  tools/bench/run_bench.py: board 2 + the bench PC (Windows or Ubuntu); holds the board lease     |
|  B0 preflight: port by MAC, network, no stray RTPS peer, partition table == build's   (else INVALID)     |
|  B1 flash (backs up /storage once; never erases)   B2 boot markers in baseline order (serial)            |
|  B3 self test (rtps_selftest.py, 60 checks) vs the baseline's bands, never beside a sim;                 |
|     on Ethernet a SKIP of net.wifi_rssi is accepted (owner 2026-10-15), every other row stays strict      |
|  B4 screen walk (13 screens, static ones pixel-equal)  B4b PIN pad and seat grid (no seat command)       |
|  B5 drive: hold -> Drive, `e` -> Locked, XYTwist 0 while locked                                          |
|  B5a..B5e sim fault modes: exit hold, burger key, link loss, profile tap, ignored/dropped DISABLE (RECORD)|
|  --hazard c1,c3,c4,c2,seat (stick-inject image only, after the SOLE-SIM PROOF):                          |
|    c1   B5''-1..14      entry, neutral first, MCB stops, stale status, stop re-send, calibration, profile |
|    c3   B5''-16..22b    POST at boot (5 boots), bumped stick, button held, reset reason (CRASH), budget   |
|    c4   B5f..B5j        MCB state and stale status on the ADC side, G4 holds, STALL UI, STALL ADC         |
|    c2   C2-1..16        rails, bad cycles, failed read, stale X/Y, NaN, fault while driving (C2b), soak,  |
|                         STALL CADC                                                                       |
|    seat B5s-1..17       NaN/inf seat values, link loss, MCB not IDLE, POST, stick FAULT, stuck keys,      |
|                         publish failure (SEATFAIL), lost command, the menu row, MCB moves on its own      |
|    retired: B5''-12 (by c2), B5''-15 (by c3: B5''-18b)                                                    |
|  MCB = scripts/rtps_mcb_sim.py (sim_child.py): states, refuse/ignore/drop DISABLE, pause MibStatus, seat  |
|        modes (seatval, seatdrop, seatslow), mark, --event-log JSONL                                      |
|  stick = remote UI STICK every 100 ms -> fw::Mailbox -> Read ADC, replacing the raw sample before the    |
|          real monitor, pipeline and permit (expires 300 ms after the last)                               |
|  evidence = sim event log, every XYTwist sample, STATE every 100 ms, serial log; one monotonic clock     |
+----------------------------------------------------------------------------------------------------------+
                                                 |
                                                 v
   VERDICTS  PASS / FAIL: graded by the script from the evidence, never by a person reading a diff
             NOT_RUN: could not test (no IP, a verb missing, an injection lapse over 0.28 s): no verdict, rerun
             INVALID: the preflight failed (B0, the sole-sim proof)   SKIP: declared on the command line
             RECORD: a characterisation (B5e); passes only with clean graded set-up and clean-up
             last-good image saved only if every step is PASS or a clean RECORD
                                                 |
                                                 v
   MERGE     CI green + for firmware behaviour: the bench run on the change's image + for a safety
             behaviour change: the owner's approval of the spec first, code by another agent, a new table
             fingerprint (CS-SAF-05) -> merge to the integration branch (dev_refactor)
```

## 2. What each bench group proves

| Group | Steps | What it proves on the board |
| --- | --- | --- |
| base | B0-B5 | the image boots as the baseline did, its self test is in band, every screen draws, the PIN pad and seat grid walk, the chair drives and relocks |
| sim faults | B5a-B5e | exit hold, burger key, link loss, profile tap after a relock; B5e records what happens when the MCB ignores or loses DISABLE |
| C1 | B5''-1..14 | the output permit: no motion until centred 300 ms (G1), the MCB's stop obeyed at once, DISABLE re-sent until it stops, no motion while calibrating or uncalibrated |
| C3 | B5''-16..22b | POST runs at every boot, holds the stick until PASS, latches a hardware FAIL, reports an unclean reset, finishes within its 3 s budget; one DISABLE at boot |
| C4 | B5f-B5j | the Read ADC task stops the stick on its own: MCB not ENABLED within 120 ms, stale MibStatus after 2 s, a hung UI task (STALL UI) or a hung ADC task (STALL ADC, the watchdog's report) |
| C2 | C2-1..16 | the stick monitor: rail, bad and missing reads, stale X/Y, NaN all give 0 and a FAULT where specified; a FAULT while driving stops (C2b); a 30-minute soak; X/Y producer starvation (STALL CADC) |
| seat | B5s-1..17 | every seat press goes through one gate; bad seat values read unknown; a failed or lost command is never re-sent |

## 3. CI on the RC

The RC takes every espp component from a folder outside the repo (`override_path` in
`main/idf_component.yml`), which CI's runners do not have. So on `dev_ai_rc` CI's Python checks
run, and every job that builds or runs ESP-IDF fails at dependency resolution:

| Job | On `dev_ai_rc` (2026-10-15, `ba2c7b3`) |
| --- | --- |
| L0 / checks, L0 / doxygen | PASS |
| L0 / host-l1, L0 / bench-build (3 variants), Build / build | FAIL: the `override_path` folders are missing on the runner |
| Static analysis (cppcheck) | FAIL: one style finding, `main/cues.cpp:20` `cues_attach` parameter named `cues` in the definition and `feedback` in the declaration (`funcArgNamesDifferent`) |

The builds, guards G2 and G4, and the release-ELF checks were run locally on the RC lane's
builds (all four variants, 0 warnings). They come back to CI when espp merges the PRs and the
RC switches to released espp.

## 4. The bench on Linux, and the B3 Ethernet rule

- **Linux.** Since 2026-10-14 the same bench tools run on the Ubuntu laptop as on Windows:
  `tools/bench/common.py` switches the platform parts (serial lines, the venv, process scan,
  the board lease at `~/Documents/ATDev/rammp/.board-lease`). An RTPS send to an address
  without ARP fails with EAGAIN on Linux; `rtps_host` drops that datagram (BENCH-078) and the
  sweep is strict: a listener that dies early makes B0 INVALID (BENCH-079). Hotspot restarts
  are Windows only. Details: [tools/bench/README.md](../tools/bench/README.md).
- **B3 on Ethernet** (owner decision, 2026-10-15). The self-test baseline was taken on Wi-Fi.
  On an Ethernet bench (`common.LINK == "Ethernet"`, the same signal B2's Ethernet rule
  BENCH-014 uses) the firmware SKIPs `net.wifi_rssi`, and B3 now accepts that SKIP: the row is
  shown with the baseline's result and the reason, not graded. Only that row, only a SKIP (a
  FAIL still fails), and on Wi-Fi it is graded as before. The baseline is not edited
  (`compare_selftest.ETHERNET_SKIP_OK`, CMP-010..013).

## 5. Life of a change

1. **Write it.** A behaviour change to safety code starts as a spec the owner approves; the
   code comes from another agent. A refactor and a behaviour change never share a commit.
2. **Locally.** pre-commit formats it; `make test` in the touched host apps runs their Unity
   cases; for the drive table, `make mutants` and `make equivalence` too, since CI does not run
   them.
3. **Push.** CI runs every job above. L0 fails on a debt count that grew, an include across a
   component's `src/`, a format diff, an export SquareLine lost, a stale diagram, a new
   undocumented API, a manifest or requirement-ID error, or a bench feature on in the shipping
   defaults. host-l1 fails on any Unity failure or a coverage drop. The builds fail on any
   warning, a new static initialiser or export (G4, G2), and a bench symbol in the release ELF.
   Docker-limit failures are infrastructure: rerun, never waived.
4. **Bench.** For a change that reaches the board, build the bench image (and the stick-inject
   image for the hazard steps), then `run_bench.py --flash --hazard c1,c3,c4,c2,seat`. Every
   step gets PASS, FAIL, NOT_RUN, INVALID, SKIP or RECORD in `summary.json`; a lapse or a
   missing verb is NOT_RUN and is rerun, never counted as a pass.
5. **Merge.** With CI green and the bench result read against the change (e.g.
   [plans/hazard-candidate-results.md](plans/hazard-candidate-results.md)), the change merges
   to `dev_refactor`. A release adds TESTING_SPEC's gate (TS-REL): L0-L2 on the frozen build,
   one full bench night on the same artifacts, sign-off by someone else. The RC is held: it
   cannot pass step 3 until espp merges the PRs ([plans/progress.md](plans/progress.md)).

## 6. Where each piece lives

| Piece | Path |
| --- | --- |
| Workflows | `.github/workflows/l0.yml`, `build.yml`, `static_analysis.yml`, `package_main.yml` |
| Test runner, manifests, requirement matrix | `tests/run.py`, `tests/manifest.d/*.yaml`, `tests/reqmatrix.py` |
| Host app contract | `tests/host/common.mk`, `tests/host/test_main.cpp`; each app's `Makefile` (`make test`, `make coverage`) |
| Drive oracles, mutants, equivalence | `components/drive_session/test/{oracle,oracle_by_input,oracle_full,oracle_selfcheck}` |
| Drive goldens | `tests/host/drive_golden` |
| L0 tools | `tools/l0/ratchet.py`, `include_boundary.py`, `coverage_gate.py`, `doxygen_gate.py`, `tools/gen_diagrams`, `scripts/ui_contract.py` |
| Guards and baselines | `tools/guards/*.py`, `tools/guards/baselines/` |
| Bench runner and scenarios | `tools/bench/run_bench.py`, `hazard_steps.py` (`--list` names every step), `scenario_*.py`, `hazard_rig.py`, `hazard_grade.py` |
| MCB simulator | `scripts/rtps_mcb_sim.py`, `scripts/mcb_sim_logic.py` |
| Self-test peer, remote UI client | `scripts/rtps_selftest.py`, `scripts/hmi_ui.py`, `tools/bench/ui_client.py` |
| Bench baselines | `tests/characterisation/baseline-e2047a4/` (boot log and expected markers, self-test JSON, walk PNGs) |
| Desktop simulator | `sim/`: out of service (its README says so); not part of any gate |
