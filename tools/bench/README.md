# tools/bench: the board-test runner (plan §6, B0–B5 and B4b)

Board 2 (Tab5, ESP32-P4), USB serial number `80:F1:B2:D1:51:A6`, on the Windows hotspot
192.168.137.0/24 (PC = 192.168.137.2). No motors exist: the MCB is `scripts/rtps_mcb_sim.py`.
Scripts decide every verdict (TS-PRI-02).

Run everything with the IDF venv python (the only one with pyserial and esptool):

```
set PY=C:\Espressif\tools\python\v6.0\venv\Scripts\python.exe
%PY% tools\bench\run_bench.py --build-dir C:\b\main_bench --label baseline-bench --flash
%PY% tools\bench\run_bench.py --build-dir <build> --label dry            # no flash: B1's flash is SKIP
%PY% tools\bench\run_bench.py --build-dir <build> --label x --steps B0,B2,B3
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

Verdicts: PASS, FAIL, INVALID (B0 preflight; B2's no-IP-after-join rule), SKIP (declared on the
command line), NOT_RUN (nothing to test against, e.g. no IP; or a runner crash: not a verdict).

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

## Single tools
```
%PY% tools\bench\board.py port | watch 20 | boot 90 --out f.log | pt --build-dir D | backup-storage
%PY% tools\bench\flash.py preflight|flash|save --build-dir D [--label L]   restore L   list
%PY% tools\bench\boot_check.py capture.log
%PY% tools\bench\compare_selftest.py --ip A [--out f.json]     or  --compare-only f.json
%PY% tools\bench\walk_check.py --ip A --out DIR
%PY% tools\bench\scenario_drive.py --ip A --out DIR
%PY% tools\bench\ui_models_check.py --ip A --out DIR
powershell -NoProfile -ExecutionPolicy Bypass -File tools\bench\hotspot.ps1 status|restart
```
`--tree` (default: this worktree) is where `scripts/` is run from: run the tree that built the
image. Children run with `PYTHONDONTWRITEBYTECODE=1`.

## Decisions to revisit
- **No "joystick self test passed" line exists** in the baseline boot. B2 uses the nearest
  boot-time evidence: `Adding joystick keypad input device...` and
  `selftest/I ready: 54 checks (selftest_spec.h)` (text equal to the baseline).
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
- **ROM banner**: the reset capture starts in the 2nd-stage bootloader (the ROM banner is lost
  while the USB port re-opens), so a second boot is detected by a second `Calling app_main()`.
- **RTPS sweep** seeds 64 addresses at a time (one listener for all 252 never reached the board).
- **Self-test bands** are in `compare_selftest.py` (`BANDS`, documented in its docstring).
  The baseline `selftest.json` was taken while `rtps_mcb_sim.py` was running (scratchpad
  `ready_rtps.py`), against the rule; its values passed their own limits.
