# Architecture: pace-hmi-fw

Generated sections are rewritten by `tools/gen_diagrams` and checked by its `check` mode in L0
(CS-ARC-02; the "Diagrams" step of `.github/workflows/l0.yml`).
Do not edit between the markers. Regenerate with `python tools/gen_diagrams/gen_diagrams.py write`;
see [tools/gen_diagrams/README.md](../tools/gen_diagrams/README.md).

## Today vs target

The firmware as it runs today, every task and every channel between them, is one diagram in
[fw-overview.md](fw-overview.md); all of its testing is one diagram in
[testing-overview.md](testing-overview.md). For a file-by-file walk through the code (boot,
tasks, the motion path, the drive state machine, where each part of the old `main.cpp` went),
read [how-the-firmware-works.md](how-the-firmware-works.md).

- **Today** (the release candidate `dev_ai_rc`, 2026-10-15): `main` wires and the work lives
  in `components/*` (no `frag_*.inc` fragments any more). `main.cpp` builds the UI, stick and
  housekeeping islands from their `Config`s; its wiring files fill the UI's ports (`ui_ports`,
  `cues`), the stick task's I/O (`stick_io`), the task configs (`task_configs`), the bench verbs'
  hooks (`bench_hooks`) and the self test's probes (`selftest_platform`). The network link and
  RTPS are `net_link`, the log ring `log_capture`, the POST's (power-on self test's) ESP-IDF facts `post_facts`; the
  extended self test and the calibration adapter stay in `main/` as approved deviations. No espp
  component is vendored: espp comes as it is, on the RC from a local espp worktree with the
  drafted espp PRs (TEST ONLY, see [plans/progress.md](plans/progress.md)). Tasks share state through atomics, `fw_core` channels (the
  output permit's `AtomicValue`s, the POST rest-window `Mailbox`, the bench stick injection's
  `Mailbox`), LVGL subjects and a global recursive `lvgl_mutex`. The quick POST (`post`) is
  wired; `topology` is built and tested but not wired, so **D2 below is the target, not
  today's tasks**: today's are in [fw-overview.md](fw-overview.md).
- **Target:** the islands, adapters and channels of
  [docs/plans/refactor.md §2.1–2.3](plans/refactor.md#2-target-design), wired by
  `components/topology/include/topology.hpp`. D2 below is generated from that file; D3 grows as
  components move out of `main`. The target sketches of D2 and D3 are in
  [§2.5](plans/refactor.md#25-diagrams).
- D4 (state machines inside a component): the drive session's is drawn by hand in
  [components/drive_session/TABLE.md §5](../components/drive_session/TABLE.md#5-d4-state-diagram-drawn-by-hand-from-2),
  the stick monitor's in [components/stick/README.md](../components/stick/README.md), the
  POST's overall state in [components/post/README.md](../components/post/README.md). The link
  and seat tables are still drafts in
  [§2.4](plans/refactor.md#24-safety-state-machines-transition-tables).

## Legend (CS-ARC-03)

| Element | Mermaid |
| --- | --- |
| island | `subgraph` titled `<Name> island · <task>` |
| adapter | stadium `A([name])` |
| component | rectangle `C[name]` |
| peer or external system | subroutine `P[[name]]` |
| storage | cylinder `S[(name)]` |
| hardware / safety-relevant / generated | `:::hw` / `:::safety` / `:::gen` |
| mailbox (state) | solid arrow, label `Mailbox#lt;T#gt; · rate` |
| queue (event) | thick arrow `==>`, label `Queue#lt;T#gt; · depth` |
| dependency (D3) | dotted arrow `-.->` |

Every diagram ends with these lines:
```
classDef hw fill:#d9dde3,stroke:#7a8590,color:#111
classDef safety stroke:#c0392b,stroke-width:3px
classDef gen stroke-dasharray:5 4
```

## D1 Context (hand-written)

The HMI (human-machine interface) is the joystick and touch screen of a powered wheelchair. It
sends motion and seat commands to the MIB/MCB (Meebot interface board / main control board, the
chair's motor controller) over RTPS (Real-Time Publish-Subscribe) and shows the chair's state. The network is either Wi-Fi
(the Tab5's ESP32-C6 over SDIO) or Ethernet (an optional W5500 on the M5-Bus SPI lines), one at a
time, chosen on the Internet Settings screen. The bench PC tools stand in for the MCB and drive the
screen in test builds.

```mermaid
flowchart LR
  user[[Wheelchair user]]
  joy["Joystick: X/Y/twist pots, stick button"]:::hw
  hmi["Tab5 HMI · ESP32-P4 · this firmware"]:::safety
  mib[[MIB / MCB]]
  c6["ESP32-C6 Wi-Fi · esp_hosted"]:::hw
  w5500["W5500 Ethernet · optional"]:::hw
  pc[[Bench PC: rtps_mcb_sim.py, rtps_selftest.py, hmi_ui.py]]
  gh[[GitHub releases: rammp-org/pace-hmi-fw]]
  st[(LittleFS /storage: settings, joystick_cal, wifi, fwinfo)]
  user -->|deflects, presses| joy
  joy -->|ADC 30 Hz, GPIO48| hmi
  user -->|touch, side button| hmi
  hmi -->|screen, haptics, sound| user
  hmi -->|"RTPS: XYTwist 30 Hz, DriveCommand, SeatCommand"| mib
  mib -->|"RTPS: MibStatus 2 Hz, Diagnostics"| hmi
  hmi <-->|SDIO| c6
  hmi <-->|SPI · optional| w5500
  pc <-->|"RTPS: MCB simulator or self-test peer"| hmi
  pc <-->|"TCP 3333 remote UI · bench builds only"| hmi
  pc -->|"USB serial: flash, boot log"| hmi
  gh -->|"HTTPS: release list, OTA image"| hmi
  hmi --- st
  classDef hw fill:#d9dde3,stroke:#7a8590,color:#111
  classDef safety stroke:#c0392b,stroke-width:3px
  classDef gen stroke-dasharray:5 4
```

Sources for D1: `components/net_link/src/rtps_comms.cpp` (Wi-Fi or W5500, one link), `components/hmi_rtps_spec/include/hmi_rtps_spec.hpp`
(`kMibStatusPeriod` 500 ms, bench topics), the stick task's 33 ms ADC period, `components/ota/src/github_ota.cpp`,
`components/remote_ui/src/remote_ui.cpp` (`CONFIG_HMI_REMOTE_UI`), `components/storage`, and the Bench section of
[project-profile.md](project-profile.md).

## D2 Islands
<!-- BEGIN GENERATED D2 (tools/gen_diagrams): do not edit -->
Islands (subgraphs), adapters (stadiums) and channels from `components/topology/include/topology.hpp`. Components inside an island are those its COMPONENTS rows assign to the island's task.

```mermaid
flowchart LR
  subgraph i_CONTROL["Control island · control"]
    c_drive_session["drive_session"]:::safety
    c_stick["stick"]:::safety
  end
  subgraph i_FW_HASH["Fw hash island · fw_hash"]
    c_fw_hash["fw_hash"]
  end
  subgraph i_HOUSEKEEPING["Housekeeping island · housekeeping"]
    c_housekeeping["housekeeping"]
  end
  subgraph i_NET["Net island · net"]
    c_net_link["net_link"]:::safety
  end
  subgraph i_OTA["Ota island · ota"]
    c_github_ota["github_ota"]
  end
  subgraph i_SELFTEST["Selftest island · selftest"]
    c_selftest["selftest"]:::safety
  end
  subgraph i_UI["UI island · ui"]
    c_hmi_ui["hmi_ui"]
    c_log_capture["log_capture"]
    c_settings["settings"]
  end
  subgraph i_WIFI_SCAN["Wifi scan island · wifi_scan"]
    c_wifi_scan["wifi_scan"]
  end
  a_LOG_HOOK(["log_hook"])
  a_REMOTE_UI(["remote_ui"])
  a_RTPS_RX(["rtps_rx"]):::safety
  a_SIDE_BUTTON(["side_button"])
  a_STICK_BUTTON(["Button"]):::safety
  a_TOUCH(["touch"])
  a_RTPS_RX -->|Mailbox#lt;BrightnessMsg#gt; · on change| i_UI
  a_STICK_BUTTON ==>|Queue#lt;ButtonEdgeMsg#gt; · BUTTON_DEPTH| i_UI
  a_RTPS_RX -->|Mailbox#lt;DiagMsg#gt; · DIAG_HZ| i_UI
  i_UI ==>|Queue#lt;DriveIntentMsg#gt; · DRIVE_INTENT_DEPTH| i_CONTROL
  i_CONTROL -->|Mailbox#lt;DriveViewMsg#gt; · CONTROL_HZ| i_UI
  a_RTPS_RX ==>|Queue#lt;HmiCommandMsg#gt; · HMI_COMMAND_DEPTH| i_UI
  i_NET -->|Mailbox#lt;LinkMsg#gt; · NET_TICK_HZ| i_CONTROL
  i_NET -->|Mailbox#lt;LinkMsg#gt; · NET_TICK_HZ| i_UI
  a_LOG_HOOK ==>|Queue#lt;LogChunkMsg#gt; · LOG_DEPTH| i_UI
  a_RTPS_RX -->|Mailbox#lt;McbStatusMsg#gt; · MIB_STATUS_HZ| i_CONTROL
  a_RTPS_RX -->|Mailbox#lt;McbStatusMsg#gt; · MIB_STATUS_HZ| i_UI
  i_CONTROL ==>|Queue#lt;NavKeyMsg#gt; · NAV_KEY_DEPTH| i_UI
  i_OTA ==>|Queue#lt;OtaEventMsg#gt; · OTA_DEPTH| i_UI
  i_SELFTEST -->|atomic PostVerdict| i_CONTROL
  i_UI ==>|Queue#lt;RemoteUiRepMsg#gt; · REMOTE_DEPTH| a_REMOTE_UI
  a_REMOTE_UI ==>|Queue#lt;RemoteUiReqMsg#gt; · REMOTE_DEPTH| i_UI
  i_UI ==>|Queue#lt;SeatRequestMsg#gt; · SEAT_REQUEST_DEPTH| i_CONTROL
  i_SELFTEST ==>|Queue#lt;SelfTestEventMsg#gt; · SELFTEST_DEPTH| i_UI
  i_UI ==>|Queue#lt;SelfTestReqMsg#gt; · SELFTEST_REQ_DEPTH| i_SELFTEST
  a_STICK_BUTTON -->|atomic bool| i_CONTROL
  i_UI -->|Mailbox#lt;StickSettingsMsg#gt; · on change| i_CONTROL
  i_CONTROL -->|Mailbox#lt;StickViewMsg#gt; · CONTROL_HZ| i_UI
  i_UI -->|Mailbox#lt;UiContextMsg#gt; · UI_FRAME_HZ| i_CONTROL
  a_SIDE_BUTTON ==>|Queue#lt;UiInputMsg#gt; · UI_INPUT_DEPTH| i_UI
  i_WIFI_SCAN ==>|Queue#lt;WifiEventMsg#gt; · WIFI_DEPTH| i_UI
  classDef hw fill:#d9dde3,stroke:#7a8590,color:#111
  classDef safety stroke:#c0392b,stroke-width:3px
  classDef gen stroke-dasharray:5 4
```
<!-- END GENERATED D2 -->

## D3 Components
<!-- BEGIN GENERATED D3 (tools/gen_diagrams): do not edit -->
First-party components (`main` and `components/*` except `joystick`, `m5stack-tab5`, `ui`) and their direct `REQUIRES`/`PRIV_REQUIRES`. Vendored, generated and submodule components appear only as targets. Third-party dependencies are folded into one node per source; the number is how many of its components are used directly.

```mermaid
flowchart LR
  c_alloc_guard["alloc_guard"]
  c_board["board"]
  c_control["control"]:::safety
  c_drive_adapter["drive_adapter"]:::safety
  c_drive_session["drive_session"]:::safety
  c_drive_ui["drive_ui"]:::safety
  c_feedback["feedback"]
  c_fw_core["fw_core"]
  c_hmi_format["hmi_format"]
  c_hmi_models["hmi_models"]
  c_hmi_rtps_spec["hmi_rtps_spec"]:::safety
  c_hmi_ui["hmi_ui"]
  c_housekeeping["housekeeping"]
  c_joystick_cal["joystick_cal"]:::safety
  c_log_capture["log_capture"]
  c_main["main"]:::safety
  c_net_link["net_link"]:::safety
  c_ota["ota"]
  c_ota_parse["ota_parse"]
  c_post["post"]:::safety
  c_post_facts["post_facts"]:::safety
  c_rammp_rtps_messages["rammp_rtps_messages (submodule)"]:::safety
  c_remote_ui["remote_ui"]
  c_seat["seat"]:::safety
  c_settings["settings"]
  c_stick["stick"]:::safety
  c_storage["storage"]
  c_ui["ui"]:::gen
  g_ESP_IDF["ESP-IDF · 56"]
  g_Espressif_registry["Espressif registry · 8"]
  g_joltwallet["joltwallet · 1"]
  g_LVGL["LVGL · 1"]
  c_board -.-> c_hmi_format
  c_board -.-> c_housekeeping
  c_board -.-> g_ESP_IDF
  c_control -.-> c_hmi_rtps_spec
  c_control -.-> c_stick
  c_control -.-> g_ESP_IDF
  c_drive_adapter -.-> c_drive_session
  c_drive_adapter -.-> c_stick
  c_drive_adapter -.-> g_ESP_IDF
  c_drive_ui -.-> c_drive_adapter
  c_drive_ui -.-> c_drive_session
  c_drive_ui -.-> c_rammp_rtps_messages
  c_drive_ui -.-> c_stick
  c_drive_ui -.-> c_ui
  c_drive_ui -.-> g_ESP_IDF
  c_drive_ui -.-> g_LVGL
  c_feedback -.-> g_ESP_IDF
  c_fw_core -.-> g_ESP_IDF
  c_hmi_rtps_spec -.-> c_rammp_rtps_messages
  c_hmi_ui -.-> c_control
  c_hmi_ui -.-> c_drive_adapter
  c_hmi_ui -.-> c_drive_session
  c_hmi_ui -.-> c_drive_ui
  c_hmi_ui -.-> c_hmi_format
  c_hmi_ui -.-> c_hmi_models
  c_hmi_ui -.-> c_hmi_rtps_spec
  c_hmi_ui -.-> c_ota_parse
  c_hmi_ui -.-> c_post
  c_hmi_ui -.-> c_rammp_rtps_messages
  c_hmi_ui -.-> c_seat
  c_hmi_ui -.-> c_settings
  c_hmi_ui -.-> c_stick
  c_hmi_ui -.-> c_ui
  c_hmi_ui -.-> g_ESP_IDF
  c_hmi_ui -.-> g_LVGL
  c_housekeeping -.-> c_hmi_format
  c_housekeeping -.-> c_rammp_rtps_messages
  c_housekeeping -.-> g_ESP_IDF
  c_joystick_cal -.-> c_stick
  c_joystick_cal -.-> g_ESP_IDF
  c_main -.-> c_alloc_guard
  c_main -.-> c_board
  c_main -.-> c_control
  c_main -.-> c_drive_adapter
  c_main -.-> c_drive_session
  c_main -.-> c_drive_ui
  c_main -.-> c_feedback
  c_main -.-> c_fw_core
  c_main -.-> c_hmi_format
  c_main -.-> c_hmi_models
  c_main -.-> c_hmi_rtps_spec
  c_main -.-> c_hmi_ui
  c_main -.-> c_housekeeping
  c_main -.-> c_joystick_cal
  c_main -.-> c_log_capture
  c_main -.-> c_net_link
  c_main -.-> c_ota
  c_main -.-> c_ota_parse
  c_main -.-> c_post_facts
  c_main -.-> c_rammp_rtps_messages
  c_main -.-> c_remote_ui
  c_main -.-> c_settings
  c_main -.-> c_stick
  c_main -.-> c_storage
  c_main -.-> c_ui
  c_main -.-> g_ESP_IDF
  c_main -.-> g_Espressif_registry
  c_main -.-> g_LVGL
  c_net_link -.-> c_hmi_rtps_spec
  c_net_link -.-> c_storage
  c_net_link -.-> g_ESP_IDF
  c_net_link -.-> g_Espressif_registry
  c_ota -.-> c_ota_parse
  c_ota -.-> c_storage
  c_ota -.-> g_ESP_IDF
  c_ota -.-> g_Espressif_registry
  c_ota_parse -.-> g_Espressif_registry
  c_post -.-> c_stick
  c_post_facts -.-> c_post
  c_post_facts -.-> g_ESP_IDF
  c_remote_ui -.-> c_fw_core
  c_remote_ui -.-> c_stick
  c_remote_ui -.-> c_ui
  c_remote_ui -.-> g_ESP_IDF
  c_remote_ui -.-> g_LVGL
  c_seat -.-> c_hmi_format
  c_seat -.-> c_hmi_rtps_spec
  c_seat -.-> c_stick
  c_settings -.-> c_storage
  c_settings -.-> g_ESP_IDF
  c_stick -.-> c_fw_core
  c_stick -.-> g_ESP_IDF
  c_storage -.-> g_ESP_IDF
  c_storage -.-> g_joltwallet
  classDef hw fill:#d9dde3,stroke:#7a8590,color:#111
  classDef safety stroke:#c0392b,stroke-width:3px
  classDef gen stroke-dasharray:5 4
```

<details><summary>Folded dependencies</summary>

- **ESP-IDF**: `adc`, `app_update`, `base_component`, `base_peripheral`, `bmi270`, `bootloader_support`, `button`, `cdr`, `cli`, `codec`, `display`, `display_drivers`, `drv2605`, `esp_app_format`, `esp_driver_gpio`, `esp_driver_ppa`, `esp_driver_spi`, `esp_eth`, `esp_event`, `esp_http_client`, `esp_mm`, `esp_netif`, `esp_partition`, `esp_timer`, `esp_wifi`, `file_system`, `filters`, `format`, `freertos`, `gt911`, `i2c`, `ina226`, `input_drivers`, `interrupt`, `joystick`, `led`, `log`, `logger`, `lwip`, `m5stack-tab5`, `math`, `mbedtls`, `pi4ioe5v`, `pthread`, `reflect_cpp`, `rtps`, `rx8130ce`, `sdcard`, `socket`, `spi`, `spi_flash`, `st7123touch`, `task`, `thread_pool`, `timer`, `touch`
- **Espressif registry**: `espressif__cjson`, `espressif__esp-dsp`, `espressif__esp_hosted`, `espressif__esp_sccb_intf`, `espressif__esp_wifi_remote`, `espressif__usb`, `espressif__usb_host_uvc`, `espressif__w5500`
- **joltwallet**: `joltwallet__littlefs`
- **LVGL**: `lvgl__lvgl`

</details>

Source: `docs/diagrams/project_components.json`, a path-free snapshot of an ESP-IDF build's `project_description.json`. Regenerate after a build with `python tools/gen_diagrams/gen_diagrams.py write --build-dir <idf build dir>`.
<!-- END GENERATED D3 -->

Snapshot taken from the RC's bench build of `ba2c7b3` (2026-10-15). On the RC every espp
component is an `override_path` to a folder outside the project, so the generator cannot tell
it from ESP-IDF and folds it into the **ESP-IDF** node. The espp ones in that list are `adc`,
`base_component`, `base_peripheral`, `bmi270`, `button`, `cdr`, `cli`, `codec`, `display`,
`display_drivers`, `drv2605`, `file_system`, `filters`, `format`, `gt911`, `i2c`, `ina226`,
`input_drivers`, `interrupt`, `joystick`, `led`, `logger`, `m5stack-tab5`, `math`, `pi4ioe5v`,
`reflect_cpp`, `rtps`, `rx8130ce`, `sdcard`, `socket`, `spi`, `st7123touch`, `task`,
`thread_pool`, `timer` and `touch`. With espp from the component manager they fold into an
**espp** node again.
