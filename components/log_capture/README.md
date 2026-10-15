# log_capture

Keeps what the firmware prints, for the LogScreen: `log_capture_start()` points stdout and stderr
at a stream that passes every byte on to the serial console unchanged and also keeps the last
500 lines in a ring in PSRAM. The code moved here from `main/log_capture.*` unchanged
(app-main-shrink V15, CS-LAY-01); the topology names the component (`COMPONENTS`: `log_capture`
on the UI task).

## Interface

| Header | What |
| --- | --- |
| `log_capture.hpp` | `log_capture_start`, `log_capture_active`, `log_capture_count`, `log_capture_visit`; `LogLevel`, `kLogCaptureLines`, `kLogCaptureLineLen` |

The header keeps its name and the functions their names, so the callers (`main.cpp`,
`log_view.cpp`, `selftest.cpp`) only gained the component dependency.

## Tasks and dependencies

- Tasks: none of its own. `log_capture_start` runs once, first in `app_main`. The capture path
  runs on whichever task prints, under picolibc's stream lock; the LogScreen (UI task) and the
  self test read the ring under the ring's lock.
- Dependencies: ESP-IDF's heap (the ring's PSRAM block) and picolibc's stdio. Nothing first-party.
- Not safety-relevant. Board-facing (stdout, PSRAM): no host L1 app; the self test's
  `log.capture` check covers it on the bench.
- Legacy debt moved with it under `tools/l0/ratchet.py transfer` grants: the ring's mutex and
  its namespace-scope state, and the ring's `heap_caps_calloc`.
