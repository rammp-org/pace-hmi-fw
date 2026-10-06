# Architecture: pace-hmi-fw

Generated sections are rewritten by `tools/gen_diagrams` and checked by its `check` mode, meant
for L0 in CI (CS-ARC-02; not yet a step in `.github/workflows/l0.yml`).
Do not edit between the markers. Regenerate with `python tools/gen_diagrams/gen_diagrams.py write`;
see [tools/gen_diagrams/README.md](../tools/gen_diagrams/README.md).

## Today vs target

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
(`kMibStatusPeriod` 500 ms, bench topics), the stick task's 33 ms ADC period, `main/github_ota.cpp`,
`main/remote_ui.cpp` (`CONFIG_HMI_REMOTE_UI`), `main/storage.cpp`, and the Bench section of
[project-profile.md](project-profile.md).

## D2 Islands
<!-- BEGIN GENERATED D2 (tools/gen_diagrams): do not edit -->
D2 is generated once `topology.hpp` lands (draft on `dev_ai_refactor_topology`). Until then the target sketch is in [the plan, §2.5](plans/refactor.md#25-diagrams).
<!-- END GENERATED D2 -->

## D3 Components
<!-- BEGIN GENERATED D3 (tools/gen_diagrams): do not edit -->
First-party components (`main` and `components/*` except `joystick`, `m5stack-tab5`, `ui`) and their direct `REQUIRES`/`PRIV_REQUIRES`. Vendored, generated and submodule components appear only as targets. Third-party dependencies are folded into one node per source; the number is how many of its components are used directly.

```mermaid
flowchart LR
  c_fw_core["fw_core"]
  c_hmi_format["hmi_format"]
  c_hmi_models["hmi_models"]
  c_joystick["joystick (vendored)"]:::safety
  c_m5stack_tab5["m5stack-tab5 (vendored)"]
  c_main["main"]:::safety
  c_ota_parse["ota_parse"]
  c_rammp_rtps_messages["rammp_rtps_messages (submodule)"]:::safety
  c_ui["ui"]:::gen
  g_ESP_IDF["ESP-IDF · 13"]
  g_espp["espp · 33"]
  g_Espressif_registry["Espressif registry · 7"]
  g_LVGL["LVGL · 1"]
  c_fw_core -.-> g_ESP_IDF
  c_fw_core -.-> g_espp
  c_main -.-> c_fw_core
  c_main -.-> c_hmi_format
  c_main -.-> c_hmi_models
  c_main -.-> c_joystick
  c_main -.-> c_m5stack_tab5
  c_main -.-> c_ota_parse
  c_main -.-> c_rammp_rtps_messages
  c_main -.-> c_ui
  c_main -.-> g_ESP_IDF
  c_main -.-> g_espp
  c_main -.-> g_Espressif_registry
  c_main -.-> g_LVGL
  c_ota_parse -.-> g_Espressif_registry
  classDef hw fill:#d9dde3,stroke:#7a8590,color:#111
  classDef safety stroke:#c0392b,stroke-width:3px
  classDef gen stroke-dasharray:5 4
```

<details><summary>Folded dependencies</summary>

- **ESP-IDF**: `app_update`, `bootloader_support`, `esp_app_format`, `esp_driver_ppa`, `esp_eth`, `esp_event`, `esp_http_client`, `esp_netif`, `esp_partition`, `esp_wifi`, `freertos`, `lwip`, `mbedtls`
- **espp**: `espp__adc`, `espp__base_component`, `espp__base_peripheral`, `espp__bmi270`, `espp__button`, `espp__cdr`, `espp__cli`, `espp__codec`, `espp__display`, `espp__display_drivers`, `espp__drv2605`, `espp__file_system`, `espp__filters`, `espp__format`, `espp__gt911`, `espp__i2c`, `espp__ina226`, `espp__input_drivers`, `espp__interrupt`, `espp__led`, `espp__logger`, `espp__math`, `espp__pi4ioe5v`, `espp__reflect_cpp`, `espp__rtps`, `espp__rx8130ce`, `espp__socket`, `espp__spi`, `espp__st7123touch`, `espp__task`, `espp__thread_pool`, `espp__timer`, `espp__touch`
- **Espressif registry**: `espressif__cjson`, `espressif__esp-dsp`, `espressif__esp_hosted`, `espressif__esp_sccb_intf`, `espressif__esp_wifi_remote`, `espressif__usb`, `espressif__w5500`
- **LVGL**: `lvgl__lvgl`

</details>

Source: `docs/diagrams/project_components.json`, a path-free snapshot of an ESP-IDF build's `project_description.json`. Regenerate after a build with `python tools/gen_diagrams/gen_diagrams.py write --build-dir <idf build dir>`.
<!-- END GENERATED D3 -->
