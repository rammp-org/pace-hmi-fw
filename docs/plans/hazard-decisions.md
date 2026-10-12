# Owner decision sheet: hazard fixes C1, C3, C4, C2

Status: **for the owner** (2026-10-08). Inputs: `hazard-fixes.md` §9 (decisions M1, G1-G5, D4,
review mode) and the four reconciled specs: `hazard-c1-spec.md`, `hazard-c3-spec.md`,
`hazard-c4-spec.md`, `hazard-c2-spec.md`. The reconciliation is logged in `hazard-fixes.md` §10.
Nothing here changes a §9 decision.

## 1. Exec summary

**What the four fixes do together.**

- **The MCB decides (C1, M1).** MCB ENABLED → the Tab5 goes to Drive by itself; MCB not
  ENABLED or silent 2 s → back to Locked, with one DISABLE (H5).
- **One output permit on the stick task (C1, filled by C3, C4, C2).** The stick drives only if
  all hold, in this order: Drive gate open; link, MibStatus, MCB state and UI heartbeat fresh
  (C4); no calibration running; a measured calibration (G5); POST passed (C3); stick not
  faulted and not suspect (C2); stick centred 300 ms since the last hold (G1). Otherwise a literal
  0 goes out and XYTwist keeps flowing. The first failing condition is shown on the Drive screen.
- **Stop (C1, G4).** Exit hold or burger key: DISABLE every 250 ms, "MCB did not stop" at 5 s,
  then 1 Hz. While the MCB stays ENABLED the HMI stays on Drive and the stick keeps driving.
- **Boot (C3).** POST at every boot: hardware checks latch a FAIL until reset; "stick at rest"
  only delays. One DISABLE on the first fresh link. No ENABLE from the HMI before POST pass. A
  TopBar indicator shows a waiting or failed POST; the reset reason is shown; an unclean reset
  fails POST until a clean one.
- **Stick task independent of the UI (C4).** The stick task checks staleness itself (output 0
  within 240 ms of a UI stall); it runs above the UI (prio 21, core 0, 6144 B). A task watchdog
  resets a UI or stick task hung for 2 s.
- **Stick plausibility (C2).** Raw reads checked every cycle: failed, NaN, above the rail,
  stale X/Y. One bad sample → 0 for ~0.1 s, then centre again. 3 bad in 30 cycles → latched
  fault, cleared only by a recalibration or a reboot; optionally a DISABLE (C2b). Failed reads
  send neutral instead of nothing (H9). Calibrations are validated at load and at the end of a run.

**Residual hazards (after all four).**

1. The HMI's stop is a request: a lost or ignored DISABLE leaves the chair driving (G4, accepted).
2. The HMI enters Drive whenever the MCB says ENABLED, asked or not (H1, accepted by M1).
3. An open pot at 0 mV reads as full travel; one that opens **while driving** drives at full
   deflection until the user or the MCB stops (D2, firmware cannot see it).
4. A short to 3.3 V is caught only if the ADC reads above the limit (unmeasured, M-2).
5. Wrong but plausible readings (stuck wiper, drift, cross-short) are not detected.
6. Stick task hung, panic, HMI reset or link loss: only the MCB's XYTwist timeout stops the
   chair (T_mcb unknown).
7. A hung UI task sends no DISABLE re-sends (C4 zeroes the stick, not the stop).
8. A profile tap in the ≤ 0.75 s before the HMI sees the MCB stop re-sends ENABLE.
9. A latched stick fault and a POST FAIL live in RAM: a reboot clears them.
10. POST runs on the UI task (CS-SAF-04 deviation until the islands work).
11. A spike shorter than one X/Y window (~128 ms) is seen only through its maximum; dips are not.

## Answers (owner, 2026-10-08)

- **All 42 questions: the recommendation ("rec")**, accepted in one answer. Explicitly confirmed:
  C1 (a) an unclean reset latches a POST FAIL until a clean reset; C3 (a) calibration only while
  locked.
- **D9: (b)**: with a latched stick FAULT, refuse the unlock hold (D9a, row 57) **and** the
  Locked-screen entry push and the menu's DRIVE row (D9b: two more C2b rows, numbered after 57,
  same pattern as C3's rows 53-54, with the fault named in the refusal).
- C5 (board 1, I2C 0x5a): measure boards 1 and 3 first (M5), then decide.
- D3 (rail limits): approved as proposed, to revisit after the owner's measurements M2-M4.
- Any single answer can be changed later by ID.
- **B5''-8 conflict (2026-10-09):** C1 §2.3 keeps the exit-refused row clearing THEN_MENU (TABLE.md:
  no THEN_MENU in EXIT_REFUSED; row 9 F2 → F2), while C1 §6 B5''-8 expected "Locked with the menu
  open". Owner: **keep the table**; B5''-8 expects Locked with the menu closed. In EXIT_REFUSED a second
  burger-key press still asks again with the menu (row 28).

## Answers (owner, 2026-10-11)

On the Tuesday candidate (`dev_ai_hazard_candidate`, results:
[hazard-candidate-results.md](hazard-candidate-results.md)). IDs are the coordinator's T-list,
plus this sheet's E5.

- **T11, approved:** espp RTPS's `rtps_worker_*` run at priority 20, raised at RTPS start
  ([hazard-c4-rtps-priority.md](hazard-c4-rtps-priority.md), option 1).
- **E5, approved:** the `ContinuousAdc Task` runs at priority 20 (`task_priority` in main's
  StickIsland config; [hazard-c2-cadc-priority.md](hazard-c2-cadc-priority.md), option 1). This
  replaces the 2026-10-08 answer "detect only".
- **B5i:** the 240 ms is measured at the board's publish, not end to end; the network's share is
  reported, not graded.
- **T1:** the boot DISABLE is sent once the DriveCommand writer is matched (B5''-16's race; lane L1
  implements it).
  - Done (2026-10-11) with no table change (the fingerprint is unchanged): espp rtps is vendored
    as `components/espp_rtps` (the D6 precedent) with one accessor, `matched_readers(topic)`; the
    drive adapter holds row 50's DISABLE until `rtps_comms_drive_writer_matched()`, publishes it
    once on the first tick that finds the match (a refused publish, which hands nothing to RTPS,
    is tried on the next tick), and drops it when the user's own ask (the unlock hold's ENABLE)
    leaves LOCKED. Still one message, no re-send (REQ-DAD-12; DAD-014..016, GLD-131..133).
    Board evidence: in boot 2 of 5 the sim matched the HMI's DriveCommand writer at 8.06 s
    (4.41 s otherwise), after the boot DISABLE had gone.
  - **Residual for the owner:** under M1, Drive can be entered before the deferred DISABLE goes
    out. With the MCB ENABLED at boot and the writer not matched yet, the first CONNECTED tick
    marks the boot stop, the next tick enters Drive (rows 1-2), and the DISABLE leads the first
    tick after the match (GLD-133). The stick output stays 0 until it is centred (C1 G1) and
    POST has passed; before T1 the same DISABLE was lost.
- **T15:** C2-16 runs without the stick injection (the real stick at rest), so the starved X/Y
  producer is what the monitor judges (REQ-STK-26 unchanged).
- **T12:** espp's `RtpsParticipant::publish` allocating per XYTwist on Read ADC is accepted for
  now, documented as a residual, to fix later.
- **T13:** C-ABI hook state is allowed (the allocation-hook lane).
- **T8 / T18:** the new texts are approved: the calibration reject "Calibration failed: an end
  reads too high." and the Joystick screen's stick-fault words (C2), and the others listed there.
- **T4:** when click.wav fails to load, the UI keeps running without the click (an error in the
  log).
- **T6 / T16:** B5i/B5j grade the watchdog phase as proposed (phase 1: the report naming the task,
  no reset); C2-14's `xy_age_max_ms` band is 250 ms until the measurement M8.
- **T7:** golden rows 1876 and 1878 (the key re-engaging with a flick after an invalid row) and
  GLD-126's order (the stop's DISABLE, then the relock's) are accepted.
- **C2-8:** "exactly one DISABLE while the MCB reads ENABLED, at most one more at the relock" is
  accepted as C2-8's pass rule.

## Measurement fixes (orchestrator, 2026-10-11, for owner review)

From L1's analysis of the candidate bench on 1485932 (`cand3-1485932-20261011-213852`). Each is
"orchestrator, 2026-10-11, measurement fix, for owner review": the bench measures closer to the
firmware; no pass band moves and no safety property changes.

- **Sim stamps (SIM-015):** the sim stamps XYTwist and DriveCommand as its single receive loop
  decodes them, and its SEDP rounds slept in that loop (~0.19-0.22 s, three times a step in the
  first ~5 discovery rounds), so samples arrived in a burst, stamped late. The sim's SEDP rounds
  now go from a thread of their own (`rtps_host.SEDP_OFF_LOOP`). Orchestrator, 2026-10-11,
  measurement fix, for owner review.
- **B5''-18 from the gate:** the POST runner takes RERUN on its next 250 ms PostStage tick
  (hazard-c3-spec.md), so "XYTwist zero" is graded from the first STATE poll showing the gate
  off PASS plus one ADC cycle and its publish (35 ms), not from RERUN + 0.2 s. Still: no non-zero
  XYTwist after the gate is shut. Orchestrator, 2026-10-11, measurement fix, for owner review.
- **Link-stall rule (BENCH-073):** a graded window holding a proven link stall (an XYTwist gap
  over 0.15 s that an unrelated remote-UI reply delayed over 0.15 s overlaps) makes the step
  NOT_RUN "bench network stall", never PASS, retried like an injection lapse (B5''-6's 0.453 s
  DISABLE gap). Exempt: B5i, B5j, C2-16, C2-15. Orchestrator, 2026-10-11, measurement fix, for
  owner review.

- **B5''-18c's DISABLEs (orchestrator 2026-10-12, consistent with T1 residual + C2-8, for owner
  review):** after the HMI reboot, exactly one DISABLE while the MCB reads ENABLED (the boot
  DISABLE), and at most one more once it has left ENABLED: the relock's own DISABLE (C1 B1) after
  a one-tick M1 entry, the owner-accepted T1 residual (GLD-133, C3 §2.5), as the owner's C2-8
  rule. That relock must show C1's DRIVE_STOPPED banner, and XYTwist stays 0 from the HMI's link
  on (cand4-1485932: the second DISABLE 0.236 s after the first, with the sim IDLE).

## 2. How to answer

Answer by ID: "rec" takes the recommendation. Rec = the spec agents' recommendation. "Blocks"
names the first implementation step that needs the answer (§4): `C1.n` / `C3.n` / `C4.n` /
`C2.n` = that spec's commit n; `C2b`; `bench` = the board run before merge. Every question
marked C1/C3 also blocks the owner's approval of the C1+C3 spec pair, which comes before any
code (§9 review mode).

42 questions: the 55 open in the four specs and §9 (C3 Q14, Q15 and C2 O13 were settled by the
reconciliation), merged where two ask the same thing, plus D9b, found in the reconciliation.

## 3. Questions

### A. MCB authority and the MCB team (D3)

**A1. Send the extended D3 message now.** §9's four questions, plus: profile change without
re-sending ENABLE (C1 Q10); what the MCB does with the stick button bit (C2 O12); a fault flag in
XYTwist (C2 §1); ask for T_mcb ≤ 500 ms (C4 O8).
Options: (a) send the extended message; (b) §9's four only.
Rec: a. Consequence: answers may change G4's 5 s, B3, B6 and D12 later; (b) leaves T_mcb, the
only stop for residual 6, unasked.
Blocks: C3 sign-off (§7 D3); nothing to start.

**A2. POST is a gate, not drive phases (C3 Q1).** v2's "no row leaves POST_PENDING" cannot
hold under M1.
Options: (a) a gate atomic + Env guard `POST_OK`; (b) POST phases.
Rec: a. Consequence: (a) MCB ENABLED before POST pass → Drive screen, output 0, the check named;
(b) contradicts M1, table redesign.
Blocks: C3.5.

**A3. Entry "at once" (C1 Q1).** On an MCB-initiated entry, keep the 1 s padlock beat and
280 ms fade?
Options: (a) keep; (b) load Drive on the same tick.
Rec: a. Consequence: (a) Drive shows 1 s later, output is 0 meanwhile anyway; (b) rows 1-2 and
35-37 change, more goldens.
Blocks: C1.4.

### B. Stop and re-send

**B1. Every relock sends one DISABLE (C1 Q2)**, also when the MCB stopped on its own or the link
went stale.
Options: (a) yes; (b) no.
Rec: a. Consequence: (b) leaves H5 open.
Blocks: C1.4.

**B2. A stale link ends the user's stop (C1 Q3 + Q13).** C1 drops v2's `DISABLE_PENDING`
(kept through link loss, never stopping); the re-send runs only in the exit phases.
Options: (a) end the stop at link loss (relock, one DISABLE); (b) keep re-sending through the
loss (v2).
Rec: a. Consequence: (a) link back with the MCB still ENABLED → Drive again (M1), no stop
warning; (b) table redesign (+1 d), and the HMI would DISABLE an MCB that M1 says to follow.
Blocks: C1.4.

**B3. Boot DISABLE (C3 Q2).** One DISABLE on the first fresh link after boot; Drive entry one
tick later; no re-send, no fault; none extra when POST fails with the MCB ENABLED.
Options: (a) as written; (b) none (rely on M1 + the permit).
Rec: a. Consequence: (a) costs one message, a slow MCB may give one tick of Drive then a relock;
(b) after an HMI reset mid-drive the MCB is never asked to start from a deliberate request.
Depends on D3 Q2/Q4.
Blocks: C3.5.

**B4. "MCB did not stop" lifetime (C1 Q7).**
Options: (a) clears at the relock, stays in the log; (b) stays until acknowledged.
Rec: a. Consequence: (b) adds an acknowledge path and a hidden bit (+0.5 d).
Blocks: C1.4.

**B5. Exit-refused banner (C1 Q8).** Keep the 2 s banner at 750 ms beside the persistent
"Stopping" notice (row 14 unchanged)?
Options: (a) keep; (b) drop.
Rec: a. Consequence: (b) changes row 14 and its goldens.
Blocks: C1.4.

**B6. Profile tap (C1 Q10 + C3 Q8).** Rows 31-32 send ENABLE only with the MCB ENABLED and
POST passed; nothing otherwise. DriveCommand has no "profile only" request.
Options: (a) as written, accept the ≤ 0.75 s race (residual 8), ask the MCB team (A1); (b) never
send on a tap.
Rec: a. Consequence: (b) profile changes need a protocol change first.
Blocks: C1.4, C3.5.

**B7. UI sends DISABLE when the stick task dies (C4 O10).** An ADC heartbeat checked in the drive
table would give a stop path that does not need the stick task.
Options: (a) lane D costs it as a follow-up spec after C4; (b) no.
Rec: a. Consequence: (b) keeps residual 6 for a stick-task hang.
Blocks: nothing now.

### C. POST and calibration

**C1. Unclean reset fails POST until a clean reset (C3 Q3).**
Options: (a) latch (power cycle or Restart HMI clears it); (b) show only.
Rec: a (CS-SAF-03). Consequence: (a) with C4's watchdog a trip strands the user until a power
cycle; (b) a crash loop can drive between crashes.
Blocks: C3.4.

**C2. A calibration done in this boot (C1 Q5 + C3 Q4).**
Options: (a) counts for G5 at once (also unsaved), but POST's latched `joy.cal_saved` FAIL holds
until a restart ("Calibration saved. Restart the HMI to drive"); (b) re-judge POST's two
calibration checks on a save (evaluator change); (c) unsaved runs never count.
Rec: a. Consequence: (b) +0.5 d and an evaluator change; (c) a C1 change only. With C3 the
unlock hold is refused anyway until POST passes, which answers C1 Q5's second part.
Blocks: C1.5, C3.4.

**C3. Calibration only while locked (C1 Q6).** Needed so no screen change can hit a running
calibration (G5).
Options: (a) yes; (b) no.
Rec: a. Consequence: (a) with a stick FAULT and an MCB ignoring DISABLE, the user stays on Drive
at output 0 and cannot recalibrate until the MCB stops (see D8); (b) G5 needs another mechanism.
Blocks: C1.4.

**C4. POST time budget (C3 Q5).** 3000 ms from POST start (not in D4); live checks have none.
Options: (a) 3000 ms; (b) another value.
Rec: a (expected 1.3 s). Consequence: a dead ADC fails POST at 3 s instead of never.
Blocks: C3.4.

**C5. Board 1 has no I2C 0x5a (C3 Q6)** and fails POST.
Options: (a) per-board list (Kconfig); (b) drop 0x5a from the list.
Rec: measure boards 1 and 3 first (M5), then decide. Consequence: until then board 1 cannot
drive with C3.
Blocks: bench on board 1 only.

**C6. POST runner on the UI task (C3 Q7)** until the islands work moves it to `control`.
Options: (a) yes, CS-SAF-04 deviation; (b) on the stick task now.
Rec: a. Consequence: (b) needs C4's stack in C3 and more stick-task time.
Blocks: C3.4.

**C7. Image check (C3 Q10).**
Options: (a) the bootloader's validation (`static_assert` no skip option, 0 ms); (b)
`esp_image_verify` at boot (4 MB read).
Rec: a. Consequence: (b) adds boot time.
Blocks: C3.4.

### D. The stick: permit and faults

**D1. Neutral wait on every held → allowed change (C1 Q12 = C4 O1 = C2 O8).** Not only on
entering Drive: also after a menu, POST pass, a C4 trip, a stick glitch.
Options: (a) yes; (b) only on entering Drive (§9's text).
Rec: a. Consequence: (a) a single glitch stops the chair until the stick is centred 300 ms
(soak must show 0 glitches); (b) a stick held deflected jumps back when a stall or glitch ends,
and C4 needs its own re-arm again.
Blocks: C1.5.

**D2. Hooks before their fix lands (C1 Q4).** The motion-guard hook passes until C4; stick
health NOT_MONITORED passes until C2.
Options: (a) yes; (b) block the stick until C4 / C2.
Rec: a. Consequence: (a) H3/H4 stay as today in between; (b) no stick driving on dev_refactor
for weeks.
Blocks: C1.5.

**D3. Rail limits (C2 O1).** `HIGH_RAIL_MV` 3150, `CAL_OVERSHOOT_MV` 100 (cal max ≤ 3050 mV).
Options: (a) approve, revisit after M2-M4; (b) wait for the measurements.
Rec: a. Consequence: a board with a saved max > 3050 mV loses its calibration at C2's first
boot (M4 first).
Blocks: C2.3.

**D4. Fault window (C2 O2).** 3 bad of 30 cycles latches; 3 good samples end SUSPECT.
Rec: approve. Consequence: lower N latches faster but more false latches.
Blocks: C2.3.

**D5. Stale and start limits (C2 O3).** `XY_STALE_MS` 300, `START_MAX_MS` 1000.
Rec: approve, confirm with `joy.xy_age_max` (M8).
Blocks: C2.3.

**D6. Vendor espp's `adc` (C2 O4)** for a per-window maximum and a sequence number.
Options: (a) vendor now, offer upstream; (b) upstream PR first; (c) window means only.
Rec: a. Consequence: (b) slower; (c) no stale detection and no "before averaging" for X/Y.
Blocks: C2.1.

**D7. Twist: all 8 reads must succeed (C2 O5).**
Options: (a) 8 of 8; (b) allow 1 failure.
Rec: a, loosen only if the soak shows partials.
Blocks: C2.3.

**D8. Stick FAULT while driving (C2 O6).** C2b sends DISABLE as an exit hold does (rows 55-56),
and G3 wins over G4: output stays 0 while the MCB stays ENABLED.
Options: (a) both; (b) output 0 only, no DISABLE (no rows 55-56).
Rec: a. Consequence: (b) the MCB stays ENABLED behind a broken stick. In both, with C3 = a the
user cannot recalibrate until the MCB stops.
Blocks: C2b.

**D9. Refusals with a stick FAULT (C2 O7, and D9b from the reconciliation).**
D9a: refuse the unlock hold (row 57). D9b (not in any spec): also refuse the Locked-screen entry
push and the menu's DRIVE row, as C3 rows 53-54 do before POST.
Options: (a) D9a only; (b) D9a and D9b (+2 rows); (c) neither.
Rec: D9a yes (C2); D9b has no recommendation yet. Consequence: (a) the push and the menu row
behave as with a healthy stick (no ENABLE is reachable without the hold).
Blocks: C2b.

**D10. Fault latch in RAM only (C2 O9).**
Options: (a) RAM (a reboot clears it); (b) persist "fault since the last calibration" in flash.
Rec: a for now, revisit with field data.
Blocks: nothing.

**D11. Stick keys live in RECOVERING and when never calibrated (C2 O10).**
Rec: yes. Consequence: no: menus need touch then.
Blocks: C2.7.

**D12. Stick button bit while output is held (C1 §3.1 + C3 Q11 + C2 O12).** C1 passes it;
C3 proposes "released" before POST pass; C2 passes it in every stick state (the exit hold needs
the GPIO, not the bit).
Options: (a) released before POST pass, passed otherwise; (b) always passed; (c) released
whenever output is held.
Rec: a; ask the MCB what it does with the bit (A1). Consequence: (c) changes C1 and C2 too.
Blocks: C3.6.

**D13. Residual-hazard wording (C2 O16)** for the risk file and the user manual (C2 §0; §1 here).
Rec: approve. The EE fix (D2) stays open.
Blocks: C2.11.

### E. Watchdog and tasks

**E1. `Read ADC` priority (C4 O2).** 21 (above every app task, below `esp_timer`) or 22
(topology draft, ties with `esp_timer` on core 0).
Rec: 21, align the draft. Blocks: C4.4.

**E2. Watchdog idle-task checks (C4 O3).**
Options: (a) off; (b) keep, fix the 8 s boot hog on `main` first.
Rec: a. Consequence: (b) with panic on, today's boot hog becomes a boot loop.
Blocks: C4.5.

**E3. Subscribe `rtps_pub` to the watchdog (C4 O4).**
Rec: no (not safety; a panic would reset mid-drive for it). Blocks: C4.5.

**E4. Watchdog timeout 2 s (C4 O5)**, report-only first, panic only after a clean soak (M6).
Options: (a) 2 s; (b) another value.
Rec: a. Consequence: a trip strands the user until a power cycle (C1 = a).
Blocks: C4.5.

**E5. `ContinuousAdc` task (C4 O9 + C2 O14).** It can starve and serve old X/Y. C2 detects that
in ≤ 340 ms.
Options: (a) detect only (C2), add `STALL CADC`, decide pinning from M8 and C2-16; (b) pin or
raise it now (G10 row in C4.4).
Rec: a. Blocks: C4.4 only if b.

**E6. ADC stack in C3 (C3 Q13).** If C3's accumulator grows the stick task's frame by more than
64 B, take C4's 4096 → 6144 B (`tasks.json` `Read ADC`) into C3.
Rec: yes (conditional). Consequence: C4.4 then changes only priority and core.
Blocks: C3.4.

### F. UI texts

**F1. Approve the texts** (one table in C1 §3.3, all in `hmi_rtps_spec`):

| Where | Texts | Spec |
| --- | --- | --- |
| Drive notice | "MCB did not stop", "Stopping: waiting for the MCB", "The joystick must be calibrated first", "Start-up check not run", "Centre the joystick to drive" | C1 Q9 |
| Drive notice (new in the reconciliation) | "Waiting for the MCB" (MOTION_GUARD) | C4 |
| TopBar, banner, About | per-check POST texts (C3 §2.8), "Not ready to drive" / "Seat not ready", "Calibration saved. Restart the HMI to drive", "Last reset: <name>" | C3 Q9 |
| Stick | "Joystick fault: recalibrate to clear", "Checking the joystick" | C2 O11 |
| Stall | "Display stalled: stick paused" (if F2) | C4 O7 |

Rec: approve or reword. Blocks: C1.6, C3.6, C4.8, C2.8.

**F2. Show "Display stalled: stick paused" for 3 s after a stall while driving (C4 O7).**
Rec: yes (CS-SAF-03). Consequence: no: a stall only shows as a pause. Blocks: C4.8.

### G. Goldens, tests and data retired or changed

**G1. Approve the declared changes**, per spec, each before its commit:
C1 E1-E11 (GLD-001/002/004 retired and kept as history, GLD-003 → GLD-115, fingerprint, sizes,
DRV-020/022, DAD-006, STK-016/035 removed, STK-001..009 expect +0.0 where gated, B5c-e graded);
C3 F1-F6 (booted preamble for C1's goldens, `WINDOW_MIN_SAMPLES` 30, B2 markers, B5''-15 →
18b); C4 (G10 `Read ADC` row, TWDT lines, three self-test checks, count 54 → 57); C2 E1-E10 (8
invalid golden rows expect neutral, 73 rail rows only through the monitor, STK-005/015 and
CAL-013/107 retired, `PERMIT STICK` and B5''-12 removed, count 57 → 60). No golden is ever
re-recorded from new code.
Rec: approve. Blocks: C1.4, C3.5, C4.3, C2.7 (each spec's first behaviour commit).

**G2. Full-product oracle out of CI (C1 E5).** DRV-001..012 grow from 1.35 M to ~38 M steps
with C1, ×4 with C3 (POST_OK, BOOT_STOP_DONE), ×2 with C2b: past the 30 s budget.
Options: (a) on demand (`make full`), with `make equivalence` and `make mutants` as evidence;
(b) keep in CI, raise the budget.
Rec: a, measured first; keep it in CI if it fits. Blocks: C1.3.

### H. Bench verbs

**H1. The bench verb set**, all under `CONFIG_HMI_BENCH_STICK_INJECT`, none in a release ELF:
`STATE`, `PERMIT`, `CAL UNSAVED` (C1); `POST RERUN`, `CRASH` (C3); `STALL UI`, `STALL ADC` (C4
O6); injection mask bits 3-7, `STATE` stick fields, `STALL CADC` (C2 O14, O15).
Rec: all. Consequence: without them B5''-x, B5i/j and C2-x cannot be graded on the board.
Blocks: C1.2, C3.3, C4.7, C2.5.

**H2. `POST RERUN` as evidence for "stick bumped at boot" (C3 Q12).** A real boot's POST ends
before the remote UI is up.
Options: (a) accept; (b) a pre-boot injection path or a hand test.
Rec: a. Blocks: bench (C1+C3).

### M. Measurements the owner makes (not questions)

| # | What | Needed by |
| --- | --- | --- |
| M1 | T_mcb, DISABLE semantics, ramp-down time (MCB team, A1) | C3 sign-off; G4's 5 s |
| M2 | Each end stop held 2 s on boards 1-3: max vs the limit (C2 M-1) | D3 revisit |
| M3 | A wiper shorted to 3.3 V through 100 Ω, each axis, one board (C2 M-2) | D3, residual 4 |
| M4 | Boot line `loaded …joystick_cal.txt` on boards 1, 3: every max ≤ 3050 mV (C2 M-3) | C2.6 |
| M5 | I2C scan on boards 1, 3 (C3 Q6) | C5 |

Run by the implementing agents, reviewed by the owner: M6 the watchdog soak (zero `task_wdt`
lines, `ui_stalls_drive` 0) before C4.6; C2-15 (30 min, `joy.bad_samples` 0); B5''-16 (five
boots, no `adc.valid` FAIL at warm-up); M8 `joy.xy_age_max` ≤ 200 ms (the 128 ms X/Y window).

## 4. Implementation plan

Order (§9): C1 and C3 together, then C4, then C2, then C2b, then the seat path. Each: the
owner approves the spec, a different agent writes the code, bench on board 2, merge to
`dev_refactor`. Effort is AI agent-days; reviews and soaks on top.

| Step | What | Lane | Can start | Effort |
| --- | --- | --- | --- | --- |
| 0 | Owner answers this sheet; spec agent folds the answers into the specs; owner approves C1+C3 (and C4, C2 if ready) | — | now | 0.5 d |
| 1a | C1.1, C1.3, C1.4 (refactor, oracle switch, drive table), then C3.5 (C3's rows) | D | after 0 | 2 d |
| 1b | C1.5 (output permit, hooks, ADC clock, `joystick_cal_measured`) | S | after 0 | 0.75 d |
| 1c | C3.1, C3.2, C3.4 (values, accumulator/runner/indicator, POST wiring) | P | after 0; C3.4 after 1b | 1.5 d |
| 1d | C1.2, C3.3 (bench verbs), C1.7, C3.7 (bench scripts) | T | after 0 | 1 d |
| 1e | C1.6, C3.6 (Drive notice, TopBar indicator, REFUSED_POST, seat gate, About, texts) | U | after 1a's table commits | 1 d |
| 1f | Bench B5''-1..22 on board 2; merge C1+C3 together (C3 sign-off waits for D3) | — | after 1a-1e | 0.5 d |
| 2a | C4.1, C4.2 (motion guard + L1, writers; unused) | S | after C4 approval, parallel to 1 | 0.5 d |
| 2b | C4.3-C4.6 (permit condition 2, task row, watchdog phase 1, soak, phase 2), C4.7-9 | S | after 1f | 1.5 d + soak |
| 3a | C2.1-C2.3 (vendor espp, window max/seq, monitor table + L1; unused) | S2 | after C2 approval, parallel to 1-2 | 1.5 d |
| 3b | C2.4-C2.8, C2.10-11 (island wiring, injection, cal validation, monitor wired, UI) | S | after 2b; C2.6 after M4 | 2 d |
| 4 | C2b = C2.9 (rows 55-57, fingerprint) | D | after 1f; merges after 3b | 0.5-1 d |
| 5 | Seat path (H8, H10/H6 seat): spec, then code | D | spec after 1f; code after 4 | 0.5 d + 1 d |

**Parallel:** lanes D, S, P, T run side by side in step 1; 2a and 3a are pure code with host
tests and can run during step 1. Behaviour commits stay serial: C1+C3 → C4 → C2 → C2b.

**Shared files (merge in this order):** `main/main.cpp` (1b → 1c → 2b → 3b);
`components/remote_ui` (1d → 2b → 3b); `drive_port.hpp` (1a → 1c); the drive table (lane D
only); `hmi_rtps_spec` (1e → 2b → 3b); `tools/guards/baselines/tasks.json` (C4.4, or C3 if E6).

**Totals:** about 15-17 agent-days. Calendar with 3-4 lanes: about 8-10 working days plus the
owner's approvals, the watchdog soak and the D3 answer. Critical path: step 0 → 1a → 1e → 1f →
2b → 3b → 4.

**What blocks each step (from §3):**

| Step | Needs answers to |
| --- | --- |
| Approval of C1+C3 | A2, A3, B1-B6, C1-C4, C6, C7, D1, D2, D12, E6, G1, G2, H1, H2 |
| 1f (merge C1+C3) | A1 answered (C3 sign-off, §7 D3); F1 for the texts; C5 only for board 1 |
| 2b (C4) | E1-E5, F1, F2, G1, H1 (STALL verbs) |
| 3a/3b (C2) | D3-D7, D10, D11, D13, E5, F1, G1, H1; M4 before C2.6 |
| 4 (C2b) | D8, D9 |
| Not blocking | B7 (follow-up spec), D10 |
