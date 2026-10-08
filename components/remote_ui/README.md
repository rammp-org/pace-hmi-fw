# remote_ui

The bench debug channel: a TCP server on port 3333 (`kRemoteUiPort`) that lets a PC take
screenshots of the panel, tap and swipe it, press keys and the stick button, switch the theme,
ask for the screen, the focus and the running tasks, and (with `CONFIG_HMI_BENCH_STICK_INJECT`)
drive the stick through the injection mailbox. The PC end is `scripts/hmi_ui.py` and
`tools/bench/ui_client.py`.

It exists only in bench builds: with `CONFIG_HMI_REMOTE_UI` off (the release default, checked
in L0) the file compiles to a no-op `remote_ui_start` and nothing else. The code moved here from
`main/remote_ui.*` and `main/stick_inject.hpp` unchanged (app-main-shrink V15, CS-LAY-01), after
the casts of its step-(a) commit.

## Requirements

| ID | Requirement | Verified by |
| --- | --- | --- |
| REQ-RUI-01 | In a release build (`CONFIG_HMI_REMOTE_UI` off) the component adds only a no-op `remote_ui_start` to the image, and no stick-injection symbol. | the ELF symbol check in `build.yml`; the remote_ui symbol set of the default ELF equal before and after the move |
| REQ-RUI-02 | In a bench build it serves one client at a time on TCP 3333; input goes into the same latches the stick and the GPIO48 button use, so a script exercises the real handling. When a client goes, a held touch, key or button is released. | bench B0-B5 (the runner drives the board through it) |
| REQ-RUI-03 | `TASKS` answers one JSON line with every task's name, priority, core, stack size and high-water mark, copied under the kernel lock (`tools/guards/README.md`, G10). | bench (task_dump.py) |
| REQ-RUI-04 | Stick injection (`CONFIG_HMI_BENCH_STICK_INJECT`, needs the remote UI: a static_assert stops a build without it) reaches the stick only through `StickInjectMailbox`, whose write end `remote_ui_attach_stick_inject` hands over before the server starts. | bench B5 (simulated MCB) |

## Interface

| Header | What |
| --- | --- |
| `remote_ui.hpp` | `RemoteUiConfig`, `remote_ui_start`, `kRemoteUiPort` |
| `stick_inject.hpp` | `BENCH_STICK_INJECT`, the injection mailbox types and the slot main builds, `remote_ui_attach_stick_inject` |

The headers keep their names: `main.cpp` and `frag_stick_config.inc` only gained the
component dependency.

## Tasks and dependencies

- Tasks (bench builds only): the server thread; it calls `lv_*` under main's `lvgl_mutex` (a
  CS-UI-02 finding, kept as it was: the remote UI is a bench tool). Its legacy debt (locks,
  globals, sleeps, `ESP_LOG*`, the `#if CONFIG_HMI_REMOTE_UI`) moved with `ratchet.py transfer`;
  its two files keep the UI-path status `main/remote_ui.*` had (reviewed ratchet change).
- Dependencies: public `fw_core`, `stick` (the injection message types); private `lvgl`, `ui`,
  `lwip`, `freertos`, `esp_timer`, `log`, `pthread`.
- Not safety-relevant in release builds (absent). In bench builds it can move the stick: only on
  the simulated bench (CORE never-list: no real motion).
