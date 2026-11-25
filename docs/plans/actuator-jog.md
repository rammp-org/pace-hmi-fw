# Leg actuator jog, several at once (issue #29)

Move the carriages, or the front and rear casters, together from the HMI instead of one
actuator at a time. Same shape as the motor debug list ([motor-debug.md](motor-debug.md)).

## Messages (rammp-rtps `messages/joystick_message.hpp`)

| Message | Topic | Direction | Rate |
|---|---|---|---|
| `Actuators {seq, actuators[]}` | `rammp/mib/actuators` | MIB -> HMI | ~5 Hz |
| `ActuatorJog {ids[], direction}` | `rammp/joystick/actuator_jog` | HMI -> MIB | every 100 ms while held |

`ActuatorSummary`: id, ready, moving, position (counts), min/max counts, label. The MIB owns
the list and its names, so the HMI needs no rebuild when actuators are renamed or added.

`ActuatorJog` names the whole set each time: an actuator left out stops. The MIB stops them
all 300 ms after the last message, as for `MotorJog`.

## HMI (DEBUG ACTUATORS)

- One row per `Actuators` entry, below the motor rows. Short name = state (`RDY`, `MOV`,
  `OFF`), label = the MIB's name, value = position in counts. Not ready: `--`, -/+ greyed.
- A click (tap on the row, or the stick button) ticks or unticks a row (a check mark before
  the name). Ticks persist while the page is closed; an id the MIB drops loses its tick.
- -/+ (or stick left/right on the focused row) held: on a ticked row, every ticked row
  jogs; on an unticked row, that row alone. Release sends one stop for the set.
- Nothing is sent while the HMI e-stop is active.

## MIB (RAMMP-mib-fw, on `feat/20-seat-kinematics`)

- `publish_actuators()` beside `Motors` (200 ms), cached readings only.
- `run_leg_jog_step()` on the seat task (100 ms): each named ready leg gets
  `jog(+/- jog_step)`; one dropped from the set gets `jog(0)`; leaving IDLE quick-stops.
  Only in IDLE, e-stop clear, no seat move.

## Decided unattended (revisit)

- Separate messages from `Motors`/`MotorJog`: the legs are CANopen actuators with counts and
  ranges, not RACER boards; `MotorJog` stays one motor at a time.
- Same direction for every actuator in the set. Opposite directions (carriages apart) would
  need a per-id direction; not asked for.
- -/+ grey at a row's own limit, so a set cannot be pushed further from a row already at
  its end; hold on another ticked row. The MIB clamps each actuator anyway.

## Verified, and not (2026-11-25)

- HMI on Tab5 board 2 (local test build: a fake MIB's six actuators inside the HMI, the
  jog it would send logged; touch and remote UI over USB). Rows built from the list; MR
  not ready reads `--` with -/+ grey; ticks by tap and by stick button; RC + ML held + by
  touch moved together (`ids=[1 2]`); an unticked row jogged alone with another row
  ticked; LCarr + RCarr by stick right (`ids=[4 5]`, 1000 counts in 2 s); one stop on
  every release.
- A screenshot that blocks the LVGL task for over 300 ms lapses the jog (the fail-safe);
  a long screen build would do the same.
- Not run: the RTPS wire between HMI and fake MIB (the PC had no network path to the
  board), and the MIB side on a MIB (none on this bench). Both build.
