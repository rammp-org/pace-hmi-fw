# Project profile: pace-hmi-fw

The project's facts that are not in code. Wiring will live in
`components/topology/include/topology.hpp` (not yet written; draft in `docs/plans/refactor.md`);
link to it, don't copy it. Filled 2026-10-06 from facts verified that day; "unknown" means not
yet measured or decided, and "none" means checked and absent.

## Identity and toolchain
- Project: `rammp-hmi-p4` (repo rammp-org/pace-hmi-fw), the joystick HMI of a powered wheelchair.
  It talks RTPS to the MIB/MCB.
- Namespace: none yet for application code (CS-NAM-02 gap). The shared RTPS spec uses `rammp::`.
- Target: ESP32-P4, chip rev v1.3 (board 2 boot log), on the M5Stack Tab5. Wi-Fi comes from the
  Tab5's ESP32-C6 over SDIO (esp_hosted 2.12; the C6 reports esp_hosted 1.4.1). Ethernet is an
  optional W5500 on SPI.
- Boards (told apart by the USB serial number = MAC):

  | Board | MAC | Features | Notes |
  | --- | --- | --- | --- |
  | 1 | 30:ED:A0:EA:BB:F5 | none (no joystick, no haptic answering) | |
  | 2 | 80:F1:B2:D1:51:A6 | `board:joystick`, `board:drv2605` | DA7280 not fitted. Verified 2026-10-06 on COM9 |
  | 3 | 80:F1:B2:D1:42:DB | `board:joystick` | |

- ESP-IDF v6.0 (`C:\esp\v6.0\esp-idf`; CI `IDF_VERSION: v6.0`) · espp 1.3.2, every `espp/*` pinned
  `==1.3.2` in `main/idf_component.yml` · LVGL 9.5.0 · clang-format 14.0.6 (not installed locally;
  pre-commit fetches it) · esp-clang 20.1.1 (from ESP-IDF 6.0) · cppcheck from
  `esp-cpp/StaticAnalysis@master` in CI (version unknown; not installed locally).
- C++ standard: IDF 6.0 builds `-std=gnu++26`; that is what `main` and every third-party component
  get. First-party components get `-std=gnu++23` and the CS-LNG-02 warning set as errors
  from `fw_component_options()` (`cmake/fw_standards.cmake`), placed after IDF's flags so they
  win. That has applied only since `dev_ai_refactor_fwopts`. Before it, the function was
  defined after `project()`, where IDF has already run every component's `CMakeLists.txt`, so
  the call was skipped and every first-party component built with IDF's defaults.
  `CMAKE_CXX_STANDARD` has no effect on IDF component targets.
- Build variants (CS-LAY-09):

  | Variant | How | Differences |
  | --- | --- | --- |
  | default / release | `sdkconfig.defaults` | Ethernet is the default network setting; remote UI, DA7280 boot test and FPS report off |
  | bench (CI) | `-D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;ci/sdkconfig.bench"` | `CONFIG_HMI_REMOTE_UI=y`, `CONFIG_HMI_BENCH_DA7280_TEST=y` (the DA7280 tests run at boot) |
  | bench test (board) | `-D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.wifi.local"` (untracked) | `CONFIG_HMI_REMOTE_UI=y`, Wi-Fi SSID and password; DA7280 boot test off unless that file sets it |
  | debug FPS | a local sdkconfig with `CONFIG_HMI_DEBUG_FPS=y` (and optionally `CONFIG_HMI_DEBUG_FPS_STRESS=y`) | per-second `[FPS]` report at debug level (tag `fps`); DriveScreen loaded at boot; stress invalidates the whole screen every LVGL cycle |

  CI (`.github/workflows/build.yml`, `l0.yml`) builds the default and the bench (CI) variants;
  `l0.yml` fails if `sdkconfig.defaults` turns on any of the debug or bench options above. The
  debug FPS variant is not built in CI: its code is in `if constexpr` arms, compiled in every
  build.
- Branch protection on `main` and `dev`: not recorded (CS-GIT-05). Until then approvals are by
  review only.

## Components
| Component | Concern (one sentence) | Safety-relevant | Builds for linux | D4 diagram |
| --- | --- | --- | --- | --- |
| `main` | everything not listed below; `main.cpp` #includes 27 `frag_*.inc` (one TU, `tools/split_main.py`) | yes | no | none |
| `components/feedback` | plays the user's haptic and sound cues: the DRV2605 motor, the click and refusal sounds, the bench-only DA7280 test | no (cues only; what is shown about a fault is decided by its callers, and a cue failing changes nothing else) | no (board drivers; bench B3 hap.*) | README |
| `components/fw_core` | the channel helpers, ThreadChecker, `Owned<T>`, `check()`, context tokens (not used by the firmware yet) | used by safety | host L1 (FWC-L1) | none |
| `components/hmi_format` | pure screen-text formatting (speed, steppers, seat, clock, diagnostics) | no | host L1 (L1-FMT) | README |
| `components/hmi_models` | pure UI models: the button-grid cursor walk and the bench PIN entry | no | host L1 (L1-MOD) | README |
| `components/hmi_ui` | the UI island: every LVGL view the UI task draws (so far the TopBar clock, link and RTPS label, the DriveBand status cells, the backlight) | no (shows the chair's state and faults, commands nothing) | no (LVGL-bound; bench B4) | README |
| `components/joystick` | espp joystick plus a twist axis (vendored espp 1.2.0, sha 615b8df; README + upstream.diff) | yes | host L1 (L1-JOY) | none |
| `components/post` | the quick POST evaluator: boot facts in, a verdict per check and an overall state out (not wired yet; hazard-fixes C3) | yes (gates motion once C3 wires it) | host L1 (L1-POST) | README |
| `components/m5stack-tab5` | vendored espp Tab5 BSP 1.2.0 (sha 615b8df), modified (VENDORED.md + upstream.diff) | no | no | none |
| `components/ui` | SquareLine export, generated | no | n/a | none |
| `rammp_rtps_messages` (submodule `external/rammp-rtps`) | shared RTPS message and topic spec | yes (wire format of motion commands) | header-only | none |

## Tasks and islands
- Topology: a draft exists on `dev_ai_refactor_topology` (not merged, for review). Today's tasks are
  inventoried in `docs/plans/refactor.md`.
- Free stack at the end of a self-test run, board 2, 2026-10-06 (`rtps_selftest.py`; the self test
  reports free bytes, not a stress-test high-water mark): LVGL 11060 B, ADC 1496 B, RTPS 6112 B.
  The stress test (TS-TGT-03) has not been run, so CS-MEM-04 margins are unknown.
- Watchdog: the task watchdog fired on `main` (CPU 0) at about 8 s into boot (board 2,
  2026-10-06, firmware b13b103). Which safety tasks the TWDT covers is unknown (CS-SAF-06).
- Islands, context types, cycle periods: none yet.
- Components allowed to hold locks (CS-OWN-08): `fw_core` (the channel helpers; the ratchet's
  RULE_PLACEMENTS). Today `main` uses a global
  recursive `lvgl_mutex` (47 mentions in 10 files).
- Ownership checks in release builds: not implemented.
- Fan-out cap for scheduled agent runs: 8 (set by the user for the 2026-10-06 overnight run).

## Budgets and interfaces
- Flash: two 6 MiB OTA slots (`partitions.csv`). App binary at e2047a4: 4,398,912 B (measured from
  `build/rammp-hmi-p4.bin`), about 70% of a slot.
- RAM: PSRAM free at the end of a self test is 28,146 KB (board 2). Internal and DMA budgets:
  unknown; POST does not exist yet.
- External interfaces (CS-CFG-03; none is versioned yet unless stated):

  | Interface | Where documented |
  | --- | --- |
  | RTPS topics and messages | `external/rammp-rtps`, `main/hmi_rtps_spec.hpp` |
  | Remote UI debug channel, TCP 3333 | `main/remote_ui.hpp`, `scripts/hmi_ui.py` |
  | Self-test report lines and the `SelfTestReport` message | `main/selftest_spec.hpp`, `scripts/rammp_rtps.py` |
  | `/storage/joystick_cal.txt` | `main/joystick_cal.cpp` (`version 1`) |
  | `/storage/settings.txt`, `fwinfo.txt`, `wifi.txt` | their `.cpp` files (unversioned) |
  | SquareLine widget names (UI contract) | `scripts/ui_contract.py` |

- Non-espp dependencies: LVGL 9.5.0, espressif/w5500, esp_wifi_remote, esp_hosted ~2.12, cjson,
  esp-dsp, littlefs (via espp file_system). The espp alternatives considered: not recorded.
- Vendored copies in `components/`: `m5stack-tab5`, `joystick`. Neither has a README listing its
  upstream version and changes (CS-LAY-05).

## Bench
- PC: Windows 11 laptop. ESP-IDF via `C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1`;
  pyserial only in `C:\Espressif\tools\python\v6.0\venv\Scripts\python.exe`.
- Serial: the board's USB CDC port (board 2 was COM9). Opening it resets the board unless `dtr` and
  `rts` are set False before `open()`.
- Network: the Windows Mobile Hotspot on 192.168.137.0/24, PC = 192.168.137.2. VMware VMnet8 also
  holds 192.168.137.1, so RTPS scripts take `--bind-address 192.168.137.2`. Board 2 got
  192.168.137.180 on 2026-10-06. The hotspot's peerless timeout is off.
- Power switching: none.
- Nightly window and board lease file: not set up yet.
- Reset to known state: no regions are erased by any runner. Flashing a build with a different
  partition table reformats `/storage` and loses the joystick calibration. The preflight compares
  `read_flash 0x8000 0xc00` with the build's table.
- Environment traps and their preflight checks (TS-DET-06):

  | Trap | Preflight |
  | --- | --- |
  | Port open resets the board | open with dtr/rts False |
  | Partition table differs → storage lost | compare the 0x8000 table before flashing |
  | Ethernet is the default network; with no W5500 link the board is unreachable | boot log says `Network: WiFi`; the stored network setting must be 1 |
  | Hotspot joins but gives no DHCP lease ("LINK_DOWN -> NO_IP" > 60 s) | restart tethering (WinRT NetworkOperatorTetheringManager) |
  | Wi-Fi join takes several tries (4 failures, about 27 s, seen 2026-10-06) | wait for `Got IP` in the boot log, with a timeout |
  | VMnet8 on the same subnet | `--bind-address 192.168.137.2` |
  | Component manager dies silently on long paths in a new worktree | copy `managed_components/` from the main checkout |
  | `rtps_selftest.py` and `rtps_mcb_sim.py` both publish MibStatus | never run them together (two publishers fail `rtps.mcb_period` and `rtps.mcb_loss`) |

- Board runner: `tools/bench/run_bench.py` (B0-B5; lease file `C:/Users/halai/Offline_Documents/ATDev/rammp/.board-lease`;
  last-good images in `C:/b/bench/good/`; results in `C:/b/bench/results/`). Flakiness seen on
  2026-10-06: `time.render_max` outside 20 % in 1 of 6 runs on m3 (band widened to 30 %, owner P4).
- Power: board 2 has NO battery; it is powered over PoE (owner, 2026-10-06). `pwr.vbat` therefore
  does not read a battery: the ~8.4 V it reports, the occasional ~4.4 V reads and the top bar's
  "78 %" are not battery state. Ignored for now (owner, P3): B3 (`compare_selftest.py`) shows the
  pwr.* checks (value, verdict, baseline) but does not grade them until a battery is fitted.
- Peer simulators: `scripts/rtps_mcb_sim.py --peer <ip> --bind-address 192.168.137.2`, with stdin
  commands `e`, `ok`, `x`, `s`, `p`/`r` (pause MibStatus), and the bench fault modes `ign N` / `drop N`
  (ignore or drop the next N DISABLEs), `ongone keep|idle` (what the MIB does when the HMI goes
  away), `mark <label>`; `--event-log PATH` writes a JSONL log of what it received and sent. Its
  decision logic is `scripts/mcb_sim_logic.py` (`rtps_mcb_sim.py selftest`, cases SIM-001.., in
  L0). `scripts/rtps_selftest.py` acts as the MCB during a self-test run.
- Debug channel: `scripts/hmi_ui.py` on TCP 3333 (screenshots, taps, keys, walk). Test builds
  enable it with `CONFIG_HMI_REMOTE_UI=y`.

## Test parameters (defaults in brackets)
- POST time budget: none (no boot POST exists) · nightly boot count [20] · extended self-test runs [5]
- Statistical tests: Wi-Fi link up is judged over at least 5 boots (bench experience).
- Stress test duration [10 min] · Soak: not set.
- Release smoke list and manual checklist: none yet.

## Deviations
| Rule ID | Location | Reason | Owner | Review date |
| --- | --- | --- | --- | --- |
| TS-UNIT-01, CS-HAL-04 | `tests/` (all L1 apps) | L1 runs as host-native g++ 13 in WSL with IDF's Unity sources, not the IDF `linux` target (not installed; no sudo in WSL) | owner | approved 2026-10-06 (Q6) |
| CS-LAY (layout) | `main/frag_*.inc` | one-TU fragments of `main.cpp`, so the split cannot change static-init order, linkage or inlining; they dissolve into components grouped by concern (CS-LAY-02; app-main-shrink.md V15), not one component per fragment | owner | temporary |
| AI-UNA-02 "never merge" | `dev_refactor` | the owner authorised merges into `dev_refactor` for the 2026-10-06 run (AI-DIS-01) | owner | per run |
| CS-SAF-05 (two approvals) | safety-relevant changes | one human approver (the owner) until a second reviewer exists; nothing safety-relevant merges to `dev` meanwhile | owner | until a second reviewer is named |
| CS-SAF-03 (open circuit) | joystick low rail | firmware cannot tell an open pot (0 mV) from full travel (calibrated min 6-11 mV on board 2); firmware-only for now, residual hazard documented in `docs/plans/hazard-fixes.md` | owner | revisit with an EE change |
| CS-LNG-02 | `main` | `main` keeps IDF's gnu++26 and default warnings until its legacy counts are in the ratchet; new components use `fw_component_options` | owner | open |
