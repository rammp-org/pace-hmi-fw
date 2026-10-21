# Motor debug list plan (branches `dev_ai_motor_debug`)

Status: implemented on local `dev_ai_motor_debug` branches in rammp-rtps, RAMMP-mib-fw and
pace-hmi-fw, flashed on the bench MIB and Tab5. Nothing has been pushed. The jog itself has not
been tested on hardware (section 5). Written during an unattended session.

Scope: the HMI builds its motor debug list from RTPS messages at run time, so adding a motor
to or removing one from the shared axis table (`RAMMP_AXIS_TABLE` in rammp-rtps
`messages/motor_message.hpp`) only means rebuilding the MIB, not the HMI. Each motor can be
jogged from the HMI, hold-to-run. Keep it as simple as possible.

## 1. Messages (rammp-rtps `messages/joystick_message.hpp`)

```cpp
#define RAMMP_TOPIC_MIB_MOTORS "rammp/mib/motors"                 // MIB -> joystick, ~5 Hz
#define RAMMP_TYPE_MOTORS "rammp/msg/Motors"
#define RAMMP_TOPIC_JOYSTICK_MOTOR_JOG "rammp/joystick/motor_jog" // joystick -> MIB
#define RAMMP_TYPE_MOTOR_JOG "rammp/msg/MotorJog"

struct MotorSummary {        // one axis-table row, as the MIB last heard from its board
  uint8_t axis_id;           // AxisId as a number: the joystick lists it, never switches on it
  uint8_t online;            // 1 = a MotorState arrived recently
  BoardState state;
  FaultCode fault_code;
  float position;            // rad
  float velocity;            // rad/s
  std::string label;         // "Drive L"
};
struct Motors { uint8_t seq; std::vector<MotorSummary> motors; };  // one entry per axis
struct MotorJog { uint8_t axis_id; int8_t direction; };            // -1 / +1; 0 = stop now
```

The contract: send a `MotorJog` at least every 100 ms while held. The MIB stops the motor
300 ms after the last one, and picks the speed itself.

The catalog and the state travel in one message. That costs a label string per motor, five
times a second, about 100 bytes for four motors, and avoids a second topic and a catalog
version handshake. The rows themselves are not dynamic: `MotorSummary`'s fields are fixed, so
changing them means rebuilding both firmwares.

## 2. MIB (RAMMP-mib-fw)

- One command writer and one state reader per `rammp::kAxes` row. Before this, only the two
  drive wheels were commanded; the casters never were.
- Every 50 ms, each board gets one command:
  - while `DRIVE_ENABLED`: the two drive wheels get their wheel speeds, as before;
  - while `IDLE` with the e-stop clear and a fresh jog: the jogged board gets ARMED at
    ±1 rad/s (`motor_jog_velocity_radps`);
  - otherwise: DISARMED, 0 rad/s.
- Every 200 ms, `Motors` is sent. A board counts as online if its `MotorState` arrived
  within the last 500 ms.
- The spec now comes from the submodule, through `override_path` in `main/idf_component.yml`.
  This is the HMI's existing pattern. Before this, the copy that estop_core fetches
  (`3390723`) won and the submodule pin (`c7db102`) was ignored.
- The RTPS endpoint pool is raised in `sdkconfig.defaults`. `static_assert`s in
  `mib_system.cpp` tie the pool to `kAxisCount`, so a new axis row fails the build until the
  pool is raised with it.

## 3. HMI (pace-hmi-fw)

- One `Motors` reader and one `MotorJog` writer: the pool goes from 7 to 8 writers and from
  6 to 7 readers.
- The DEBUG ACTUATORS page (behind the PIN) gets one `SettingRow` per `Motors` entry, below
  the seat axes:
  - the short name is the state (`OFF`, `DIS`, `ARM`, `STP`, `HLD`, `F<code>`);
  - the label is the MIB's;
  - the value is the position in rad;
  - -/+ are greyed while the board is offline.
- No SquareLine change: the rows reuse the existing SettingRow component.
- Hold-to-run: `motor_jog_poll_cb` (100 ms) looks for a pressed -/+ button, or the stick
  held left/right on the focused motor row, and sends `MotorJog`. It sends one stop when the
  hold ends. The HMI e-stop blocks the send, like the seat requests.
- If the list's ids or labels change while the page is up, the page rebuilds its rows.

## 4. Decided unattended (revisit)

- **One message, not a catalog plus a state message** (the earlier proposal). Simpler. Revisit
  if labels or limits ever get large.
- **Jog only in IDLE**, with no new DEBUG or CALIBRATE state. IDLE already means drive
  disabled, and the jog stops on its own. Revisit if the jog should be refused while the
  seat moves.
- **A jog that ends sends DISARMED (coast)**, the MIB's existing idle command. A loaded seat
  actuator might be better held (ARMED + HOLD); someone who knows the mechanics should decide.
- **The MIB picks the jog speed (1 rad/s at the output shaft)**; the HMI sends a direction
  only.
- **Flashed on the bench boards.** You asked for this. The standard (AI-UNA-02) keeps safety
  changes on a simulated bench. Only the MIB and the Tab5 were on the network (no PACE RACER
  board ever reported a `MotorState`), and the MIB's e-stop was latched the whole time.
  Backups of both flashes were taken first.

## 5. Verified, and not

Verified on the bench (MIB on `/dev/ttyACM1`, Tab5 on `/dev/ttyACM2`, a direct Ethernet link):

- The MIB creates all 4 command writers, 4 state readers, `Motors` and `MotorJog`, and the
  HMI creates its `MotorJog` writer and `Motors` reader, all within the endpoint pool.
- The HMI logs `Motors arriving: 4 motors`.
- **A motor added without rebuilding the HMI:** a temporary MIB build with a fifth row
  (`BENCH_TEST`) made the still-running HMI log `Motors arriving: 5 motors`. Flashing the
  committed 4-row MIB took it back to 4, with no HMI reboot.
- With the fifth row and no pool change, the build stops on the new `static_assert`s.
- Builds: MIB with IDF v6.0.1, HMI with v6.0, and the rammp-rtps template for esp32 with
  v6.0.1. No warnings in the changed code, apart from LVGL's deprecated `lv_subject_init_int`,
  which the file already uses about 30 times.
- CI-style builds from fresh clones, with submodules checked out and components resolved
  fresh:
  - HMI (IDF v6.0, from its `dependencies.lock`): passes.
  - MIB (IDF v6.0.1): passes when LVGL resolves to 9.6.0 (forced in a scratch copy).
  - MIB: fails, as `aa157f5` already does, when LVGL resolves to 9.4.0. The error is
    `LV_MEM_SIZE >= 2kB is required`, caused by `CONFIG_LV_MEM_SIZE_KILOBYTES=0`. The local
    component manager picked 9.4.0 even from an empty cache, although the registry lists
    9.6.0 and nothing pins LVGL below it. Whether GitHub CI gets 9.4 or 9.6 was not checked
    (open item 1).
- cppcheck 2.16 with CI's arguments on every changed file: no new findings. HMI `main.cpp`
  needs its two `asm("_binary_click_wav_*")` labels removed for cppcheck to parse it at all.
  That parse error was there before this change.
- clang-format: the changed HMI and spec lines are clean.

Not verified:

- **The jog on hardware.** The HMI's e-stop was latched, so the HMI does not send a jog.
  Clearing it takes the MIB's clear button (GPIO3) held for 2 s. Even then, an ARMED command
  would only mean something with a PACE RACER board on the network. A hand test: clear the
  e-stop, open DEBUG ACTUATORS (PIN), hold + on a motor row, and watch the MIB log
  `Motor jog <label> +: moving`; let go and it logs `Motor jog stopped`.
- **The DEBUG ACTUATORS page as drawn.** Nobody looked at the screen. This session had no
  touch, and no remote UI over the network.
- GitHub CI. The branches are local. The firmware pins rammp-rtps `00773f8`, which exists
  only locally, so push rammp-rtps first or CI's submodule checkout fails.

## 6. Open items

1. **MIB and LVGL 9.4 vs 9.6 (not from this change).** If MIB CI resolves 9.4.0, its build
   fails on `CONFIG_LV_MEM_SIZE_KILOBYTES=0`. Two ways out: pin `lvgl/lvgl: '>=9.6.0'` in the
   MIB's `main/idf_component.yml`, or go back to `678b71a`. Not done here, because `aa157f5`
   reverted the second on purpose.
2. Hand test of the jog and the DEBUG ACTUATORS page (section 5).
3. Coast versus hold at the end of a jog (section 4).
