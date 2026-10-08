# control

The control island: the "Read ADC" task that reads the joystick and runs its stick pipeline
(CS-LAY-02). Namespace `hmi::control`.

The control island of [docs/plans/app-main-shrink.md](../../docs/plans/app-main-shrink.md) §3:
today's "Read ADC" task, lifted out of `app_main` clean then move (V2). The cycle is that task's
code; the ADC drivers, the stick and the twist's lowpass, which were app_main's locals and
function-local statics, are members.

| File | What |
| --- | --- |
| `include/control/stick_island.hpp` | `StickIsland<Stick, Io, kCycle>`: brings the continuous ADC (X/Y) and the twist's oneshot ADC up, builds the stick on its calibration (`start`), and every `kCycle.period_ms` reads the three pots and runs one `Stick::cycle` on them, then `Io::note_cycle`. `Cycle`: the period and the twist read, a template argument so they stay compile-time constants. Its private `read_twist_mv`, the twist's oversampled oneshot read, is out of line on purpose (its comment says why) |

Header-only. `StickIsland` is a template on main's stick (`StickSlot`: the pipeline, or the
bench injection in front of it) and main's `AdcStickIo`, so it is instantiated in main's unit
and the ADC path's calls stay in one translation unit, as before the lift. The task's frames
are guarded by `-fstack-usage` at each change (at the lift: the invoker 208 B, `read_twist_mv`
240 B, as in app_main) and on the board by B3's `mem.stk_adc`.

## Requirements

None written yet. The behaviour is the Read ADC task's as it was; read, pipeline, gate and
publish stay on this task (app-main-shrink V10). The pipeline's own requirements are
`components/stick`'s.

## Tasks

| Task | Name | Stack | Priority | Core | Period |
| --- | --- | --- | --- | --- | --- |
| the island's own | `Read ADC` | 4096 B | 0 (runs at IDF's pthread default, 5) | -1 (pinned by its first FPU use: core 0 on the board) | 33 ms |

`Config::task` is that row as a literal, espp's defaults written out, copied from the code it
replaced, so the G10 task dump (`tools/guards/baselines/tasks.json`) is unchanged. The
topology's `control` row (`components/topology`, CS-CON-02) is the target design: another
name, priority, core and stack (hazard H11, C4). Taking `Config::task` from
`topo.task_config(...)` is a later step, made once the owner has reviewed the TASKS rows; it
changes the task's behaviour, so it comes with its own G10 baseline. The continuous ADC's own
task is espp's (`ContinuousAdc`, priority 5).

## Dependencies

espp `adc` (continuous and oneshot), `filters` (the lowpass), `logger`, `task`. The stick
pipeline (`components/stick`) and main's Io come in as template arguments.

## Ratchet

Clean: no transfer grants. The task's wait is the espp Task idiom (CS-OWN-08).
