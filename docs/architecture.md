# Architecture: pace-hmi-fw

Generated sections are rewritten by `tools/gen_diagrams` and checked by its `check` mode, meant
for L0 in CI (CS-ARC-02; not yet a step in `.github/workflows/l0.yml`).
Do not edit between the markers. Regenerate with `python tools/gen_diagrams/gen_diagrams.py write`;
see [tools/gen_diagrams/README.md](../tools/gen_diagrams/README.md).

## Today vs target

For a file-by-file walk through the code as it is today (boot, tasks, the motion path, the drive
state machine, every fragment), read [how-the-firmware-works.md](how-the-firmware-works.md).

- **Today:** almost everything is one component, `main` (a `main.cpp` split into one-TU
  `frag_*.inc` fragments, plus the screen, network, OTA and self-test files). Tasks share state
  through statics and a global recursive `lvgl_mutex`. There are no islands, no channels and no
  `topology.hpp` yet. The first components have moved out: `fw_core` (channel and ownership
  helpers) and `hmi_format` (screen text formatting).
- **Target:** the islands, adapters and channels of
  [docs/plans/refactor.md §2.1–2.3](plans/refactor.md#2-target-design), wired by
  `components/topology/include/topology.hpp`. D2 below appears when that file lands; D3 grows as
  components move out of `main`. The target sketches of D2 and D3 are in
  [§2.5](plans/refactor.md#25-diagrams).
- D4 (state machines inside a component) does not exist yet: the drive session, link and seat
  tables are drafts in [§2.4](plans/refactor.md#24-safety-state-machines-transition-tables).

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

The HMI is the joystick and touch screen of a powered wheelchair. It sends motion and seat
commands to the MIB/MCB over RTPS and shows the chair's state. The network is either Wi-Fi
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

Sources for D1: `main/rtps_comms.cpp` (Wi-Fi or W5500, one link), `main/hmi_rtps_spec.hpp`
(`kMibStatusPeriod` 500 ms, bench topics), the stick task's 33 ms ADC period, `components/ota/src/github_ota.cpp`,
`main/remote_ui.cpp` (`CONFIG_HMI_REMOTE_UI`), `components/storage`, and the Bench section of
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
  c_drive_adapter["drive_adapter"]
  c_drive_session["drive_session"]
  c_feedback["feedback"]
  c_fw_core["fw_core"]
  c_hmi_format["hmi_format"]
  c_hmi_models["hmi_models"]
  c_hmi_ui["hmi_ui"]
  c_joystick["joystick (vendored)"]:::safety
  c_joystick_cal["joystick_cal"]
  c_m5stack_tab5["m5stack-tab5 (vendored)"]
  c_main["main"]:::safety
  c_ota["ota"]
  c_ota_parse["ota_parse"]
  c_post["post"]:::safety
  c_rammp_rtps_messages["rammp_rtps_messages (submodule)"]:::safety
  c_settings["settings"]
  c_stick["stick"]
  c_storage["storage"]
  c_ui["ui"]:::gen
  g_ESP_IDF["ESP-IDF · 17"]
  g_espp["espp · 33"]
  g_Espressif_registry["Espressif registry · 7"]
  g_joltwallet["joltwallet · 1"]
  g_LVGL["LVGL · 1"]
  c_drive_adapter -.-> c_drive_session
  c_drive_adapter -.-> g_espp
  c_feedback -.-> g_ESP_IDF
  c_feedback -.-> g_espp
  c_fw_core -.-> g_ESP_IDF
  c_fw_core -.-> g_espp
  c_hmi_ui -.-> c_hmi_format
  c_hmi_ui -.-> c_hmi_models
  c_hmi_ui -.-> c_ota_parse
  c_hmi_ui -.-> c_rammp_rtps_messages
  c_hmi_ui -.-> c_ui
  c_hmi_ui -.-> g_ESP_IDF
  c_hmi_ui -.-> g_espp
  c_hmi_ui -.-> g_LVGL
  c_joystick_cal -.-> g_espp
  c_main -.-> c_drive_adapter
  c_main -.-> c_drive_session
  c_main -.-> c_feedback
  c_main -.-> c_fw_core
  c_main -.-> c_hmi_format
  c_main -.-> c_hmi_models
  c_main -.-> c_hmi_ui
  c_main -.-> c_joystick
  c_main -.-> c_joystick_cal
  c_main -.-> c_m5stack_tab5
  c_main -.-> c_ota
  c_main -.-> c_ota_parse
  c_main -.-> c_rammp_rtps_messages
  c_main -.-> c_settings
  c_main -.-> c_stick
  c_main -.-> c_storage
  c_main -.-> c_ui
  c_main -.-> g_ESP_IDF
  c_main -.-> g_espp
  c_main -.-> g_Espressif_registry
  c_main -.-> g_LVGL
  c_ota -.-> c_ota_parse
  c_ota -.-> c_storage
  c_ota -.-> g_ESP_IDF
  c_ota -.-> g_espp
  c_ota -.-> g_Espressif_registry
  c_ota_parse -.-> g_Espressif_registry
  c_settings -.-> c_storage
  c_settings -.-> g_espp
  c_stick -.-> c_joystick
  c_storage -.-> g_ESP_IDF
  c_storage -.-> g_espp
  c_storage -.-> g_joltwallet
  classDef hw fill:#d9dde3,stroke:#7a8590,color:#111
  classDef safety stroke:#c0392b,stroke-width:3px
  classDef gen stroke-dasharray:5 4
```

<details><summary>Folded dependencies</summary>

- **ESP-IDF**: `app_update`, `bootloader_support`, `esp_app_format`, `esp_driver_ppa`, `esp_eth`, `esp_event`, `esp_http_client`, `esp_mm`, `esp_netif`, `esp_partition`, `esp_timer`, `esp_wifi`, `freertos`, `lwip`, `mbedtls`, `pthread`, `spi_flash`
- **espp**: `espp__adc`, `espp__base_component`, `espp__base_peripheral`, `espp__bmi270`, `espp__button`, `espp__cdr`, `espp__cli`, `espp__codec`, `espp__display`, `espp__display_drivers`, `espp__drv2605`, `espp__file_system`, `espp__filters`, `espp__format`, `espp__gt911`, `espp__i2c`, `espp__ina226`, `espp__input_drivers`, `espp__interrupt`, `espp__led`, `espp__logger`, `espp__math`, `espp__pi4ioe5v`, `espp__reflect_cpp`, `espp__rtps`, `espp__rx8130ce`, `espp__socket`, `espp__spi`, `espp__st7123touch`, `espp__task`, `espp__thread_pool`, `espp__timer`, `espp__touch`
- **Espressif registry**: `espressif__cjson`, `espressif__esp-dsp`, `espressif__esp_hosted`, `espressif__esp_sccb_intf`, `espressif__esp_wifi_remote`, `espressif__usb`, `espressif__w5500`
- **joltwallet**: `joltwallet__littlefs`
- **LVGL**: `lvgl__lvgl`

</details>

Source: `docs/diagrams/project_components.json`, a path-free snapshot of an ESP-IDF build's `project_description.json`. Regenerate after a build with `python tools/gen_diagrams/gen_diagrams.py write --build-dir <idf build dir>`.
<!-- END GENERATED D3 -->
