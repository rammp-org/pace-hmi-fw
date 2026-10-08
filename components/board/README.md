# board

The HMI's Tab5 board: its bring-up on the BSP in the order the HMI needs, and the adapters the
BSP's own tasks call (CS-LAY-02). Namespace `hmi::board`.

Built on the vendored BSP (`components/m5stack-tab5`, `espp::M5StackTab5`): every driver, bus
and LVGL display is the BSP's. This component only calls it, in `app_main`'s order, with
`app_main`'s log lines and failure policy, and adapts its callbacks to the rest of the firmware.
Moved out of `main.cpp`'s `app_main` (owner, 2026-10-08), clean then move (app-main-shrink V2).

| File | What |
| --- | --- |
| `adapters.hpp`, `adapters.cpp` | `TouchClick`: the BSP touch task's callback, a click on each press and every change logged at debug level. `SideButton`: the BSP button task's callback, each edge logged and a press stepping the brightness. Both reach main through a plain function in their `Config` (the click, the brightness step) |

## Requirements

None written yet. The behaviour is `app_main`'s as it was; the bench checks it (B0-B5, the
boot log's `main` lines in B2).

## Tasks

None of its own. The adapters run on the BSP's touch and button tasks, as before the move.

## Dependencies

`m5stack-tab5` (the BSP), espp `logger`.

## Ratchet

Clean: no transfer grants.
