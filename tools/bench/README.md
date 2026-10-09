# tools/bench: the board-test runner (plan §6, B0–B5, B4b, B5a–B5e, and the hazard fixes' B5'' steps)

Board 2 (Tab5, ESP32-P4), USB serial number `80:F1:B2:D1:51:A6`, on the Windows hotspot
192.168.137.0/24 (PC = 192.168.137.2). No motors exist: the MCB is `scripts/rtps_mcb_sim.py`.
Scripts decide every verdict (TS-PRI-02).

The network is `BENCH_NET` (common.py). Unset or `hotspot`: as above, the board on WiFi.
`lan:<pc ip>/<prefix>` (e.g. `set BENCH_NET=lan:192.168.9.226/24`): the board on Ethernet in
the PC's LAN (its Connection setting, Settings > Internet, saved as Ethernet: `network 0`).
Then B0's tethering check is not applicable, there is no hotspot restart, the RTPS sweep
covers that subnet (at most a /22), the sim binds that PC address, and B2 expects
`Network: Ethernet` and `network 0` in place of the baseline's `Network: WiFi` and
`network 1` (BENCH-014); every other marker is unchanged.
`routed:<pc ip>@<board subnet>` (e.g. `routed:100.92.133.114@10.0.0.0/24`): the same, with the
board in a subnet the PC reaches through a router (a Tailscale subnet route): the sim binds the
PC's address on that route, the sweep covers the board's subnet, and round-trip times include
the route, so timing verdicts are read against the reference image run on the same route.

Run everything with the IDF venv python (the only one with pyserial and esptool):

```
set PY=C:\Espressif\tools\python\v6.0\venv\Scripts\python.exe
%PY% tools\bench\run_bench.py --build-dir C:\b\main_bench --label baseline-bench --flash
%PY% tools\bench\run_bench.py --build-dir <build> --label dry            # no flash: B1's flash is SKIP
%PY% tools\bench\run_bench.py --build-dir <build> --label x --steps B0,B2,B3
%PY% tools\bench\run_bench.py --build-dir <build> --label x --steps B0,B5,B5a,B5b    # no B2: --ip optional
```

Results: `C:\b\bench\results\<label>-<time>\summary.json` (one verdict per step, the board IP),
plus the boot capture, self-test JSON, walk PNGs and the sim logs beside it.

| Step | Script | PASS |
| --- | --- | --- |
| B0 preflight | `run_bench.py`, `flash.py preflight` | port by MAC, tethering On, PC IP local, no stray peer process, ≤1 RTPS responder, partition table == build's, bench config (with `--flash`) |
| B1 flash | `flash.py` | `/storage` backed up once; `write-flash @flash_args` exit 0 |
| B2 boot | `board.py` + `boot_check.py` | markers in baseline order with baseline values, nothing forbidden |
| B3 self test | `compare_selftest.py` | `rtps_selftest.py` exit 0, same IDs and verdicts, values in bands |
| B4 walk | `walk_check.py` | 13 names == baseline; static screens pixel-equal below y=60 |
| B4b PIN pad and seat grid | `ui_models_check.py` | wrong PIN 1111 shows the notice and stays; 1234 opens SettingsScreen (actuators page); Seat Functions cursor walk = hmi_models goldens (3 rows of 2, clamped, DOWN off the bottom -> burger key); navigation only, no seat command |
| B5 drive | `scenario_drive.py` | hold → Drive; `e` → Locked ≤3 s; XYTwist 0 while locked; refused hold never Drive |
| B5a exit hold | `scenario_hazards.py` | exit hold on Drive → Locked ≤3 s, the sim applied a DISABLE, XYTwist 0 |
| B5b burger-key exit | `scenario_hazards.py` | burger key on Drive → Locked ≤3 s (menu over it), DISABLE applied, XYTwist 0 |
| B5c relock on link loss | `scenario_hazards.py` | sim `p` → Locked ≤ MIB_STATUS_TIMEOUT + 2 s, XYTwist 0 while down; records the DriveCommands while down and the screens after `r` with the sim still ENABLED |
| B5d profile click after relock | `scenario_hazards.py` | as B5c, then `x` and `r`: if Drive comes back, a profile tap reaches the sim as a DriveCommand (its request is recorded) |
| B5e ignored/dropped DISABLE | `scenario_hazards.py` | RECORD: with `ign 50` and with `drop 50`, the DISABLEs and screens 7 s after an exit hold, then the burger key; graded only set-up and clean-up |

Verdicts: PASS, FAIL, INVALID (B0 preflight; B2's no-IP-after-join rule), SKIP (declared on the
command line), NOT_RUN (nothing to test against, e.g. no IP; or a runner crash: not a verdict),
RECORD (a characterisation, B5e: today's behaviour as data; passes the run).
Last-good (with `--flash`) is saved when every step is PASS, or RECORD with its graded checks
(set-up, clean-up) all passed and no problems; a RECORD without clean graded checks, or any
SKIP, NOT_RUN, FAIL or INVALID, saves nothing (`run_bench.last_good_decision`, BENCH-010..012;
orchestrator decision 2026-10-06, option B). `summary.json` → `last_good` says why.

## B5a..B5e: the sim's fault modes
In the default step list, after B5, since their first board runs (2026-10-06, final-a9a040f:
B5a–B5d PASS, B5e RECORD; the H1, H5 and H6 records as expected). `--sim-mode-steps` is
still accepted and does nothing. Each step starts its own sim
(`sim_child.py` + `--event-log <step>/sim-events.jsonl`), so it begins IDLE, and ends with a
graded clean-up (every mode off, `ok`, LockedScreen). Remote-UI verbs only (SCREEN, TAP, BTN,
SHOT): no stick is moved and XYTwist is only observed. The sim's JSONL log is the evidence;
before each UI action the step writes a `mark` into it and grades only what follows. Every
`records` entry is today's behaviour, hazards included (H1, H5, H6), so the hazard fixes
(docs/plans/hazard-fixes.md §4 C1) show up as a diff, not as a FAIL. The full step list is in
`scenario_hazards.py`'s docstring. Not covered here: the sim's `ongone` policy (an HMI reset
needs a serial reset; it belongs with C3's boot steps).

## Hazard fixes C1, C3, C4, C2: B5''-1..22, B5f..B5j, C2-1..16
`hazard_steps.py` (scenarios in `scenario_c1.py`, `scenario_c3.py`, `scenario_c4.py`,
`scenario_c2.py`; rig in `hazard_rig.py`; grading helpers in `hazard_grade.py`), from
docs/plans/hazard-c1-spec.md §6, hazard-c3-spec.md §7, hazard-c4-spec.md §8 and
hazard-c2-spec.md §9. Each step's pass criteria are in its scenario file's table.
- **Not in the default list.** They need a stick-injection image with the fixes' bench verbs
  (STATE, PERMIT, CAL UNSAVED, POST RERUN, CRASH, STALL: components/remote_ui/
  include/bench_verbs.hpp). `run_bench.py ... --hazard c1,c3` names the fixes the image has:
  their steps run after B5e, retired ones left out (with c3, B5''-15 gives way to B5''-18b,
  C3 F5; with c2, B5''-12 goes, C2 E9), every step's start reads STATE post = PASS instead of
  sending PERMIT POST pass (C3 F6), and B2 requires C3's POST markers. `--steps` can name
  them too (`B5pp-6b` for `B5''-6b`). `hazard_steps.py --list` lists them.
- **Stick injection only after the sole-sim proof** (hazard-fixes.md §3 B1): before the first
  hazard step, no stray RTPS peer process on this PC and an RTPS sweep of the bench subnet
  finds no responder but the board; else every hazard step is INVALID and nothing is
  injected. After the run's sim starts, no other peer process may appear.
- **One remote-UI connection** carries the stick (STICK every 100 ms; the board lets go 300 ms
  after the last) and the step's commands, one at a time; taps are PRESS/RELEASE so nothing
  holds the connection. A refresh gap over 0.28 s in a step (other than the STALL steps) makes
  it NOT_RUN: its "no motion" checks could pass on the real stick at rest.
- **Evidence:** the sim's event log (DriveCommands, SeatCommands, every MibStatus publish,
  marks), every XYTwist sample with its time (`sim_child.py jsave`), STATE polled every
  100 ms, and the serial log for B5''-16, -18b, -21, B5i, B5j (the port from B0, or
  `--port`). One clock: time.monotonic(), system-wide on Windows (15.6 ms resolution on the
  venv's Python 3.11). "Within X" runs from the mark before the command to the first STATE
  poll that shows the result (C3 §7).
- **Verdicts:** PASS / FAIL on the graded checks; NOT_RUN when the image lacks a verb or a
  STATE field the step needs (the hook another change wires has not landed: "not wired"), or
  the rig could not do its part. Banners are STATE's `banner` (owner, 2026-10-08). B5''-18b
  times DriveScreen from the firmware's serial screen-change line; with only a STATE poll it
  is never PASS (NOT_RUN, "partial: remote UI up after the window").
- **Clean-up:** every step ends Locked with the stick centred (graded); a step that latches
  POST FAIL, or a C2 step that latches a stick FAULT, restarts the HMI (Restart HMI tile, a
  clean reset). **The bench never runs a calibration that writes flash** (the owner's
  standing rule): C2-13, whose way out is a completed run, is NOT_RUN.
- Their graders are tested without a board on hand-written traces built from the specs
  (`hazard_selftest.py`, BENCH-015..046 in `run_bench.py selftest`).

## Rules the code enforces
- Never erases. `common.esptool()` refuses any erase option. The only write is
  `esptool --chip esp32p4 -p <port> -b 460800 --before default-reset --after hard-reset write-flash @flash_args`.
- Never flashes when the board's table (`read-flash 0x8000 0xc00`) differs from the build's
  `partition_table/partition-table.bin` (compared in B0 and again right before each write).
- Only bench builds: `config/sdkconfig.h` must have `#define CONFIG_HMI_REMOTE_UI 1`.
- `/storage` (0xC20000, 0x3E0000) is read to `C:\b\bench\storage-<time>.bin` once; an existing
  backup is never replaced.
- Opening the port: DTR and RTS False before `open()` (else the board resets). The deliberate
  reset is RTS high 200 ms with DTR low (esptool's USB hard reset).
- The self test never runs beside a sim (both publish MibStatus): a process scan refuses it.
- One hotspot restart per run (`hotspot.ps1 restart`), only for "WiFi joined" without "Got IP"
  for 60 s.
- The board lease (`C:\Users\halai\Offline_Documents\ATDev\rammp\.board-lease`, JSON
  `{owner, since, pid}`) is held for the whole run; `lease.py status|acquire|release`. A lease
  whose pid is dead is taken over and the takeover is logged.
- Every esptool call ends with `--after hard-reset`, so B0's table read and B1's backup and
  flash reboot the board. When B2 does not boot it before the next app step (B3 onwards), the
  runner waits first: a serial capture without a reset until `Got IP` (90 s, saved as
  `boot-after-b0.log` / `-b1.log`; that IP replaces `--ip`, or supplies it, so `--ip` is
  optional when B0 or B1 runs first), then the remote UI's PING (30 s). Back = either; otherwise the later steps are NOT_RUN with the reason. Each wait
  is in `summary.json` → `board_back`. Seen 2026-10-06: `--steps B0,B5,...` started B5's sim
  while the board was booting ("Could not find the board").
- Remote UI from the steps (B4b, B5, B5a..e) goes through `ui_client.py`: a 5 s timeout per
  command (SHOT 20 s), on a timeout a reconnect and ONE retry for idempotent verbs (SCREEN,
  BTN, KEY, SHOT, FOCUS, PING, ...); TAP and SWIPE are not retried (a lost reply may hide a
  tap that happened, and a second burger-key tap undoes it): they raise after the reconnect,
  so the step is NOT_RUN, not graded on a guess. A reconnect releases what was held (the
  board lets go when a client goes). Every command is a line in `<step>/remote-ui.jsonl`
  (sent, answered, duration, gap since the previous answer, attempt, reply or error) and the
  step's `remote_ui` sums it up (slow commands over 1 s, retries, the largest gap). A long
  gap with fast round trips is time spent on the PC, not on the wire. B4's walk runs
  `hmi_ui.py walk` as a child and keeps hmi_ui's own client (20 s socket timeout, no retry).

## Single tools
```
%PY% tools\bench\board.py port | watch 20 | boot 90 --out f.log | pt --build-dir D | backup-storage
%PY% tools\bench\flash.py preflight|flash|save --build-dir D [--label L]   restore L   list
%PY% tools\bench\boot_check.py capture.log
%PY% tools\bench\compare_selftest.py --ip A [--out f.json]     or  --compare-only f.json
%PY% tools\bench\walk_check.py --ip A --out DIR
%PY% tools\bench\scenario_drive.py --ip A --out DIR
%PY% tools\bench\scenario_hazards.py --ip A --out DIR [--steps B5a,B5e]
%PY% tools\bench\ui_models_check.py --ip A --out DIR
powershell -NoProfile -ExecutionPolicy Bypass -File tools\bench\hotspot.ps1 status|restart
python tools\bench\ui_client.py selftest     # UI-001.., a localhost fake remote UI (in L0)
python tools\bench\run_bench.py selftest     # BENCH-001.., step sequencing on a fake board (in L0)
%PY% tools\bench\hazard_steps.py --ip A --hazard c1,c3 [--steps B5pp-6] [--port COM7]
%PY% tools\bench\hazard_steps.py --list
```
`--tree` (default: this worktree) is where `scripts/` is run from: run the tree that built the
image. Children run with `PYTHONDONTWRITEBYTECODE=1`.

## Decisions to revisit
- **The sim does not follow an HMI reboot** (found 2026-10-09): after the HMI restarts, the
  sim's RTPS discovery never re-matches the new participant, so it hears no XYTwist or
  DriveCommand again. Until rtps_host and the sim rediscover a restarted peer (the proper
  fix, a follow-up), the hazard rig replaces the sim after each HMI reboot with a fresh one
  that carries the old one's modes and MCB state (ENABLED included), sent before the new sim
  finds the HMI; both sims' samples are merged. Each affected step's verdict detail says
  "sim restarted after HMI reboot (RTPS rediscovery not supported by the sim)". It is an
  approximation of "the MCB stayed ENABLED across the HMI's reset": the MCB's participant
  is new to the HMI.
- **No "joystick self test passed" line exists** in the baseline boot. B2 uses the nearest
  boot-time evidence: `Adding joystick keypad input device...` and
  `selftest/I ready: <n> checks (selftest_spec.h)`, <n> read from the tree's
  `main/selftest_spec.hpp` (54 today; 57 with C4, 60 with C2: the counts declared in
  `boot-expected.json`, anything else is a problem). With the idle-task watchdog checks off in
  the tree's `sdkconfig.defaults` (C4), no `task_wdt` line may appear at all.
- **Marker order** is the baseline log's (settings → joystick → selftest ready → joy_cal →
  Network: WiFi → Got IP), not the order the plan lists them in.
- **Task watchdog at ~8 s** (IDLE0 starved, `main` on CPU 0) is in the baseline. The same trip is
  a known finding; a trip with other tasks, or more trips, is a FAIL.
- **Walk names**: the baseline PNGs were saved without names. The first walk records them in
  `C:\b\bench\baseline\walk-names.json` (names not graded that run); later runs compare.
- **B5 banners** (DRIVE_STOPPED, NOT_GRANTED) are not graded: the remote UI reports only the
  screen name. Screenshots are saved. After `e`, the refusal check first sends `ok` (ERROR
  would refuse anyway), then `x`.
- **XYTwist** is read through `sim_child.py`, which runs the unchanged sim and wraps its XYTwist
  decoder to keep a copy of each sample (commands `jstart`, `jstop`, `jcount`).
- **Settings screens scroll**: their row names are marquees (x 18..147, y 399..841), so B4
  masks x<150, 340≤y<1010 on those two screens (four shots 0.7 s apart differed only there).
- **Low-water marks** (`mem.int_min`, `mem.dma_min`, `mem.stk_*`) depend on what ran since boot;
  the baseline was taken at 500 s uptime, B3 runs ~90 s after reset. Their band is one-sided:
  ≥80 % of the baseline (`floor80`).
- **Reset pulse fixed 2026-10-07**: until then the RTS pulse never reached the chip, because
  Windows' usbser.sys only sends the line state when DTR is written. B2 was capturing the boot
  started by the esptool hard reset just before it (B0's table read, B1's flash), which is why
  every B2 so far followed an esptool call. `board.capture` now rewrites DTR after each RTS
  change, as esptool does, and the capture starts at the ROM banner
  (`rst:0x17 (CHIP_USB_UART_RESET)`).
- **ROM banner**: the reset capture starts in the 2nd-stage bootloader (the ROM banner is lost
  while the USB port re-opens), so a second boot is detected by a second `Calling app_main()`.
- **RTPS sweep** seeds 64 addresses at a time (one listener for all 252 never reached the board).
- **Self-test bands** are in `compare_selftest.py` (`BANDS`, documented in its docstring).
  The baseline `selftest.json` was taken while `rtps_mcb_sim.py` was running (scratchpad
  `ready_rtps.py`), against the rule; its values passed their own limits.

## Re-baseline 2026-10-06: Night theme (owner-approved, TS-DET-05)
The owner set the Night theme (settings `theme 0`) on board 2 and it stays.
- B2: `tests/characterisation/baseline-e2047a4/boot-expected.json` overrides the expected
  `settings_loaded` value (theme 1 → theme 0) and keeps the old value beside it.
  `boot-board2.log` stays the unedited capture.
- B4: the six static screens were captured fresh on final-a9a040f in Night (two walks, equal
  outside the masks; the Settings marquee masks still cover the only differences). The Day
  originals are kept as `walk/<stem>-day.png`. The side-by-side sheets (Day left, Night right)
  are in `C:\b\bench\baseline\night-vs-day\`. The screen names are unchanged and are now
  committed as `walk/names.json`.
- The board's IP is DHCP-assigned (.180 overnight, .218 since 11:31). B2 takes it from the boot
  log; `run_bench.py` refuses `--ip` when B2 is in `--steps`.
