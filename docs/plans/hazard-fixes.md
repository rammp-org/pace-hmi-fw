# Plan: fix the safety hazards (H1, H2, H3/H9, H5/H6, then the rest)

Status: **v2, for approval** (2026-10-06 morning). v1 was rewritten after two adversarial
reviews (R-A safety, R-B feasibility), which found v1 would not close H5/H6, H3 or H2 as
written. Hazard IDs: `docs/plans/refactor.md` §1. Every fix is a **behaviour change on safety
code**: spec first, two human approvals (CS-SAF-05), then code by a different agent, tests
that never regenerate goldens from the new code.

## 0. Summary

- **H1 must be fixed together with H5/H6.** Row 1 of the drive table (LOCKED + MIB ENABLED → unlock) runs first on
  every tick, so no re-send or POST gate can work while it exists (R-A 2, 3; R-B R2).
- **Firmware alone cannot fully close H3.** Board 2's stick calibrates to 6–11 mV at the low end,
  so an open pot (0 mV) can't be told from full travel. It needs an EE decision: end-stop
  resistors or a wiper bias keeping travel off both rails (R-A 1, R-B R1).
- **The UI task decides every gate today.** H4 (the ADC task checks link, MIB state and a UI heartbeat
  itself) and H11 (task watchdog) are prerequisites, not later items (R-A 13, R-B R13).
- **Order:** owner decisions → land the two drafts after about 1.5 days of fixes → test tooling
  (bench stick injection, quick POST evaluator, oracle restructure, requirement matrix) → the
  fixes as stacked table changes. Critical path ≈ 6–7 agent-days plus five two-approval reviews.

## 1. Owner decisions needed first

| # | Decision | Why |
| --- | --- | --- |
| D1 | A second reviewer for safety changes; CODEOWNERS for `components/drive_session`, `components/stick`, the tables and goldens; protect the integration branch | CS-SAF-05, CS-OWN-13; nothing safety-relevant can merge without it |
| D2 | EE: keep the stick's electrical travel off both rails (end-stop resistors or a bias), then measure real open- and short-circuit readings | H3's low-rail case |
| D3 | MCB team: does the MIB time out XYTwist on its own (how long)? What exactly does DISABLE do, and does it latch? | After an HMI reset mid-drive, only the MCB can stop the last deflected command (R-A 21) |
| D4 | Values: plausibility rail limits, fault window, recovery time, re-send rates, POST budget | proposed below |

## 2. Phase A: land the drafts (refactor only), about 1.5 days

| Draft | Work before it can merge |
| --- | --- |
| stick | per-component `.clang-tidy` (60-line functions); create the logger at start-up (no lazy allocation on a fault path); bench rerun incl. `mem.stk_adc` |
| drive_session | split `drive_perform` (115 lines) into ≤60-line action families; fold the ~10 `drive_*` globals into one struct; move the types out of the 915-line header (owner-approved; a constexpr fingerprint proves TRANSITIONS unchanged); table correction: `CLEAR_MENU_ON_ARRIVAL` on rows 3–9 instead of the side flag; document and pin the two-Env tick; U3 stays as-is |
| both | D1 in place; CI green (incl. cppcheck); bench B0–B5 rerun |

## 3. Phase B: test tooling (parallel lanes)

| Item | Design |
| --- | --- |
| B1 stick injection | `CONFIG_HMI_BENCH_STICK_INJECT` (`depends on HMI_REMOTE_UI`, default n; `#error` otherwise). The remote UI is only an adapter: it sends `StickInjectMsg{x,y,twist mV, fail_mask, seq}` through an fw_core Mailbox. The ADC task drains it at the `RawReadsMv` passed to `cycle()`, in an `if constexpr` block. Injection expires after 300 ms without a refresh. An on-screen "STICK INJECTED" marker shows while it is active. `fail_mask` simulates failed reads (H9). The bench preflight asserts the sim is the only RTPS participant; consider a bench-only DDS domain. Release guard: grep the symbol, assert release `sdkconfig.h` lacks it, tag bench images |
| B2 quick POST | a new pure evaluator over a table subset (host L1). The ADC task accumulates about 1 s of rest statistics and sends them in a message. `std::atomic<uint8_t> post_state` is read by the UI task. No lv_* off the UI task; the 1,281-line selftest is not touched |
| B3 oracle restructure | enumerate only the guards each input reads, so new hidden bits don't multiply the 1.35M combinations past the 30 s budget (TS-UNIT-09) |
| B4 requirement matrix | `run.py report` maps REQ → tests (TS-COV-01); superseded REQs are retired by ID, never reused |
| B5 sim modes | `rtps_mcb_sim.py`: ignore or drop N DISABLEs, stay ENABLED across an HMI reset, timestamped log of every DriveCommand and XYTwist |

## 4. Phase C: the fixes (each: spec commit → 2 approvals → code commit by another agent)

The v2 rules below are history. Where they differ, §9 and the four specs win
(`hazard-c1-spec.md`, `hazard-c3-spec.md`, `hazard-c4-spec.md`, `hazard-c2-spec.md`), and the
order, lanes and effort are in `hazard-decisions.md` §4. Examples: v2's `DISABLE_PENDING` and
POST phases are gone (C1 §2, C3 §1); B5'' "sim ENABLED at boot → no unlock" became "enters Drive,
output 0" (M1).

### C1 = H1 + H5 + H6 (drive table)

| Rule | Rows |
| --- | --- |
| LOCKED/ASKING + driving_ok with no outstanding ENABLE, or with `DISABLE_PENDING` → stay LOCKED, send DISABLE, show "MIB enabled without a request" | 1, 2 |
| Every transition into LOCKED sends DISABLE and sets `DISABLE_PENDING`; `drive_request := DISABLE` | 3–9, 12–13 (listed one by one in the spec commit) |
| `DISABLE_PENDING` clears only on a **fresh** (CONNECTED, < 2 s) MibStatus that is not ENABLED; it stays set through link loss | new TICK sub-step, named with its place in TICK_SEQUENCE |
| Re-send every 250 ms; after 5 s a persistent fault "MIB did not stop" and re-send at 1 Hz for as long as the MIB reports ENABLED. Never stop | new |
| EXITING/EXIT_REFUSED: the exit DISABLE also sets `DISABLE_PENDING`; EXIT_REFUSED gets a timeout row (persistent fault) | 21–28 |
| ENABLE (row 18) requires `!DISABLE_PENDING`; publish results are checked and failures counted | 18 |
| U3 path removed | — |

### C3 = H2 (drive table + POST)

| Rule |
| --- |
| New phases `POST_PENDING` and `POST_FAILED`. No row leaves them except POST pass AND link CONNECTED (row 1 included). An oracle case covers "ENABLED during POST" |
| Boot: `DISABLE_PENDING` is set at start (C1's mechanism), so the first fresh link sends and re-sends DISABLE |
| Latched POST checks are hardware only: ADC valid, `joy.cal_saved`, an expected-device I2C list, reset reason (shown, H12), image, memory and stack headroom |
| Stick at rest is a **live** guard, not a latch: `*_cal_off` inside the dead band and `joy.button_idle`. A stick bumped at power-on only delays; it doesn't lock the user out |
| A persistent fault indicator, separate from the transient refusal banner slot, shows the failing check |

### C2 = H3 + H9 (stick table), after D2

| Rule |
| --- |
| A state machine OK / SUSPECT(n) / FAULT / RECOVERING with its own table, oracle and 100 % branches |
| Plausibility on the **raw** reads (before averaging and lowpass): absolute rail limits (TBD after D2) plus the calibration band; NaN and failed reads count as implausible; N-of-M window, not N consecutive |
| SUSPECT and FAULT: output literal 0 (not ×0), keys and flicks 0, button bit defined in the spec; neutral XYTwist keeps flowing (plus a fault flag in XYTwist if the MCB team agrees, D3) |
| A fault while DRIVING → LOCKED + DISABLE via C1 (new drive-table guard `STICK_OK`, C2b). Clearing the fault never resumes driving: a new unlock hold is needed |
| Way out of a latched fault: recalibration, reachable by touch; detection is suspended (output 0) while calibrating; the cal is validated at load (min < centre < max, minimum span) |
| H10: the gate opens, and ENABLE is sent, only after ≥300 ms in the dead band |

### C4 = H4 interim and H11

| Rule |
| --- |
| ADC task forces scale 0 when the UI heartbeat atomic is older than 200 ms, MibStatus is stale, or the last MIB state is not ENABLED (the full fix waits for the islands work) |
| Task watchdog on the ADC, UI and net tasks; ADC task priority above UI, pinned, stack from the stress test |

Measured (G10 task dump, 2026-10-06): the ADC task runs at prio 5 on core 0, below lv_task (prio 20, core 1), so H11's "ADC above UI" still needs a change.
Unpinned FPU tasks (Read ADC, ContinuousAdc, rtps_*) get a boot-dependent core: ESP-IDF pins a task to the core of its first FPU use (ContinuousAdc: core 1 on one boot, core 0 on the next). C4 must pin the ADC task explicitly.

### Seat path (H8, H10 seat, H6 seat), after C1

| Rule |
| --- |
| Seat presses gated by POST_OK, STICK_OK and `seat_ready` (from Bench → Actuators and Skunk Works too); MIB seat values validated (non-finite or out of range → unknown, R2's RTPS findings); SeatCommand delivery gets the same check-and-count |

## 5. Bench B5'' (with injection and sim modes; verdicts by script)

| Step | Check |
| --- | --- |
| Gate | injected full forward: XYTwist 0 while LOCKED, on a menu, on another screen, while calibrating; > 0 on Drive |
| C2 | 6 mV (must still drive full) vs 0 mV vs rail; alternating bad/good; failed read; fault while DRIVING → relock + DISABLE; release → no auto-resume; keys silent; fault indicator persists |
| C1 | sim ignores N DISABLEs: re-send at 250 ms, fault at 5 s, never stops; exit refused; link pulled while DRIVING, restored with the sim ENABLED → stays LOCKED; profile click after relock → sim logs DISABLE |
| C3 | stick deflected ~25 % at boot → waits, names the check; release → passes; sim ENABLED at boot → no unlock, XYTwist never ≠ 0 before POST pass; seat press refused before POST; reset reason shown |
| Not bench-testable | H4 (UI stall): host fault-injection test |

## 6. Lanes (agents) and effort (AI time; reviews on top)

Superseded for Phase C by `hazard-decisions.md` §4 (order C1+C3 → C4 → C2 → C2b → seat, §9).

| Step | Lane | Effort |
| --- | --- | --- |
| A1 stick draft fixes + merge | S | 0.5 d |
| A2 drive draft fixes + merge | D | 1–1.5 d + table review |
| B1 injection (after A1) | S | 1–1.5 d |
| B2 quick POST evaluator | P | 1.5–2 d |
| B3 + B4 oracle restructure, REQ matrix | T | 1 d |
| B5 sim modes | T | 0.5 d |
| C1 (after A2) | D | 1.5 d + approvals |
| C3 (after C1, B2) | D | 1 d + approvals |
| C2a stick fault table (after B1, D2) | S | 2 d + approvals |
| C2b STICK_OK guard (after C3) | D | 0.5 d |
| C4 heartbeat + watchdog | S | 0.5–1 d |
| Seat path (after C1) | D | 1 d |

The drive table is serialised (lane D); the other lanes run in parallel. `app_main` is the only
shared file: B1 merges before B2's boot hook.

## 7. Owner decisions (2026-10-06)

| # | Decision |
| --- | --- |
| D1 | The owner is the only approver for now (CS-SAF-05 deviation in the profile); nothing safety-relevant merges to `dev` until a second reviewer exists |
| D2 | Firmware-only for H3: detect the high rail, NaN and failed reads; the low-rail open circuit (0 mV) is a documented residual hazard |
| D3 | Pending: the owner asks the MCB team about the XYTwist timeout and DISABLE semantics; C3 sign-off waits for the answer |
| Phase A | Approved to start on the draft branches (incl. the table-type move with a fingerprint, and the CLEAR_MENU_ON_ARRIVAL table correction as its own commit for review) |
| 9b8574f | Table correction approved by the owner (2026-10-06): CLEAR_MENU_ON_ARRIVAL on relock rows 3-6, 8, 9, replacing the side flag; behaviour unchanged (goldens GLD-001..004 pass); fingerprint 0xF7D77ACEBF184D80 -> 0xD8AAB0E61BE44A91. The drive branch merges once the drive-tick CPU timing settles the B3 rtt_p50 question (owner's choice) |

## 8. Findings during Phase A

| Finding | Where | Action |
| --- | --- | --- |
| A lazily built `static espp::Logger` on a fault path (CS-SAF-04) | `components/fw_core/src/port_freertos.cpp:17` (`log_error`) | Construct at start-up before fw_core is used in a safety island |
| clang-tidy is not in CI (cppcheck only), so the 60-line limit holds only via `idf.py clang-check`, which needs a clang-toolchain build | `.github/workflows/static_analysis.yml` | Add a clang-toolchain clang-check job |

## 9. Owner decisions (2026-10-08): the MCB is the authority on driving

Naming: MIB (Meebot interface board) is the MCB (main control board, the motor driver) for
Meebot. These decisions supersede C1's v2 rows 1-2 ("stay LOCKED on an unrequested ENABLED").

| # | Decision |
| --- | --- |
| M1 | The MCB decides whether the system drives. MCB reports ENABLED → the Tab5 enters the Drive screen at once; MCB leaves ENABLED (for any reason, including on its own) → the Tab5 exits the Drive screen. H1 is accepted by design under guards G1-G5. |
| G1 | Neutral first: on entering Drive, stick output stays 0 until the stick has been in the dead band for ≥ 300 ms (H10). |
| G2 | Follow only a fresh MibStatus (< 2 s old) on a CONNECTED link; stale → output 0 (C4). |
| G3 | No stick output before POST pass (C3) and with a stick fault (C2); the Drive screen shows the reason. |
| G4 | The MCB wins over the HMI's stop: when the user presses stop (burger key, exit hold) the HMI sends DISABLE, re-sends it every 250 ms, shows a warning, raises "MCB did not stop" after 5 s and re-sends at 1 Hz while the MCB reports ENABLED. While the MCB reports ENABLED the HMI stays on the Drive screen and **the stick keeps driving**. Owner-accepted risk: if a DISABLE is lost or ignored, the HMI's stop does not stop the chair; only the MCB can. |
| G5 | Joystick calibration: while a calibration is in progress the HMI does not switch screens and outputs 0. If the joystick has never been calibrated (no valid saved calibration), the HMI outputs 0 and the Drive screen shows "The joystick must be calibrated first". |
| D4 | Values approved: re-send 250 ms; fault at 5 s, then 1 Hz; neutral 300 ms; UI heartbeat 200 ms; MibStatus fresh < 2 s; POST limits per post-limits-proposal.md (WINDOW_MIN_SAMPLES 30). Each is a named constant. |
| Review | Spec per fix: each fix's spec commit is approved by the owner before code is written; code by a different agent; bench; merge to dev_refactor. Order C1 → C3 → C4 → C2 → seat path (lane D: C1, C3, seat; lane S: C4, C2). |

### To verify with the MCB team (D3)

1. Assumed: the MCB stops the motors on its own when XYTwist stops arriving (HMI reset, link
   loss). After how many ms?
2. Required by M1: the MCB enables only on a deliberate request. Can it ever report or become
   ENABLED without receiving an ENABLE (its own reset, a glitch, another participant)?
3. After a DISABLE, how long can MibStatus keep reporting ENABLED (ramp-down)? This sets G4's
   5 s fault timer.
4. What exactly does DISABLE do (immediate stop?), and does it stay disabled until a new ENABLE,
   across an MCB or HMI reset?

Message to forward: "For the HMI safety review: (1) If the MCB stops receiving XYTwist (HMI
reset or link loss), does it stop the motors on its own, and after how many ms? (2) Can the MCB
ever report or become ENABLED without receiving an ENABLE? (3) After a DISABLE, how long can
MibStatus keep reporting ENABLED? (4) Does DISABLE stop at once and stay disabled until a new
ENABLE, including across an MCB or HMI reset?"

## 10. Reconciliation log (2026-10-08, the four specs against each other and §9)

No §9 decision changed. Open questions moved to `hazard-decisions.md`.

1. One permit: C4's verdict is condition 2 of C1's output permit instead of a factor in `stick_drives` (C4 §2.1, REQ-CTL-02; `gate_open()` gone).
2. C4's own re-arm (C4-f, `REARM_PENDING`, `NEUTRAL_REARM_MS`) removed: C1's neutral latch re-arms; REQ-CTL-14 restated, CTL-020/021 unconditional; C4 O1 merged with C1 Q12 and C2 O8.
3. One hold-reason list (C1 §3.3, REQ-STK-13): GATE_SHUT, MOTION_GUARD, CALIBRATING, NOT_CALIBRATED, POST_NOT_PASSED, STICK_FAULT, STICK_CHECK, CENTRE_FIRST, NONE; C1 provides the C4 hook (OK until C4) and `StickHealth::CHECK` for C2.
4. C2 no longer retires REQ-STK-13 and drops its own order requirement; C2 E9 replaces only STK-067 (STK-066 stays).
5. C1 §2.7 Drive-notice order now follows §3.3 (not calibrated before POST); C3 Q14 resolved.
6. C4 called REQ-STK-04 "unchanged" (`mounted × scale`); C1 retires it, so C4 now cites C1's literal 0 (REQ-STK-10).
7. Text owners: one table in C1 §3.3; every new text in `hmi_rtps_spec` (C3's `hmi_ui/post_texts.hpp` moved there); new C4 notice text "Waiting for the MCB" for MOTION_GUARD (to approve).
8. REQ-UI: C3 holds 19-23, so C4 23/24 → 24/25 and C2 21/22 → 26/27.
9. REQ-STK: C3 holds 15, so C2 15-26 → 16-27 (its 27 dropped, see 4).
10. REQ-RUI: C3 holds 06; C4 adds 07 for its STALL verbs (O6); C2 06 → 08 and retires REQ-RUI-05 whole, not "the PERMIT STICK part only".
11. REQ-DRV: C2b's 35/36 clashed with C3 → 43/44; C2b adds 45 (eight tick sub-steps), retiring C3's 36.
12. C3 changed row 18 without retiring REQ-DRV-08 → C3 retires it for REQ-DRV-42; C2b retires 42 for 44.
13. Rows: C2b 50-52 → 55-57 (after C3's 50-54); C2 §5.1 "row 50/51" → 55/56.
14. C2b row 57: guard `+RDY +POST_OK +STICK_FAULT`, actions RING_REST, SHOW_REFUSED_STICK, REFUSAL_FEEDBACK, exclusive with rows 19 and 52 like C3's refusals.
15. C2b's TICK_STICK_FAULT moves from "right after FOLLOW" to after TICK_STOP_RESEND: Env 2 predates its DISABLE, so the earlier slot sent two DISABLEs on the fault tick (against C2's own golden).
16. Test IDs: C2b DSO-020 → 024, GLD-120..122 → 126..128, DRV-120 → 118; C2b updates DRV-101..117, DRV-116, DSO-001/004-006/012/018 and GLD-115 (57 rows) and uses C3's booted preamble; GLD-126's relock line is in C1's log format.
17. C2 §3.7 cited STK-078 (a classification case) for monitor + latch; now STK-084.
18. C4 §8 kept B5a..e "unchanged"; C1 E10 changes B5c-e, so C4 now says "as C1 left them".
19. Self-test count marker: C4 54 → 57, C2 57 → 60 (C2 only said "moves").
20. Task table: C4's 6144 B rationale now names C3's accumulator; C3 Q13 names the exact `tasks.json` row and C3's "G10 unchanged" notes it; `ContinuousAdc Task` stays 5/fpu unless pinning is approved (C4 O9 = C2 O14), then in C4 commit 4.
21. C3 runs C4's `-fstack-usage` SU step itself, since C3 lands before C4.
22. One ADC-side clock (`uint32` ms, injected) for C1's permit (was `now_us`), C4's guard and C2's monitor.
23. C4's STALL verbs gated by `CONFIG_HMI_BENCH_STICK_INJECT` like every bench verb (was `HMI_REMOTE_UI`).
24. C2's stick FAULT on C3's TopBar indicator ranks below every POST state (C3 §2.8, REQ-UI-26).
25. C2's button-bit rule defers to C3's REQ-STK-15 before POST pass.
26. C1 Q4 restated: C1 and C3 merge together, so the NOT_RUN block never ships alone; the motion-guard hook added.
27. C1 "every 33 ms" → the ADC cycle (33 ms wait, 35 ms measured), as C3 and C4 use.
28. §4 and §6 here marked superseded by the specs and `hazard-decisions.md` §4.
