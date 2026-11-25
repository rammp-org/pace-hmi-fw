# Several leg actuators at once (issue #29)

Move the carriages, or the front and rear casters, together from DEBUG ACTUATORS. Same
shape as the motor debug list ([motor-debug.md](motor-debug.md)).

## Messages (rammp-rtps `messages/joystick_message.hpp`)

| Message | Topic | Direction | Rate |
|---|---|---|---|
| `Actuators {seq, actuators[]}` | `rammp/mib/actuators` | MIB -> HMI | 5 Hz |
| `ActuatorJog {ids[], direction}` | `rammp/joystick/actuator_jog` | HMI -> MIB | 100 ms while held |

- Per leg: id, ready, moving, position and range (counts), name. The MIB owns the list.
- `ActuatorJog` names the whole set: a leg left out stops. The MIB stops all 300 ms after
  the last message, as for `MotorJog`.

## HMI

- One row per leg, below the motor rows: `RDY` / `MOV` / `OFF`, name, position. Not
  ready: `--`, -/+ greyed.
- Tap or stick button ticks a row. Ticks survive closing the page; a leg the MIB drops
  loses its tick.
- Hold -/+ (or stick left/right): on a ticked row, all ticked legs move; on an unticked
  row, that leg alone. Release sends one stop. Nothing sent during e-stop.

## MIB (RAMMP-mib-fw, on `feat/20-seat-kinematics`)

- `Actuators` beside `Motors` (200 ms), cached readings.
- Seat task (100 ms): each named ready leg moves one `jog_step`; a dropped leg rests;
  leaving IDLE quick-stops. Only in IDLE, e-stop clear, no seat move.

## Decisions

- New messages, not `Motors` / `MotorJog`: legs are counts with ranges, not motor boards,
  and the MIB holds one `MotorJog` at a time.
- One direction for the whole set; opposite directions would need one per leg.
- Hold to move, not step targets: release or link loss stops every leg.

## Tested (2026-11-25)

- Tab5, with a fake leg list inside the HMI: ticked legs move together (touch and stick),
  an unticked leg moves alone, one stop per release, not-ready rows grey out.
- An LVGL stall over 300 ms (e.g. a screenshot) stops the move: the fail-safe.
- Over WiFi with the fake MIB (2026-11-26): rows from `Actuators`; FC alone and FC + ML
  together (`ids [0, 2]`) moved, one stop per release.
- Not run on a MIB. Both build.
