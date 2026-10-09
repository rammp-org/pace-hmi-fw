# adc_window

`hmi::adc`: the continuous ADC's window bookkeeping for hazard fix C2
([hazard-c2-spec.md](../../docs/plans/hazard-c2-spec.md), REQ-CTL-16 in
`components/control/README.md`). It is our code, kept out of the vendored upstream files of
`components/espp_adc`: espp's `ContinuousAdc` update task calls it through a few hook lines
(listed in `components/espp_adc/README.md`), and the stick task reads it without a lock.

| File | What |
| --- | --- |
| `include/adc_window/adc_window.hpp` | `AdcWindowAccumulator` (one channel's sum, count and largest raw conversion in a window), `AdcWindowReading` (mean, max, sequence), `close_window()` (espp's mean arithmetic, the max through the same calibration, sequence + 1 only for a window holding a conversion of that channel), `AdcWindowBoard` (up to `kMaxChannels` channels; one writer closes and publishes every channel at once behind a sequence lock; `read()` returns one consistent snapshot of the channels asked for, or false after `kReadTries` torn attempts) |

Pure C++: no ESP-IDF, no allocation, no logging, no lock; every shared value is a lock-free
atomic. First-party: the ratchet, the fw-standards warnings, clang-format and cppcheck apply.

## Tests

CTL-024 and CTL-027 in the host app L1-CTL (`components/control/test`); 100 % lines and
branches without the sanitizers.

## Tasks

None of its own. The writer is espp's `ContinuousAdc Task`; readers are any task (the
`Read ADC` task from C2's island commit on).
