# POST limits: proposal from bench data (P3)

Status: proposal for the owner. No limit has been changed in code. Data: board 2 only.
Script: `tools/post_limits/characterise.py --results C:/b/bench/results`.

## Exec summary

What to decide: the 13 `D4:` limits in `components/post/include/post/checks.hpp`.

Result in one line: the data supports **keeping 12 placeholders and changing one** (the window,
25 to 30 cycles). Of the kept ones, `ADC_VALID_MIN_PERMILLE` has the thinnest data. Nothing in
the data says a placeholder is too tight or too loose. Everything about the stick is
**board 2 only** and must be measured on boards 1 and 3 before it is final.

Data: 85 self tests on board 2, 40 firmware strings, 6 Oct to 7 Oct 2026. Only 28 distinct
values for the most variable quantity, so read n as "runs", not "independent samples".

| Constant | Placeholder | Measured (board 2) | Proposed | Confidence |
| --- | --- | --- | --- | --- |
| `WINDOW_MIN_SAMPLES` | 25 cycles | cycle 35.0 to 35.1 ms; 25 = 0.87 s | **30** (1.05 s) | high |
| `ADC_VALID_MIN_PERMILLE` | 990 | 1000 in 85/85 runs (14.6k cycles, 0 bad) | keep 990 | medium (no failure ever seen) |
| `EXPECTED_I2C` | 11 addresses | 143 clean boots, all 11; 1 empty scan on a failed run | keep; split optional ones on boards 1, 3 | high (board 2), to-measure (1, 3) |
| `MEM_INT_MIN_B` | 12 KiB | 117 to 143 KB | keep | high |
| `MEM_INT_BLOCK_B` | 12 KiB | 62 KB in all 85 runs | keep | high |
| `MEM_DMA_MIN_B` | 1536 B | 78.5 to 104.6 KB | keep | high |
| `MEM_PSRAM_FREE_B` | 8 MiB | 25.7 to 27.5 MiB | keep | high |
| `STK_ADC_MIN_B` | 1024 B | 984 to 2632 B (984 only on one old image) | keep | medium (thin margin) |
| `STK_UI_MIN_B` | 2048 B | 7860 to 11068 B | keep | high |
| `REST_XY_MAX_MV` | 40 mV | X 3 to 15, Y 1 to 13 | keep | low, to-measure on boards 1, 3 |
| `REST_TWIST_MAX_MV` | 50 mV | 0 to 25 | keep | low, to-measure on boards 1, 3 |
| `STILL_XY_MAX_MV` | 30 mV | X 0 to 14, Y 0 to 13 | keep | low, to-measure on boards 1, 3 |
| `STILL_TWIST_MAX_MV` | 120 mV | 17 to 30 | keep (60 is possible later) | low, to-measure on boards 1, 3 |

(`CAL_MIN_HALF_SPAN_MV` is not D4 and is not touched. X and Y share one constant each.)

Surprises worth knowing:

1. The ADC cycle is 35.0 ms, not the 33 ms in the comment. 25 cycles is 0.87 s, below the B2
   ask of "about 1 s". 30 cycles fixes that.
2. Two old runs (image `a6ff6de`, 6 Oct 23:46 and 23:53) had 984 B of ADC stack free. The
   placeholder 1024 B would have failed them. That is the limit doing its job. Two stress runs
   sit at 1188 B. Everything else is 1348 B or more.
3. Every run used the same stick calibration (X 1507, Y 1510, twist 1477 mV). So the "rest
   offset" numbers describe one stick at rest against one calibration. They say little about
   boards 1 and 3.
4. `mem.int_block` never moves from 62 KB. It looks tied to the 64 KB internal DMA pool, not to
   heap health. It is a coarse check; 12 KiB only catches a collapse.
5. One boot (run `simmodes2-full-sim`, B2 failed) read an empty I2C scan: every probe timed
   out and the scan took 5.9 s. `i2c.missing` would catch that. Extra addresses are ignored by
   the check (seen once: 0x08, on a heap-map debug build).

## Method

- `characterise.py` reads `selftest.json` from each result directory and the boot logs.
- Memory values are "least since boot" at the end of the self test. Some runs came after a
  drive session, which lowers them. That is the worst case, and POST runs earlier, so POST
  will see more free memory than these numbers.
- Units: the self test reports `mem.int_min`, `mem.int_block` and `mem.psram_free` in whole KB.
  The script multiplies by 1024 to compare with the byte constants. One KB of rounding.
- Stick numbers (`joy.*_cal_off`, `joy.*_noise`) are over a 6 s window (171 to 172 cycles).
  POST judges a window of about 30 cycles. A shorter window can only show a smaller
  peak-to-peak, so the noise numbers here are the conservative side.
- Margin rules:
  - Least free (memory, stack): keep the placeholder if it is at least 3 times below the worst
    clean value, because POST is a fault floor, not a regression detector. Raising it to
    "half of worst seen" would make every new feature a possible false POST failure.
  - Most offset or noise (stick): keep the placeholder if it is at least 2 times above the
    worst clean value. No tightening before boards 1 and 3 are measured.
- Trade-off used throughout: a failed POST latches (hardware checks) and stops motion, so a
  false failure is costly. A missed marginal memory fault is cheap, because memory is not a
  motion hazard until it is nearly gone. So the rule leans to wide limits.

## Exclusions

- 85 of 150 directories have a `selftest.json` and are used. The rest (65) are excluded:
  - 4 where B2 failed: `draft-stick-4907b28-20261006-113147`, `s2-draft-boot-20261006-114348`,
    `s2-final-boot-20261006-114853`, `simmodes2-full-sim-20261006-182025`. They have no
    `selftest.json`. The first three failed on a settings baseline marker (theme), not on
    hardware. The fourth is the empty I2C scan.
  - 4 where B2 was not run: `dry-e2047a4-20261006-020333`, two `dt-flash-ref-1-*` runs, and
    the heap-debug graded run `heapdbg-0ae2bb5-graded-20261007-012022`.
  - 40 flash, verify, stack-repeat and sim-mode runs with no self-test step.
  - 17 boot-only directories with no `summary.json` (`heapdbg-*`, `heapmap-*`, `hp1` to `hp5`,
    `hpab-*`).
- Debug or broken images: no kept self test comes from one (none has a debug marker in its
  firmware string or name). For the boot logs, these directories are excluded from the I2C
  count: `heapdbg-0ae2bb5-boots`, `heapmap-0ae2bb5-boots`, `hp5-dbg-b1`, `hp5-dbg-b3` (debug
  builds); `hp1-flood-exact`, `hp3-flood` (build 58646ef, heap bug); `hpab-unfixed-exact`
  (build 380a594, no fix). Their 136 scans would agree (135 full, 1 with an extra 0x08).
  `heapdbg` boots show no scan line at all (guru meditation).
- Kept on purpose: two `dirty` builds (`baseline-bench`, `drive-5ce39a6`); runs where another
  check failed (`rtps.rtt_*` on network load, `sys.clean_reset` once). Those checks are not
  D4 quantities and their D4 numbers look normal.
- Runs repeat: some directories are repeated polls of the same boot (for example
  `s2-final-b3-2` to `-5` have identical memory). The distinct counts are in the JSON.

## Per limit

### WINDOW_MIN_SAMPLES: 25, propose 30

- Measured: cycle 34,998 to 35,123 us (n=85, median 35,027). Slowest single cycle 35.5 to
  39.0 ms. 25 cycles = 0.87 s. 30 cycles = 1.05 s. The spread is 0.4 %, so the time is stable.
- Reason: B2 asks for about 1 s of rest statistics. 30 cycles meets it; 25 does not.
- Trade-off: a fewer-than-limit window is PENDING, never FAIL. The cost of 30 is 0.18 s more
  wait at boot. The gain is a statistic over a full second. No false-failure risk.
- Note: the placeholder comment says 33 ms. Fix the comment when the value changes.
- Related: this limit also gates `ADC_VALID`, `REST_*`, `STILL_*` and the button check.

### ADC_VALID_MIN_PERMILLE: 990, keep

- Measured: 171 of 171 or 172 of 172 valid cycles in all 85 runs. No failed read in about
  14,600 cycles. A 95 % upper bound on the failure rate is 0.02 %.
- At 30 cycles, 990 allows no failed read (one bad read is 966 per mille). Because the check
  latches, one transient bad read would hold the POST in FAIL until reset.
- Why keep: there is no failure data at all, so any looser value is a guess. A real cable or
  ADC fault gives many bad reads. Strict costs nothing on the evidence so far.
- Open: if field logs show isolated single bad reads, loosen to 950 (allows 1 in 30). Medium
  confidence only because a real-fault signature has never been seen.

### EXPECTED_I2C: 11 addresses, keep

- Measured: the self test lists `10 28 32 36 40 41 43 44 55 5a 68` in 85/85 runs. Boot scans
  (kept directories): 143 identical, all 11.
- Five more full scans come from B2-failed runs, and 1 empty scan (`simmodes2-full-sim`):
  probe timeout on every address, 5.9 s long. That shows the check catches a hung bus.
- 0x4a (DA7280) is never present on board 2 and is correctly not in the list.
- Board-specific: the README says board 1 has no haptic at 0x5a. Required vs optional is not
  split yet. Mark to-measure on boards 1 and 3: take the boot scan and split the list.

### MEM_INT_MIN_B: 12 KiB, keep

- Measured (least free since boot): 117 KB to 143 KB, median 140 KB. Lowest: `stress-tasks`
  runs (`b677b01`, 117 KB). Highest: `baseline-bench` (143 KB).
- 12 KiB is about 10 times below the worst. Keep. A tighter value would fail on a feature
  that adds 50 KB, which is not a fault.

### MEM_INT_BLOCK_B: 12 KiB, keep

- Measured: 62 KB in every run (one distinct value, 85 runs). Looks like the reserved 64 KB
  pool, so it does not track fragmentation of the general heap.
- Keep 12 KiB. A value near 32 KiB would also pass every run, but gives no evidence-based
  extra protection. Revisit if a real fragmentation case appears.

### MEM_DMA_MIN_B: 1536 B, keep

- Measured: 80,399 B (`stress-tasks`) to 107,111 B (`baseline-bench`), median 104,691 B.
- 1536 B is 52 times below the worst. It is a real exhaustion floor. Keep.

### MEM_PSRAM_FREE_B: 8 MiB, keep

- Measured: 26,314 to 28,147 KB (25.7 to 27.5 MiB). The low values (26,314 and 26,322 KB)
  come from runs after a UI walk (`stress-tasks`, `diag-m3`, `s2-draft-b3`).
- 8 MiB is 3.2 times below the worst. Keep.

### STK_ADC_MIN_B: 1024 B, keep (thin margin)

- Measured (stack never used): 984 B (2 runs, image `a6ff6de`), 1188 B (2, `stress-tasks`
  `b677b01`), then 1348 to 1528 B for most runs, and 2024 to 2632 B for a second group.
  Median 1492 B. The two groups depend on which path the ADC task ran, not on noise.
- 1024 B sits 164 B under the lowest accepted run. It would have failed `a6ff6de`, which
  later images cleared (1524 B on `aa726ee`). Keep 1024 B.
- This is the thinnest memory margin. The value moves with ADC task code changes, so re-run
  this script after any change to that task. Do not raise it on this data: the cost of a
  false latched failure is higher than the benefit.

### STK_UI_MIN_B: 2048 B, keep

- Measured: 7,860 B (images `m3-8cff49a`, `4907b28`, after a UI walk) to 11,068 B; median
  11,060 B. 2048 B is 3.8 times below the worst. Keep.

### REST_XY_MAX_MV: 40 mV, keep (to-measure on boards 1, 3)

- Measured: X 3 to 15 mV (median 10), Y 1 to 13 mV (median 3). Max X in
  `guardsmerge-cb9226e`, max Y in `drive-5ce39a6`.
- 40 mV is 2.7 times the worst. It is inside the 10 % stick dead zone (the `static_assert`).
- One stick, one calibration (all runs). The offset is the stick's own rest position against
  that calibration, so another stick or calibration may differ. To measure on boards 1 and 3,
  with their own calibration.

### REST_TWIST_MAX_MV: 50 mV, keep (to-measure on boards 1, 3)

- Measured: 0 to 25 mV, median 3. Values of 24 to 25 mV cluster in four runs (`cmp1` to
  `cmp4`, 6 Oct 13:30, twist at rest 1502 mV against centre 1477 mV). Otherwise 0 to 16 mV.
- 50 mV is 2.0 times the worst and inside the 60 mV twist dead band. This is the tightest
  stick limit. Keep, and watch it on the other boards.

### STILL_XY_MAX_MV: 30 mV, keep (to-measure on boards 1, 3)

- Measured peak-to-peak: X 0 to 14 mV, Y 0 to 13 mV, median 1. The 13 to 14 mV runs
  (`cmp7-drive`, `chrome-0677bb5`) cause unknown (a touch on the stick is possible). Typical is 1 to 2 mV.
- 30 mV is 2.1 times the worst, and well above the typical value. Keep.

### STILL_TWIST_MAX_MV: 120 mV, keep (to-measure on boards 1, 3)

- Measured peak-to-peak: 17 to 30 mV, median 23. The comment says about 60 mV; board 2 shows
  half of that.
- 120 mV is 4 times the worst. A tighter 60 mV is possible, but the twist pot is the
  noisiest part and other boards may use another pot or ADC channel. Keep 120 until boards 1
  and 3 are measured.

## What is still missing

- Boards 1 and 3: all stick limits, the I2C required/optional split, and the stack numbers
  if their firmware differs.
- A failing ADC (bad reads), to know what a real fault looks like for `ADC_VALID`.
- POST-time measurements. The data are self-test values taken about 95 s after boot, not the
  values POST sees in its first seconds.
- Conditions not on this bench: SD card present, WiFi off, a hot board, a battery at the low
  end.
