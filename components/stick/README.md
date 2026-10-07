# stick

`hmi::stick::StickPipeline`: the three pots' raw millivolts in, the keypad key and the motion
command (XYTwist) out. Safety-relevant (CS-SAF-01): it turns the joystick into the motion
command. Extracted, refactor only, from `adc_task_fn` in `main/main.cpp` (dev_refactor
7ea7592); the ADC task still owns it and still does the reads, the twist filter and the
timing.

## Requirements (as-is, characterised 2026-10-06)

These state what the code does today, pinned by golden vectors taken from the code before the
extraction (`test/golden_stick.inc`, frozen). They are not a reviewed specification.

| ID | Requirement | Tests |
| --- | --- | --- |
| REQ-STK-01 | Raw mV maps to a position in [-1, 1] per axis through `espp::Joystick`: X/Y with a circular dead zone and a range dead zone, the vertical pot inverted (+y up), twist on its own range mapper with its own mV dead bands. | STK-001, STK-002 |
| REQ-STK-02 | The mounted stick is swapped first, then mirrored per axis; twist is untouched. | STK-002, STK-034 |
| REQ-STK-03 | The key is a Schmitt trigger on the larger axis, with thresholds set by the sensitivity (1..10, clamped); a calibration run releases it; the remote key overrides it; a new key is latched as a flick. | STK-019, STK-022, STK-023, STK-030..032, STK-036 |
| REQ-STK-04 | The command is the mounted position times a scale: 0 while calibrating or while the gate (`stick_drives`) is closed, else drive speed / 10 (1..10, clamped). The 0 is a multiply. | STK-016..018, STK-033, STK-035 |
| REQ-STK-05 | A cycle with any of the three reads missing publishes nothing and calls nothing else. | STK-005, STK-015 |
| REQ-STK-06 | A cycle calls its `Io` in the order of the original code. | STK-003, STK-004 |
| REQ-STK-08 | A stick-button release within SELECT_MAX_US (500 ms) of its press selects; every press more than COUNT_DEBOUNCE_US (30 ms) after the last counted press counts (only the counter is debounced). | STK-040..042 |
| REQ-STK-07 | `pipeline_config` gives the HMI's mounting: center dead zone radius 0.10, range dead zone 0.05, twist dead bands 60 mV (center) and 40 mV (range), with the caller's calibration and key codes (the values main passed before they moved here). | STK-037 |

## Known hazards, pinned as they are (refactor.md §1; fixes parked in §3.3)

- H3: a pot outside its calibration is clamped to full deflection, so an open-circuit pot
  (0 mV) commands full left, full **forward** or full counter-clockwise, and a short to
  3300 mV full right, reverse or clockwise (STK-010..014).
- H9: an invalid ADC cycle publishes nothing rather than neutral (STK-015).
- The gate is `x * 0.0f`: a negative deflection goes out as -0.0 and a NaN as NaN (STK-016,
  STK-035). The stick button reaches the MCB with the gate closed (STK-017).

## Tests

`test/` is a host L1 app (`make test`, `make coverage`; `tests/manifest.d/stick.yaml`).
`legacy_adc_cycle.cpp` is a verbatim copy of the original code and the oracle the golden table
came from; `make golden` regenerates the table into the build folder, and copying it over the
committed one is a reviewed change, never a way to make a test pass.
