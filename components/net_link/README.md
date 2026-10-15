# net_link

The network link and the HMI's one RTPS participant: W5500 SPI Ethernet or Wi-Fi through the
Tab5's ESP32-C6 (esp_hosted over SDIO), one at a time as Internet Settings chose; DHCP, the
gateway ping, the participant, every publisher and subscriber, and the link state the TopBar
and the motion guard read. The code moved here from `main/rtps_comms.*` unchanged
(app-main-shrink V15, CS-LAY-01); the topology names the component (`COMPONENTS`: `net_link`
on the NET task). Safety-relevant: `XYTwist`, `DriveCommand` and `SeatCommand` leave through it,
`MibStatus` arrives through it, and its link flags feed the motion guard (hazard fix C4).

## Interface

| Header | What |
| --- | --- |
| `rtps_comms.hpp` | `rtps_comms_start`; the link (`NetLink`, `RtpsLinkState`, `RtpsLinkFlags`, Wi-Fi scan and join); the handlers (`rtps_comms_on_*`, registered before start); the publishers (`rtps_comms_publish_*`); diagnostics and MibStatus statistics |

The header keeps its name and the functions their names, so the callers (`main.cpp`,
`selftest.cpp`, `about_ui.cpp`, `internet_ui.cpp`) only gained the component dependency. Log
lines keep the `rtps_comms` tag (the bench's boot check reads it).

## Tasks and dependencies

- Tasks: `rtps_start` (one-shot: the link up, an IP, the gateway ping, the participant) and
  `rtps_pub` (the 2 s heartbeat; rebinds RTPS when the IP changes), both from
  `rtps_comms_start`; espp's RTPS tasks (`rtps_reactor`, `rtps_worker_0/1` at priority 20,
  `rtps_protocol`). The handlers run on the RTPS workers. Values in
  `tools/guards/baselines/tasks.json`.
- Dependencies: `hmi_rtps_spec` (public: the topics and messages), `storage` (`wifi.txt`),
  espp `rtps`, `cdr`, `task`, `logger`; ESP-IDF `esp_eth` with Espressif's `w5500`, `esp_wifi`
  with `esp_hosted`, `esp_netif`, `esp_event`, `lwip` (ping), the GPIO and SPI drivers,
  `esp_timer`.
- Board-facing: no host L1 app; the self test's `net.*` and `rtps.*` checks and the bench
  (B1, B2) cover it.

## Ratchet

Legacy debt moved with it under `tools/l0/ratchet.py transfer` grants (its mutexes, the
namespace-scope state, the start-up sleeps, the function-local statics). Its four functions over
60 lines keep their `fn_over_60` baseline entry (4), moved with the file.
