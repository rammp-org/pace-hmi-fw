# pace-hmi-fw

<img src="docs/screenshots/MainScreenFlex.png" alt="Main Screen" width="260">

Firmware for the RAMMP wheelchair HMI: an M5Stack Tab5 (ESP32-P4) on the [RAMMP HMI PCB](https://github.com/rammp-org/pace-hmi-pcb).

## Hardware
- Tab5 (ESP32-P4), 720x1280 portrait touch panel
- 3D hall joystick (X / Y / twist) on the ADCs, up to 4 buttons on GPIO
- W5500 SPI Ethernet for RTPS
- WIP: haptic motor over I2C

## Build and flash
- Clone with the shared RTPS spec: `git clone --recursive` (or `git submodule update --init`).
- With ESP-IDF v6.0: `idf.py build flash monitor`
- Without a toolchain: download the programmer from **Actions → Build and Package Main → Artifacts** and run it.
- Or put the [release](https://github.com/rammp-org/pace-hmi-fw/releases) images in `precompiled/` and run `.\flash_precompiled.ps1` (needs esptool v5+).

## Screens
- Joystick only: **push up and hold** to enter, **pull and hold** (or hold the button) to leave. The bottom prompt says which.
- The UI is designed in SquareLine Studio ([pace-hmi-gui](https://github.com/rammp-org/pace-hmi-gui)); `main/ui/` is its export (`import_ui.ps1`). Don't edit it here.

| | | |
|:--:|:--:|:--:|
| <img src="docs/screenshots/BootScreen.png" width="200"> | <img src="docs/screenshots/MainScreenFlex.png" width="200"> | <img src="docs/screenshots/DriveScreen.png" width="200"> |
| Boot splash | Home pager | Drive: speed, drive mode |
| <img src="docs/screenshots/SeatAdjustmentFlexScreen.png" width="200"> | <img src="docs/screenshots/GenericActionsScreen.png" width="200"> | <img src="docs/screenshots/SpecificSettingScreen.png" width="200"> |
| Seat functions | Generic actions | Setting page (brightness, actuators) |
| <img src="docs/screenshots/RDScreen.png" width="200"> | <img src="docs/screenshots/DiagnosticsScreen.png" width="200"> | <img src="docs/screenshots/LogScreen.png" width="200"> |
| PIN before DEBUG ACTUATORS | Live MCB diagnostics | System logs |
| <img src="docs/screenshots/JoystickTest.png" width="200"> | | |
| Joystick test and calibration | | |

- **Drive / Seat**: only enter while the MCB link is up and its state is OK; otherwise a red banner says why.
- **Generic actions**: one row per entry in `main/actions_spec.h` (haptic test, self test, seat up, restart).

## Settings menu

| row | what it does |
| --- | --- |
| CHANGE THEME | switch the colour theme (saved) |
| SCREEN BRIGHTNESS | backlight 5-100 % (saved); the side button and RTPS can set it too |
| DIAGNOSTICS | live readings from the MCB, see below |
| DEBUG ACTUATORS | PIN, then step each actuator with -/+ (the MCB moves it) |
| SELF TEST | checks the HMI; results on screen and on serial |
| SYSTEM LOGS | the last 500 serial log lines |
| FPS COUNTER | show the render rate |
| HAPTIC TEST | buzz the vibration motor |
| Joystick Test → CALIBRATE | 6-step stick calibration, saved to flash |

Saved settings live in LittleFS (`/storage`), so they survive a reboot.

## RTPS

- The shared spec is [rammp-rtps](https://github.com/rammp-org/rammp-rtps), the git submodule `external/rammp-rtps`.
- `messages/joystick_message.hpp` there holds every message, topic, enum and table the MCB and the HMI share. Its top has an espp example.
- `main/hmi_rtps_spec.hpp` adds what only this HMI needs: timing, display limits, the bench self-test topics.
- Messages are plain C++ structs serialized by espp/cdr as **XCDR1** (classic CDR), so any DDS / ROS 2 stack can talk to it.
- Every topic is best-effort; the MCB resends its state periodically.
- The MCB owns the chair's state; the HMI shows it and asks for changes.

| topic | type | direction | carries |
| --- | --- | --- | --- |
| `rammp/mcb/system_state` | `SystemState` | MCB → HMI, 2 Hz | drive status, fault, profile, speed, clock, label and error text |
| `rammp/mcb/seat_state` | `SeatState` | MCB → HMI, 2 Hz + on change | seat axis positions, verdict on the last command |
| `rammp/mcb/diagnostics` | `Diagnostics` | MCB → HMI, 2 Hz | readings for each `RAMMP_DIAG_TABLE` row |
| `rammp/joystick/xy_twist` | `XYTwist` | HMI → MCB, ~30 Hz | calibrated X / Y / twist (-1..+1), buttons |
| `rammp/joystick/drive_command` | `DriveCommand` | HMI → MCB, per request | enable / disable driving, and the drive profile |
| `rammp/joystick/seat_command` | `SeatCommand` | HMI → MCB, per press | put seat axis N at an absolute target |
| `rammp/hmi/counter`, `command`, `brightness` | `std_msgs/UInt32` | bench PC | heartbeat, self-test run / ping, backlight % |
| `rammp/selftest/report` | `SelfTestReport` | HMI → PC | one per self-test check |

Example: an MCB (same espp / ESP-IDF stack) sending Diagnostics:

```cpp
#include "rtps_pubsub.hpp"
#include "messages.hpp" // rammp-rtps

espp::RtpsParticipant rtps({.interface_address = my_ip});
rtps.start();
const auto &topic = rammp::kMcbDiagnostics; // a Topic<rammp::Diagnostics>
espp::Publisher<rammp::Diagnostics> pub(rtps, {.topic = topic.name, .type_name = topic.type});

pub.publish({.seq = seq++, .items = {{.values = {305, 150, 450}}}}); // T1: 30.5 C, 1.50 A, 45.0 deg
```

Receiving works the same way: `espp::Subscriber<rammp::SeatCommand>` with an `.on_message` callback.

The spec is C++20 and strictly typed:
- Every enum is scoped with a fixed wire width (`enum class DriveStatus : uint8_t`), so a raw number or the wrong enum does not compile.
- Every topic carries its message type (`Topic<SystemState>`), so the wrong message or handler for a topic does not compile.
- Timings are `std::chrono::milliseconds`; names come from typed `to_string(...)` overloads.

How the messages reach the screens:
- **SystemState** → the status labels on every screen, the drive speed, the error banner, and the TopBar clock.
- No SystemState for 2 s → "RTPS LINK LOST"; Drive and Seat are refused.
- The MCB decides whether the chair drives. Holding the stick up sends a **DriveCommand** and waits: the DriveScreen opens only once `SystemState.drive_status` is `ACTIVE`, and a request unanswered for 2 s raises the same banner a refusal does. Leaving the screen sends `DISABLE`, and picking a drive profile re-sends the request carrying it.
- **SeatState** → the numbers on the seat screen and the DEBUG ACTUATORS rows; a refused request flashes its row. The MCB owns every position: a press asks, and the number moves when `SeatState` says the seat did.
- Each seat press sends a **SeatCommand** with an **absolute** target, so a step button and a preset are the same message, and a lost or repeated one cannot drift the seat.
- **Diagnostics** → the DIAGNOSTICS rows; the rate label shows the arrival Hz.
- No Diagnostics for 2 s → every row turns red and blinks.
- **XYTwist** goes out continuously once the MCB is found.

### Adding a seat axis or a diagnostics item

One row in `messages/joystick_message.hpp` (rammp-rtps); the HMI screens and the Python tools pick it up.

```c
// RAMMP_SEAT_AXIS_TABLE: X(id, NAME, short, label, min, max, step, decimals, unit)
  X(4, HEADREST, "M5", "Headrest", 0, 900, 25, 1, "deg")

// RAMMP_DIAG_TABLE: D(id, NAME, short, label, unit1, dec1, unit2, dec2, unit3, dec3)
  D(3, TEST_4, "T4", "Test actuator 4", "Temp [C]", 1, "Current [A]", 2, "Pos [deg]", 1)
```

- `id` is the next number, in table order.
- Every row except the last ends with `\`.
- Values are raw integers in units of 10^-decimals `unit` (250 with 1 decimal is 25.0).
- A diagnostics unit of `""` hides that reading.
- The MCB must send one more value (`SeatState.values` / `Diagnostics.items`), in table order.
- A seat axis also takes the next free button on the seat screen, in table order; past the fourth there is no button for it yet, and it appears only on the DEBUG ACTUATORS page.

## Testing

| command | what it does |
| --- | --- |
| `python scripts/rtps_mcb_gui.py` | plays the MCB: status, drive requests, error banner, seat, diagnostics, drive view |
| `python scripts/rtps_selftest.py` | runs the self test over RTPS (exit 0 = pass) |
| `python scripts/rtps_mcb_sim.py` | CLI version of the GUI (`--cycle` walks every state) |
| `python scripts/rtps_adc_plot.py` | live joystick plot (needs matplotlib) |
| `cd sim; python run.py` | the screens on a PC, no board ([sim/README.md](sim/README.md)) |

<img src="docs/screenshots/McbSimGui.png" alt="MCB simulator" width="480">

- Nothing arriving? The PC picked the wrong network adapter: set **Via** in the GUI, or `--advertised-address <PC_IP>`.
- The serial log shows the Ethernet link, the IP, and every MCB status change.
