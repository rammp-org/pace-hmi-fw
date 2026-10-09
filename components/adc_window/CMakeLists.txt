# adc_window: the continuous ADC's window bookkeeping that hazard fix C2 adds to espp's
# ContinuousAdc (window maximum, sequence, lock-free publish). README.md says what is in it.
#
# Header-only and pure C++ (no ESP-IDF), so an INTERFACE library: its includers build it with
# their own flags (espp_adc's hook lines, control, and the host app L1-CTL).
idf_component_register(INCLUDE_DIRS "include")
