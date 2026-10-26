# Motor debug list plan (branches `dev_ai_motor_debug`)

Status (2026-10-26): implemented on `dev_ai_motor_debug` branches in rammp-rtps, RAMMP-mib-fw and
pace-hmi-fw. The HMI side is tested on a Tab5 against the MCB simulator, jog included (section 5);
a jog with a PACE RACER board on the network is still open. First written during an unattended
session on 2026-10-21.

Branches:

- pace-hmi-fw: on `dev-estop` (PR #17, with #18's estop-core v0.3.0, espp 1.3.10 and RTPS tasks at
  priority 20). Components are the newest published: espp 1.3.10, LVGL 9.6.0~1, w5500 2.0.1,
  esp-dsp 1.8.2.
- rammp-rtps: rammp-rtps `main`, then `feat/estop` merged (PR #10; a merge, so `3390723`, which
  estop-core pins, stays reachable), then the motor commit. Once #10 is in `main`, this branch's
  diff is the motor commit alone.

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
  - the value is the position in rad, one decimal and no unit, like the seat rows ("0.00 rad"
    overflowed the column and scrolled);
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

Hand test (2026-10-21, attended, no PACE RACER board connected):

- With DEBUG ACTUATORS open, the e-stop cleared and stayed clear. The four motor rows came
  from the MIB's list. Holding + on Drive L was refused: the motor is offline, so -/+ are
  greyed. No `MotorJog` was sent and the MIB logged no jog.
- **Found: entering the PIN re-latches the e-stop, and this predates the change.** Building
  the Settings screen holds the LVGL task (priority 20) for about 1 s. That starves the RTPS
  writer threads (priority 5): `History overflow` on `xy_twist` and `estop`, then the MIB's
  300 ms deadman latches. The original image (`880d815`, flashed back from the backup) does
  the same. Workaround for testing: open the page first, then clear the e-stop. The fix
  under way on `dev_ai_rc` (RTPS workers at priority 20, which needs an espp change) is not
  on `dev-estop`.
- Fixed while testing (`eb63c62`): the Motors handler no longer waits on `lvgl_mutex`.
  There are only two RTPS reader workers, so with it and the MibStatus handler both waiting
  behind a screen build, the e-stop reader was starved too.

Bench (2026-10-26, Tab5 board 2 on `0d0a450`, MCB simulator and GUI on a PC on the same LAN, no
MIB, no PACE RACER board):

- Rows follow the list with no HMI rebuild: 4 (the axis table), then 22 (the page draws the first
  16, ending at "Motor 15"), then 5; the page rebuilt itself each time with the page open.
- OFF greys -/+ and no MotorJog is sent. A faulted row (F10, F5) sends; the MIB side refuses it.
- Hold-to-run: a 1.0 s touch hold moved a row +1.00 rad, 0.6 s on - moved -0.60 rad; the row read
  ARM while held, and one stop (direction 0) followed the release. The stick held right on the
  focused row did the same.
- With the e-stop latched by the MIB, a held + sent nothing.
- Entering the PIN with the e-stop clear no longer re-latches it: the RTPS priority-20 change from
  `dev-estop` fixed open item 3 below.
- Remote-UI trap: a held `PRESS` is released when the hmi_ui.py connection closes, so a hold must
  be sent on one `hmi_ui.Hmi` connection (one `hmi_ui.py raw` call per command never holds).
- Not covered: the HMI keeps the last Motors list if the MIB stops sending it (the rows stay
  enabled and a jog goes nowhere). A staleness timeout would grey them; not done.

With the real MIB (2026-10-26): RAMMP-mib-fw `dev_ai_motor_debug` rebased onto `feat/estop-recovery`
(#39), rammp-rtps at `cc85a40`, LVGL pinned `>=9.6.0` (`4df9bac`), espp 1.3.10, IDF v6.0.1. A
fake PACE RACER board on the PC (MotorState out on `rammp/drive_left/motor_state`, MotorCommand in)
stood in for a wheel.

- The HMI got its rows from the MIB: `Motors arriving: 4 motors`, all OFF; Drive L went to DIS
  with -/+ enabled once the fake board's MotorState reached the MIB.
- E-stop latched: a held + sent nothing; the board stayed DISARMED.
- E-stop cleared (the MIB's GPIO3 button): + held 2.0 s gave one ARMED +1.000 rad/s segment and
  +2.00 rad at the board, then DISARMED on release; - held 1.0 s gave -1.000 rad/s, -1.02 rad. The
  HMI showed ARM while held and the board's position afterwards.
- A remote-UI screenshot during a hold stalls the LVGL task past the MIB's 300 ms jog timeout,
  so the MIB stops the motor and restarts it when MotorJog resumes. Any LVGL stall that long does
  the same; it fails safe.
- Bench network: the MIB serves DHCP on 192.168.4.1. Both boards on a home router left the Tab5
  with the router's lease and no route to the MIB; a Tab5 reset that won the MIB's offer fixed it.

## 6. Open items

1. **MIB and LVGL 9.4 vs 9.6 (not from this change; `4df9bac` pins `>=9.6.0` on the MIB branch).** If MIB CI resolves 9.4.0, its build
   fails on `CONFIG_LV_MEM_SIZE_KILOBYTES=0`. Two ways out: pin `lvgl/lvgl: '>=9.6.0'` in the
   MIB's `main/idf_component.yml`, or go back to `678b71a`. Not done here, because `aa157f5`
   reverted the second on purpose.
2. A jog with a real PACE RACER board online (done with the real MIB and a fake board).
3. ~~The e-stop deadman trips on a screen build.~~ Fixed by `dev-estop`'s RTPS tasks at priority
   20 (PR #18); checked on the bench 2026-10-26.
4. Coast versus hold at the end of a jog (section 4).
