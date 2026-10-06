#pragma once
// The PIN key sequences the golden table walks, as text: R = rd_pin_reset (a BenchGateScreen
// visit), B = backspace (kRdBack), 0-9 = that digit key. Each sequence starts with R, as every
// visit does. Chosen to reach every branch of rd_keypad_cb (main/frag_bench_pin.inc:70-105):
// backspace on an empty entry and on a partial one, the fourth digit right and wrong, the digit
// after a verdict, and a visit in the middle of an entry or after a rejection.

namespace models_test {

inline constexpr const char *PIN_SEQUENCES[] = {
    "R1234",           // right first time
    "R1235",           // wrong on the fourth digit
    "R12351234",       // wrong, then right
    "RBB",             // backspace on an empty entry, twice
    "R12BBB1234",      // backspace past the start, then right
    "R00005",          // the digit after a rejection starts a new entry
    "R12345",          // the digit after an acceptance starts a new entry
    "R123B34",         // a corrected last digit, then right
    "R9999B1",         // backspace on the empty entry a rejection left keeps the notice
    "R12R3412",        // a visit in the middle of an entry empties it
    "R4321R",          // a visit after a rejection puts the prompt back
    "R12341234",       // two acceptances in one visit
    "R1B2B3B4B",       // every digit taken back
    "R0123456789",     // a sweep of every digit: two rejections and two pending
    "R2341R1243R0234", // near misses
    "R123412B34",      // backspace in a second entry after an acceptance
};

} // namespace models_test
