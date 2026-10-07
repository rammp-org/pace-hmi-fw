// GENERATED ONCE by components/stick/test/gen_golden.cpp from the legacy harness
// (legacy_adc_cycle.cpp, a verbatim copy of the ADC task's stick math at dev_refactor
// 7ea7592), then FROZEN. Never edit it to make a test pass (CORE never-list).
// Row: inputs {reset, power_on_cal, horiz_ok, vert_ok, twist_ok, horiz_mv, vert_mv,
// twist_mv, new_cal, calibrating, swap, invert_x, invert_y, sensitivity, remote_key,
// drive_speed, drives, button} then outputs {published, cmd_x, cmd_y, cmd_twist (float
// bits), buttons, bars_set, bar_x, bar_y, bar_twist, joy_key, joy_flick}.
// clang-format off
#pragma once

#include <array>
#include <cstddef>

#include "stick_vectors.hpp"

namespace stick_test {

inline constexpr std::size_t ROW_GATE_CLOSED_NEGATIVE_BUTTON = 1805;
inline constexpr std::size_t ROW_CALIBRATING_FULL_RIGHT = 1848;
inline constexpr std::size_t ROW_CALIBRATING_RELEASES_KEY = 1865;
inline constexpr std::size_t ROW_CAL_OVER_REFLICK = 1867;
inline constexpr std::size_t ROW_INVALID_HORIZ_MISSING = 1875;
inline constexpr std::size_t ROW_PENDING_CAL_APPLIED = 1878;
inline constexpr std::size_t ROW_IDEAL_2971 = 1879;
inline constexpr std::size_t ROW_BOARD2_TAKEN_2971 = 1880;
inline constexpr std::size_t ROW_REMOTE_OVERRIDES = 1884;
inline constexpr std::size_t ROW_HELD_NO_FLICK = 1888;
inline constexpr std::size_t ROW_REAIM_FLICK = 1889;
inline constexpr std::size_t ROW_OPEN_HORIZ = 1891;
inline constexpr std::size_t ROW_OPEN_VERT = 1892;
inline constexpr std::size_t ROW_OPEN_TWIST = 1893;
inline constexpr std::size_t ROW_SHORT_HORIZ = 1894;
inline constexpr std::size_t ROW_SHORT_VERT = 1895;
inline constexpr std::size_t ROW_SHORT_TWIST = 1896;
inline constexpr std::size_t ROW_OPEN_ALL = 1897;

inline constexpr std::array<StickVector, 1898> GOLDEN{{
  /*    0 sweep horizontal cal1 */
  {true, 1, true, true, true, 0.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /*    1 sweep horizontal cal1 */
  {false, 1, true, true, true, 50.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 0u},
  /*    2 sweep horizontal cal1 */
  {false, 1, true, true, true, 100.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf7cce43u, 0x00000000u, 0x00000000u, 0u, true, -98, 0, 0, 20u, 0u},
  /*    3 sweep horizontal cal1 */
  {false, 1, true, true, true, 150.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf73adddu, 0x00000000u, 0x00000000u, 0u, true, -95, 0, 0, 20u, 0u},
  /*    4 sweep horizontal cal1 */
  {false, 1, true, true, true, 200.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf6a8d76u, 0x00000000u, 0x00000000u, 0u, true, -91, 0, 0, 20u, 0u},
  /*    5 sweep horizontal cal1 */
  {false, 1, true, true, true, 250.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf616d11u, 0x00000000u, 0x00000000u, 0u, true, -88, 0, 0, 20u, 0u},
  /*    6 sweep horizontal cal1 */
  {false, 1, true, true, true, 300.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf584caau, 0x00000000u, 0x00000000u, 0u, true, -84, 0, 0, 20u, 0u},
  /*    7 sweep horizontal cal1 */
  {false, 1, true, true, true, 350.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf4f2c44u, 0x00000000u, 0x00000000u, 0u, true, -80, 0, 0, 20u, 0u},
  /*    8 sweep horizontal cal1 */
  {false, 1, true, true, true, 400.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf460bddu, 0x00000000u, 0x00000000u, 0u, true, -77, 0, 0, 20u, 0u},
  /*    9 sweep horizontal cal1 */
  {false, 1, true, true, true, 450.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf3ceb78u, 0x00000000u, 0x00000000u, 0u, true, -73, 0, 0, 20u, 0u},
  /*   10 sweep horizontal cal1 */
  {false, 1, true, true, true, 500.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf33cb11u, 0x00000000u, 0x00000000u, 0u, true, -70, 0, 0, 20u, 0u},
  /*   11 sweep horizontal cal1 */
  {false, 1, true, true, true, 550.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf2aaaabu, 0x00000000u, 0x00000000u, 0u, true, -66, 0, 0, 20u, 0u},
  /*   12 sweep horizontal cal1 */
  {false, 1, true, true, true, 600.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf218a44u, 0x00000000u, 0x00000000u, 0u, true, -63, 0, 0, 20u, 0u},
  /*   13 sweep horizontal cal1 */
  {false, 1, true, true, true, 650.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf1869dfu, 0x00000000u, 0x00000000u, 0u, true, -59, 0, 0, 20u, 0u},
  /*   14 sweep horizontal cal1 */
  {false, 1, true, true, true, 700.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf0f4978u, 0x00000000u, 0x00000000u, 0u, true, -55, 0, 0, 20u, 0u},
  /*   15 sweep horizontal cal1 */
  {false, 1, true, true, true, 750.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf062913u, 0x00000000u, 0x00000000u, 0u, true, -52, 0, 0, 20u, 0u},
  /*   16 sweep horizontal cal1 */
  {false, 1, true, true, true, 800.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbefa1158u, 0x00000000u, 0x00000000u, 0u, true, -48, 0, 0, 20u, 0u},
  /*   17 sweep horizontal cal1 */
  {false, 1, true, true, true, 850.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbee7d08cu, 0x00000000u, 0x00000000u, 0u, true, -45, 0, 0, 20u, 0u},
  /*   18 sweep horizontal cal1 */
  {false, 1, true, true, true, 900.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbed58fc0u, 0x00000000u, 0x00000000u, 0u, true, -41, 0, 0, 20u, 0u},
  /*   19 sweep horizontal cal1 */
  {false, 1, true, true, true, 950.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbec34ef3u, 0x00000000u, 0x00000000u, 0u, true, -38, 0, 0, 20u, 0u},
  /*   20 sweep horizontal cal1 */
  {false, 1, true, true, true, 1000.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbeb10e27u, 0x00000000u, 0x00000000u, 0u, true, -34, 0, 0, 20u, 0u},
  /*   21 sweep horizontal cal1 */
  {false, 1, true, true, true, 1050.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe9ecd5au, 0x00000000u, 0x00000000u, 0u, true, -31, 0, 0, 20u, 0u},
  /*   22 sweep horizontal cal1 */
  {false, 1, true, true, true, 1100.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, -27, 0, 0, 20u, 0u},
  /*   23 sweep horizontal cal1 */
  {false, 1, true, true, true, 1150.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe749782u, 0x00000000u, 0x00000000u, 0u, true, -23, 0, 0, 20u, 0u},
  /*   24 sweep horizontal cal1 */
  {false, 1, true, true, true, 1200.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe5015e9u, 0x00000000u, 0x00000000u, 0u, true, -20, 0, 0, 20u, 0u},
  /*   25 sweep horizontal cal1 */
  {false, 1, true, true, true, 1250.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe2b9450u, 0x00000000u, 0x00000000u, 0u, true, -16, 0, 0, 20u, 0u},
  /*   26 sweep horizontal cal1 */
  {false, 1, true, true, true, 1300.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe0712b6u, 0x00000000u, 0x00000000u, 0u, true, -13, 0, 0, 20u, 0u},
  /*   27 sweep horizontal cal1 */
  {false, 1, true, true, true, 1350.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbdc5223bu, 0x00000000u, 0x00000000u, 0u, true, -9, 0, 0, 20u, 0u},
  /*   28 sweep horizontal cal1 */
  {false, 1, true, true, true, 1400.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbd783e11u, 0x00000000u, 0x00000000u, 0u, true, -6, 0, 0, 20u, 0u},
  /*   29 sweep horizontal cal1 */
  {false, 1, true, true, true, 1450.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbccc6f5au, 0x00000000u, 0x00000000u, 0u, true, -2, 0, 0, 20u, 0u},
  /*   30 sweep horizontal cal1 */
  {false, 1, true, true, true, 1500.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*   31 sweep horizontal cal1 */
  {false, 1, true, true, true, 1550.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*   32 sweep horizontal cal1 */
  {false, 1, true, true, true, 1600.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*   33 sweep horizontal cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*   34 sweep horizontal cal1 */
  {false, 1, true, true, true, 1700.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*   35 sweep horizontal cal1 */
  {false, 1, true, true, true, 1750.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*   36 sweep horizontal cal1 */
  {false, 1, true, true, true, 1800.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*   37 sweep horizontal cal1 */
  {false, 1, true, true, true, 1850.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ccc6f5au, 0x00000000u, 0x00000000u, 0u, true, 2, 0, 0, 0u, 0u},
  /*   38 sweep horizontal cal1 */
  {false, 1, true, true, true, 1900.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3d783e11u, 0x00000000u, 0x00000000u, 0u, true, 6, 0, 0, 19u, 19u},
  /*   39 sweep horizontal cal1 */
  {false, 1, true, true, true, 1950.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3dc5223bu, 0x00000000u, 0x00000000u, 0u, true, 9, 0, 0, 19u, 0u},
  /*   40 sweep horizontal cal1 */
  {false, 1, true, true, true, 2000.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e0712b6u, 0x00000000u, 0x00000000u, 0u, true, 13, 0, 0, 19u, 0u},
  /*   41 sweep horizontal cal1 */
  {false, 1, true, true, true, 2050.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e2b9450u, 0x00000000u, 0x00000000u, 0u, true, 16, 0, 0, 19u, 0u},
  /*   42 sweep horizontal cal1 */
  {false, 1, true, true, true, 2100.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e5015e9u, 0x00000000u, 0x00000000u, 0u, true, 20, 0, 0, 19u, 0u},
  /*   43 sweep horizontal cal1 */
  {false, 1, true, true, true, 2150.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e749782u, 0x00000000u, 0x00000000u, 0u, true, 23, 0, 0, 19u, 0u},
  /*   44 sweep horizontal cal1 */
  {false, 1, true, true, true, 2200.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /*   45 sweep horizontal cal1 */
  {false, 1, true, true, true, 2250.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e9ecd5au, 0x00000000u, 0x00000000u, 0u, true, 31, 0, 0, 19u, 0u},
  /*   46 sweep horizontal cal1 */
  {false, 1, true, true, true, 2300.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3eb10e27u, 0x00000000u, 0x00000000u, 0u, true, 34, 0, 0, 19u, 0u},
  /*   47 sweep horizontal cal1 */
  {false, 1, true, true, true, 2350.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ec34ef3u, 0x00000000u, 0x00000000u, 0u, true, 38, 0, 0, 19u, 0u},
  /*   48 sweep horizontal cal1 */
  {false, 1, true, true, true, 2400.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ed58fc0u, 0x00000000u, 0x00000000u, 0u, true, 41, 0, 0, 19u, 0u},
  /*   49 sweep horizontal cal1 */
  {false, 1, true, true, true, 2450.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ee7d08cu, 0x00000000u, 0x00000000u, 0u, true, 45, 0, 0, 19u, 0u},
  /*   50 sweep horizontal cal1 */
  {false, 1, true, true, true, 2500.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3efa1158u, 0x00000000u, 0x00000000u, 0u, true, 48, 0, 0, 19u, 0u},
  /*   51 sweep horizontal cal1 */
  {false, 1, true, true, true, 2550.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f062913u, 0x00000000u, 0x00000000u, 0u, true, 52, 0, 0, 19u, 0u},
  /*   52 sweep horizontal cal1 */
  {false, 1, true, true, true, 2600.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f0f4978u, 0x00000000u, 0x00000000u, 0u, true, 55, 0, 0, 19u, 0u},
  /*   53 sweep horizontal cal1 */
  {false, 1, true, true, true, 2650.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f1869dfu, 0x00000000u, 0x00000000u, 0u, true, 59, 0, 0, 19u, 0u},
  /*   54 sweep horizontal cal1 */
  {false, 1, true, true, true, 2700.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f218a44u, 0x00000000u, 0x00000000u, 0u, true, 63, 0, 0, 19u, 0u},
  /*   55 sweep horizontal cal1 */
  {false, 1, true, true, true, 2750.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /*   56 sweep horizontal cal1 */
  {false, 1, true, true, true, 2800.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f33cb11u, 0x00000000u, 0x00000000u, 0u, true, 70, 0, 0, 19u, 0u},
  /*   57 sweep horizontal cal1 */
  {false, 1, true, true, true, 2850.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f3ceb78u, 0x00000000u, 0x00000000u, 0u, true, 73, 0, 0, 19u, 0u},
  /*   58 sweep horizontal cal1 */
  {false, 1, true, true, true, 2900.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f460bddu, 0x00000000u, 0x00000000u, 0u, true, 77, 0, 0, 19u, 0u},
  /*   59 sweep horizontal cal1 */
  {false, 1, true, true, true, 2950.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f4f2c44u, 0x00000000u, 0x00000000u, 0u, true, 80, 0, 0, 19u, 0u},
  /*   60 sweep horizontal cal1 */
  {false, 1, true, true, true, 3000.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f584caau, 0x00000000u, 0x00000000u, 0u, true, 84, 0, 0, 19u, 0u},
  /*   61 sweep horizontal cal1 */
  {false, 1, true, true, true, 3050.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f616d11u, 0x00000000u, 0x00000000u, 0u, true, 88, 0, 0, 19u, 0u},
  /*   62 sweep horizontal cal1 */
  {false, 1, true, true, true, 3100.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f6a8d76u, 0x00000000u, 0x00000000u, 0u, true, 91, 0, 0, 19u, 0u},
  /*   63 sweep horizontal cal1 */
  {false, 1, true, true, true, 3150.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f73adddu, 0x00000000u, 0x00000000u, 0u, true, 95, 0, 0, 19u, 0u},
  /*   64 sweep horizontal cal1 */
  {false, 1, true, true, true, 3200.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f7cce43u, 0x00000000u, 0x00000000u, 0u, true, 98, 0, 0, 19u, 0u},
  /*   65 sweep horizontal cal1 */
  {false, 1, true, true, true, 3250.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /*   66 sweep horizontal cal1 */
  {false, 1, true, true, true, 3300.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /*   67 sweep horizontal cal1 */
  {false, 1, true, true, true, 1.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /*   68 sweep horizontal cal1 */
  {false, 1, true, true, true, 5.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 0u},
  /*   69 sweep horizontal cal1 */
  {false, 1, true, true, true, 6.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 0u},
  /*   70 sweep horizontal cal1 */
  {false, 1, true, true, true, 10.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 0u},
  /*   71 sweep horizontal cal1 */
  {false, 1, true, true, true, 11.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 0u},
  /*   72 sweep horizontal cal1 */
  {false, 1, true, true, true, 12.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 0u},
  /*   73 sweep horizontal cal1 */
  {false, 1, true, true, true, 1477.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbbbae96au, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*   74 sweep horizontal cal1 */
  {false, 1, true, true, true, 1507.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*   75 sweep horizontal cal1 */
  {false, 1, true, true, true, 1510.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*   76 sweep horizontal cal1 */
  {false, 1, true, true, true, 2960.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f50ff8cu, 0x00000000u, 0x00000000u, 0u, true, 81, 0, 0, 19u, 19u},
  /*   77 sweep horizontal cal1 */
  {false, 1, true, true, true, 2962.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f515d00u, 0x00000000u, 0x00000000u, 0u, true, 81, 0, 0, 19u, 0u},
  /*   78 sweep horizontal cal1 */
  {false, 1, true, true, true, 2971.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f53018eu, 0x00000000u, 0x00000000u, 0u, true, 82, 0, 0, 19u, 0u},
  /*   79 sweep horizontal cal1 */
  {false, 1, true, true, true, 2972.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f533048u, 0x00000000u, 0x00000000u, 0u, true, 82, 0, 0, 19u, 0u},
  /*   80 sweep horizontal cal1 */
  {false, 1, true, true, true, 3000.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f584caau, 0x00000000u, 0x00000000u, 0u, true, 84, 0, 0, 19u, 0u},
  /*   81 sweep vertical cal1 */
  {true, 1, true, true, true, 1650.0f, 0.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /*   82 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 50.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /*   83 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 100.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f7cce43u, 0x00000000u, 0u, true, 0, 98, 0, 17u, 0u},
  /*   84 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 150.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f73adddu, 0x00000000u, 0u, true, 0, 95, 0, 17u, 0u},
  /*   85 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 200.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f6a8d76u, 0x00000000u, 0u, true, 0, 91, 0, 17u, 0u},
  /*   86 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 250.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f616d11u, 0x00000000u, 0u, true, 0, 88, 0, 17u, 0u},
  /*   87 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f584caau, 0x00000000u, 0u, true, 0, 84, 0, 17u, 0u},
  /*   88 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 350.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f4f2c44u, 0x00000000u, 0u, true, 0, 80, 0, 17u, 0u},
  /*   89 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 400.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f460bddu, 0x00000000u, 0u, true, 0, 77, 0, 17u, 0u},
  /*   90 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 450.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f3ceb78u, 0x00000000u, 0u, true, 0, 73, 0, 17u, 0u},
  /*   91 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 500.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f33cb11u, 0x00000000u, 0u, true, 0, 70, 0, 17u, 0u},
  /*   92 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 550.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /*   93 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 600.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f218a44u, 0x00000000u, 0u, true, 0, 63, 0, 17u, 0u},
  /*   94 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f1869dfu, 0x00000000u, 0u, true, 0, 59, 0, 17u, 0u},
  /*   95 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 700.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f0f4978u, 0x00000000u, 0u, true, 0, 55, 0, 17u, 0u},
  /*   96 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 750.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f062913u, 0x00000000u, 0u, true, 0, 52, 0, 17u, 0u},
  /*   97 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 800.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3efa1158u, 0x00000000u, 0u, true, 0, 48, 0, 17u, 0u},
  /*   98 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 850.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3ee7d08cu, 0x00000000u, 0u, true, 0, 45, 0, 17u, 0u},
  /*   99 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 900.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3ed58fc0u, 0x00000000u, 0u, true, 0, 41, 0, 17u, 0u},
  /*  100 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 950.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3ec34ef3u, 0x00000000u, 0u, true, 0, 38, 0, 17u, 0u},
  /*  101 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1000.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3eb10e27u, 0x00000000u, 0u, true, 0, 34, 0, 17u, 0u},
  /*  102 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1050.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3e9ecd5au, 0x00000000u, 0u, true, 0, 31, 0, 17u, 0u},
  /*  103 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1100.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8eu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /*  104 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1150.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3e749782u, 0x00000000u, 0u, true, 0, 23, 0, 17u, 0u},
  /*  105 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1200.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3e5015e9u, 0x00000000u, 0u, true, 0, 20, 0, 17u, 0u},
  /*  106 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1250.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3e2b9450u, 0x00000000u, 0u, true, 0, 16, 0, 17u, 0u},
  /*  107 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3e0712b6u, 0x00000000u, 0u, true, 0, 13, 0, 17u, 0u},
  /*  108 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1350.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3dc5223bu, 0x00000000u, 0u, true, 0, 9, 0, 17u, 0u},
  /*  109 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1400.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3d783e11u, 0x00000000u, 0u, true, 0, 6, 0, 17u, 0u},
  /*  110 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1450.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3ccc6f5au, 0x00000000u, 0u, true, 0, 2, 0, 17u, 0u},
  /*  111 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1500.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  112 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1550.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  113 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1600.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  114 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  115 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1700.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  116 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1750.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  117 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1800.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  118 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1850.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbccc6f5au, 0x00000000u, 0u, true, 0, -2, 0, 0u, 0u},
  /*  119 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1900.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbd783e11u, 0x00000000u, 0u, true, 0, -6, 0, 18u, 18u},
  /*  120 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1950.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbdc5223bu, 0x00000000u, 0u, true, 0, -9, 0, 18u, 0u},
  /*  121 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2000.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbe0712b6u, 0x00000000u, 0u, true, 0, -13, 0, 18u, 0u},
  /*  122 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2050.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbe2b9450u, 0x00000000u, 0u, true, 0, -16, 0, 18u, 0u},
  /*  123 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2100.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbe5015e9u, 0x00000000u, 0u, true, 0, -20, 0, 18u, 0u},
  /*  124 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2150.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbe749782u, 0x00000000u, 0u, true, 0, -23, 0, 18u, 0u},
  /*  125 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2200.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbe8c8c8eu, 0x00000000u, 0u, true, 0, -27, 0, 18u, 0u},
  /*  126 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2250.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbe9ecd5au, 0x00000000u, 0u, true, 0, -31, 0, 18u, 0u},
  /*  127 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbeb10e27u, 0x00000000u, 0u, true, 0, -34, 0, 18u, 0u},
  /*  128 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2350.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbec34ef3u, 0x00000000u, 0u, true, 0, -38, 0, 18u, 0u},
  /*  129 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2400.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbed58fc0u, 0x00000000u, 0u, true, 0, -41, 0, 18u, 0u},
  /*  130 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2450.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbee7d08cu, 0x00000000u, 0u, true, 0, -45, 0, 18u, 0u},
  /*  131 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2500.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbefa1158u, 0x00000000u, 0u, true, 0, -48, 0, 18u, 0u},
  /*  132 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2550.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf062913u, 0x00000000u, 0u, true, 0, -52, 0, 18u, 0u},
  /*  133 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2600.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf0f4978u, 0x00000000u, 0u, true, 0, -55, 0, 18u, 0u},
  /*  134 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf1869dfu, 0x00000000u, 0u, true, 0, -59, 0, 18u, 0u},
  /*  135 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2700.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf218a44u, 0x00000000u, 0u, true, 0, -63, 0, 18u, 0u},
  /*  136 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2750.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf2aaaabu, 0x00000000u, 0u, true, 0, -66, 0, 18u, 0u},
  /*  137 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2800.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf33cb11u, 0x00000000u, 0u, true, 0, -70, 0, 18u, 0u},
  /*  138 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2850.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf3ceb78u, 0x00000000u, 0u, true, 0, -73, 0, 18u, 0u},
  /*  139 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2900.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf460bddu, 0x00000000u, 0u, true, 0, -77, 0, 18u, 0u},
  /*  140 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2950.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf4f2c44u, 0x00000000u, 0u, true, 0, -80, 0, 18u, 0u},
  /*  141 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 3000.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf584caau, 0x00000000u, 0u, true, 0, -84, 0, 18u, 0u},
  /*  142 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 3050.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf616d11u, 0x00000000u, 0u, true, 0, -88, 0, 18u, 0u},
  /*  143 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 3100.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf6a8d76u, 0x00000000u, 0u, true, 0, -91, 0, 18u, 0u},
  /*  144 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 3150.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf73adddu, 0x00000000u, 0u, true, 0, -95, 0, 18u, 0u},
  /*  145 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 3200.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf7cce43u, 0x00000000u, 0u, true, 0, -98, 0, 18u, 0u},
  /*  146 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 3250.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 0u},
  /*  147 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 3300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 0u},
  /*  148 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /*  149 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 5.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /*  150 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 6.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /*  151 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 10.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /*  152 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 11.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /*  153 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 12.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /*  154 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1477.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3bbae96au, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  155 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1507.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  156 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 1510.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  157 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2960.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf50ff8cu, 0x00000000u, 0u, true, 0, -81, 0, 18u, 18u},
  /*  158 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2962.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf515d00u, 0x00000000u, 0u, true, 0, -81, 0, 18u, 0u},
  /*  159 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2971.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf53018eu, 0x00000000u, 0u, true, 0, -82, 0, 18u, 0u},
  /*  160 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 2972.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf533048u, 0x00000000u, 0u, true, 0, -82, 0, 18u, 0u},
  /*  161 sweep vertical cal1 */
  {false, 1, true, true, true, 1650.0f, 3000.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf584caau, 0x00000000u, 0u, true, 0, -84, 0, 18u, 0u},
  /*  162 sweep twist cal1 */
  {true, 1, true, true, true, 1650.0f, 1650.0f, 0.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf800000u, 0u, true, 0, 0, -100, 0u, 0u},
  /*  163 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 50.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf7e5930u, 0u, true, 0, 0, -99, 0u, 0u},
  /*  164 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 100.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf76171fu, 0u, true, 0, 0, -96, 0u, 0u},
  /*  165 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 150.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf6dd50fu, 0u, true, 0, 0, -92, 0u, 0u},
  /*  166 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 200.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf6592feu, 0u, true, 0, 0, -89, 0u, 0u},
  /*  167 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 250.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf5d50eeu, 0u, true, 0, 0, -86, 0u, 0u},
  /*  168 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 300.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf550eddu, 0u, true, 0, 0, -83, 0u, 0u},
  /*  169 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 350.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf4ccccdu, 0u, true, 0, 0, -80, 0u, 0u},
  /*  170 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 400.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf448abcu, 0u, true, 0, 0, -76, 0u, 0u},
  /*  171 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 450.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf3c48acu, 0u, true, 0, 0, -73, 0u, 0u},
  /*  172 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 500.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf34069bu, 0u, true, 0, 0, -70, 0u, 0u},
  /*  173 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 550.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf2bc48bu, 0u, true, 0, 0, -67, 0u, 0u},
  /*  174 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 600.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf23827au, 0u, true, 0, 0, -63, 0u, 0u},
  /*  175 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf1b406au, 0u, true, 0, 0, -60, 0u, 0u},
  /*  176 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 700.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf12fe59u, 0u, true, 0, 0, -57, 0u, 0u},
  /*  177 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 750.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf0abc49u, 0u, true, 0, 0, -54, 0u, 0u},
  /*  178 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 800.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf027a38u, 0u, true, 0, 0, -50, 0u, 0u},
  /*  179 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 850.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbef4704fu, 0u, true, 0, 0, -47, 0u, 0u},
  /*  180 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 900.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbee3ec2eu, 0u, true, 0, 0, -44, 0u, 0u},
  /*  181 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 950.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbed3680du, 0u, true, 0, 0, -41, 0u, 0u},
  /*  182 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1000.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbec2e3ecu, 0u, true, 0, 0, -38, 0u, 0u},
  /*  183 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1050.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbeb25fcbu, 0u, true, 0, 0, -34, 0u, 0u},
  /*  184 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1100.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbea1dbaau, 0u, true, 0, 0, -31, 0u, 0u},
  /*  185 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1150.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbe915789u, 0u, true, 0, 0, -28, 0u, 0u},
  /*  186 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1200.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbe80d368u, 0u, true, 0, 0, -25, 0u, 0u},
  /*  187 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1250.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbe609e8eu, 0u, true, 0, 0, -21, 0u, 0u},
  /*  188 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1300.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbe3f964cu, 0u, true, 0, 0, -18, 0u, 0u},
  /*  189 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1350.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbe1e8e0au, 0u, true, 0, 0, -15, 0u, 0u},
  /*  190 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1400.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbdfb0b90u, 0u, true, 0, 0, -12, 0u, 0u},
  /*  191 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1450.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbdb8fb0cu, 0u, true, 0, 0, -9, 0u, 0u},
  /*  192 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1500.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbd6dd50fu, 0u, true, 0, 0, -5, 0u, 0u},
  /*  193 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1550.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbcd3680du, 0u, true, 0, 0, -2, 0u, 0u},
  /*  194 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1600.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  195 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  196 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1700.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  197 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1750.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3cd3680du, 0u, true, 0, 0, 2, 0u, 0u},
  /*  198 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1800.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3d6dd50fu, 0u, true, 0, 0, 5, 0u, 0u},
  /*  199 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1850.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3db8fb0cu, 0u, true, 0, 0, 9, 0u, 0u},
  /*  200 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1900.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3dfb0b90u, 0u, true, 0, 0, 12, 0u, 0u},
  /*  201 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1950.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3e1e8e0au, 0u, true, 0, 0, 15, 0u, 0u},
  /*  202 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2000.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3e3f964cu, 0u, true, 0, 0, 18, 0u, 0u},
  /*  203 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2050.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3e609e8eu, 0u, true, 0, 0, 21, 0u, 0u},
  /*  204 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2100.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3e80d368u, 0u, true, 0, 0, 25, 0u, 0u},
  /*  205 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2150.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3e915789u, 0u, true, 0, 0, 28, 0u, 0u},
  /*  206 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2200.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3ea1dbaau, 0u, true, 0, 0, 31, 0u, 0u},
  /*  207 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2250.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3eb25fcbu, 0u, true, 0, 0, 34, 0u, 0u},
  /*  208 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2300.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3ec2e3ecu, 0u, true, 0, 0, 38, 0u, 0u},
  /*  209 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2350.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3ed3680du, 0u, true, 0, 0, 41, 0u, 0u},
  /*  210 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2400.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3ee3ec2eu, 0u, true, 0, 0, 44, 0u, 0u},
  /*  211 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2450.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3ef4704fu, 0u, true, 0, 0, 47, 0u, 0u},
  /*  212 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2500.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f027a38u, 0u, true, 0, 0, 50, 0u, 0u},
  /*  213 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2550.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f0abc49u, 0u, true, 0, 0, 54, 0u, 0u},
  /*  214 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2600.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f12fe59u, 0u, true, 0, 0, 57, 0u, 0u},
  /*  215 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f1b406au, 0u, true, 0, 0, 60, 0u, 0u},
  /*  216 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2700.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f23827au, 0u, true, 0, 0, 63, 0u, 0u},
  /*  217 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2750.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f2bc48bu, 0u, true, 0, 0, 67, 0u, 0u},
  /*  218 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2800.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f34069bu, 0u, true, 0, 0, 70, 0u, 0u},
  /*  219 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2850.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f3c48acu, 0u, true, 0, 0, 73, 0u, 0u},
  /*  220 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2900.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f448abcu, 0u, true, 0, 0, 76, 0u, 0u},
  /*  221 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2950.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f4ccccdu, 0u, true, 0, 0, 80, 0u, 0u},
  /*  222 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 3000.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f550eddu, 0u, true, 0, 0, 83, 0u, 0u},
  /*  223 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 3050.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f5d50eeu, 0u, true, 0, 0, 86, 0u, 0u},
  /*  224 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 3100.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f6592feu, 0u, true, 0, 0, 89, 0u, 0u},
  /*  225 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 3150.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f6dd50fu, 0u, true, 0, 0, 92, 0u, 0u},
  /*  226 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 3200.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f76171fu, 0u, true, 0, 0, 96, 0u, 0u},
  /*  227 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 3250.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f7e5930u, 0u, true, 0, 0, 99, 0u, 0u},
  /*  228 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 3300.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /*  229 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf800000u, 0u, true, 0, 0, -100, 0u, 0u},
  /*  230 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 5.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf800000u, 0u, true, 0, 0, -100, 0u, 0u},
  /*  231 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 6.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf800000u, 0u, true, 0, 0, -100, 0u, 0u},
  /*  232 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 10.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf800000u, 0u, true, 0, 0, -100, 0u, 0u},
  /*  233 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 11.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf800000u, 0u, true, 0, 0, -100, 0u, 0u},
  /*  234 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 12.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf800000u, 0u, true, 0, 0, -100, 0u, 0u},
  /*  235 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbd954e49u, 0u, true, 0, 0, -7, 0u, 0u},
  /*  236 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1507.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbd5b558eu, 0u, true, 0, 0, -5, 0u, 0u},
  /*  237 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 1510.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbd53680du, 0u, true, 0, 0, -5, 0u, 0u},
  /*  238 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2960.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f4e739du, 0u, true, 0, 0, 80, 0u, 0u},
  /*  239 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2962.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f4ec82du, 0u, true, 0, 0, 80, 0u, 0u},
  /*  240 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2971.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f5044b5u, 0u, true, 0, 0, 81, 0u, 0u},
  /*  241 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 2972.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f506efdu, 0u, true, 0, 0, 81, 0u, 0u},
  /*  242 sweep twist cal1 */
  {false, 1, true, true, true, 1650.0f, 1650.0f, 3000.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f550eddu, 0u, true, 0, 0, 83, 0u, 0u},
  /*  243 sweep horizontal cal2 */
  {true, 2, true, true, true, 0.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /*  244 sweep horizontal cal2 */
  {false, 2, true, true, true, 50.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 0u},
  /*  245 sweep horizontal cal2 */
  {false, 2, true, true, true, 100.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf7d2428u, 0x00000000u, 0x00000000u, 0u, true, -98, 0, 0, 20u, 0u},
  /*  246 sweep horizontal cal2 */
  {false, 2, true, true, true, 150.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf73133fu, 0x00000000u, 0x00000000u, 0u, true, -94, 0, 0, 20u, 0u},
  /*  247 sweep horizontal cal2 */
  {false, 2, true, true, true, 200.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf690257u, 0x00000000u, 0x00000000u, 0u, true, -91, 0, 0, 20u, 0u},
  /*  248 sweep horizontal cal2 */
  {false, 2, true, true, true, 250.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf5ef16du, 0x00000000u, 0x00000000u, 0u, true, -87, 0, 0, 20u, 0u},
  /*  249 sweep horizontal cal2 */
  {false, 2, true, true, true, 300.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf54e084u, 0x00000000u, 0x00000000u, 0u, true, -83, 0, 0, 20u, 0u},
  /*  250 sweep horizontal cal2 */
  {false, 2, true, true, true, 350.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf4acf9au, 0x00000000u, 0x00000000u, 0u, true, -79, 0, 0, 20u, 0u},
  /*  251 sweep horizontal cal2 */
  {false, 2, true, true, true, 400.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf40beb1u, 0x00000000u, 0x00000000u, 0u, true, -75, 0, 0, 20u, 0u},
  /*  252 sweep horizontal cal2 */
  {false, 2, true, true, true, 450.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf36adc7u, 0x00000000u, 0x00000000u, 0u, true, -71, 0, 0, 20u, 0u},
  /*  253 sweep horizontal cal2 */
  {false, 2, true, true, true, 500.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf2c9cdfu, 0x00000000u, 0x00000000u, 0u, true, -67, 0, 0, 20u, 0u},
  /*  254 sweep horizontal cal2 */
  {false, 2, true, true, true, 550.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf228bf6u, 0x00000000u, 0x00000000u, 0u, true, -63, 0, 0, 20u, 0u},
  /*  255 sweep horizontal cal2 */
  {false, 2, true, true, true, 600.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf187b0cu, 0x00000000u, 0x00000000u, 0u, true, -59, 0, 0, 20u, 0u},
  /*  256 sweep horizontal cal2 */
  {false, 2, true, true, true, 650.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf0e6a23u, 0x00000000u, 0x00000000u, 0u, true, -55, 0, 0, 20u, 0u},
  /*  257 sweep horizontal cal2 */
  {false, 2, true, true, true, 700.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf04593au, 0x00000000u, 0x00000000u, 0u, true, -51, 0, 0, 20u, 0u},
  /*  258 sweep horizontal cal2 */
  {false, 2, true, true, true, 750.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbef490a1u, 0x00000000u, 0x00000000u, 0u, true, -47, 0, 0, 20u, 0u},
  /*  259 sweep horizontal cal2 */
  {false, 2, true, true, true, 800.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbee06ecfu, 0x00000000u, 0x00000000u, 0u, true, -43, 0, 0, 20u, 0u},
  /*  260 sweep horizontal cal2 */
  {false, 2, true, true, true, 850.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbecc4cfeu, 0x00000000u, 0x00000000u, 0u, true, -39, 0, 0, 20u, 0u},
  /*  261 sweep horizontal cal2 */
  {false, 2, true, true, true, 900.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbeb82b2bu, 0x00000000u, 0x00000000u, 0u, true, -35, 0, 0, 20u, 0u},
  /*  262 sweep horizontal cal2 */
  {false, 2, true, true, true, 950.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbea40958u, 0x00000000u, 0x00000000u, 0u, true, -32, 0, 0, 20u, 0u},
  /*  263 sweep horizontal cal2 */
  {false, 2, true, true, true, 1000.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe8fe786u, 0x00000000u, 0x00000000u, 0u, true, -28, 0, 0, 20u, 0u},
  /*  264 sweep horizontal cal2 */
  {false, 2, true, true, true, 1050.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe778b66u, 0x00000000u, 0x00000000u, 0u, true, -24, 0, 0, 20u, 0u},
  /*  265 sweep horizontal cal2 */
  {false, 2, true, true, true, 1100.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe4f47c1u, 0x00000000u, 0x00000000u, 0u, true, -20, 0, 0, 20u, 0u},
  /*  266 sweep horizontal cal2 */
  {false, 2, true, true, true, 1150.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe27041bu, 0x00000000u, 0x00000000u, 0u, true, -16, 0, 0, 20u, 0u},
  /*  267 sweep horizontal cal2 */
  {false, 2, true, true, true, 1200.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbdfd80eeu, 0x00000000u, 0x00000000u, 0u, true, -12, 0, 0, 20u, 0u},
  /*  268 sweep horizontal cal2 */
  {false, 2, true, true, true, 1250.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbdacf9a4u, 0x00000000u, 0x00000000u, 0u, true, -8, 0, 0, 20u, 0u},
  /*  269 sweep horizontal cal2 */
  {false, 2, true, true, true, 1300.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbd38e4b2u, 0x00000000u, 0x00000000u, 0u, true, -4, 0, 0, 20u, 0u},
  /*  270 sweep horizontal cal2 */
  {false, 2, true, true, true, 1350.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbbbeb0fdu, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  271 sweep horizontal cal2 */
  {false, 2, true, true, true, 1400.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  272 sweep horizontal cal2 */
  {false, 2, true, true, true, 1450.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  273 sweep horizontal cal2 */
  {false, 2, true, true, true, 1500.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  274 sweep horizontal cal2 */
  {false, 2, true, true, true, 1550.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  275 sweep horizontal cal2 */
  {false, 2, true, true, true, 1600.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  276 sweep horizontal cal2 */
  {false, 2, true, true, true, 1650.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  277 sweep horizontal cal2 */
  {false, 2, true, true, true, 1700.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3d1962d5u, 0x00000000u, 0x00000000u, 0u, true, 3, 0, 0, 0u, 0u},
  /*  278 sweep horizontal cal2 */
  {false, 2, true, true, true, 1750.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3d9efb4fu, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 19u, 19u},
  /*  279 sweep horizontal cal2 */
  {false, 2, true, true, true, 1800.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3df14536u, 0x00000000u, 0x00000000u, 0u, true, 11, 0, 0, 19u, 0u},
  /*  280 sweep horizontal cal2 */
  {false, 2, true, true, true, 1850.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e21c78eu, 0x00000000u, 0x00000000u, 0u, true, 15, 0, 0, 19u, 0u},
  /*  281 sweep horizontal cal2 */
  {false, 2, true, true, true, 1900.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e4aec81u, 0x00000000u, 0x00000000u, 0u, true, 19, 0, 0, 19u, 0u},
  /*  282 sweep horizontal cal2 */
  {false, 2, true, true, true, 1950.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e741172u, 0x00000000u, 0x00000000u, 0u, true, 23, 0, 0, 19u, 0u},
  /*  283 sweep horizontal cal2 */
  {false, 2, true, true, true, 2000.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e8e9b33u, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /*  284 sweep horizontal cal2 */
  {false, 2, true, true, true, 2050.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ea32dacu, 0x00000000u, 0x00000000u, 0u, true, 31, 0, 0, 19u, 0u},
  /*  285 sweep horizontal cal2 */
  {false, 2, true, true, true, 2100.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3eb7c026u, 0x00000000u, 0x00000000u, 0u, true, 35, 0, 0, 19u, 0u},
  /*  286 sweep horizontal cal2 */
  {false, 2, true, true, true, 2150.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ecc529fu, 0x00000000u, 0x00000000u, 0u, true, 39, 0, 0, 19u, 0u},
  /*  287 sweep horizontal cal2 */
  {false, 2, true, true, true, 2200.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ee0e518u, 0x00000000u, 0x00000000u, 0u, true, 43, 0, 0, 19u, 0u},
  /*  288 sweep horizontal cal2 */
  {false, 2, true, true, true, 2250.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ef57791u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /*  289 sweep horizontal cal2 */
  {false, 2, true, true, true, 2300.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f050506u, 0x00000000u, 0x00000000u, 0u, true, 51, 0, 0, 19u, 0u},
  /*  290 sweep horizontal cal2 */
  {false, 2, true, true, true, 2350.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f0f4e42u, 0x00000000u, 0x00000000u, 0u, true, 55, 0, 0, 19u, 0u},
  /*  291 sweep horizontal cal2 */
  {false, 2, true, true, true, 2400.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f19977eu, 0x00000000u, 0x00000000u, 0u, true, 59, 0, 0, 19u, 0u},
  /*  292 sweep horizontal cal2 */
  {false, 2, true, true, true, 2450.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f23e0bbu, 0x00000000u, 0x00000000u, 0u, true, 64, 0, 0, 19u, 0u},
  /*  293 sweep horizontal cal2 */
  {false, 2, true, true, true, 2500.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f2e29f8u, 0x00000000u, 0x00000000u, 0u, true, 68, 0, 0, 19u, 0u},
  /*  294 sweep horizontal cal2 */
  {false, 2, true, true, true, 2550.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f387334u, 0x00000000u, 0x00000000u, 0u, true, 72, 0, 0, 19u, 0u},
  /*  295 sweep horizontal cal2 */
  {false, 2, true, true, true, 2600.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f42bc71u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /*  296 sweep horizontal cal2 */
  {false, 2, true, true, true, 2650.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f4d05aeu, 0x00000000u, 0x00000000u, 0u, true, 80, 0, 0, 19u, 0u},
  /*  297 sweep horizontal cal2 */
  {false, 2, true, true, true, 2700.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f574eeau, 0x00000000u, 0x00000000u, 0u, true, 84, 0, 0, 19u, 0u},
  /*  298 sweep horizontal cal2 */
  {false, 2, true, true, true, 2750.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f619827u, 0x00000000u, 0x00000000u, 0u, true, 88, 0, 0, 19u, 0u},
  /*  299 sweep horizontal cal2 */
  {false, 2, true, true, true, 2800.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f6be163u, 0x00000000u, 0x00000000u, 0u, true, 92, 0, 0, 19u, 0u},
  /*  300 sweep horizontal cal2 */
  {false, 2, true, true, true, 2850.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f762aa1u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /*  301 sweep horizontal cal2 */
  {false, 2, true, true, true, 2900.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /*  302 sweep horizontal cal2 */
  {false, 2, true, true, true, 2950.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /*  303 sweep horizontal cal2 */
  {false, 2, true, true, true, 3000.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /*  304 sweep horizontal cal2 */
  {false, 2, true, true, true, 3050.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /*  305 sweep horizontal cal2 */
  {false, 2, true, true, true, 3100.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /*  306 sweep horizontal cal2 */
  {false, 2, true, true, true, 3150.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /*  307 sweep horizontal cal2 */
  {false, 2, true, true, true, 3200.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /*  308 sweep horizontal cal2 */
  {false, 2, true, true, true, 3250.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /*  309 sweep horizontal cal2 */
  {false, 2, true, true, true, 3300.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /*  310 sweep horizontal cal2 */
  {false, 2, true, true, true, 1.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /*  311 sweep horizontal cal2 */
  {false, 2, true, true, true, 5.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 0u},
  /*  312 sweep horizontal cal2 */
  {false, 2, true, true, true, 6.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 0u},
  /*  313 sweep horizontal cal2 */
  {false, 2, true, true, true, 10.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 0u},
  /*  314 sweep horizontal cal2 */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 0u},
  /*  315 sweep horizontal cal2 */
  {false, 2, true, true, true, 12.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 0u},
  /*  316 sweep horizontal cal2 */
  {false, 2, true, true, true, 1477.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  317 sweep horizontal cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  318 sweep horizontal cal2 */
  {false, 2, true, true, true, 1510.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  319 sweep horizontal cal2 */
  {false, 2, true, true, true, 2960.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /*  320 sweep horizontal cal2 */
  {false, 2, true, true, true, 2962.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /*  321 sweep horizontal cal2 */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /*  322 sweep horizontal cal2 */
  {false, 2, true, true, true, 2972.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /*  323 sweep horizontal cal2 */
  {false, 2, true, true, true, 3000.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /*  324 sweep vertical cal2 */
  {true, 2, true, true, true, 1507.0f, 0.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /*  325 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 50.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /*  326 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f7c3c3cu, 0x00000000u, 0u, true, 0, 98, 0, 17u, 0u},
  /*  327 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 150.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f723908u, 0x00000000u, 0u, true, 0, 94, 0, 17u, 0u},
  /*  328 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 200.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f6835d4u, 0x00000000u, 0u, true, 0, 90, 0, 17u, 0u},
  /*  329 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 250.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5e329fu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /*  330 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f542f6bu, 0x00000000u, 0u, true, 0, 82, 0, 17u, 0u},
  /*  331 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 350.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f4a2c37u, 0x00000000u, 0u, true, 0, 78, 0, 17u, 0u},
  /*  332 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 400.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f402903u, 0x00000000u, 0u, true, 0, 75, 0, 17u, 0u},
  /*  333 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 450.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f3625cfu, 0x00000000u, 0u, true, 0, 71, 0, 17u, 0u},
  /*  334 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 500.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2c229au, 0x00000000u, 0u, true, 0, 67, 0, 17u, 0u},
  /*  335 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 550.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f221f66u, 0x00000000u, 0u, true, 0, 63, 0, 17u, 0u},
  /*  336 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 600.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f181c32u, 0x00000000u, 0u, true, 0, 59, 0, 17u, 0u},
  /*  337 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 650.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f0e18feu, 0x00000000u, 0u, true, 0, 55, 0, 17u, 0u},
  /*  338 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 700.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f0415cau, 0x00000000u, 0u, true, 0, 51, 0, 17u, 0u},
  /*  339 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 750.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef4252du, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /*  340 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 800.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3ee01ec3u, 0x00000000u, 0u, true, 0, 43, 0, 17u, 0u},
  /*  341 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 850.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3ecc185au, 0x00000000u, 0u, true, 0, 39, 0, 17u, 0u},
  /*  342 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 900.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3eb811f2u, 0x00000000u, 0u, true, 0, 35, 0, 17u, 0u},
  /*  343 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 950.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3ea40b89u, 0x00000000u, 0u, true, 0, 32, 0, 17u, 0u},
  /*  344 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3e900521u, 0x00000000u, 0u, true, 0, 28, 0, 17u, 0u},
  /*  345 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1050.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3e77fd72u, 0x00000000u, 0u, true, 0, 24, 0, 17u, 0u},
  /*  346 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3e4ff09fu, 0x00000000u, 0u, true, 0, 20, 0, 17u, 0u},
  /*  347 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1150.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3e27e3cfu, 0x00000000u, 0u, true, 0, 16, 0, 17u, 0u},
  /*  348 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1200.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3dffadfau, 0x00000000u, 0u, true, 0, 12, 0, 17u, 0u},
  /*  349 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1250.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3daf9458u, 0x00000000u, 0u, true, 0, 8, 0, 17u, 0u},
  /*  350 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3d3ef56fu, 0x00000000u, 0u, true, 0, 4, 0, 17u, 0u},
  /*  351 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1350.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3bf61150u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  352 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1400.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  353 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1450.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  354 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1500.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  355 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1550.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  356 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1600.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  357 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1650.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  358 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1700.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbd14ae12u, 0x00000000u, 0u, true, 0, -3, 0, 0u, 0u},
  /*  359 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1750.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbd9d4f07u, 0x00000000u, 0u, true, 0, -7, 0, 18u, 18u},
  /*  360 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1800.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbdf04707u, 0x00000000u, 0u, true, 0, -11, 0, 18u, 0u},
  /*  361 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1850.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbe219f82u, 0x00000000u, 0u, true, 0, -15, 0, 18u, 0u},
  /*  362 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1900.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbe4b1b82u, 0x00000000u, 0u, true, 0, -19, 0, 18u, 0u},
  /*  363 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1950.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbe749782u, 0x00000000u, 0u, true, 0, -23, 0, 18u, 0u},
  /*  364 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbe8f09c0u, 0x00000000u, 0u, true, 0, -27, 0, 18u, 0u},
  /*  365 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2050.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbea3c7c0u, 0x00000000u, 0u, true, 0, -31, 0, 18u, 0u},
  /*  366 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbeb885c0u, 0x00000000u, 0u, true, 0, -36, 0, 18u, 0u},
  /*  367 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2150.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbecd43bfu, 0x00000000u, 0u, true, 0, -40, 0, 18u, 0u},
  /*  368 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2200.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbee201bfu, 0x00000000u, 0u, true, 0, -44, 0, 18u, 0u},
  /*  369 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2250.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbef6bfbfu, 0x00000000u, 0u, true, 0, -48, 0, 18u, 0u},
  /*  370 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf05bee0u, 0x00000000u, 0u, true, 0, -52, 0, 18u, 0u},
  /*  371 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2350.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf101ddeu, 0x00000000u, 0u, true, 0, -56, 0, 18u, 0u},
  /*  372 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2400.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf1a7cdeu, 0x00000000u, 0u, true, 0, -60, 0, 18u, 0u},
  /*  373 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2450.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf24dbdeu, 0x00000000u, 0u, true, 0, -64, 0, 18u, 0u},
  /*  374 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2500.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf2f3adeu, 0x00000000u, 0u, true, 0, -68, 0, 18u, 0u},
  /*  375 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2550.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf3999deu, 0x00000000u, 0u, true, 0, -72, 0, 18u, 0u},
  /*  376 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2600.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf43f8deu, 0x00000000u, 0u, true, 0, -76, 0, 18u, 0u},
  /*  377 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2650.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf4e57ddu, 0x00000000u, 0u, true, 0, -80, 0, 18u, 0u},
  /*  378 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2700.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf58b6ddu, 0x00000000u, 0u, true, 0, -84, 0, 18u, 0u},
  /*  379 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2750.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf6315ddu, 0x00000000u, 0u, true, 0, -88, 0, 18u, 0u},
  /*  380 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2800.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf6d74ddu, 0x00000000u, 0u, true, 0, -92, 0, 18u, 0u},
  /*  381 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2850.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf77d3ddu, 0x00000000u, 0u, true, 0, -96, 0, 18u, 0u},
  /*  382 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2900.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 0u},
  /*  383 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2950.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 0u},
  /*  384 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 3000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 0u},
  /*  385 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 3050.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 0u},
  /*  386 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 3100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 0u},
  /*  387 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 3150.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 0u},
  /*  388 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 3200.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 0u},
  /*  389 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 3250.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 0u},
  /*  390 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 3300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 0u},
  /*  391 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /*  392 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 5.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /*  393 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /*  394 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 10.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /*  395 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 11.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /*  396 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 12.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /*  397 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1477.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  398 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1507.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  399 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  400 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2960.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /*  401 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 0u},
  /*  402 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2971.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 0u},
  /*  403 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 2972.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 0u},
  /*  404 sweep vertical cal2 */
  {false, 2, true, true, true, 1507.0f, 3000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 0u},
  /*  405 sweep twist cal2 */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 0.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf800000u, 0u, true, 0, 0, -100, 0u, 0u},
  /*  406 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 50.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf800000u, 0u, true, 0, 0, -100, 0u, 0u},
  /*  407 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 100.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf76a2edu, 0u, true, 0, 0, -96, 0u, 0u},
  /*  408 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 150.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf6d45dau, 0u, true, 0, 0, -92, 0u, 0u},
  /*  409 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 200.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf63e8c7u, 0u, true, 0, 0, -89, 0u, 0u},
  /*  410 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 250.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf5a8bb4u, 0u, true, 0, 0, -85, 0u, 0u},
  /*  411 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 300.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf512ea1u, 0u, true, 0, 0, -81, 0u, 0u},
  /*  412 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 350.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf47d18fu, 0u, true, 0, 0, -78, 0u, 0u},
  /*  413 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 400.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf3e747cu, 0u, true, 0, 0, -74, 0u, 0u},
  /*  414 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 450.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf351769u, 0u, true, 0, 0, -70, 0u, 0u},
  /*  415 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 500.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf2bba56u, 0u, true, 0, 0, -67, 0u, 0u},
  /*  416 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 550.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf225d43u, 0u, true, 0, 0, -63, 0u, 0u},
  /*  417 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 600.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf190030u, 0u, true, 0, 0, -59, 0u, 0u},
  /*  418 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf0fa31du, 0u, true, 0, 0, -56, 0u, 0u},
  /*  419 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf06460au, 0u, true, 0, 0, -52, 0u, 0u},
  /*  420 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 750.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbef9d1eeu, 0u, true, 0, 0, -48, 0u, 0u},
  /*  421 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 800.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbee717c9u, 0u, true, 0, 0, -45, 0u, 0u},
  /*  422 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 850.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbed45da3u, 0u, true, 0, 0, -41, 0u, 0u},
  /*  423 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 900.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbec1a37du, 0u, true, 0, 0, -37, 0u, 0u},
  /*  424 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 950.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbeaee957u, 0u, true, 0, 0, -34, 0u, 0u},
  /*  425 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1000.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbe9c2f31u, 0u, true, 0, 0, -30, 0u, 0u},
  /*  426 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1050.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbe89750bu, 0u, true, 0, 0, -26, 0u, 0u},
  /*  427 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1100.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbe6d75cbu, 0u, true, 0, 0, -23, 0u, 0u},
  /*  428 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1150.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbe480180u, 0u, true, 0, 0, -19, 0u, 0u},
  /*  429 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1200.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbe228d34u, 0u, true, 0, 0, -15, 0u, 0u},
  /*  430 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1250.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbdfa31d0u, 0u, true, 0, 0, -12, 0u, 0u},
  /*  431 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1300.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbdaf4939u, 0u, true, 0, 0, -8, 0u, 0u},
  /*  432 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1350.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbd48c144u, 0u, true, 0, 0, -4, 0u, 0u},
  /*  433 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1400.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbc4bc054u, 0u, true, 0, 0, -1, 0u, 0u},
  /*  434 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1450.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  435 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1500.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  436 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1550.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3c1a01dau, 0u, true, 0, 0, 0, 0u, 0u},
  /*  437 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1600.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3d3a95efu, 0u, true, 0, 0, 4, 0u, 0u},
  /*  438 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3da755b4u, 0u, true, 0, 0, 8, 0u, 0u},
  /*  439 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1700.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3df16071u, 0u, true, 0, 0, 11, 0u, 0u},
  /*  440 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1750.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3e1db596u, 0u, true, 0, 0, 15, 0u, 0u},
  /*  441 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1800.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3e42baf5u, 0u, true, 0, 0, 19, 0u, 0u},
  /*  442 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1850.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3e67c053u, 0u, true, 0, 0, 22, 0u, 0u},
  /*  443 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1900.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3e8662d9u, 0u, true, 0, 0, 26, 0u, 0u},
  /*  444 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1950.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3e98e588u, 0u, true, 0, 0, 29, 0u, 0u},
  /*  445 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2000.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3eab6837u, 0u, true, 0, 0, 33, 0u, 0u},
  /*  446 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2050.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3ebdeae6u, 0u, true, 0, 0, 37, 0u, 0u},
  /*  447 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2100.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3ed06d95u, 0u, true, 0, 0, 40, 0u, 0u},
  /*  448 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2150.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3ee2f044u, 0u, true, 0, 0, 44, 0u, 0u},
  /*  449 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2200.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3ef572f3u, 0u, true, 0, 0, 47, 0u, 0u},
  /*  450 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2250.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f03fad1u, 0u, true, 0, 0, 51, 0u, 0u},
  /*  451 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2300.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f0d3c29u, 0u, true, 0, 0, 55, 0u, 0u},
  /*  452 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2350.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f167d80u, 0u, true, 0, 0, 58, 0u, 0u},
  /*  453 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2400.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f1fbed8u, 0u, true, 0, 0, 62, 0u, 0u},
  /*  454 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2450.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f29002fu, 0u, true, 0, 0, 66, 0u, 0u},
  /*  455 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2500.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f324187u, 0u, true, 0, 0, 69, 0u, 0u},
  /*  456 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2550.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f3b82deu, 0u, true, 0, 0, 73, 0u, 0u},
  /*  457 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2600.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f44c436u, 0u, true, 0, 0, 76, 0u, 0u},
  /*  458 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f4e058eu, 0u, true, 0, 0, 80, 0u, 0u},
  /*  459 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2700.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f5746e5u, 0u, true, 0, 0, 84, 0u, 0u},
  /*  460 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2750.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f60883du, 0u, true, 0, 0, 87, 0u, 0u},
  /*  461 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2800.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f69c994u, 0u, true, 0, 0, 91, 0u, 0u},
  /*  462 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2850.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f730aecu, 0u, true, 0, 0, 94, 0u, 0u},
  /*  463 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2900.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f7c4c43u, 0u, true, 0, 0, 98, 0u, 0u},
  /*  464 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2950.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /*  465 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 3000.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /*  466 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 3050.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /*  467 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 3100.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /*  468 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 3150.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /*  469 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 3200.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /*  470 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 3250.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /*  471 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 3300.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /*  472 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf800000u, 0u, true, 0, 0, -100, 0u, 0u},
  /*  473 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 5.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf800000u, 0u, true, 0, 0, -100, 0u, 0u},
  /*  474 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 6.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf800000u, 0u, true, 0, 0, -100, 0u, 0u},
  /*  475 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 10.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf800000u, 0u, true, 0, 0, -100, 0u, 0u},
  /*  476 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 11.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf800000u, 0u, true, 0, 0, -100, 0u, 0u},
  /*  477 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 12.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf800000u, 0u, true, 0, 0, -100, 0u, 0u},
  /*  478 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  479 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1507.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  480 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1510.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  481 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /*  482 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2962.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /*  483 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2971.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /*  484 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2972.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /*  485 sweep twist cal2 */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 3000.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /*  486 grid cal1 */
  {true, 1, true, true, true, 0.0f, 0.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf3504f3u, 0x3f3504f3u, 0x00000000u, 0u, true, -70, 70, 0, 20u, 20u},
  /*  487 grid cal1 */
  {false, 1, true, true, true, 0.0f, 300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf462211u, 0x3f221bdfu, 0x00000000u, 0u, true, -77, 63, 0, 20u, 0u},
  /*  488 grid cal1 */
  {false, 1, true, true, true, 0.0f, 600.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf57fa33u, 0x3f0970acu, 0x00000000u, 0u, true, -84, 53, 0, 20u, 0u},
  /*  489 grid cal1 */
  {false, 1, true, true, true, 0.0f, 900.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf690dc6u, 0x3ed3ddfbu, 0x00000000u, 0u, true, -91, 41, 0, 20u, 0u},
  /*  490 grid cal1 */
  {false, 1, true, true, true, 0.0f, 1200.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf76fac3u, 0x3e86b753u, 0x00000000u, 0u, true, -96, 26, 0, 20u, 0u},
  /*  491 grid cal1 */
  {false, 1, true, true, true, 0.0f, 1500.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf7ef2dcu, 0x3db96acfu, 0x00000000u, 0u, true, -99, 9, 0, 20u, 0u},
  /*  492 grid cal1 */
  {false, 1, true, true, true, 0.0f, 1800.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf7ef2dcu, 0xbdb96acfu, 0x00000000u, 0u, true, -99, -9, 0, 20u, 0u},
  /*  493 grid cal1 */
  {false, 1, true, true, true, 0.0f, 2100.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf76fac3u, 0xbe86b753u, 0x00000000u, 0u, true, -96, -26, 0, 20u, 0u},
  /*  494 grid cal1 */
  {false, 1, true, true, true, 0.0f, 2400.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf690dc6u, 0xbed3ddfbu, 0x00000000u, 0u, true, -91, -41, 0, 20u, 0u},
  /*  495 grid cal1 */
  {false, 1, true, true, true, 0.0f, 2700.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf57fa33u, 0xbf0970acu, 0x00000000u, 0u, true, -84, -53, 0, 20u, 0u},
  /*  496 grid cal1 */
  {false, 1, true, true, true, 0.0f, 3000.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf462211u, 0xbf221bdfu, 0x00000000u, 0u, true, -77, -63, 0, 20u, 0u},
  /*  497 grid cal1 */
  {false, 1, true, true, true, 0.0f, 3300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf3504f3u, 0xbf3504f3u, 0x00000000u, 0u, true, -70, -70, 0, 20u, 0u},
  /*  498 grid cal1 */
  {false, 1, true, true, true, 300.0f, 0.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf221bdfu, 0x3f462211u, 0x00000000u, 0u, true, -63, 77, 0, 17u, 17u},
  /*  499 grid cal1 */
  {false, 1, true, true, true, 300.0f, 300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf3504f3u, 0x3f3504f3u, 0x00000000u, 0u, true, -70, 70, 0, 20u, 20u},
  /*  500 grid cal1 */
  {false, 1, true, true, true, 300.0f, 600.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf4a12fdu, 0x3f1d2b36u, 0x00000000u, 0u, true, -78, 61, 0, 20u, 0u},
  /*  501 grid cal1 */
  {false, 1, true, true, true, 300.0f, 900.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf5c16edu, 0x3ef48b41u, 0x00000000u, 0u, true, -85, 47, 0, 20u, 0u},
  /*  502 grid cal1 */
  {false, 1, true, true, true, 300.0f, 1200.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf59d852u, 0x3e913ae2u, 0x00000000u, 0u, true, -85, 28, 0, 20u, 0u},
  /*  503 grid cal1 */
  {false, 1, true, true, true, 300.0f, 1500.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf587bd2u, 0x3dc06e11u, 0x00000000u, 0u, true, -84, 9, 0, 20u, 0u},
  /*  504 grid cal1 */
  {false, 1, true, true, true, 300.0f, 1800.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf587bd2u, 0xbdc06e11u, 0x00000000u, 0u, true, -84, -9, 0, 20u, 0u},
  /*  505 grid cal1 */
  {false, 1, true, true, true, 300.0f, 2100.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf59d852u, 0xbe913ae2u, 0x00000000u, 0u, true, -85, -28, 0, 20u, 0u},
  /*  506 grid cal1 */
  {false, 1, true, true, true, 300.0f, 2400.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf5c16edu, 0xbef48b41u, 0x00000000u, 0u, true, -85, -47, 0, 20u, 0u},
  /*  507 grid cal1 */
  {false, 1, true, true, true, 300.0f, 2700.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf4a12fdu, 0xbf1d2b36u, 0x00000000u, 0u, true, -78, -61, 0, 20u, 0u},
  /*  508 grid cal1 */
  {false, 1, true, true, true, 300.0f, 3000.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf3504f3u, 0xbf3504f3u, 0x00000000u, 0u, true, -70, -70, 0, 20u, 0u},
  /*  509 grid cal1 */
  {false, 1, true, true, true, 300.0f, 3300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf221bdfu, 0xbf462211u, 0x00000000u, 0u, true, -63, -77, 0, 18u, 18u},
  /*  510 grid cal1 */
  {false, 1, true, true, true, 600.0f, 0.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf0970acu, 0x3f57fa33u, 0x00000000u, 0u, true, -53, 84, 0, 17u, 17u},
  /*  511 grid cal1 */
  {false, 1, true, true, true, 600.0f, 300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf1d2b36u, 0x3f4a12fdu, 0x00000000u, 0u, true, -61, 78, 0, 17u, 0u},
  /*  512 grid cal1 */
  {false, 1, true, true, true, 600.0f, 600.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf2a5c82u, 0x3f2a5c82u, 0x00000000u, 0u, true, -66, 66, 0, 20u, 20u},
  /*  513 grid cal1 */
  {false, 1, true, true, true, 600.0f, 900.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf272667u, 0x3eeec926u, 0x00000000u, 0u, true, -65, 46, 0, 20u, 0u},
  /*  514 grid cal1 */
  {false, 1, true, true, true, 600.0f, 1200.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf23f9acu, 0x3e8c8cddu, 0x00000000u, 0u, true, -64, 27, 0, 20u, 0u},
  /*  515 grid cal1 */
  {false, 1, true, true, true, 600.0f, 1500.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf21d7c1u, 0x3db8f695u, 0x00000000u, 0u, true, -63, 9, 0, 20u, 0u},
  /*  516 grid cal1 */
  {false, 1, true, true, true, 600.0f, 1800.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf21d7c1u, 0xbdb8f695u, 0x00000000u, 0u, true, -63, -9, 0, 20u, 0u},
  /*  517 grid cal1 */
  {false, 1, true, true, true, 600.0f, 2100.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf23f9acu, 0xbe8c8cddu, 0x00000000u, 0u, true, -64, -27, 0, 20u, 0u},
  /*  518 grid cal1 */
  {false, 1, true, true, true, 600.0f, 2400.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf272667u, 0xbeeec926u, 0x00000000u, 0u, true, -65, -46, 0, 20u, 0u},
  /*  519 grid cal1 */
  {false, 1, true, true, true, 600.0f, 2700.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf2a5c82u, 0xbf2a5c82u, 0x00000000u, 0u, true, -66, -66, 0, 20u, 0u},
  /*  520 grid cal1 */
  {false, 1, true, true, true, 600.0f, 3000.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf1d2b36u, 0xbf4a12fdu, 0x00000000u, 0u, true, -61, -78, 0, 18u, 18u},
  /*  521 grid cal1 */
  {false, 1, true, true, true, 600.0f, 3300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf0970acu, 0xbf57fa33u, 0x00000000u, 0u, true, -53, -84, 0, 18u, 0u},
  /*  522 grid cal1 */
  {false, 1, true, true, true, 900.0f, 0.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbed3ddfbu, 0x3f690dc6u, 0x00000000u, 0u, true, -41, 91, 0, 17u, 17u},
  /*  523 grid cal1 */
  {false, 1, true, true, true, 900.0f, 300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbef48b41u, 0x3f5c16edu, 0x00000000u, 0u, true, -47, 85, 0, 17u, 0u},
  /*  524 grid cal1 */
  {false, 1, true, true, true, 900.0f, 600.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbeeec926u, 0x3f272667u, 0x00000000u, 0u, true, -46, 65, 0, 17u, 0u},
  /*  525 grid cal1 */
  {false, 1, true, true, true, 900.0f, 900.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbee73439u, 0x3ee73439u, 0x00000000u, 0u, true, -45, 45, 0, 20u, 20u},
  /*  526 grid cal1 */
  {false, 1, true, true, true, 900.0f, 1200.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbede253eu, 0x3e85498cu, 0x00000000u, 0u, true, -43, 26, 0, 20u, 0u},
  /*  527 grid cal1 */
  {false, 1, true, true, true, 900.0f, 1500.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbed6bb33u, 0x3dabc8f6u, 0x00000000u, 0u, true, -41, 8, 0, 20u, 0u},
  /*  528 grid cal1 */
  {false, 1, true, true, true, 900.0f, 1800.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbed6bb33u, 0xbdabc8f6u, 0x00000000u, 0u, true, -41, -8, 0, 20u, 0u},
  /*  529 grid cal1 */
  {false, 1, true, true, true, 900.0f, 2100.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbede253eu, 0xbe85498cu, 0x00000000u, 0u, true, -43, -26, 0, 20u, 0u},
  /*  530 grid cal1 */
  {false, 1, true, true, true, 900.0f, 2400.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbee73439u, 0xbee73439u, 0x00000000u, 0u, true, -45, -45, 0, 20u, 0u},
  /*  531 grid cal1 */
  {false, 1, true, true, true, 900.0f, 2700.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbeeec926u, 0xbf272667u, 0x00000000u, 0u, true, -46, -65, 0, 18u, 18u},
  /*  532 grid cal1 */
  {false, 1, true, true, true, 900.0f, 3000.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbef48b41u, 0xbf5c16edu, 0x00000000u, 0u, true, -47, -85, 0, 18u, 0u},
  /*  533 grid cal1 */
  {false, 1, true, true, true, 900.0f, 3300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbed3ddfbu, 0xbf690dc6u, 0x00000000u, 0u, true, -41, -91, 0, 18u, 0u},
  /*  534 grid cal1 */
  {false, 1, true, true, true, 1200.0f, 0.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe86b753u, 0x3f76fac3u, 0x00000000u, 0u, true, -26, 96, 0, 17u, 17u},
  /*  535 grid cal1 */
  {false, 1, true, true, true, 1200.0f, 300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe913ae2u, 0x3f59d852u, 0x00000000u, 0u, true, -28, 85, 0, 17u, 0u},
  /*  536 grid cal1 */
  {false, 1, true, true, true, 1200.0f, 600.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe8c8cddu, 0x3f23f9acu, 0x00000000u, 0u, true, -27, 64, 0, 17u, 0u},
  /*  537 grid cal1 */
  {false, 1, true, true, true, 1200.0f, 900.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe85498cu, 0x3ede253eu, 0x00000000u, 0u, true, -26, 43, 0, 17u, 0u},
  /*  538 grid cal1 */
  {false, 1, true, true, true, 1200.0f, 1200.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe735ee0u, 0x3e735ee0u, 0x00000000u, 0u, true, -23, 23, 0, 20u, 20u},
  /*  539 grid cal1 */
  {false, 1, true, true, true, 1200.0f, 1500.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe56448au, 0x3d8ed85cu, 0x00000000u, 0u, true, -20, 6, 0, 20u, 0u},
  /*  540 grid cal1 */
  {false, 1, true, true, true, 1200.0f, 1800.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe56448au, 0xbd8ed85cu, 0x00000000u, 0u, true, -20, -6, 0, 20u, 0u},
  /*  541 grid cal1 */
  {false, 1, true, true, true, 1200.0f, 2100.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe735ee0u, 0xbe735ee0u, 0x00000000u, 0u, true, -23, -23, 0, 20u, 0u},
  /*  542 grid cal1 */
  {false, 1, true, true, true, 1200.0f, 2400.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe85498cu, 0xbede253eu, 0x00000000u, 0u, true, -26, -43, 0, 18u, 18u},
  /*  543 grid cal1 */
  {false, 1, true, true, true, 1200.0f, 2700.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe8c8cddu, 0xbf23f9acu, 0x00000000u, 0u, true, -27, -64, 0, 18u, 0u},
  /*  544 grid cal1 */
  {false, 1, true, true, true, 1200.0f, 3000.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe913ae2u, 0xbf59d852u, 0x00000000u, 0u, true, -28, -85, 0, 18u, 0u},
  /*  545 grid cal1 */
  {false, 1, true, true, true, 1200.0f, 3300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe86b753u, 0xbf76fac3u, 0x00000000u, 0u, true, -26, -96, 0, 18u, 0u},
  /*  546 grid cal1 */
  {false, 1, true, true, true, 1500.0f, 0.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbdb96acfu, 0x3f7ef2dcu, 0x00000000u, 0u, true, -9, 99, 0, 17u, 17u},
  /*  547 grid cal1 */
  {false, 1, true, true, true, 1500.0f, 300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbdc06e11u, 0x3f587bd2u, 0x00000000u, 0u, true, -9, 84, 0, 17u, 0u},
  /*  548 grid cal1 */
  {false, 1, true, true, true, 1500.0f, 600.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbdb8f695u, 0x3f21d7c1u, 0x00000000u, 0u, true, -9, 63, 0, 17u, 0u},
  /*  549 grid cal1 */
  {false, 1, true, true, true, 1500.0f, 900.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbdabc8f6u, 0x3ed6bb33u, 0x00000000u, 0u, true, -8, 41, 0, 17u, 0u},
  /*  550 grid cal1 */
  {false, 1, true, true, true, 1500.0f, 1200.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbd8ed85cu, 0x3e56448au, 0x00000000u, 0u, true, -6, 20, 0, 17u, 0u},
  /*  551 grid cal1 */
  {false, 1, true, true, true, 1500.0f, 1500.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbcc2aa4du, 0x3cc2aa4du, 0x00000000u, 0u, true, -2, 2, 0, 20u, 20u},
  /*  552 grid cal1 */
  {false, 1, true, true, true, 1500.0f, 1800.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbcc2aa4du, 0xbcc2aa4du, 0x00000000u, 0u, true, -2, -2, 0, 20u, 0u},
  /*  553 grid cal1 */
  {false, 1, true, true, true, 1500.0f, 2100.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbd8ed85cu, 0xbe56448au, 0x00000000u, 0u, true, -6, -20, 0, 18u, 18u},
  /*  554 grid cal1 */
  {false, 1, true, true, true, 1500.0f, 2400.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbdabc8f6u, 0xbed6bb33u, 0x00000000u, 0u, true, -8, -41, 0, 18u, 0u},
  /*  555 grid cal1 */
  {false, 1, true, true, true, 1500.0f, 2700.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbdb8f695u, 0xbf21d7c1u, 0x00000000u, 0u, true, -9, -63, 0, 18u, 0u},
  /*  556 grid cal1 */
  {false, 1, true, true, true, 1500.0f, 3000.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbdc06e11u, 0xbf587bd2u, 0x00000000u, 0u, true, -9, -84, 0, 18u, 0u},
  /*  557 grid cal1 */
  {false, 1, true, true, true, 1500.0f, 3300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbdb96acfu, 0xbf7ef2dcu, 0x00000000u, 0u, true, -9, -99, 0, 18u, 0u},
  /*  558 grid cal1 */
  {false, 1, true, true, true, 1800.0f, 0.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3db96acfu, 0x3f7ef2dcu, 0x00000000u, 0u, true, 9, 99, 0, 17u, 17u},
  /*  559 grid cal1 */
  {false, 1, true, true, true, 1800.0f, 300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3dc06e11u, 0x3f587bd2u, 0x00000000u, 0u, true, 9, 84, 0, 17u, 0u},
  /*  560 grid cal1 */
  {false, 1, true, true, true, 1800.0f, 600.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3db8f695u, 0x3f21d7c1u, 0x00000000u, 0u, true, 9, 63, 0, 17u, 0u},
  /*  561 grid cal1 */
  {false, 1, true, true, true, 1800.0f, 900.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3dabc8f6u, 0x3ed6bb33u, 0x00000000u, 0u, true, 8, 41, 0, 17u, 0u},
  /*  562 grid cal1 */
  {false, 1, true, true, true, 1800.0f, 1200.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3d8ed85cu, 0x3e56448au, 0x00000000u, 0u, true, 6, 20, 0, 17u, 0u},
  /*  563 grid cal1 */
  {false, 1, true, true, true, 1800.0f, 1500.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3cc2aa4du, 0x3cc2aa4du, 0x00000000u, 0u, true, 2, 2, 0, 19u, 19u},
  /*  564 grid cal1 */
  {false, 1, true, true, true, 1800.0f, 1800.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3cc2aa4du, 0xbcc2aa4du, 0x00000000u, 0u, true, 2, -2, 0, 19u, 0u},
  /*  565 grid cal1 */
  {false, 1, true, true, true, 1800.0f, 2100.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3d8ed85cu, 0xbe56448au, 0x00000000u, 0u, true, 6, -20, 0, 18u, 18u},
  /*  566 grid cal1 */
  {false, 1, true, true, true, 1800.0f, 2400.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3dabc8f6u, 0xbed6bb33u, 0x00000000u, 0u, true, 8, -41, 0, 18u, 0u},
  /*  567 grid cal1 */
  {false, 1, true, true, true, 1800.0f, 2700.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3db8f695u, 0xbf21d7c1u, 0x00000000u, 0u, true, 9, -63, 0, 18u, 0u},
  /*  568 grid cal1 */
  {false, 1, true, true, true, 1800.0f, 3000.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3dc06e11u, 0xbf587bd2u, 0x00000000u, 0u, true, 9, -84, 0, 18u, 0u},
  /*  569 grid cal1 */
  {false, 1, true, true, true, 1800.0f, 3300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3db96acfu, 0xbf7ef2dcu, 0x00000000u, 0u, true, 9, -99, 0, 18u, 0u},
  /*  570 grid cal1 */
  {false, 1, true, true, true, 2100.0f, 0.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e86b753u, 0x3f76fac3u, 0x00000000u, 0u, true, 26, 96, 0, 17u, 17u},
  /*  571 grid cal1 */
  {false, 1, true, true, true, 2100.0f, 300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e913ae2u, 0x3f59d852u, 0x00000000u, 0u, true, 28, 85, 0, 17u, 0u},
  /*  572 grid cal1 */
  {false, 1, true, true, true, 2100.0f, 600.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e8c8cddu, 0x3f23f9acu, 0x00000000u, 0u, true, 27, 64, 0, 17u, 0u},
  /*  573 grid cal1 */
  {false, 1, true, true, true, 2100.0f, 900.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e85498cu, 0x3ede253eu, 0x00000000u, 0u, true, 26, 43, 0, 17u, 0u},
  /*  574 grid cal1 */
  {false, 1, true, true, true, 2100.0f, 1200.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e735ee0u, 0x3e735ee0u, 0x00000000u, 0u, true, 23, 23, 0, 19u, 19u},
  /*  575 grid cal1 */
  {false, 1, true, true, true, 2100.0f, 1500.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e56448au, 0x3d8ed85cu, 0x00000000u, 0u, true, 20, 6, 0, 19u, 0u},
  /*  576 grid cal1 */
  {false, 1, true, true, true, 2100.0f, 1800.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e56448au, 0xbd8ed85cu, 0x00000000u, 0u, true, 20, -6, 0, 19u, 0u},
  /*  577 grid cal1 */
  {false, 1, true, true, true, 2100.0f, 2100.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e735ee0u, 0xbe735ee0u, 0x00000000u, 0u, true, 23, -23, 0, 19u, 0u},
  /*  578 grid cal1 */
  {false, 1, true, true, true, 2100.0f, 2400.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e85498cu, 0xbede253eu, 0x00000000u, 0u, true, 26, -43, 0, 18u, 18u},
  /*  579 grid cal1 */
  {false, 1, true, true, true, 2100.0f, 2700.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e8c8cddu, 0xbf23f9acu, 0x00000000u, 0u, true, 27, -64, 0, 18u, 0u},
  /*  580 grid cal1 */
  {false, 1, true, true, true, 2100.0f, 3000.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e913ae2u, 0xbf59d852u, 0x00000000u, 0u, true, 28, -85, 0, 18u, 0u},
  /*  581 grid cal1 */
  {false, 1, true, true, true, 2100.0f, 3300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e86b753u, 0xbf76fac3u, 0x00000000u, 0u, true, 26, -96, 0, 18u, 0u},
  /*  582 grid cal1 */
  {false, 1, true, true, true, 2400.0f, 0.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ed3ddfbu, 0x3f690dc6u, 0x00000000u, 0u, true, 41, 91, 0, 17u, 17u},
  /*  583 grid cal1 */
  {false, 1, true, true, true, 2400.0f, 300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ef48b41u, 0x3f5c16edu, 0x00000000u, 0u, true, 47, 85, 0, 17u, 0u},
  /*  584 grid cal1 */
  {false, 1, true, true, true, 2400.0f, 600.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3eeec926u, 0x3f272667u, 0x00000000u, 0u, true, 46, 65, 0, 17u, 0u},
  /*  585 grid cal1 */
  {false, 1, true, true, true, 2400.0f, 900.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ee73439u, 0x3ee73439u, 0x00000000u, 0u, true, 45, 45, 0, 19u, 19u},
  /*  586 grid cal1 */
  {false, 1, true, true, true, 2400.0f, 1200.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ede253eu, 0x3e85498cu, 0x00000000u, 0u, true, 43, 26, 0, 19u, 0u},
  /*  587 grid cal1 */
  {false, 1, true, true, true, 2400.0f, 1500.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ed6bb33u, 0x3dabc8f6u, 0x00000000u, 0u, true, 41, 8, 0, 19u, 0u},
  /*  588 grid cal1 */
  {false, 1, true, true, true, 2400.0f, 1800.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ed6bb33u, 0xbdabc8f6u, 0x00000000u, 0u, true, 41, -8, 0, 19u, 0u},
  /*  589 grid cal1 */
  {false, 1, true, true, true, 2400.0f, 2100.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ede253eu, 0xbe85498cu, 0x00000000u, 0u, true, 43, -26, 0, 19u, 0u},
  /*  590 grid cal1 */
  {false, 1, true, true, true, 2400.0f, 2400.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ee73439u, 0xbee73439u, 0x00000000u, 0u, true, 45, -45, 0, 19u, 0u},
  /*  591 grid cal1 */
  {false, 1, true, true, true, 2400.0f, 2700.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3eeec926u, 0xbf272667u, 0x00000000u, 0u, true, 46, -65, 0, 18u, 18u},
  /*  592 grid cal1 */
  {false, 1, true, true, true, 2400.0f, 3000.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ef48b41u, 0xbf5c16edu, 0x00000000u, 0u, true, 47, -85, 0, 18u, 0u},
  /*  593 grid cal1 */
  {false, 1, true, true, true, 2400.0f, 3300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ed3ddfbu, 0xbf690dc6u, 0x00000000u, 0u, true, 41, -91, 0, 18u, 0u},
  /*  594 grid cal1 */
  {false, 1, true, true, true, 2700.0f, 0.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f0970acu, 0x3f57fa33u, 0x00000000u, 0u, true, 53, 84, 0, 17u, 17u},
  /*  595 grid cal1 */
  {false, 1, true, true, true, 2700.0f, 300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f1d2b36u, 0x3f4a12fdu, 0x00000000u, 0u, true, 61, 78, 0, 17u, 0u},
  /*  596 grid cal1 */
  {false, 1, true, true, true, 2700.0f, 600.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f2a5c82u, 0x3f2a5c82u, 0x00000000u, 0u, true, 66, 66, 0, 19u, 19u},
  /*  597 grid cal1 */
  {false, 1, true, true, true, 2700.0f, 900.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f272667u, 0x3eeec926u, 0x00000000u, 0u, true, 65, 46, 0, 19u, 0u},
  /*  598 grid cal1 */
  {false, 1, true, true, true, 2700.0f, 1200.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f23f9acu, 0x3e8c8cddu, 0x00000000u, 0u, true, 64, 27, 0, 19u, 0u},
  /*  599 grid cal1 */
  {false, 1, true, true, true, 2700.0f, 1500.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f21d7c1u, 0x3db8f695u, 0x00000000u, 0u, true, 63, 9, 0, 19u, 0u},
  /*  600 grid cal1 */
  {false, 1, true, true, true, 2700.0f, 1800.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f21d7c1u, 0xbdb8f695u, 0x00000000u, 0u, true, 63, -9, 0, 19u, 0u},
  /*  601 grid cal1 */
  {false, 1, true, true, true, 2700.0f, 2100.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f23f9acu, 0xbe8c8cddu, 0x00000000u, 0u, true, 64, -27, 0, 19u, 0u},
  /*  602 grid cal1 */
  {false, 1, true, true, true, 2700.0f, 2400.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f272667u, 0xbeeec926u, 0x00000000u, 0u, true, 65, -46, 0, 19u, 0u},
  /*  603 grid cal1 */
  {false, 1, true, true, true, 2700.0f, 2700.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f2a5c82u, 0xbf2a5c82u, 0x00000000u, 0u, true, 66, -66, 0, 19u, 0u},
  /*  604 grid cal1 */
  {false, 1, true, true, true, 2700.0f, 3000.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f1d2b36u, 0xbf4a12fdu, 0x00000000u, 0u, true, 61, -78, 0, 18u, 18u},
  /*  605 grid cal1 */
  {false, 1, true, true, true, 2700.0f, 3300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f0970acu, 0xbf57fa33u, 0x00000000u, 0u, true, 53, -84, 0, 18u, 0u},
  /*  606 grid cal1 */
  {false, 1, true, true, true, 3000.0f, 0.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f221bdfu, 0x3f462211u, 0x00000000u, 0u, true, 63, 77, 0, 17u, 17u},
  /*  607 grid cal1 */
  {false, 1, true, true, true, 3000.0f, 300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f3504f3u, 0x3f3504f3u, 0x00000000u, 0u, true, 70, 70, 0, 19u, 19u},
  /*  608 grid cal1 */
  {false, 1, true, true, true, 3000.0f, 600.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f4a12fdu, 0x3f1d2b36u, 0x00000000u, 0u, true, 78, 61, 0, 19u, 0u},
  /*  609 grid cal1 */
  {false, 1, true, true, true, 3000.0f, 900.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f5c16edu, 0x3ef48b41u, 0x00000000u, 0u, true, 85, 47, 0, 19u, 0u},
  /*  610 grid cal1 */
  {false, 1, true, true, true, 3000.0f, 1200.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f59d852u, 0x3e913ae2u, 0x00000000u, 0u, true, 85, 28, 0, 19u, 0u},
  /*  611 grid cal1 */
  {false, 1, true, true, true, 3000.0f, 1500.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f587bd2u, 0x3dc06e11u, 0x00000000u, 0u, true, 84, 9, 0, 19u, 0u},
  /*  612 grid cal1 */
  {false, 1, true, true, true, 3000.0f, 1800.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f587bd2u, 0xbdc06e11u, 0x00000000u, 0u, true, 84, -9, 0, 19u, 0u},
  /*  613 grid cal1 */
  {false, 1, true, true, true, 3000.0f, 2100.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f59d852u, 0xbe913ae2u, 0x00000000u, 0u, true, 85, -28, 0, 19u, 0u},
  /*  614 grid cal1 */
  {false, 1, true, true, true, 3000.0f, 2400.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f5c16edu, 0xbef48b41u, 0x00000000u, 0u, true, 85, -47, 0, 19u, 0u},
  /*  615 grid cal1 */
  {false, 1, true, true, true, 3000.0f, 2700.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f4a12fdu, 0xbf1d2b36u, 0x00000000u, 0u, true, 78, -61, 0, 19u, 0u},
  /*  616 grid cal1 */
  {false, 1, true, true, true, 3000.0f, 3000.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f3504f3u, 0xbf3504f3u, 0x00000000u, 0u, true, 70, -70, 0, 19u, 0u},
  /*  617 grid cal1 */
  {false, 1, true, true, true, 3000.0f, 3300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f221bdfu, 0xbf462211u, 0x00000000u, 0u, true, 63, -77, 0, 18u, 18u},
  /*  618 grid cal1 */
  {false, 1, true, true, true, 3300.0f, 0.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f3504f3u, 0x3f3504f3u, 0x00000000u, 0u, true, 70, 70, 0, 19u, 19u},
  /*  619 grid cal1 */
  {false, 1, true, true, true, 3300.0f, 300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f462211u, 0x3f221bdfu, 0x00000000u, 0u, true, 77, 63, 0, 19u, 0u},
  /*  620 grid cal1 */
  {false, 1, true, true, true, 3300.0f, 600.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f57fa33u, 0x3f0970acu, 0x00000000u, 0u, true, 84, 53, 0, 19u, 0u},
  /*  621 grid cal1 */
  {false, 1, true, true, true, 3300.0f, 900.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f690dc6u, 0x3ed3ddfbu, 0x00000000u, 0u, true, 91, 41, 0, 19u, 0u},
  /*  622 grid cal1 */
  {false, 1, true, true, true, 3300.0f, 1200.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f76fac3u, 0x3e86b753u, 0x00000000u, 0u, true, 96, 26, 0, 19u, 0u},
  /*  623 grid cal1 */
  {false, 1, true, true, true, 3300.0f, 1500.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f7ef2dcu, 0x3db96acfu, 0x00000000u, 0u, true, 99, 9, 0, 19u, 0u},
  /*  624 grid cal1 */
  {false, 1, true, true, true, 3300.0f, 1800.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f7ef2dcu, 0xbdb96acfu, 0x00000000u, 0u, true, 99, -9, 0, 19u, 0u},
  /*  625 grid cal1 */
  {false, 1, true, true, true, 3300.0f, 2100.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f76fac3u, 0xbe86b753u, 0x00000000u, 0u, true, 96, -26, 0, 19u, 0u},
  /*  626 grid cal1 */
  {false, 1, true, true, true, 3300.0f, 2400.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f690dc6u, 0xbed3ddfbu, 0x00000000u, 0u, true, 91, -41, 0, 19u, 0u},
  /*  627 grid cal1 */
  {false, 1, true, true, true, 3300.0f, 2700.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f57fa33u, 0xbf0970acu, 0x00000000u, 0u, true, 84, -53, 0, 19u, 0u},
  /*  628 grid cal1 */
  {false, 1, true, true, true, 3300.0f, 3000.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f462211u, 0xbf221bdfu, 0x00000000u, 0u, true, 77, -63, 0, 19u, 0u},
  /*  629 grid cal1 */
  {false, 1, true, true, true, 3300.0f, 3300.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f3504f3u, 0xbf3504f3u, 0x00000000u, 0u, true, 70, -70, 0, 19u, 0u},
  /*  630 grid cal2 */
  {true, 2, true, true, true, 0.0f, 0.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf3504f3u, 0x3f3504f3u, 0x00000000u, 0u, true, -70, 70, 0, 20u, 20u},
  /*  631 grid cal2 */
  {false, 2, true, true, true, 0.0f, 300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf477636u, 0x3f2078a2u, 0x00000000u, 0u, true, -77, 62, 0, 20u, 0u},
  /*  632 grid cal2 */
  {false, 2, true, true, true, 0.0f, 600.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf5b0745u, 0x3f04861au, 0x00000000u, 0u, true, -85, 51, 0, 20u, 0u},
  /*  633 grid cal2 */
  {false, 2, true, true, true, 0.0f, 900.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf6d3afcu, 0x3ec06f2au, 0x00000000u, 0u, true, -92, 37, 0, 20u, 0u},
  /*  634 grid cal2 */
  {false, 2, true, true, true, 0.0f, 1200.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf7abab7u, 0x3e4eb7e6u, 0x00000000u, 0u, true, -97, 20, 0, 20u, 0u},
  /*  635 grid cal2 */
  {false, 2, true, true, true, 0.0f, 1500.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf7ffe8eu, 0x3bd9de17u, 0x00000000u, 0u, true, -99, 0, 0, 20u, 0u},
  /*  636 grid cal2 */
  {false, 2, true, true, true, 0.0f, 1800.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf7b0abdu, 0xbe488e94u, 0x00000000u, 0u, true, -98, -19, 0, 20u, 0u},
  /*  637 grid cal2 */
  {false, 2, true, true, true, 0.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf6d2b17u, 0xbec0bd77u, 0x00000000u, 0u, true, -92, -37, 0, 20u, 0u},
  /*  638 grid cal2 */
  {false, 2, true, true, true, 0.0f, 2400.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf5a42f9u, 0xbf05c86fu, 0x00000000u, 0u, true, -85, -52, 0, 20u, 0u},
  /*  639 grid cal2 */
  {false, 2, true, true, true, 0.0f, 2700.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf45ffd5u, 0xbf2245aeu, 0x00000000u, 0u, true, -77, -63, 0, 20u, 0u},
  /*  640 grid cal2 */
  {false, 2, true, true, true, 0.0f, 3000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf3504f3u, 0xbf3504f3u, 0x00000000u, 0u, true, -70, -70, 0, 20u, 0u},
  /*  641 grid cal2 */
  {false, 2, true, true, true, 0.0f, 3300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf3504f3u, 0xbf3504f3u, 0x00000000u, 0u, true, -70, -70, 0, 20u, 0u},
  /*  642 grid cal2 */
  {false, 2, true, true, true, 300.0f, 0.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf20bfb6u, 0x3f473cf0u, 0x00000000u, 0u, true, -62, 77, 0, 17u, 17u},
  /*  643 grid cal2 */
  {false, 2, true, true, true, 300.0f, 300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf3546f6u, 0x3f34c2d8u, 0x00000000u, 0u, true, -70, 70, 0, 20u, 20u},
  /*  644 grid cal2 */
  {false, 2, true, true, true, 300.0f, 600.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf4cceb0u, 0x3f199717u, 0x00000000u, 0u, true, -80, 59, 0, 20u, 0u},
  /*  645 grid cal2 */
  {false, 2, true, true, true, 300.0f, 900.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf5815f1u, 0x3ed94054u, 0x00000000u, 0u, true, -84, 42, 0, 20u, 0u},
  /*  646 grid cal2 */
  {false, 2, true, true, true, 300.0f, 1200.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf55d06eu, 0x3e5a7dd7u, 0x00000000u, 0u, true, -83, 21, 0, 20u, 0u},
  /*  647 grid cal2 */
  {false, 2, true, true, true, 300.0f, 1500.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf54e0c7u, 0x3be08d5du, 0x00000000u, 0u, true, -83, 0, 0, 20u, 0u},
  /*  648 grid cal2 */
  {false, 2, true, true, true, 300.0f, 1800.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf55c26bu, 0xbe53a93du, 0x00000000u, 0u, true, -83, -20, 0, 20u, 0u},
  /*  649 grid cal2 */
  {false, 2, true, true, true, 300.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf581883u, 0xbed9a9e7u, 0x00000000u, 0u, true, -84, -42, 0, 20u, 0u},
  /*  650 grid cal2 */
  {false, 2, true, true, true, 300.0f, 2400.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf4bd895u, 0xbf1add23u, 0x00000000u, 0u, true, -79, -60, 0, 20u, 0u},
  /*  651 grid cal2 */
  {false, 2, true, true, true, 300.0f, 2700.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf339881u, 0xbf366e8du, 0x00000000u, 0u, true, -70, -71, 0, 18u, 18u},
  /*  652 grid cal2 */
  {false, 2, true, true, true, 300.0f, 3000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf20bfb6u, 0xbf473cf0u, 0x00000000u, 0u, true, -62, -77, 0, 18u, 0u},
  /*  653 grid cal2 */
  {false, 2, true, true, true, 300.0f, 3300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf20bfb6u, 0xbf473cf0u, 0x00000000u, 0u, true, -62, -77, 0, 18u, 0u},
  /*  654 grid cal2 */
  {false, 2, true, true, true, 600.0f, 0.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf04b88du, 0x3f5ae8b5u, 0x00000000u, 0u, true, -51, 85, 0, 17u, 17u},
  /*  655 grid cal2 */
  {false, 2, true, true, true, 600.0f, 300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf1a11eeu, 0x3f4c7258u, 0x00000000u, 0u, true, -60, 79, 0, 17u, 0u},
  /*  656 grid cal2 */
  {false, 2, true, true, true, 600.0f, 600.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf2147c1u, 0x3f20f3f9u, 0x00000000u, 0u, true, -63, 62, 0, 20u, 20u},
  /*  657 grid cal2 */
  {false, 2, true, true, true, 600.0f, 900.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf1d90c7u, 0x3ed2d026u, 0x00000000u, 0u, true, -61, 41, 0, 20u, 0u},
  /*  658 grid cal2 */
  {false, 2, true, true, true, 600.0f, 1200.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf1a155du, 0x3e5188aau, 0x00000000u, 0u, true, -60, 20, 0, 20u, 0u},
  /*  659 grid cal2 */
  {false, 2, true, true, true, 600.0f, 1500.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf187b83u, 0x3bd60baeu, 0x00000000u, 0u, true, -59, 0, 0, 20u, 0u},
  /*  660 grid cal2 */
  {false, 2, true, true, true, 600.0f, 1800.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf19fe2au, 0xbe4aea7cu, 0x00000000u, 0u, true, -60, -19, 0, 20u, 0u},
  /*  661 grid cal2 */
  {false, 2, true, true, true, 600.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf1d9472u, 0xbed338feu, 0x00000000u, 0u, true, -61, -41, 0, 20u, 0u},
  /*  662 grid cal2 */
  {false, 2, true, true, true, 600.0f, 2400.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf216b2cu, 0xbf233164u, 0x00000000u, 0u, true, -63, -63, 0, 18u, 18u},
  /*  663 grid cal2 */
  {false, 2, true, true, true, 600.0f, 2700.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf183fb9u, 0xbf4dce70u, 0x00000000u, 0u, true, -59, -80, 0, 18u, 0u},
  /*  664 grid cal2 */
  {false, 2, true, true, true, 600.0f, 3000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf04b88du, 0xbf5ae8b5u, 0x00000000u, 0u, true, -51, -85, 0, 18u, 0u},
  /*  665 grid cal2 */
  {false, 2, true, true, true, 600.0f, 3300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf04b88du, 0xbf5ae8b5u, 0x00000000u, 0u, true, -51, -85, 0, 18u, 0u},
  /*  666 grid cal2 */
  {false, 2, true, true, true, 900.0f, 0.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbec08039u, 0x3f6d3786u, 0x00000000u, 0u, true, -37, 92, 0, 17u, 17u},
  /*  667 grid cal2 */
  {false, 2, true, true, true, 900.0f, 300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbed9478cu, 0x3f576961u, 0x00000000u, 0u, true, -42, 84, 0, 17u, 0u},
  /*  668 grid cal2 */
  {false, 2, true, true, true, 900.0f, 600.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbed2daf3u, 0x3f1d36c1u, 0x00000000u, 0u, true, -41, 61, 0, 17u, 0u},
  /*  669 grid cal2 */
  {false, 2, true, true, true, 900.0f, 900.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbec9cd73u, 0x3ec9b8a0u, 0x00000000u, 0u, true, -39, 39, 0, 20u, 20u},
  /*  670 grid cal2 */
  {false, 2, true, true, true, 900.0f, 1200.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbebeb35au, 0x3e41bfabu, 0x00000000u, 0u, true, -37, 18, 0, 20u, 0u},
  /*  671 grid cal2 */
  {false, 2, true, true, true, 900.0f, 1500.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbeb82d3du, 0x3bc1281du, 0x00000000u, 0u, true, -35, 0, 0, 20u, 0u},
  /*  672 grid cal2 */
  {false, 2, true, true, true, 900.0f, 1800.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbebe5c6fu, 0xbe3b67d1u, 0x00000000u, 0u, true, -37, -18, 0, 20u, 0u},
  /*  673 grid cal2 */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbec9d78au, 0xbeca2259u, 0x00000000u, 0u, true, -39, -39, 0, 18u, 18u},
  /*  674 grid cal2 */
  {false, 2, true, true, true, 900.0f, 2400.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbed327b6u, 0xbf1f7ddbu, 0x00000000u, 0u, true, -41, -62, 0, 18u, 0u},
  /*  675 grid cal2 */
  {false, 2, true, true, true, 900.0f, 2700.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbed9adb0u, 0xbf5bd74du, 0x00000000u, 0u, true, -42, -85, 0, 18u, 0u},
  /*  676 grid cal2 */
  {false, 2, true, true, true, 900.0f, 3000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbec08039u, 0xbf6d3786u, 0x00000000u, 0u, true, -37, -92, 0, 18u, 0u},
  /*  677 grid cal2 */
  {false, 2, true, true, true, 900.0f, 3300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbec08039u, 0xbf6d3786u, 0x00000000u, 0u, true, -37, -92, 0, 18u, 0u},
  /*  678 grid cal2 */
  {false, 2, true, true, true, 1200.0f, 0.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe4dd96cu, 0x3f7ac629u, 0x00000000u, 0u, true, -20, 97, 0, 17u, 17u},
  /*  679 grid cal2 */
  {false, 2, true, true, true, 1200.0f, 300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe597259u, 0x3f551ea2u, 0x00000000u, 0u, true, -21, 83, 0, 17u, 0u},
  /*  680 grid cal2 */
  {false, 2, true, true, true, 1200.0f, 600.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe50871eu, 0x3f19b4bbu, 0x00000000u, 0u, true, -20, 60, 0, 17u, 0u},
  /*  681 grid cal2 */
  {false, 2, true, true, true, 1200.0f, 900.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe40d563u, 0x3ebe8eebu, 0x00000000u, 0u, true, -18, 37, 0, 17u, 0u},
  /*  682 grid cal2 */
  {false, 2, true, true, true, 1200.0f, 1200.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe22395du, 0x3e22f020u, 0x00000000u, 0u, true, -15, 15, 0, 17u, 0u},
  /*  683 grid cal2 */
  {false, 2, true, true, true, 1200.0f, 1500.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbdfda147u, 0x3b837b5du, 0x00000000u, 0u, true, -12, 0, 0, 20u, 20u},
  /*  684 grid cal2 */
  {false, 2, true, true, true, 1200.0f, 1800.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe20e3d7u, 0xbe1c9615u, 0x00000000u, 0u, true, -15, -15, 0, 20u, 0u},
  /*  685 grid cal2 */
  {false, 2, true, true, true, 1200.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe40e9e3u, 0xbebefd8au, 0x00000000u, 0u, true, -18, -37, 0, 18u, 18u},
  /*  686 grid cal2 */
  {false, 2, true, true, true, 1200.0f, 2400.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe50f9c5u, 0xbf1c0bbfu, 0x00000000u, 0u, true, -20, -60, 0, 18u, 0u},
  /*  687 grid cal2 */
  {false, 2, true, true, true, 1200.0f, 2700.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe59f5e3u, 0xbf599dc3u, 0x00000000u, 0u, true, -21, -85, 0, 18u, 0u},
  /*  688 grid cal2 */
  {false, 2, true, true, true, 1200.0f, 3000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe4dd96cu, 0xbf7ac629u, 0x00000000u, 0u, true, -20, -97, 0, 18u, 0u},
  /*  689 grid cal2 */
  {false, 2, true, true, true, 1200.0f, 3300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbe4dd96cu, 0xbf7ac629u, 0x00000000u, 0u, true, -20, -97, 0, 18u, 0u},
  /*  690 grid cal2 */
  {false, 2, true, true, true, 1500.0f, 0.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbb995314u, 0x3f7fff48u, 0x00000000u, 0u, true, 0, 99, 0, 17u, 17u},
  /*  691 grid cal2 */
  {false, 2, true, true, true, 1500.0f, 300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbb9df680u, 0x3f542f8cu, 0x00000000u, 0u, true, 0, 82, 0, 17u, 0u},
  /*  692 grid cal2 */
  {false, 2, true, true, true, 1500.0f, 600.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbb969260u, 0x3f181c6cu, 0x00000000u, 0u, true, 0, 59, 0, 17u, 0u},
  /*  693 grid cal2 */
  {false, 2, true, true, true, 1500.0f, 900.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbb87e96bu, 0x3eb812f9u, 0x00000000u, 0u, true, 0, 35, 0, 17u, 0u},
  /*  694 grid cal2 */
  {false, 2, true, true, true, 1500.0f, 1200.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbb39c83cu, 0x3dffbdddu, 0x00000000u, 0u, true, 0, 12, 0, 17u, 0u},
  /*  695 grid cal2 */
  {false, 2, true, true, true, 1500.0f, 1500.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  696 grid cal2 */
  {false, 2, true, true, true, 1500.0f, 1800.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbb342f3du, 0xbdf057f2u, 0x00000000u, 0u, true, 0, -11, 0, 18u, 18u},
  /*  697 grid cal2 */
  {false, 2, true, true, true, 1500.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbb87fe75u, 0xbeb886c6u, 0x00000000u, 0u, true, 0, -36, 0, 18u, 0u},
  /*  698 grid cal2 */
  {false, 2, true, true, true, 1500.0f, 2400.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbb96f4abu, 0xbf1a7d18u, 0x00000000u, 0u, true, 0, -60, 0, 18u, 0u},
  /*  699 grid cal2 */
  {false, 2, true, true, true, 1500.0f, 2700.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbb9e5fd0u, 0xbf58b6fdu, 0x00000000u, 0u, true, 0, -84, 0, 18u, 0u},
  /*  700 grid cal2 */
  {false, 2, true, true, true, 1500.0f, 3000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbb995314u, 0xbf7fff48u, 0x00000000u, 0u, true, 0, -99, 0, 18u, 0u},
  /*  701 grid cal2 */
  {false, 2, true, true, true, 1500.0f, 3300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbb995314u, 0xbf7fff48u, 0x00000000u, 0u, true, 0, -99, 0, 18u, 0u},
  /*  702 grid cal2 */
  {false, 2, true, true, true, 1800.0f, 0.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e48f46fu, 0x3f7b05a5u, 0x00000000u, 0u, true, 19, 98, 0, 17u, 17u},
  /*  703 grid cal2 */
  {false, 2, true, true, true, 1800.0f, 300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e5405f6u, 0x3f551374u, 0x00000000u, 0u, true, 20, 83, 0, 17u, 0u},
  /*  704 grid cal2 */
  {false, 2, true, true, true, 1800.0f, 600.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e4b45e7u, 0x3f19a241u, 0x00000000u, 0u, true, 19, 60, 0, 17u, 0u},
  /*  705 grid cal2 */
  {false, 2, true, true, true, 1800.0f, 900.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e3bcbe1u, 0x3ebe49e0u, 0x00000000u, 0u, true, 18, 37, 0, 17u, 0u},
  /*  706 grid cal2 */
  {false, 2, true, true, true, 1800.0f, 1200.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e1d2eecu, 0x3e21e154u, 0x00000000u, 0u, true, 15, 15, 0, 17u, 0u},
  /*  707 grid cal2 */
  {false, 2, true, true, true, 1800.0f, 1500.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3df16739u, 0x3b80517du, 0x00000000u, 0u, true, 11, 0, 0, 19u, 19u},
  /*  708 grid cal2 */
  {false, 2, true, true, true, 1800.0f, 1800.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e1bd51bu, 0xbe1b82f7u, 0x00000000u, 0u, true, 15, -15, 0, 19u, 0u},
  /*  709 grid cal2 */
  {false, 2, true, true, true, 1800.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e3be02cu, 0xbebeb8acu, 0x00000000u, 0u, true, 18, -37, 0, 18u, 18u},
  /*  710 grid cal2 */
  {false, 2, true, true, true, 1800.0f, 2400.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e4bb68du, 0xbf1bf9acu, 0x00000000u, 0u, true, 19, -60, 0, 18u, 0u},
  /*  711 grid cal2 */
  {false, 2, true, true, true, 1800.0f, 2700.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e5486d0u, 0xbf5992f6u, 0x00000000u, 0u, true, 20, -84, 0, 18u, 0u},
  /*  712 grid cal2 */
  {false, 2, true, true, true, 1800.0f, 3000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e48f46fu, 0xbf7b05a5u, 0x00000000u, 0u, true, 19, -98, 0, 18u, 0u},
  /*  713 grid cal2 */
  {false, 2, true, true, true, 1800.0f, 3300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e48f46fu, 0xbf7b05a5u, 0x00000000u, 0u, true, 19, -98, 0, 18u, 0u},
  /*  714 grid cal2 */
  {false, 2, true, true, true, 2100.0f, 0.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ec037d2u, 0x3f6d4632u, 0x00000000u, 0u, true, 37, 92, 0, 17u, 17u},
  /*  715 grid cal2 */
  {false, 2, true, true, true, 2100.0f, 300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ed8e601u, 0x3f5766fdu, 0x00000000u, 0u, true, 42, 84, 0, 17u, 0u},
  /*  716 grid cal2 */
  {false, 2, true, true, true, 2100.0f, 600.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ed27a13u, 0x3f1d335cu, 0x00000000u, 0u, true, 41, 61, 0, 17u, 0u},
  /*  717 grid cal2 */
  {false, 2, true, true, true, 2100.0f, 900.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ec96bc5u, 0x3ec9af4cu, 0x00000000u, 0u, true, 39, 39, 0, 17u, 0u},
  /*  718 grid cal2 */
  {false, 2, true, true, true, 2100.0f, 1200.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ebe4d2bu, 0x3e41aca8u, 0x00000000u, 0u, true, 37, 18, 0, 19u, 19u},
  /*  719 grid cal2 */
  {false, 2, true, true, true, 2100.0f, 1500.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3eb7c23au, 0x3bc10c69u, 0x00000000u, 0u, true, 35, 0, 0, 19u, 0u},
  /*  720 grid cal2 */
  {false, 2, true, true, true, 2100.0f, 1800.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ebdf60au, 0xbe3b550bu, 0x00000000u, 0u, true, 37, -18, 0, 19u, 0u},
  /*  721 grid cal2 */
  {false, 2, true, true, true, 2100.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ec975deu, 0xbeca1907u, 0x00000000u, 0u, true, 39, -39, 0, 18u, 18u},
  /*  722 grid cal2 */
  {false, 2, true, true, true, 2100.0f, 2400.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ed2c6d3u, 0xbf1f7a81u, 0x00000000u, 0u, true, 41, -62, 0, 18u, 0u},
  /*  723 grid cal2 */
  {false, 2, true, true, true, 2100.0f, 2700.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ed94c13u, 0xbf5bd4f9u, 0x00000000u, 0u, true, 42, -85, 0, 18u, 0u},
  /*  724 grid cal2 */
  {false, 2, true, true, true, 2100.0f, 3000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ec037d2u, 0xbf6d4632u, 0x00000000u, 0u, true, 37, -92, 0, 18u, 0u},
  /*  725 grid cal2 */
  {false, 2, true, true, true, 2100.0f, 3300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ec037d2u, 0xbf6d4632u, 0x00000000u, 0u, true, 37, -92, 0, 18u, 0u},
  /*  726 grid cal2 */
  {false, 2, true, true, true, 2400.0f, 0.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f054f5cu, 0x3f5a8d00u, 0x00000000u, 0u, true, 52, 85, 0, 17u, 17u},
  /*  727 grid cal2 */
  {false, 2, true, true, true, 2400.0f, 300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f1aaa7eu, 0x3f4bff04u, 0x00000000u, 0u, true, 60, 79, 0, 17u, 0u},
  /*  728 grid cal2 */
  {false, 2, true, true, true, 2400.0f, 600.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f2253b4u, 0x3f21048eu, 0x00000000u, 0u, true, 63, 62, 0, 19u, 19u},
  /*  729 grid cal2 */
  {false, 2, true, true, true, 2400.0f, 900.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f1ea13eu, 0x3ed2f413u, 0x00000000u, 0u, true, 61, 41, 0, 19u, 0u},
  /*  730 grid cal2 */
  {false, 2, true, true, true, 2400.0f, 1200.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f1b2d3eu, 0x3e51be87u, 0x00000000u, 0u, true, 60, 20, 0, 19u, 0u},
  /*  731 grid cal2 */
  {false, 2, true, true, true, 2400.0f, 1500.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f1997f3u, 0x3bd64d20u, 0x00000000u, 0u, true, 59, 0, 0, 19u, 0u},
  /*  732 grid cal2 */
  {false, 2, true, true, true, 2400.0f, 1800.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f1b1648u, 0xbe4b1f2du, 0x00000000u, 0u, true, 60, -19, 0, 19u, 0u},
  /*  733 grid cal2 */
  {false, 2, true, true, true, 2400.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f1ea4e3u, 0xbed35cedu, 0x00000000u, 0u, true, 61, -41, 0, 19u, 0u},
  /*  734 grid cal2 */
  {false, 2, true, true, true, 2400.0f, 2400.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f227703u, 0xbf2341ddu, 0x00000000u, 0u, true, 63, -63, 0, 18u, 18u},
  /*  735 grid cal2 */
  {false, 2, true, true, true, 2400.0f, 2700.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f18d882u, 0xbf4d5d10u, 0x00000000u, 0u, true, 59, -80, 0, 18u, 0u},
  /*  736 grid cal2 */
  {false, 2, true, true, true, 2400.0f, 3000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f054f5cu, 0xbf5a8d00u, 0x00000000u, 0u, true, 52, -85, 0, 18u, 0u},
  /*  737 grid cal2 */
  {false, 2, true, true, true, 2400.0f, 3300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f054f5cu, 0xbf5a8d00u, 0x00000000u, 0u, true, 52, -85, 0, 18u, 0u},
  /*  738 grid cal2 */
  {false, 2, true, true, true, 2700.0f, 0.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f21b7a3u, 0x3f4673eau, 0x00000000u, 0u, true, 63, 77, 0, 17u, 17u},
  /*  739 grid cal2 */
  {false, 2, true, true, true, 2700.0f, 300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f362cbau, 0x3f33db47u, 0x00000000u, 0u, true, 71, 70, 0, 19u, 19u},
  /*  740 grid cal2 */
  {false, 2, true, true, true, 2700.0f, 600.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f4d89b6u, 0x3f189c73u, 0x00000000u, 0u, true, 80, 59, 0, 19u, 0u},
  /*  741 grid cal2 */
  {false, 2, true, true, true, 2700.0f, 900.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f5a7699u, 0x3ed97743u, 0x00000000u, 0u, true, 85, 42, 0, 19u, 0u},
  /*  742 grid cal2 */
  {false, 2, true, true, true, 2700.0f, 1200.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f583a51u, 0x3e5ac4e5u, 0x00000000u, 0u, true, 84, 21, 0, 19u, 0u},
  /*  743 grid cal2 */
  {false, 2, true, true, true, 2700.0f, 1500.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f574f2cu, 0x3be0ddebu, 0x00000000u, 0u, true, 84, 0, 0, 19u, 0u},
  /*  744 grid cal2 */
  {false, 2, true, true, true, 2700.0f, 1800.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f582c8eu, 0xbe53ee79u, 0x00000000u, 0u, true, 84, -20, 0, 19u, 0u},
  /*  745 grid cal2 */
  {false, 2, true, true, true, 2700.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f5a7924u, 0xbed9e0e1u, 0x00000000u, 0u, true, 85, -42, 0, 19u, 0u},
  /*  746 grid cal2 */
  {false, 2, true, true, true, 2700.0f, 2400.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f4c95d8u, 0xbf19e2c3u, 0x00000000u, 0u, true, 79, -60, 0, 19u, 0u},
  /*  747 grid cal2 */
  {false, 2, true, true, true, 2700.0f, 2700.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f348066u, 0xbf358921u, 0x00000000u, 0u, true, 70, -70, 0, 18u, 18u},
  /*  748 grid cal2 */
  {false, 2, true, true, true, 2700.0f, 3000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f21b7a3u, 0xbf4673eau, 0x00000000u, 0u, true, 63, -77, 0, 18u, 0u},
  /*  749 grid cal2 */
  {false, 2, true, true, true, 2700.0f, 3300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f21b7a3u, 0xbf4673eau, 0x00000000u, 0u, true, 63, -77, 0, 18u, 0u},
  /*  750 grid cal2 */
  {false, 2, true, true, true, 3000.0f, 0.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f3504f3u, 0x3f3504f3u, 0x00000000u, 0u, true, 70, 70, 0, 19u, 19u},
  /*  751 grid cal2 */
  {false, 2, true, true, true, 3000.0f, 300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f477636u, 0x3f2078a2u, 0x00000000u, 0u, true, 77, 62, 0, 19u, 0u},
  /*  752 grid cal2 */
  {false, 2, true, true, true, 3000.0f, 600.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f5b0745u, 0x3f04861au, 0x00000000u, 0u, true, 85, 51, 0, 19u, 0u},
  /*  753 grid cal2 */
  {false, 2, true, true, true, 3000.0f, 900.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f6d3afcu, 0x3ec06f2au, 0x00000000u, 0u, true, 92, 37, 0, 19u, 0u},
  /*  754 grid cal2 */
  {false, 2, true, true, true, 3000.0f, 1200.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f7abab7u, 0x3e4eb7e6u, 0x00000000u, 0u, true, 97, 20, 0, 19u, 0u},
  /*  755 grid cal2 */
  {false, 2, true, true, true, 3000.0f, 1500.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f7ffe8eu, 0x3bd9de17u, 0x00000000u, 0u, true, 99, 0, 0, 19u, 0u},
  /*  756 grid cal2 */
  {false, 2, true, true, true, 3000.0f, 1800.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f7b0abdu, 0xbe488e94u, 0x00000000u, 0u, true, 98, -19, 0, 19u, 0u},
  /*  757 grid cal2 */
  {false, 2, true, true, true, 3000.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f6d2b17u, 0xbec0bd77u, 0x00000000u, 0u, true, 92, -37, 0, 19u, 0u},
  /*  758 grid cal2 */
  {false, 2, true, true, true, 3000.0f, 2400.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f5a42f9u, 0xbf05c86fu, 0x00000000u, 0u, true, 85, -52, 0, 19u, 0u},
  /*  759 grid cal2 */
  {false, 2, true, true, true, 3000.0f, 2700.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f45ffd5u, 0xbf2245aeu, 0x00000000u, 0u, true, 77, -63, 0, 19u, 0u},
  /*  760 grid cal2 */
  {false, 2, true, true, true, 3000.0f, 3000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f3504f3u, 0xbf3504f3u, 0x00000000u, 0u, true, 70, -70, 0, 19u, 0u},
  /*  761 grid cal2 */
  {false, 2, true, true, true, 3000.0f, 3300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f3504f3u, 0xbf3504f3u, 0x00000000u, 0u, true, 70, -70, 0, 19u, 0u},
  /*  762 grid cal2 */
  {false, 2, true, true, true, 3300.0f, 0.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f3504f3u, 0x3f3504f3u, 0x00000000u, 0u, true, 70, 70, 0, 19u, 0u},
  /*  763 grid cal2 */
  {false, 2, true, true, true, 3300.0f, 300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f477636u, 0x3f2078a2u, 0x00000000u, 0u, true, 77, 62, 0, 19u, 0u},
  /*  764 grid cal2 */
  {false, 2, true, true, true, 3300.0f, 600.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f5b0745u, 0x3f04861au, 0x00000000u, 0u, true, 85, 51, 0, 19u, 0u},
  /*  765 grid cal2 */
  {false, 2, true, true, true, 3300.0f, 900.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f6d3afcu, 0x3ec06f2au, 0x00000000u, 0u, true, 92, 37, 0, 19u, 0u},
  /*  766 grid cal2 */
  {false, 2, true, true, true, 3300.0f, 1200.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f7abab7u, 0x3e4eb7e6u, 0x00000000u, 0u, true, 97, 20, 0, 19u, 0u},
  /*  767 grid cal2 */
  {false, 2, true, true, true, 3300.0f, 1500.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f7ffe8eu, 0x3bd9de17u, 0x00000000u, 0u, true, 99, 0, 0, 19u, 0u},
  /*  768 grid cal2 */
  {false, 2, true, true, true, 3300.0f, 1800.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f7b0abdu, 0xbe488e94u, 0x00000000u, 0u, true, 98, -19, 0, 19u, 0u},
  /*  769 grid cal2 */
  {false, 2, true, true, true, 3300.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f6d2b17u, 0xbec0bd77u, 0x00000000u, 0u, true, 92, -37, 0, 19u, 0u},
  /*  770 grid cal2 */
  {false, 2, true, true, true, 3300.0f, 2400.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f5a42f9u, 0xbf05c86fu, 0x00000000u, 0u, true, 85, -52, 0, 19u, 0u},
  /*  771 grid cal2 */
  {false, 2, true, true, true, 3300.0f, 2700.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f45ffd5u, 0xbf2245aeu, 0x00000000u, 0u, true, 77, -63, 0, 19u, 0u},
  /*  772 grid cal2 */
  {false, 2, true, true, true, 3300.0f, 3000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f3504f3u, 0xbf3504f3u, 0x00000000u, 0u, true, 70, -70, 0, 19u, 0u},
  /*  773 grid cal2 */
  {false, 2, true, true, true, 3300.0f, 3300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f3504f3u, 0xbf3504f3u, 0x00000000u, 0u, true, 70, -70, 0, 19u, 0u},
  /*  774 noise +-40 mV */
  {true, 2, true, true, true, 1468.6322f, 1471.32385f, 1483.04187f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  775 noise +-40 mV */
  {false, 2, true, true, true, 1517.79236f, 1542.80237f, 1422.74463f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  776 noise +-40 mV */
  {false, 2, true, true, true, 1506.67114f, 1513.86792f, 1490.45398f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  777 noise +-40 mV */
  {false, 2, true, true, true, 1529.67603f, 1473.18835f, 1511.1322f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  778 noise +-40 mV */
  {false, 2, true, true, true, 1544.26477f, 1475.89343f, 1504.94495f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  779 noise +-40 mV */
  {false, 2, true, true, true, 1503.82349f, 1501.71863f, 1541.33301f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3b4d53dau, 0u, true, 0, 0, 0, 0u, 0u},
  /*  780 noise +-40 mV */
  {false, 2, true, true, true, 1499.48999f, 1546.22119f, 1546.11499f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3bd7f71du, 0u, true, 0, 0, 0, 0u, 0u},
  /*  781 noise +-40 mV */
  {false, 2, true, true, true, 1520.09607f, 1503.83167f, 1476.9895f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  782 noise +-40 mV */
  {false, 2, true, true, true, 1489.98279f, 1503.86414f, 1501.54468f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  783 noise +-40 mV */
  {false, 2, true, true, true, 1479.42432f, 1477.10156f, 1526.37598f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  784 noise +-40 mV */
  {false, 2, true, true, true, 1491.92505f, 1470.56567f, 1471.77344f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  785 noise +-40 mV */
  {false, 2, true, true, true, 1490.33423f, 1515.83875f, 1541.71875f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3b5f9b4eu, 0u, true, 0, 0, 0, 0u, 0u},
  /*  786 noise +-40 mV */
  {false, 2, true, true, true, 1468.23718f, 1532.38037f, 1449.3147f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  787 noise +-40 mV */
  {false, 2, true, true, true, 1492.6908f, 1546.39905f, 1483.55591f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  788 noise +-40 mV */
  {false, 2, true, true, true, 1520.38098f, 1522.55713f, 1494.27832f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  789 noise +-40 mV */
  {false, 2, true, true, true, 1479.8999f, 1544.3252f, 1514.43921f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  790 noise +-40 mV */
  {false, 2, true, true, true, 1503.56531f, 1528.05444f, 1453.44824f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  791 noise +-40 mV */
  {false, 2, true, true, true, 1524.01208f, 1516.08423f, 1513.59912f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  792 noise +-40 mV */
  {false, 2, true, true, true, 1502.41907f, 1486.47729f, 1470.97314f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  793 noise +-40 mV */
  {false, 2, true, true, true, 1520.74915f, 1511.68079f, 1529.17822f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  794 noise +-40 mV */
  {false, 2, true, true, true, 1498.32837f, 1544.05139f, 1510.91516f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  795 noise +-40 mV */
  {false, 2, true, true, true, 1478.42834f, 1524.81433f, 1456.02307f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  796 noise +-40 mV */
  {false, 2, true, true, true, 1495.6582f, 1533.45093f, 1518.52551f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  797 noise +-40 mV */
  {false, 2, true, true, true, 1481.08789f, 1479.51831f, 1463.56909f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  798 noise +-40 mV */
  {false, 2, true, true, true, 1536.96399f, 1527.87805f, 1482.01648f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  799 noise +-40 mV */
  {false, 2, true, true, true, 1486.8175f, 1501.35547f, 1528.80029f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  800 noise +-40 mV */
  {false, 2, true, true, true, 1532.06677f, 1499.74292f, 1523.53369f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  801 noise +-40 mV */
  {false, 2, true, true, true, 1522.37988f, 1512.26208f, 1532.37292f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  802 noise +-40 mV */
  {false, 2, true, true, true, 1517.50537f, 1543.62292f, 1460.74658f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  803 noise +-40 mV */
  {false, 2, true, true, true, 1529.65979f, 1536.02332f, 1420.5033f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  804 noise +-40 mV */
  {false, 2, true, true, true, 1516.39563f, 1518.88098f, 1422.72278f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  805 noise +-40 mV */
  {false, 2, true, true, true, 1513.3053f, 1541.12744f, 1419.88672f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  806 noise +-40 mV */
  {false, 2, true, true, true, 1489.11523f, 1532.40173f, 1509.27673f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  807 noise +-40 mV */
  {false, 2, true, true, true, 1497.13049f, 1484.38867f, 1446.49194f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  808 noise +-40 mV */
  {false, 2, true, true, true, 1537.50122f, 1501.35901f, 1515.54443f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  809 noise +-40 mV */
  {false, 2, true, true, true, 1524.42493f, 1477.24084f, 1510.30115f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  810 noise +-40 mV */
  {false, 2, true, true, true, 1534.38525f, 1514.75391f, 1436.87305f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  811 noise +-40 mV */
  {false, 2, true, true, true, 1539.271f, 1501.99963f, 1435.70728f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  812 noise +-40 mV */
  {false, 2, true, true, true, 1521.30908f, 1535.6665f, 1546.98962f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3becb038u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  813 noise +-40 mV */
  {false, 2, true, true, true, 1467.29578f, 1522.65088f, 1410.06995f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbba61e57u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  814 noise +-40 mV */
  {false, 2, true, true, true, 1495.15198f, 1478.74377f, 1478.55884f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  815 noise +-40 mV */
  {false, 2, true, true, true, 1539.98804f, 1548.86877f, 1449.63208f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  816 noise +-40 mV */
  {false, 2, true, true, true, 1494.16907f, 1544.39734f, 1533.43054f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  817 noise +-40 mV */
  {false, 2, true, true, true, 1481.79077f, 1527.97913f, 1491.35388f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  818 noise +-40 mV */
  {false, 2, true, true, true, 1500.57788f, 1497.51428f, 1543.83728f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3ba1ffa1u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  819 noise +-40 mV */
  {false, 2, true, true, true, 1528.63f, 1522.47583f, 1469.98328f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  820 noise +-40 mV */
  {false, 2, true, true, true, 1505.34949f, 1509.1864f, 1459.30103f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  821 noise +-40 mV */
  {false, 2, true, true, true, 1495.37073f, 1545.46289f, 1460.60034f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  822 noise +-40 mV */
  {false, 2, true, true, true, 1476.24731f, 1520.04321f, 1500.78418f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  823 noise +-40 mV */
  {false, 2, true, true, true, 1502.89319f, 1538.03662f, 1426.69556f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  824 noise +-40 mV */
  {false, 2, true, true, true, 1477.42407f, 1518.22375f, 1487.42969f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  825 noise +-40 mV */
  {false, 2, true, true, true, 1518.06055f, 1506.13696f, 1471.32373f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  826 noise +-40 mV */
  {false, 2, true, true, true, 1513.03003f, 1498.854f, 1495.82422f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  827 noise +-40 mV */
  {false, 2, true, true, true, 1476.80444f, 1494.78699f, 1432.76245f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  828 noise +-40 mV */
  {false, 2, true, true, true, 1513.29712f, 1470.0979f, 1420.83862f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  829 noise +-40 mV */
  {false, 2, true, true, true, 1536.87756f, 1489.90759f, 1535.14575f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  830 noise +-40 mV */
  {false, 2, true, true, true, 1474.45789f, 1498.4281f, 1484.50452f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  831 noise +-40 mV */
  {false, 2, true, true, true, 1467.5332f, 1487.03247f, 1427.49353f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  832 noise +-40 mV */
  {false, 2, true, true, true, 1528.62122f, 1487.71643f, 1408.81812f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbbc42036u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  833 noise +-40 mV */
  {false, 2, true, true, true, 1493.46313f, 1517.26587f, 1460.16199f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  834 noise +-170 mV */
  {true, 2, true, true, true, 1568.06958f, 1586.80457f, 1423.5752f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  835 noise +-170 mV */
  {false, 2, true, true, true, 1370.06958f, 1547.03589f, 1424.75378f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  836 noise +-170 mV */
  {false, 2, true, true, true, 1470.25769f, 1365.73486f, 1445.09241f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  837 noise +-170 mV */
  {false, 2, true, true, true, 1599.09058f, 1430.91833f, 1480.69312f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  838 noise +-170 mV */
  {false, 2, true, true, true, 1445.66382f, 1480.18457f, 1509.69177f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  839 noise +-170 mV */
  {false, 2, true, true, true, 1589.87146f, 1383.51917f, 1497.74438f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3a6c6a44u, 0x3aaf9d15u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  840 noise +-170 mV */
  {false, 2, true, true, true, 1673.64258f, 1371.81287f, 1440.88733f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3d2d8b13u, 0x3d0c1504u, 0x00000000u, 0u, true, 4, 3, 0, 19u, 19u},
  /*  841 noise +-170 mV */
  {false, 2, true, true, true, 1468.07617f, 1634.96228f, 1537.48682f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x39b88caeu, 0u, true, 0, 0, 0, 0u, 0u},
  /*  842 noise +-170 mV */
  {false, 2, true, true, true, 1478.30273f, 1540.95361f, 1465.05176f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  843 noise +-170 mV */
  {false, 2, true, true, true, 1412.15894f, 1409.3999f, 1506.88989f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  844 noise +-170 mV */
  {false, 2, true, true, true, 1586.65381f, 1630.82141f, 1450.7124f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  845 noise +-170 mV */
  {false, 2, true, true, true, 1479.66919f, 1473.80103f, 1542.43103f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3b80ae00u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  846 noise +-170 mV */
  {false, 2, true, true, true, 1387.71558f, 1521.58398f, 1545.27625f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3bc417b2u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  847 noise +-170 mV */
  {false, 2, true, true, true, 1583.51587f, 1440.42261f, 1452.39355f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  848 noise +-170 mV */
  {false, 2, true, true, true, 1570.02087f, 1472.13464f, 1523.67102f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  849 noise +-170 mV */
  {false, 2, true, true, true, 1513.89124f, 1623.32239f, 1496.84082f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  850 noise +-170 mV */
  {false, 2, true, true, true, 1590.66248f, 1581.7854f, 1539.85046f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3b071311u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  851 noise +-170 mV */
  {false, 2, true, true, true, 1494.23096f, 1624.05835f, 1408.02197f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbbd735bfu, 0u, true, 0, 0, 0, 0u, 0u},
  /*  852 noise +-170 mV */
  {false, 2, true, true, true, 1567.72815f, 1357.09546f, 1528.10999f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3b8ec0f3u, 0x3c2eefccu, 0x00000000u, 0u, true, 0, 1, 0, 0u, 0u},
  /*  853 noise +-170 mV */
  {false, 2, true, true, true, 1374.60535f, 1526.97229f, 1487.38306f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  854 noise +-170 mV */
  {false, 2, true, true, true, 1635.87646f, 1401.8783f, 1490.43616f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3c4bdc67u, 0x3c267b50u, 0x00000000u, 0u, true, 1, 1, 0, 0u, 0u},
  /*  855 noise +-170 mV */
  {false, 2, true, true, true, 1594.55261f, 1646.42737f, 1427.6842f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3beb7bbau, 0xbc38fc36u, 0x00000000u, 0u, true, 0, -1, 0, 0u, 0u},
  /*  856 noise +-170 mV */
  {false, 2, true, true, true, 1398.18628f, 1532.35852f, 1472.42139f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  857 noise +-170 mV */
  {false, 2, true, true, true, 1450.81897f, 1543.31287f, 1478.6991f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  858 noise +-170 mV */
  {false, 2, true, true, true, 1572.50977f, 1535.71704f, 1447.36609f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  859 noise +-170 mV */
  {false, 2, true, true, true, 1455.02405f, 1622.28784f, 1431.36511f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  860 noise +-170 mV */
  {false, 2, true, true, true, 1583.62524f, 1390.12854f, 1479.80957f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  861 noise +-170 mV */
  {false, 2, true, true, true, 1463.16174f, 1450.86426f, 1416.79028f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xb920ddbbu, 0u, true, 0, 0, 0, 0u, 0u},
  /*  862 noise +-170 mV */
  {false, 2, true, true, true, 1399.77124f, 1409.10156f, 1539.63647f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3af9de50u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  863 noise +-170 mV */
  {false, 2, true, true, true, 1434.33826f, 1432.77905f, 1531.97754f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  864 noise +-170 mV */
  {false, 2, true, true, true, 1609.89783f, 1633.73022f, 1449.83545f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3c004a25u, 0xbc1b89a5u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  865 noise +-170 mV */
  {false, 2, true, true, true, 1391.47717f, 1637.41992f, 1492.22693f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbc571bdeu, 0xbc747399u, 0x00000000u, 0u, true, -1, -1, 0, 0u, 0u},
  /*  866 noise +-170 mV */
  {false, 2, true, true, true, 1652.4425f, 1374.61572f, 1428.70605f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3cf34442u, 0x3cdc6bbcu, 0x00000000u, 0u, true, 2, 2, 0, 0u, 0u},
  /*  867 noise +-170 mV */
  {false, 2, true, true, true, 1383.15088f, 1480.60535f, 1479.69397f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  868 noise +-170 mV */
  {false, 2, true, true, true, 1433.88074f, 1571.94824f, 1454.55029f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  869 noise +-170 mV */
  {false, 2, true, true, true, 1624.46521f, 1486.02234f, 1467.24414f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  870 noise +-170 mV */
  {false, 2, true, true, true, 1461.96082f, 1358.82776f, 1471.41821f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbad9edd9u, 0x3bb5e517u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  871 noise +-170 mV */
  {false, 2, true, true, true, 1457.28674f, 1457.34998f, 1435.32275f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  872 noise +-170 mV */
  {false, 2, true, true, true, 1600.66089f, 1630.57324f, 1432.03223f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3b631618u, 0xbb93605eu, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  873 noise +-170 mV */
  {false, 2, true, true, true, 1497.3125f, 1454.78577f, 1505.78748f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  874 noise +-170 mV */
  {false, 2, true, true, true, 1397.75574f, 1642.79187f, 1512.35474f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbc4cd86au, 0xbc8045cau, 0x00000000u, 0u, true, -1, -1, 0, 0u, 0u},
  /*  875 noise +-170 mV */
  {false, 2, true, true, true, 1358.77832f, 1622.60425f, 1419.28784f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbcc3f528u, 0xbc99616bu, 0x00000000u, 0u, true, -2, -1, 0, 0u, 0u},
  /*  876 noise +-170 mV */
  {false, 2, true, true, true, 1497.69434f, 1534.59326f, 1435.13733f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  877 noise +-170 mV */
  {false, 2, true, true, true, 1588.24939f, 1492.75439f, 1520.56909f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  878 noise +-170 mV */
  {false, 2, true, true, true, 1478.52747f, 1541.78857f, 1520.9375f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  879 noise +-170 mV */
  {false, 2, true, true, true, 1548.17493f, 1659.60107f, 1437.77783f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3b0a1505u, 0xbbfceb9bu, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  880 noise +-170 mV */
  {false, 2, true, true, true, 1478.12549f, 1359.46753f, 1488.27844f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xb9e1a992u, 0x3b124642u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  881 noise +-170 mV */
  {false, 2, true, true, true, 1629.96875f, 1438.98291f, 1546.19971f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3bd9f8f7u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  882 noise +-170 mV */
  {false, 2, true, true, true, 1488.1532f, 1355.24023f, 1463.58716f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xba097084u, 0x3b8c526fu, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  883 noise +-170 mV */
  {false, 2, true, true, true, 1556.55688f, 1437.04028f, 1522.62561f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  884 noise +-170 mV */
  {false, 2, true, true, true, 1410.45361f, 1514.91809f, 1540.04358f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3b1039c1u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  885 noise +-170 mV */
  {false, 2, true, true, true, 1616.84705f, 1657.02808f, 1482.45569f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3c955923u, 0xbcc98d38u, 0x00000000u, 0u, true, 1, -2, 0, 0u, 0u},
  /*  886 noise +-170 mV */
  {false, 2, true, true, true, 1635.91125f, 1482.74036f, 1476.25415f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  887 noise +-170 mV */
  {false, 2, true, true, true, 1490.69666f, 1631.79211f, 1516.47949f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  888 noise +-170 mV */
  {false, 2, true, true, true, 1556.73499f, 1549.68042f, 1474.46619f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  889 noise +-170 mV */
  {false, 2, true, true, true, 1533.90295f, 1562.03687f, 1526.74646f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  890 noise +-170 mV */
  {false, 2, true, true, true, 1345.70776f, 1527.13293f, 1477.63293f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbc221711u, 0xba8dea5du, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  891 noise +-170 mV */
  {false, 2, true, true, true, 1349.80005f, 1572.1676f, 1449.83179f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbc7068b8u, 0xbbc3e910u, 0x00000000u, 0u, true, -1, 0, 0, 0u, 0u},
  /*  892 noise +-170 mV */
  {false, 2, true, true, true, 1566.50732f, 1490.64722f, 1458.76123f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  893 noise +-170 mV */
  {false, 2, true, true, true, 1338.43176f, 1602.62048f, 1429.84314f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbcf743adu, 0xbc8bfa34u, 0x00000000u, 0u, true, -3, -1, 0, 0u, 0u},
  /*  894 sensitivity -1 ramp right */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  895 sensitivity -1 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  896 sensitivity -1 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 0u, 0u},
  /*  897 sensitivity -1 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 0u, 0u},
  /*  898 sensitivity -1 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 0u, 0u},
  /*  899 sensitivity -1 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 19u},
  /*  900 sensitivity -1 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /*  901 sensitivity -1 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /*  902 sensitivity -1 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /*  903 sensitivity -1 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /*  904 sensitivity -1 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /*  905 sensitivity -1 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /*  906 sensitivity -1 ramp right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /*  907 sensitivity -1 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /*  908 sensitivity -1 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /*  909 sensitivity -1 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /*  910 sensitivity -1 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /*  911 sensitivity -1 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /*  912 sensitivity -1 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /*  913 sensitivity -1 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /*  914 sensitivity -1 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /*  915 sensitivity -1 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 0u},
  /*  916 sensitivity -1 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 0u, 0u},
  /*  917 sensitivity -1 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  918 sensitivity -1 ramp right */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  919 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  920 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  921 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 0u, 0u},
  /*  922 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 0u, 0u},
  /*  923 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 0u, 0u},
  /*  924 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 17u},
  /*  925 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /*  926 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /*  927 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /*  928 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /*  929 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /*  930 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /*  931 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /*  932 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /*  933 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /*  934 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /*  935 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /*  936 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /*  937 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /*  938 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /*  939 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /*  940 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 0u},
  /*  941 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 0u, 0u},
  /*  942 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  943 sensitivity -1 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, -1, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  944 sensitivity 0 ramp right */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  945 sensitivity 0 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  946 sensitivity 0 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 0u, 0u},
  /*  947 sensitivity 0 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 0u, 0u},
  /*  948 sensitivity 0 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 0u, 0u},
  /*  949 sensitivity 0 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 19u},
  /*  950 sensitivity 0 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /*  951 sensitivity 0 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /*  952 sensitivity 0 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /*  953 sensitivity 0 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /*  954 sensitivity 0 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /*  955 sensitivity 0 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /*  956 sensitivity 0 ramp right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /*  957 sensitivity 0 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /*  958 sensitivity 0 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /*  959 sensitivity 0 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /*  960 sensitivity 0 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /*  961 sensitivity 0 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /*  962 sensitivity 0 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /*  963 sensitivity 0 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /*  964 sensitivity 0 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /*  965 sensitivity 0 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 0u},
  /*  966 sensitivity 0 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 0u, 0u},
  /*  967 sensitivity 0 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  968 sensitivity 0 ramp right */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  969 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  970 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  971 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 0u, 0u},
  /*  972 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 0u, 0u},
  /*  973 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 0u, 0u},
  /*  974 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 17u},
  /*  975 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /*  976 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /*  977 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /*  978 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /*  979 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /*  980 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /*  981 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /*  982 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /*  983 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /*  984 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /*  985 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /*  986 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /*  987 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /*  988 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /*  989 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /*  990 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 0u},
  /*  991 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 0u, 0u},
  /*  992 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  993 sensitivity 0 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 0, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  994 sensitivity 1 ramp right */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  995 sensitivity 1 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /*  996 sensitivity 1 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 0u, 0u},
  /*  997 sensitivity 1 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 0u, 0u},
  /*  998 sensitivity 1 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 0u, 0u},
  /*  999 sensitivity 1 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 19u},
  /* 1000 sensitivity 1 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1001 sensitivity 1 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1002 sensitivity 1 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1003 sensitivity 1 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1004 sensitivity 1 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1005 sensitivity 1 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1006 sensitivity 1 ramp right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /* 1007 sensitivity 1 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1008 sensitivity 1 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1009 sensitivity 1 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1010 sensitivity 1 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1011 sensitivity 1 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1012 sensitivity 1 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1013 sensitivity 1 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1014 sensitivity 1 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /* 1015 sensitivity 1 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 0u},
  /* 1016 sensitivity 1 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 0u, 0u},
  /* 1017 sensitivity 1 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1018 sensitivity 1 ramp right */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1019 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1020 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1021 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 0u, 0u},
  /* 1022 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 0u, 0u},
  /* 1023 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 0u, 0u},
  /* 1024 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 17u},
  /* 1025 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1026 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1027 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1028 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1029 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1030 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1031 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /* 1032 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1033 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1034 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1035 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1036 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1037 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1038 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1039 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /* 1040 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 0u},
  /* 1041 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 0u, 0u},
  /* 1042 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1043 sensitivity 1 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 1, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1044 sensitivity 2 ramp right */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1045 sensitivity 2 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1046 sensitivity 2 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 0u, 0u},
  /* 1047 sensitivity 2 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 0u, 0u},
  /* 1048 sensitivity 2 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 19u},
  /* 1049 sensitivity 2 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1050 sensitivity 2 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1051 sensitivity 2 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1052 sensitivity 2 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1053 sensitivity 2 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1054 sensitivity 2 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1055 sensitivity 2 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1056 sensitivity 2 ramp right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /* 1057 sensitivity 2 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1058 sensitivity 2 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1059 sensitivity 2 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1060 sensitivity 2 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1061 sensitivity 2 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1062 sensitivity 2 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1063 sensitivity 2 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1064 sensitivity 2 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /* 1065 sensitivity 2 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 0u},
  /* 1066 sensitivity 2 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 0u, 0u},
  /* 1067 sensitivity 2 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1068 sensitivity 2 ramp right */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1069 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1070 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1071 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 0u, 0u},
  /* 1072 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 0u, 0u},
  /* 1073 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 17u},
  /* 1074 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1075 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1076 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1077 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1078 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1079 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1080 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1081 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /* 1082 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1083 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1084 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1085 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1086 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1087 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1088 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1089 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /* 1090 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 0u},
  /* 1091 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 0u, 0u},
  /* 1092 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1093 sensitivity 2 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 2, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1094 sensitivity 3 ramp right */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1095 sensitivity 3 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1096 sensitivity 3 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 0u, 0u},
  /* 1097 sensitivity 3 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 0u, 0u},
  /* 1098 sensitivity 3 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 19u},
  /* 1099 sensitivity 3 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1100 sensitivity 3 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1101 sensitivity 3 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1102 sensitivity 3 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1103 sensitivity 3 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1104 sensitivity 3 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1105 sensitivity 3 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1106 sensitivity 3 ramp right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /* 1107 sensitivity 3 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1108 sensitivity 3 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1109 sensitivity 3 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1110 sensitivity 3 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1111 sensitivity 3 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1112 sensitivity 3 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1113 sensitivity 3 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1114 sensitivity 3 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /* 1115 sensitivity 3 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 0u},
  /* 1116 sensitivity 3 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 0u, 0u},
  /* 1117 sensitivity 3 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1118 sensitivity 3 ramp right */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1119 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1120 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1121 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 0u, 0u},
  /* 1122 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 0u, 0u},
  /* 1123 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 17u},
  /* 1124 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1125 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1126 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1127 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1128 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1129 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1130 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1131 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /* 1132 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1133 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1134 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1135 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1136 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1137 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1138 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1139 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /* 1140 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 0u},
  /* 1141 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 0u, 0u},
  /* 1142 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1143 sensitivity 3 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 3, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1144 sensitivity 4 ramp right */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1145 sensitivity 4 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1146 sensitivity 4 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 0u, 0u},
  /* 1147 sensitivity 4 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 0u, 0u},
  /* 1148 sensitivity 4 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 19u},
  /* 1149 sensitivity 4 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1150 sensitivity 4 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1151 sensitivity 4 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1152 sensitivity 4 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1153 sensitivity 4 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1154 sensitivity 4 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1155 sensitivity 4 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1156 sensitivity 4 ramp right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /* 1157 sensitivity 4 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1158 sensitivity 4 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1159 sensitivity 4 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1160 sensitivity 4 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1161 sensitivity 4 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1162 sensitivity 4 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1163 sensitivity 4 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1164 sensitivity 4 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /* 1165 sensitivity 4 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 0u},
  /* 1166 sensitivity 4 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 0u, 0u},
  /* 1167 sensitivity 4 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1168 sensitivity 4 ramp right */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1169 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1170 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1171 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 0u, 0u},
  /* 1172 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 0u, 0u},
  /* 1173 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 17u},
  /* 1174 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1175 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1176 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1177 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1178 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1179 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1180 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1181 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /* 1182 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1183 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1184 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1185 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1186 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1187 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1188 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1189 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /* 1190 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 0u},
  /* 1191 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 0u, 0u},
  /* 1192 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1193 sensitivity 4 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 4, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1194 sensitivity 5 ramp right */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1195 sensitivity 5 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1196 sensitivity 5 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 0u, 0u},
  /* 1197 sensitivity 5 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 19u},
  /* 1198 sensitivity 5 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /* 1199 sensitivity 5 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1200 sensitivity 5 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1201 sensitivity 5 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1202 sensitivity 5 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1203 sensitivity 5 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1204 sensitivity 5 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1205 sensitivity 5 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1206 sensitivity 5 ramp right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /* 1207 sensitivity 5 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1208 sensitivity 5 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1209 sensitivity 5 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1210 sensitivity 5 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1211 sensitivity 5 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1212 sensitivity 5 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1213 sensitivity 5 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1214 sensitivity 5 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /* 1215 sensitivity 5 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 0u},
  /* 1216 sensitivity 5 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 0u, 0u},
  /* 1217 sensitivity 5 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1218 sensitivity 5 ramp right */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1219 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1220 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1221 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 0u, 0u},
  /* 1222 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 17u},
  /* 1223 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /* 1224 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1225 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1226 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1227 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1228 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1229 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1230 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1231 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /* 1232 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1233 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1234 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1235 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1236 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1237 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1238 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1239 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /* 1240 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 0u},
  /* 1241 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 0u, 0u},
  /* 1242 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1243 sensitivity 5 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 5, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1244 sensitivity 6 ramp right */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1245 sensitivity 6 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1246 sensitivity 6 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 0u, 0u},
  /* 1247 sensitivity 6 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 19u},
  /* 1248 sensitivity 6 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /* 1249 sensitivity 6 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1250 sensitivity 6 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1251 sensitivity 6 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1252 sensitivity 6 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1253 sensitivity 6 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1254 sensitivity 6 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1255 sensitivity 6 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1256 sensitivity 6 ramp right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /* 1257 sensitivity 6 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1258 sensitivity 6 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1259 sensitivity 6 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1260 sensitivity 6 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1261 sensitivity 6 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1262 sensitivity 6 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1263 sensitivity 6 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1264 sensitivity 6 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /* 1265 sensitivity 6 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 0u},
  /* 1266 sensitivity 6 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 19u, 0u},
  /* 1267 sensitivity 6 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1268 sensitivity 6 ramp right */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1269 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1270 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1271 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 0u, 0u},
  /* 1272 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 17u},
  /* 1273 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /* 1274 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1275 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1276 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1277 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1278 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1279 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1280 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1281 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /* 1282 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1283 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1284 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1285 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1286 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1287 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1288 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1289 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /* 1290 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 0u},
  /* 1291 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 17u, 0u},
  /* 1292 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1293 sensitivity 6 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 6, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1294 sensitivity 7 ramp right */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1295 sensitivity 7 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1296 sensitivity 7 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 0u, 0u},
  /* 1297 sensitivity 7 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 19u},
  /* 1298 sensitivity 7 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /* 1299 sensitivity 7 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1300 sensitivity 7 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1301 sensitivity 7 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1302 sensitivity 7 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1303 sensitivity 7 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1304 sensitivity 7 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1305 sensitivity 7 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1306 sensitivity 7 ramp right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /* 1307 sensitivity 7 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1308 sensitivity 7 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1309 sensitivity 7 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1310 sensitivity 7 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1311 sensitivity 7 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1312 sensitivity 7 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1313 sensitivity 7 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1314 sensitivity 7 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /* 1315 sensitivity 7 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 0u},
  /* 1316 sensitivity 7 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 19u, 0u},
  /* 1317 sensitivity 7 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1318 sensitivity 7 ramp right */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1319 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1320 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1321 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 0u, 0u},
  /* 1322 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 17u},
  /* 1323 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /* 1324 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1325 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1326 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1327 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1328 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1329 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1330 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1331 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /* 1332 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1333 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1334 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1335 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1336 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1337 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1338 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1339 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /* 1340 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 0u},
  /* 1341 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 17u, 0u},
  /* 1342 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1343 sensitivity 7 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 7, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1344 sensitivity 8 ramp right */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1345 sensitivity 8 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1346 sensitivity 8 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 19u, 19u},
  /* 1347 sensitivity 8 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 0u},
  /* 1348 sensitivity 8 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /* 1349 sensitivity 8 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1350 sensitivity 8 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1351 sensitivity 8 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1352 sensitivity 8 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1353 sensitivity 8 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1354 sensitivity 8 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1355 sensitivity 8 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1356 sensitivity 8 ramp right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /* 1357 sensitivity 8 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1358 sensitivity 8 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1359 sensitivity 8 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1360 sensitivity 8 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1361 sensitivity 8 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1362 sensitivity 8 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1363 sensitivity 8 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1364 sensitivity 8 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /* 1365 sensitivity 8 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 0u},
  /* 1366 sensitivity 8 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 19u, 0u},
  /* 1367 sensitivity 8 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1368 sensitivity 8 ramp right */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1369 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1370 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1371 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 17u, 17u},
  /* 1372 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 0u},
  /* 1373 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /* 1374 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1375 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1376 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1377 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1378 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1379 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1380 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1381 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /* 1382 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1383 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1384 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1385 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1386 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1387 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1388 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1389 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /* 1390 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 0u},
  /* 1391 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 17u, 0u},
  /* 1392 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1393 sensitivity 8 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 8, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1394 sensitivity 9 ramp right */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1395 sensitivity 9 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1396 sensitivity 9 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 19u, 19u},
  /* 1397 sensitivity 9 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 0u},
  /* 1398 sensitivity 9 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /* 1399 sensitivity 9 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1400 sensitivity 9 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1401 sensitivity 9 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1402 sensitivity 9 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1403 sensitivity 9 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1404 sensitivity 9 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1405 sensitivity 9 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1406 sensitivity 9 ramp right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /* 1407 sensitivity 9 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1408 sensitivity 9 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1409 sensitivity 9 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1410 sensitivity 9 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1411 sensitivity 9 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1412 sensitivity 9 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1413 sensitivity 9 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1414 sensitivity 9 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /* 1415 sensitivity 9 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 0u},
  /* 1416 sensitivity 9 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 19u, 0u},
  /* 1417 sensitivity 9 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1418 sensitivity 9 ramp right */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1419 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1420 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1421 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 17u, 17u},
  /* 1422 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 0u},
  /* 1423 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /* 1424 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1425 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1426 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1427 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1428 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1429 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1430 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1431 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /* 1432 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1433 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1434 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1435 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1436 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1437 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1438 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1439 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /* 1440 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 0u},
  /* 1441 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 17u, 0u},
  /* 1442 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1443 sensitivity 9 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1444 sensitivity 10 ramp right */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1445 sensitivity 10 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1446 sensitivity 10 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 19u, 19u},
  /* 1447 sensitivity 10 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 0u},
  /* 1448 sensitivity 10 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /* 1449 sensitivity 10 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1450 sensitivity 10 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1451 sensitivity 10 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1452 sensitivity 10 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1453 sensitivity 10 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1454 sensitivity 10 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1455 sensitivity 10 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1456 sensitivity 10 ramp right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /* 1457 sensitivity 10 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1458 sensitivity 10 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1459 sensitivity 10 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1460 sensitivity 10 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1461 sensitivity 10 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1462 sensitivity 10 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1463 sensitivity 10 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1464 sensitivity 10 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /* 1465 sensitivity 10 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 0u},
  /* 1466 sensitivity 10 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 19u, 0u},
  /* 1467 sensitivity 10 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1468 sensitivity 10 ramp right */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1469 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1470 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1471 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 17u, 17u},
  /* 1472 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 0u},
  /* 1473 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /* 1474 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1475 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1476 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1477 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1478 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1479 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1480 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1481 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /* 1482 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1483 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1484 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1485 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1486 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1487 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1488 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1489 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /* 1490 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 0u},
  /* 1491 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 17u, 0u},
  /* 1492 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1493 sensitivity 10 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 10, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1494 sensitivity 11 ramp right */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1495 sensitivity 11 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1496 sensitivity 11 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 19u, 19u},
  /* 1497 sensitivity 11 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 0u},
  /* 1498 sensitivity 11 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /* 1499 sensitivity 11 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1500 sensitivity 11 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1501 sensitivity 11 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1502 sensitivity 11 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1503 sensitivity 11 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1504 sensitivity 11 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1505 sensitivity 11 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1506 sensitivity 11 ramp right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /* 1507 sensitivity 11 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1508 sensitivity 11 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1509 sensitivity 11 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1510 sensitivity 11 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1511 sensitivity 11 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1512 sensitivity 11 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1513 sensitivity 11 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1514 sensitivity 11 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /* 1515 sensitivity 11 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 0u},
  /* 1516 sensitivity 11 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 19u, 0u},
  /* 1517 sensitivity 11 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1518 sensitivity 11 ramp right */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1519 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1520 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1521 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 17u, 17u},
  /* 1522 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 0u},
  /* 1523 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /* 1524 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1525 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1526 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1527 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1528 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1529 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1530 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1531 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /* 1532 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1533 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1534 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1535 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1536 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1537 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1538 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1539 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /* 1540 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 0u},
  /* 1541 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 17u, 0u},
  /* 1542 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1543 sensitivity 11 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 11, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1544 sensitivity 100 ramp right */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1545 sensitivity 100 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1546 sensitivity 100 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 19u, 19u},
  /* 1547 sensitivity 100 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 0u},
  /* 1548 sensitivity 100 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /* 1549 sensitivity 100 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1550 sensitivity 100 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1551 sensitivity 100 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1552 sensitivity 100 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1553 sensitivity 100 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1554 sensitivity 100 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1555 sensitivity 100 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1556 sensitivity 100 ramp right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /* 1557 sensitivity 100 ramp right */
  {false, 2, true, true, true, 2849.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3f75f5f7u, 0x00000000u, 0x00000000u, 0u, true, 96, 0, 0, 19u, 0u},
  /* 1558 sensitivity 100 ramp right */
  {false, 2, true, true, true, 2727.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3f5cdcddu, 0x00000000u, 0x00000000u, 0u, true, 86, 0, 0, 19u, 0u},
  /* 1559 sensitivity 100 ramp right */
  {false, 2, true, true, true, 2605.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3f43c3c4u, 0x00000000u, 0x00000000u, 0u, true, 76, 0, 0, 19u, 0u},
  /* 1560 sensitivity 100 ramp right */
  {false, 2, true, true, true, 2483.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3f2aaaabu, 0x00000000u, 0x00000000u, 0u, true, 66, 0, 0, 19u, 0u},
  /* 1561 sensitivity 100 ramp right */
  {false, 2, true, true, true, 2361.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3f119192u, 0x00000000u, 0x00000000u, 0u, true, 56, 0, 0, 19u, 0u},
  /* 1562 sensitivity 100 ramp right */
  {false, 2, true, true, true, 2239.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3ef0f0f2u, 0x00000000u, 0x00000000u, 0u, true, 47, 0, 0, 19u, 0u},
  /* 1563 sensitivity 100 ramp right */
  {false, 2, true, true, true, 2117.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3ebebebfu, 0x00000000u, 0x00000000u, 0u, true, 37, 0, 0, 19u, 0u},
  /* 1564 sensitivity 100 ramp right */
  {false, 2, true, true, true, 1995.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3e8c8c8eu, 0x00000000u, 0x00000000u, 0u, true, 27, 0, 0, 19u, 0u},
  /* 1565 sensitivity 100 ramp right */
  {false, 2, true, true, true, 1873.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3e34b4b6u, 0x00000000u, 0x00000000u, 0u, true, 17, 0, 0, 19u, 0u},
  /* 1566 sensitivity 100 ramp right */
  {false, 2, true, true, true, 1751.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x3da0a0a2u, 0x00000000u, 0x00000000u, 0u, true, 7, 0, 0, 19u, 0u},
  /* 1567 sensitivity 100 ramp right */
  {false, 2, true, true, true, 1629.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1568 sensitivity 100 ramp right */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1569 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1570 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1571 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 17u, 17u},
  /* 1572 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 0u},
  /* 1573 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /* 1574 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1575 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1576 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1577 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1578 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1579 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1580 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1581 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 0u},
  /* 1582 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 131.333374f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3f75f5f5u, 0x00000000u, 0u, true, 0, 96, 0, 17u, 0u},
  /* 1583 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 256.666626f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3f5cdcdeu, 0x00000000u, 0u, true, 0, 86, 0, 17u, 0u},
  /* 1584 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 382.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3f43c3c4u, 0x00000000u, 0u, true, 0, 76, 0, 17u, 0u},
  /* 1585 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 507.333313f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3f2aaaabu, 0x00000000u, 0u, true, 0, 66, 0, 17u, 0u},
  /* 1586 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 632.666687f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3f119192u, 0x00000000u, 0u, true, 0, 56, 0, 17u, 0u},
  /* 1587 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 758.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3ef0f0f2u, 0x00000000u, 0u, true, 0, 47, 0, 17u, 0u},
  /* 1588 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 883.333313f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3ebebec0u, 0x00000000u, 0u, true, 0, 37, 0, 17u, 0u},
  /* 1589 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 1008.66663f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3e8c8c8fu, 0x00000000u, 0u, true, 0, 27, 0, 17u, 0u},
  /* 1590 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 1134.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3e34b4b6u, 0x00000000u, 0u, true, 0, 17, 0, 17u, 0u},
  /* 1591 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 1259.33337f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x3da0a09du, 0x00000000u, 0u, true, 0, 7, 0, 17u, 0u},
  /* 1592 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 1384.66663f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1593 sensitivity 100 ramp up */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 100, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1594 drive speed -3 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, -3, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1595 drive speed -3 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, -3, true, false,
   true, 0x3dcccccdu, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1596 drive speed -3 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, -3, true, false,
   true, 0xbdcccccdu, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1597 drive speed -3 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, -3, true, false,
   true, 0x00000000u, 0x3dcccccdu, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1598 drive speed -3 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, false, 9, 0u, -3, true, false,
   true, 0x00000000u, 0xbdcccccdu, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1599 drive speed -3 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, -3, true, false,
   true, 0x3d1e3d37u, 0x3d0478b6u, 0x00000000u, 0u, true, 38, 32, 0, 19u, 19u},
  /* 1600 drive speed -3 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, -3, true, false,
   true, 0xbd21793bu, 0xbd21b514u, 0x00000000u, 0u, true, -39, -39, 0, 18u, 18u},
  /* 1601 drive speed -3 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, false, 9, 0u, -3, true, false,
   true, 0x00000000u, 0x00000000u, 0x3dcccccdu, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1602 drive speed -3 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, false, 9, 0u, -3, true, false,
   true, 0x00000000u, 0x00000000u, 0xbd56d677u, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1603 drive speed 0 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 0, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1604 drive speed 0 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 0, true, false,
   true, 0x3dcccccdu, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1605 drive speed 0 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 0, true, false,
   true, 0xbdcccccdu, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1606 drive speed 0 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 0, true, false,
   true, 0x00000000u, 0x3dcccccdu, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1607 drive speed 0 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 0, true, false,
   true, 0x00000000u, 0xbdcccccdu, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1608 drive speed 0 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 0, true, false,
   true, 0x3d1e3d37u, 0x3d0478b6u, 0x00000000u, 0u, true, 38, 32, 0, 19u, 19u},
  /* 1609 drive speed 0 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 0, true, false,
   true, 0xbd21793bu, 0xbd21b514u, 0x00000000u, 0u, true, -39, -39, 0, 18u, 18u},
  /* 1610 drive speed 0 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, false, 9, 0u, 0, true, false,
   true, 0x00000000u, 0x00000000u, 0x3dcccccdu, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1611 drive speed 0 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, false, 9, 0u, 0, true, false,
   true, 0x00000000u, 0x00000000u, 0xbd56d677u, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1612 drive speed 1 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 1, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1613 drive speed 1 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 1, true, false,
   true, 0x3dcccccdu, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1614 drive speed 1 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 1, true, false,
   true, 0xbdcccccdu, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1615 drive speed 1 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 1, true, false,
   true, 0x00000000u, 0x3dcccccdu, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1616 drive speed 1 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 1, true, false,
   true, 0x00000000u, 0xbdcccccdu, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1617 drive speed 1 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 1, true, false,
   true, 0x3d1e3d37u, 0x3d0478b6u, 0x00000000u, 0u, true, 38, 32, 0, 19u, 19u},
  /* 1618 drive speed 1 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 1, true, false,
   true, 0xbd21793bu, 0xbd21b514u, 0x00000000u, 0u, true, -39, -39, 0, 18u, 18u},
  /* 1619 drive speed 1 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, false, 9, 0u, 1, true, false,
   true, 0x00000000u, 0x00000000u, 0x3dcccccdu, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1620 drive speed 1 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, false, 9, 0u, 1, true, false,
   true, 0x00000000u, 0x00000000u, 0xbd56d677u, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1621 drive speed 2 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 2, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1622 drive speed 2 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 2, true, false,
   true, 0x3e4ccccdu, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1623 drive speed 2 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 2, true, false,
   true, 0xbe4ccccdu, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1624 drive speed 2 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 2, true, false,
   true, 0x00000000u, 0x3e4ccccdu, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1625 drive speed 2 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 2, true, false,
   true, 0x00000000u, 0xbe4ccccdu, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1626 drive speed 2 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 2, true, false,
   true, 0x3d9e3d37u, 0x3d8478b6u, 0x00000000u, 0u, true, 38, 32, 0, 19u, 19u},
  /* 1627 drive speed 2 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 2, true, false,
   true, 0xbda1793bu, 0xbda1b514u, 0x00000000u, 0u, true, -39, -39, 0, 18u, 18u},
  /* 1628 drive speed 2 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, false, 9, 0u, 2, true, false,
   true, 0x00000000u, 0x00000000u, 0x3e4ccccdu, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1629 drive speed 2 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, false, 9, 0u, 2, true, false,
   true, 0x00000000u, 0x00000000u, 0xbdd6d677u, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1630 drive speed 3 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 3, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1631 drive speed 3 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 3, true, false,
   true, 0x3e99999au, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1632 drive speed 3 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 3, true, false,
   true, 0xbe99999au, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1633 drive speed 3 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 3, true, false,
   true, 0x00000000u, 0x3e99999au, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1634 drive speed 3 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 3, true, false,
   true, 0x00000000u, 0xbe99999au, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1635 drive speed 3 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 3, true, false,
   true, 0x3ded5bd2u, 0x3dc6b511u, 0x00000000u, 0u, true, 38, 32, 0, 19u, 19u},
  /* 1636 drive speed 3 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 3, true, false,
   true, 0xbdf235d9u, 0xbdf28f9fu, 0x00000000u, 0u, true, -39, -39, 0, 18u, 18u},
  /* 1637 drive speed 3 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, false, 9, 0u, 3, true, false,
   true, 0x00000000u, 0x00000000u, 0x3e99999au, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1638 drive speed 3 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, false, 9, 0u, 3, true, false,
   true, 0x00000000u, 0x00000000u, 0xbe2120d9u, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1639 drive speed 4 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 4, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1640 drive speed 4 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 4, true, false,
   true, 0x3ecccccdu, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1641 drive speed 4 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 4, true, false,
   true, 0xbecccccdu, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1642 drive speed 4 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 4, true, false,
   true, 0x00000000u, 0x3ecccccdu, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1643 drive speed 4 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 4, true, false,
   true, 0x00000000u, 0xbecccccdu, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1644 drive speed 4 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 4, true, false,
   true, 0x3e1e3d37u, 0x3e0478b6u, 0x00000000u, 0u, true, 38, 32, 0, 19u, 19u},
  /* 1645 drive speed 4 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 4, true, false,
   true, 0xbe21793bu, 0xbe21b514u, 0x00000000u, 0u, true, -39, -39, 0, 18u, 18u},
  /* 1646 drive speed 4 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, false, 9, 0u, 4, true, false,
   true, 0x00000000u, 0x00000000u, 0x3ecccccdu, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1647 drive speed 4 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, false, 9, 0u, 4, true, false,
   true, 0x00000000u, 0x00000000u, 0xbe56d677u, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1648 drive speed 5 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 5, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1649 drive speed 5 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 5, true, false,
   true, 0x3f000000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1650 drive speed 5 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 5, true, false,
   true, 0xbf000000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1651 drive speed 5 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 5, true, false,
   true, 0x00000000u, 0x3f000000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1652 drive speed 5 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 5, true, false,
   true, 0x00000000u, 0xbf000000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1653 drive speed 5 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 5, true, false,
   true, 0x3e45cc84u, 0x3e2596e3u, 0x00000000u, 0u, true, 38, 32, 0, 19u, 19u},
  /* 1654 drive speed 5 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 5, true, false,
   true, 0xbe49d78au, 0xbe4a2259u, 0x00000000u, 0u, true, -39, -39, 0, 18u, 18u},
  /* 1655 drive speed 5 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, false, 9, 0u, 5, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f000000u, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1656 drive speed 5 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, false, 9, 0u, 5, true, false,
   true, 0x00000000u, 0x00000000u, 0xbe86460au, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1657 drive speed 6 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 6, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1658 drive speed 6 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 6, true, false,
   true, 0x3f19999au, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1659 drive speed 6 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 6, true, false,
   true, 0xbf19999au, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1660 drive speed 6 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 6, true, false,
   true, 0x00000000u, 0x3f19999au, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1661 drive speed 6 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 6, true, false,
   true, 0x00000000u, 0xbf19999au, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1662 drive speed 6 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 6, true, false,
   true, 0x3e6d5bd2u, 0x3e46b511u, 0x00000000u, 0u, true, 38, 32, 0, 19u, 19u},
  /* 1663 drive speed 6 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 6, true, false,
   true, 0xbe7235d9u, 0xbe728f9fu, 0x00000000u, 0u, true, -39, -39, 0, 18u, 18u},
  /* 1664 drive speed 6 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, false, 9, 0u, 6, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f19999au, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1665 drive speed 6 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, false, 9, 0u, 6, true, false,
   true, 0x00000000u, 0x00000000u, 0xbea120d9u, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1666 drive speed 7 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 7, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1667 drive speed 7 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 7, true, false,
   true, 0x3f333333u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1668 drive speed 7 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 7, true, false,
   true, 0xbf333333u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1669 drive speed 7 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 7, true, false,
   true, 0x00000000u, 0x3f333333u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1670 drive speed 7 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 7, true, false,
   true, 0x00000000u, 0xbf333333u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1671 drive speed 7 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 7, true, false,
   true, 0x3e8a758fu, 0x3e67d33eu, 0x00000000u, 0u, true, 38, 32, 0, 19u, 19u},
  /* 1672 drive speed 7 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 7, true, false,
   true, 0xbe8d4a14u, 0xbe8d7e71u, 0x00000000u, 0u, true, -39, -39, 0, 18u, 18u},
  /* 1673 drive speed 7 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, false, 9, 0u, 7, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f333333u, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1674 drive speed 7 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, false, 9, 0u, 7, true, false,
   true, 0x00000000u, 0x00000000u, 0xbebbfba7u, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1675 drive speed 8 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 8, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1676 drive speed 8 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 8, true, false,
   true, 0x3f4ccccdu, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1677 drive speed 8 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 8, true, false,
   true, 0xbf4ccccdu, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1678 drive speed 8 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 8, true, false,
   true, 0x00000000u, 0x3f4ccccdu, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1679 drive speed 8 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 8, true, false,
   true, 0x00000000u, 0xbf4ccccdu, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1680 drive speed 8 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 8, true, false,
   true, 0x3e9e3d37u, 0x3e8478b6u, 0x00000000u, 0u, true, 38, 32, 0, 19u, 19u},
  /* 1681 drive speed 8 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 8, true, false,
   true, 0xbea1793bu, 0xbea1b514u, 0x00000000u, 0u, true, -39, -39, 0, 18u, 18u},
  /* 1682 drive speed 8 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, false, 9, 0u, 8, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f4ccccdu, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1683 drive speed 8 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, false, 9, 0u, 8, true, false,
   true, 0x00000000u, 0x00000000u, 0xbed6d677u, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1684 drive speed 9 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 9, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1685 drive speed 9 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 9, true, false,
   true, 0x3f666666u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1686 drive speed 9 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 9, true, false,
   true, 0xbf666666u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1687 drive speed 9 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 9, true, false,
   true, 0x00000000u, 0x3f666666u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1688 drive speed 9 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 9, true, false,
   true, 0x00000000u, 0xbf666666u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1689 drive speed 9 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 9, true, false,
   true, 0x3eb204ddu, 0x3e9507ccu, 0x00000000u, 0u, true, 38, 32, 0, 19u, 19u},
  /* 1690 drive speed 9 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 9, true, false,
   true, 0xbeb5a862u, 0xbeb5ebb6u, 0x00000000u, 0u, true, -39, -39, 0, 18u, 18u},
  /* 1691 drive speed 9 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, false, 9, 0u, 9, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f666666u, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1692 drive speed 9 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, false, 9, 0u, 9, true, false,
   true, 0x00000000u, 0x00000000u, 0xbef1b145u, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1693 drive speed 10 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1694 drive speed 10 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1695 drive speed 10 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1696 drive speed 10 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1697 drive speed 10 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1698 drive speed 10 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ec5cc84u, 0x3ea596e3u, 0x00000000u, 0u, true, 38, 32, 0, 19u, 19u},
  /* 1699 drive speed 10 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbec9d78au, 0xbeca2259u, 0x00000000u, 0u, true, -39, -39, 0, 18u, 18u},
  /* 1700 drive speed 10 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1701 drive speed 10 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf06460au, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1702 drive speed 11 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 11, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1703 drive speed 11 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 11, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1704 drive speed 11 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 11, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1705 drive speed 11 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 11, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1706 drive speed 11 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 11, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1707 drive speed 11 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 11, true, false,
   true, 0x3ec5cc84u, 0x3ea596e3u, 0x00000000u, 0u, true, 38, 32, 0, 19u, 19u},
  /* 1708 drive speed 11 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 11, true, false,
   true, 0xbec9d78au, 0xbeca2259u, 0x00000000u, 0u, true, -39, -39, 0, 18u, 18u},
  /* 1709 drive speed 11 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, false, 9, 0u, 11, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1710 drive speed 11 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, false, 9, 0u, 11, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf06460au, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1711 drive speed 50 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 50, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1712 drive speed 50 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 50, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1713 drive speed 50 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 50, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1714 drive speed 50 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 50, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1715 drive speed 50 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 50, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1716 drive speed 50 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 50, true, false,
   true, 0x3ec5cc84u, 0x3ea596e3u, 0x00000000u, 0u, true, 38, 32, 0, 19u, 19u},
  /* 1717 drive speed 50 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 50, true, false,
   true, 0xbec9d78au, 0xbeca2259u, 0x00000000u, 0u, true, -39, -39, 0, 18u, 18u},
  /* 1718 drive speed 50 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, false, 9, 0u, 50, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1719 drive speed 50 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, false, 9, 0u, 50, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf06460au, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1720 mount swap=0 invert_x=0 invert_y=0 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1721 mount swap=0 invert_x=0 invert_y=0 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1722 mount swap=0 invert_x=0 invert_y=0 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1723 mount swap=0 invert_x=0 invert_y=0 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1724 mount swap=0 invert_x=0 invert_y=0 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1725 mount swap=0 invert_x=0 invert_y=0 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ec5cc84u, 0x3ea596e3u, 0x00000000u, 0u, true, 38, 32, 0, 19u, 19u},
  /* 1726 mount swap=0 invert_x=0 invert_y=0 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbec9d78au, 0xbeca2259u, 0x00000000u, 0u, true, -39, -39, 0, 18u, 18u},
  /* 1727 mount swap=0 invert_x=0 invert_y=0 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1728 mount swap=0 invert_x=0 invert_y=0 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf06460au, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1729 mount swap=1 invert_x=0 invert_y=0 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, true, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1730 mount swap=1 invert_x=0 invert_y=0 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, true, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1731 mount swap=1 invert_x=0 invert_y=0 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, true, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1732 mount swap=1 invert_x=0 invert_y=0 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, true, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1733 mount swap=1 invert_x=0 invert_y=0 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, true, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1734 mount swap=1 invert_x=0 invert_y=0 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, true, false, false, 9, 0u, 10, true, false,
   true, 0x3ea596e3u, 0x3ec5cc84u, 0x00000000u, 0u, true, 32, 38, 0, 17u, 17u},
  /* 1735 mount swap=1 invert_x=0 invert_y=0 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, true, false, false, 9, 0u, 10, true, false,
   true, 0xbeca2259u, 0xbec9d78au, 0x00000000u, 0u, true, -39, -39, 0, 20u, 20u},
  /* 1736 mount swap=1 invert_x=0 invert_y=0 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, true, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1737 mount swap=1 invert_x=0 invert_y=0 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, true, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf06460au, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1738 mount swap=0 invert_x=1 invert_y=0 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, true, false, 9, 0u, 10, true, false,
   true, 0x80000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1739 mount swap=0 invert_x=1 invert_y=0 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, true, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1740 mount swap=0 invert_x=1 invert_y=0 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, true, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1741 mount swap=0 invert_x=1 invert_y=0 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, true, false, 9, 0u, 10, true, false,
   true, 0x80000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1742 mount swap=0 invert_x=1 invert_y=0 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, true, false, 9, 0u, 10, true, false,
   true, 0x80000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1743 mount swap=0 invert_x=1 invert_y=0 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, true, false, 9, 0u, 10, true, false,
   true, 0xbec5cc84u, 0x3ea596e3u, 0x00000000u, 0u, true, -38, 32, 0, 20u, 20u},
  /* 1744 mount swap=0 invert_x=1 invert_y=0 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, true, false, 9, 0u, 10, true, false,
   true, 0x3ec9d78au, 0xbeca2259u, 0x00000000u, 0u, true, 39, -39, 0, 18u, 18u},
  /* 1745 mount swap=0 invert_x=1 invert_y=0 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, true, false, 9, 0u, 10, true, false,
   true, 0x80000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1746 mount swap=0 invert_x=1 invert_y=0 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, true, false, 9, 0u, 10, true, false,
   true, 0x80000000u, 0x00000000u, 0xbf06460au, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1747 mount swap=1 invert_x=1 invert_y=0 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, true, true, false, 9, 0u, 10, true, false,
   true, 0x80000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1748 mount swap=1 invert_x=1 invert_y=0 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, true, true, false, 9, 0u, 10, true, false,
   true, 0x80000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1749 mount swap=1 invert_x=1 invert_y=0 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, true, true, false, 9, 0u, 10, true, false,
   true, 0x80000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1750 mount swap=1 invert_x=1 invert_y=0 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, true, true, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1751 mount swap=1 invert_x=1 invert_y=0 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, true, true, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1752 mount swap=1 invert_x=1 invert_y=0 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, true, true, false, 9, 0u, 10, true, false,
   true, 0xbea596e3u, 0x3ec5cc84u, 0x00000000u, 0u, true, -32, 38, 0, 17u, 17u},
  /* 1753 mount swap=1 invert_x=1 invert_y=0 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, true, true, false, 9, 0u, 10, true, false,
   true, 0x3eca2259u, 0xbec9d78au, 0x00000000u, 0u, true, 39, -39, 0, 19u, 19u},
  /* 1754 mount swap=1 invert_x=1 invert_y=0 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, true, true, false, 9, 0u, 10, true, false,
   true, 0x80000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1755 mount swap=1 invert_x=1 invert_y=0 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, true, true, false, 9, 0u, 10, true, false,
   true, 0x80000000u, 0x00000000u, 0xbf06460au, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1756 mount swap=0 invert_x=0 invert_y=1 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, true, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x80000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1757 mount swap=0 invert_x=0 invert_y=1 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, true, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x80000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1758 mount swap=0 invert_x=0 invert_y=1 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, true, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x80000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1759 mount swap=0 invert_x=0 invert_y=1 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, true, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1760 mount swap=0 invert_x=0 invert_y=1 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, true, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1761 mount swap=0 invert_x=0 invert_y=1 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, false, true, 9, 0u, 10, true, false,
   true, 0x3ec5cc84u, 0xbea596e3u, 0x00000000u, 0u, true, 38, -32, 0, 19u, 19u},
  /* 1762 mount swap=0 invert_x=0 invert_y=1 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, true, 9, 0u, 10, true, false,
   true, 0xbec9d78au, 0x3eca2259u, 0x00000000u, 0u, true, -39, 39, 0, 17u, 17u},
  /* 1763 mount swap=0 invert_x=0 invert_y=1 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, true, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x80000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1764 mount swap=0 invert_x=0 invert_y=1 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, true, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x80000000u, 0xbf06460au, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1765 mount swap=1 invert_x=0 invert_y=1 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, true, false, true, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x80000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1766 mount swap=1 invert_x=0 invert_y=1 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, true, false, true, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1767 mount swap=1 invert_x=0 invert_y=1 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, true, false, true, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1768 mount swap=1 invert_x=0 invert_y=1 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, true, false, true, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x80000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1769 mount swap=1 invert_x=0 invert_y=1 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, true, false, true, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x80000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1770 mount swap=1 invert_x=0 invert_y=1 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, true, false, true, 9, 0u, 10, true, false,
   true, 0x3ea596e3u, 0xbec5cc84u, 0x00000000u, 0u, true, 32, -38, 0, 18u, 18u},
  /* 1771 mount swap=1 invert_x=0 invert_y=1 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, true, false, true, 9, 0u, 10, true, false,
   true, 0xbeca2259u, 0x3ec9d78au, 0x00000000u, 0u, true, -39, 39, 0, 20u, 20u},
  /* 1772 mount swap=1 invert_x=0 invert_y=1 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, true, false, true, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x80000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1773 mount swap=1 invert_x=0 invert_y=1 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, true, false, true, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x80000000u, 0xbf06460au, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1774 mount swap=0 invert_x=1 invert_y=1 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, true, true, 9, 0u, 10, true, false,
   true, 0x80000000u, 0x80000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1775 mount swap=0 invert_x=1 invert_y=1 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, true, true, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x80000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1776 mount swap=0 invert_x=1 invert_y=1 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, true, true, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x80000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1777 mount swap=0 invert_x=1 invert_y=1 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, true, true, 9, 0u, 10, true, false,
   true, 0x80000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1778 mount swap=0 invert_x=1 invert_y=1 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, true, true, 9, 0u, 10, true, false,
   true, 0x80000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1779 mount swap=0 invert_x=1 invert_y=1 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, true, true, 9, 0u, 10, true, false,
   true, 0xbec5cc84u, 0xbea596e3u, 0x00000000u, 0u, true, -38, -32, 0, 20u, 20u},
  /* 1780 mount swap=0 invert_x=1 invert_y=1 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, true, true, 9, 0u, 10, true, false,
   true, 0x3ec9d78au, 0x3eca2259u, 0x00000000u, 0u, true, 39, 39, 0, 17u, 17u},
  /* 1781 mount swap=0 invert_x=1 invert_y=1 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, true, true, 9, 0u, 10, true, false,
   true, 0x80000000u, 0x80000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1782 mount swap=0 invert_x=1 invert_y=1 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, true, true, 9, 0u, 10, true, false,
   true, 0x80000000u, 0x80000000u, 0xbf06460au, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1783 mount swap=1 invert_x=1 invert_y=1 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, true, true, true, 9, 0u, 10, true, false,
   true, 0x80000000u, 0x80000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1784 mount swap=1 invert_x=1 invert_y=1 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, true, true, true, 9, 0u, 10, true, false,
   true, 0x80000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1785 mount swap=1 invert_x=1 invert_y=1 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, true, true, true, 9, 0u, 10, true, false,
   true, 0x80000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1786 mount swap=1 invert_x=1 invert_y=1 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, true, true, true, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x80000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1787 mount swap=1 invert_x=1 invert_y=1 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, true, true, true, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x80000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1788 mount swap=1 invert_x=1 invert_y=1 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, true, true, true, 9, 0u, 10, true, false,
   true, 0xbea596e3u, 0xbec5cc84u, 0x00000000u, 0u, true, -32, -38, 0, 18u, 18u},
  /* 1789 mount swap=1 invert_x=1 invert_y=1 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, true, true, true, 9, 0u, 10, true, false,
   true, 0x3eca2259u, 0x3ec9d78au, 0x00000000u, 0u, true, 39, 39, 0, 19u, 19u},
  /* 1790 mount swap=1 invert_x=1 invert_y=1 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, true, true, true, 9, 0u, 10, true, false,
   true, 0x80000000u, 0x80000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1791 mount swap=1 invert_x=1 invert_y=1 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, true, true, true, 9, 0u, 10, true, false,
   true, 0x80000000u, 0x80000000u, 0xbf06460au, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1792 gate drives=0 calibrating=0 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, false, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1793 gate drives=0 calibrating=0 rest button */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, false, true,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 1u, true, 0, 0, 0, 0u, 0u},
  /* 1794 gate drives=0 calibrating=0 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, false, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1795 gate drives=0 calibrating=0 full right button */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, false, true,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 1u, true, 100, 0, 0, 19u, 0u},
  /* 1796 gate drives=0 calibrating=0 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, false, false,
   true, 0x80000000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1797 gate drives=0 calibrating=0 full left button */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, false, true,
   true, 0x80000000u, 0x00000000u, 0x00000000u, 1u, true, -100, 0, 0, 20u, 0u},
  /* 1798 gate drives=0 calibrating=0 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, false, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1799 gate drives=0 calibrating=0 full up button */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, false, true,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 1u, true, 0, 100, 0, 17u, 0u},
  /* 1800 gate drives=0 calibrating=0 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, false, false,
   true, 0x00000000u, 0x80000000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1801 gate drives=0 calibrating=0 full down button */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, false, true,
   true, 0x00000000u, 0x80000000u, 0x00000000u, 1u, true, 0, -100, 0, 18u, 0u},
  /* 1802 gate drives=0 calibrating=0 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, false, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 38, 32, 0, 19u, 19u},
  /* 1803 gate drives=0 calibrating=0 half up-right button */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, false, true,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 1u, true, 38, 32, 0, 19u, 0u},
  /* 1804 gate drives=0 calibrating=0 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, false, false,
   true, 0x80000000u, 0x80000000u, 0x00000000u, 0u, true, -39, -39, 0, 18u, 18u},
  /* 1805 gate drives=0 calibrating=0 half down-left button */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, false, true,
   true, 0x80000000u, 0x80000000u, 0x00000000u, 1u, true, -39, -39, 0, 18u, 0u},
  /* 1806 gate drives=0 calibrating=0 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, false, 9, 0u, 10, false, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1807 gate drives=0 calibrating=0 twist cw button */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, false, 9, 0u, 10, false, true,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 1u, true, 0, 0, 100, 0u, 0u},
  /* 1808 gate drives=0 calibrating=0 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, false, 9, 0u, 10, false, false,
   true, 0x00000000u, 0x00000000u, 0x80000000u, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1809 gate drives=0 calibrating=0 twist half ccw button */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, false, 9, 0u, 10, false, true,
   true, 0x00000000u, 0x00000000u, 0x80000000u, 1u, true, 0, 0, -52, 0u, 0u},
  /* 1810 gate drives=1 calibrating=0 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1811 gate drives=1 calibrating=0 rest button */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, true,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 1u, true, 0, 0, 0, 0u, 0u},
  /* 1812 gate drives=1 calibrating=0 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1813 gate drives=1 calibrating=0 full right button */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, true,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 1u, true, 100, 0, 0, 19u, 0u},
  /* 1814 gate drives=1 calibrating=0 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1815 gate drives=1 calibrating=0 full left button */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, true,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 1u, true, -100, 0, 0, 20u, 0u},
  /* 1816 gate drives=1 calibrating=0 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1817 gate drives=1 calibrating=0 full up button */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, true,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 1u, true, 0, 100, 0, 17u, 0u},
  /* 1818 gate drives=1 calibrating=0 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1819 gate drives=1 calibrating=0 full down button */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, true,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 1u, true, 0, -100, 0, 18u, 0u},
  /* 1820 gate drives=1 calibrating=0 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3ec5cc84u, 0x3ea596e3u, 0x00000000u, 0u, true, 38, 32, 0, 19u, 19u},
  /* 1821 gate drives=1 calibrating=0 half up-right button */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, true,
   true, 0x3ec5cc84u, 0x3ea596e3u, 0x00000000u, 1u, true, 38, 32, 0, 19u, 0u},
  /* 1822 gate drives=1 calibrating=0 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbec9d78au, 0xbeca2259u, 0x00000000u, 0u, true, -39, -39, 0, 18u, 18u},
  /* 1823 gate drives=1 calibrating=0 half down-left button */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, true,
   true, 0xbec9d78au, 0xbeca2259u, 0x00000000u, 1u, true, -39, -39, 0, 18u, 0u},
  /* 1824 gate drives=1 calibrating=0 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1825 gate drives=1 calibrating=0 twist cw button */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, false, false, false, false, 9, 0u, 10, true, true,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 1u, true, 0, 0, 100, 0u, 0u},
  /* 1826 gate drives=1 calibrating=0 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf06460au, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1827 gate drives=1 calibrating=0 twist half ccw button */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, false, false, false, false, 9, 0u, 10, true, true,
   true, 0x00000000u, 0x00000000u, 0xbf06460au, 1u, true, 0, 0, -52, 0u, 0u},
  /* 1828 gate drives=0 calibrating=1 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, false, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1829 gate drives=0 calibrating=1 rest button */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, false, true,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 1u, true, 0, 0, 0, 0u, 0u},
  /* 1830 gate drives=0 calibrating=1 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, false, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 0u, 0u},
  /* 1831 gate drives=0 calibrating=1 full right button */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, false, true,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 1u, true, 100, 0, 0, 0u, 0u},
  /* 1832 gate drives=0 calibrating=1 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, false, false,
   true, 0x80000000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 0u, 0u},
  /* 1833 gate drives=0 calibrating=1 full left button */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, false, true,
   true, 0x80000000u, 0x00000000u, 0x00000000u, 1u, true, -100, 0, 0, 0u, 0u},
  /* 1834 gate drives=0 calibrating=1 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, false, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 100, 0, 0u, 0u},
  /* 1835 gate drives=0 calibrating=1 full up button */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, false, true,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 1u, true, 0, 100, 0, 0u, 0u},
  /* 1836 gate drives=0 calibrating=1 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, false, false,
   true, 0x00000000u, 0x80000000u, 0x00000000u, 0u, true, 0, -100, 0, 0u, 0u},
  /* 1837 gate drives=0 calibrating=1 full down button */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, false, true,
   true, 0x00000000u, 0x80000000u, 0x00000000u, 1u, true, 0, -100, 0, 0u, 0u},
  /* 1838 gate drives=0 calibrating=1 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, false, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 38, 32, 0, 0u, 0u},
  /* 1839 gate drives=0 calibrating=1 half up-right button */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, false, true,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 1u, true, 38, 32, 0, 0u, 0u},
  /* 1840 gate drives=0 calibrating=1 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, false, false,
   true, 0x80000000u, 0x80000000u, 0x00000000u, 0u, true, -39, -39, 0, 0u, 0u},
  /* 1841 gate drives=0 calibrating=1 half down-left button */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, false, true,
   true, 0x80000000u, 0x80000000u, 0x00000000u, 1u, true, -39, -39, 0, 0u, 0u},
  /* 1842 gate drives=0 calibrating=1 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, true, false, false, false, 9, 0u, 10, false, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1843 gate drives=0 calibrating=1 twist cw button */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, true, false, false, false, 9, 0u, 10, false, true,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 1u, true, 0, 0, 100, 0u, 0u},
  /* 1844 gate drives=0 calibrating=1 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, true, false, false, false, 9, 0u, 10, false, false,
   true, 0x00000000u, 0x00000000u, 0x80000000u, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1845 gate drives=0 calibrating=1 twist half ccw button */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, true, false, false, false, 9, 0u, 10, false, true,
   true, 0x00000000u, 0x00000000u, 0x80000000u, 1u, true, 0, 0, -52, 0u, 0u},
  /* 1846 gate drives=1 calibrating=1 rest */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1847 gate drives=1 calibrating=1 rest button */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, true, true,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 1u, true, 0, 0, 0, 0u, 0u},
  /* 1848 gate drives=1 calibrating=1 full right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 0u, 0u},
  /* 1849 gate drives=1 calibrating=1 full right button */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, true, true,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 1u, true, 100, 0, 0, 0u, 0u},
  /* 1850 gate drives=1 calibrating=1 full left */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, true, false,
   true, 0x80000000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 0u, 0u},
  /* 1851 gate drives=1 calibrating=1 full left button */
  {false, 2, true, true, true, 11.0f, 1510.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, true, true,
   true, 0x80000000u, 0x00000000u, 0x00000000u, 1u, true, -100, 0, 0, 0u, 0u},
  /* 1852 gate drives=1 calibrating=1 full up */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 100, 0, 0u, 0u},
  /* 1853 gate drives=1 calibrating=1 full up button */
  {false, 2, true, true, true, 1507.0f, 6.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, true, true,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 1u, true, 0, 100, 0, 0u, 0u},
  /* 1854 gate drives=1 calibrating=1 full down */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x80000000u, 0x00000000u, 0u, true, 0, -100, 0, 0u, 0u},
  /* 1855 gate drives=1 calibrating=1 full down button */
  {false, 2, true, true, true, 1507.0f, 2962.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, true, true,
   true, 0x00000000u, 0x80000000u, 0x00000000u, 1u, true, 0, -100, 0, 0u, 0u},
  /* 1856 gate drives=1 calibrating=1 half up-right */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 38, 32, 0, 0u, 0u},
  /* 1857 gate drives=1 calibrating=1 half up-right button */
  {false, 2, true, true, true, 2100.0f, 1000.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, true, true,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 1u, true, 38, 32, 0, 0u, 0u},
  /* 1858 gate drives=1 calibrating=1 half down-left */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, true, false,
   true, 0x80000000u, 0x80000000u, 0x00000000u, 0u, true, -39, -39, 0, 0u, 0u},
  /* 1859 gate drives=1 calibrating=1 half down-left button */
  {false, 2, true, true, true, 900.0f, 2100.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, true, true,
   true, 0x80000000u, 0x80000000u, 0x00000000u, 1u, true, -39, -39, 0, 0u, 0u},
  /* 1860 gate drives=1 calibrating=1 twist cw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, true, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1861 gate drives=1 calibrating=1 twist cw button */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 2960.0f, 0, true, false, false, false, 9, 0u, 10, true, true,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 1u, true, 0, 0, 100, 0u, 0u},
  /* 1862 gate drives=1 calibrating=1 twist half ccw */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, true, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x80000000u, 0u, true, 0, 0, -52, 0u, 0u},
  /* 1863 gate drives=1 calibrating=1 twist half ccw button */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 700.0f, 0, true, false, false, false, 9, 0u, 10, true, true,
   true, 0x00000000u, 0x00000000u, 0x80000000u, 1u, true, 0, 0, -52, 0u, 0u},
  /* 1864 calibrating keys: right engages */
  {true, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1865 calibrating keys: calibrating releases the key */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 0u, 0u},
  /* 1866 calibrating keys: still calibrating */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, true, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 0u, 0u},
  /* 1867 calibrating keys: run over, the held stick re-engages and flicks */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1868 invalid: valid full right first */
  {true, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1869 invalid: horiz_ok=0 vert_ok=0 twist_ok=0 */
  {false, 2, false, false, false, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   false, 0x00000000u, 0x00000000u, 0x00000000u, 0u, false, 0, 0, 0, 19u, 0u},
  /* 1870 invalid: horiz_ok=1 vert_ok=0 twist_ok=0 */
  {false, 2, true, false, false, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   false, 0x00000000u, 0x00000000u, 0x00000000u, 0u, false, 0, 0, 0, 19u, 0u},
  /* 1871 invalid: horiz_ok=0 vert_ok=1 twist_ok=0 */
  {false, 2, false, true, false, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   false, 0x00000000u, 0x00000000u, 0x00000000u, 0u, false, 0, 0, 0, 19u, 0u},
  /* 1872 invalid: horiz_ok=1 vert_ok=1 twist_ok=0 */
  {false, 2, true, true, false, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   false, 0x00000000u, 0x00000000u, 0x00000000u, 0u, false, 0, 0, 0, 19u, 0u},
  /* 1873 invalid: horiz_ok=0 vert_ok=0 twist_ok=1 */
  {false, 2, false, false, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   false, 0x00000000u, 0x00000000u, 0x00000000u, 0u, false, 0, 0, 0, 19u, 0u},
  /* 1874 invalid: horiz_ok=1 vert_ok=0 twist_ok=1 */
  {false, 2, true, false, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   false, 0x00000000u, 0x00000000u, 0x00000000u, 0u, false, 0, 0, 0, 19u, 0u},
  /* 1875 invalid: horiz_ok=0 vert_ok=1 twist_ok=1 */
  {false, 2, false, true, true, 11.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   false, 0x00000000u, 0x00000000u, 0x00000000u, 0u, false, 0, 0, 0, 19u, 0u},
  /* 1876 invalid: valid again */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /* 1877 invalid: new calibration (ideal) arrives on an invalid cycle */
  {false, 2, true, true, false, 1507.0f, 1510.0f, 1477.0f, 1, false, false, false, false, 9, 0u, 10, true, false,
   false, 0x00000000u, 0x00000000u, 0x00000000u, 0u, false, 0, 0, 0, 19u, 0u},
  /* 1878 invalid: next valid cycle applies it (2971 mV is no longer full right) */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f532c7eu, 0x3db30ac9u, 0xbd954e49u, 0u, true, 82, 8, -7, 19u, 0u},
  /* 1879 new cal: ideal power-on, 2971 mV */
  {true, 1, true, true, true, 2971.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f53018eu, 0x00000000u, 0x00000000u, 0u, true, 82, 0, 0, 19u, 19u},
  /* 1880 new cal: board 2 taken this cycle, 2971 mV is full right */
  {false, 1, true, true, true, 2971.0f, 1650.0f, 1650.0f, 2, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f7ed17bu, 0xbdc48dd6u, 0x3da755b4u, 0u, true, 99, -9, 8, 19u, 0u},
  /* 1881 new cal: board 2 stays */
  {false, 1, true, true, true, 2971.0f, 1650.0f, 1650.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f7ed17bu, 0xbdc48dd6u, 0x3da755b4u, 0u, true, 99, -9, 8, 19u, 0u},
  /* 1882 new cal: back to ideal */
  {false, 1, true, true, true, 2971.0f, 1650.0f, 1650.0f, 1, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f53018eu, 0x00000000u, 0x00000000u, 0u, true, 82, 0, 0, 19u, 0u},
  /* 1883 remote: stick right */
  {true, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1884 remote: remote LEFT overrides the stick */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 20u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 20u, 20u},
  /* 1885 remote: remote UP at rest */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 17u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 17u, 17u},
  /* 1886 remote: released, stick at rest */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1887 flick: right */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1888 flick: right held (no new flick) */
  {false, 2, true, true, true, 2971.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 0u},
  /* 1889 flick: rolled to up-right, up larger: re-aim flicks UP */
  {false, 2, true, true, true, 2300.0f, 400.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f11513bu, 0x3f45ff70u, 0x00000000u, 0u, true, 56, 77, 0, 17u, 17u},
  /* 1890 flick: back to rest */
  {false, 2, true, true, true, 1507.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x00000000u, 0u, true, 0, 0, 0, 0u, 0u},
  /* 1891 hazard: open-circuit horizontal (0 mV) */
  {true, 2, true, true, true, 0.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf800000u, 0x00000000u, 0x00000000u, 0u, true, -100, 0, 0, 20u, 20u},
  /* 1892 hazard: open-circuit vertical (0 mV) */
  {true, 2, true, true, true, 1507.0f, 0.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x3f800000u, 0x00000000u, 0u, true, 0, 100, 0, 17u, 17u},
  /* 1893 hazard: open-circuit twist (0 mV) */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 0.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0xbf800000u, 0u, true, 0, 0, -100, 0u, 0u},
  /* 1894 hazard: horizontal shorted to 3300 mV */
  {true, 2, true, true, true, 3300.0f, 1510.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x3f800000u, 0x00000000u, 0x00000000u, 0u, true, 100, 0, 0, 19u, 19u},
  /* 1895 hazard: vertical shorted to 3300 mV */
  {true, 2, true, true, true, 1507.0f, 3300.0f, 1477.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0xbf800000u, 0x00000000u, 0u, true, 0, -100, 0, 18u, 18u},
  /* 1896 hazard: twist shorted to 3300 mV */
  {true, 2, true, true, true, 1507.0f, 1510.0f, 3300.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0x00000000u, 0x00000000u, 0x3f800000u, 0u, true, 0, 0, 100, 0u, 0u},
  /* 1897 hazard: all three open (0 mV) */
  {true, 2, true, true, true, 0.0f, 0.0f, 0.0f, 0, false, false, false, false, 9, 0u, 10, true, false,
   true, 0xbf3504f3u, 0x3f3504f3u, 0xbf800000u, 0u, true, -70, 70, -100, 20u, 20u},
}};

} // namespace stick_test
// clang-format on
