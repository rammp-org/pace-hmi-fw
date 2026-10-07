/**
 * @file selftest_spec.hpp
 * @brief The HMI self test: every check it runs, and the limits it is held to.
 *
 * THIS FILE IS THE SPEC. A firmware change is checked by running the self test
 * and reading PASS/FAIL against these rows, not by reading the code:
 *
 *   - on the device:  Settings -> SELF TEST (results on screen and serial)
 *   - from a PC:      python scripts/rtps_selftest.py  (exit 0 = all passed)
 *
 * To tighten or loosen a check, change its limits here. To add one, add a row
 * to kChecks, its enumerator to Id at the same position, and its measurement in
 * selftest.cpp. A row with no measurement behind it reports FAIL ("not
 * measured"), so the spec can never silently claim a check the firmware does
 * not run.
 *
 *   Check{Id::ID, "name", "unit", lo, hi, Need::..., "what it proves"},
 *
 * lo/hi are inclusive integers in `unit`; kAnyLo/kAnyHi means no limit on that
 * side. A check with lo == hi == 1 and no unit is a yes/no check.
 *
 * `need` decides what a check that cannot be measured counts as:
 *   Need::REQUIRED  FAIL - the hardware or feature is expected on every unit
 *   Need::OPTIONAL  SKIP - fitted on some units only
 *   Need::REMOTE    FAIL when the run was requested over RTPS (a self-test peer
 *                   is known to be there), SKIP when started from the HMI's own
 *                   settings row. Only the ping checks use it: they need a peer
 *                   that answers self-test pings, which a production MCB does not.
 *                   Everything that needs the MCB itself (McbStatus arriving, its
 *                   timing, a subscriber for the joystick stream) is REQUIRED, so
 *                   an unplugged cable or a silent MCB fails wherever it is run.
 *
 * The limits come from measurements on the bench unit, with margin for
 * unit-to-unit spread. A check that fails on a healthy unit means the limit
 * is wrong, and the fix belongs here rather than in the code.
 *
 * One row per line, so a script can scrape it: scripts/rammp_rtps.py
 * (parse_selftest_spec, used by rtps_selftest.py) reads kChecks, and the L1 app
 * tests/host/selftest_spec_parity proves that copy matches this table
 * (TS-UNIT-04). A change to the row format changes that parser in the same commit
 * (CS-CFG-03).
 */

#ifndef SELFTEST_SPEC_HPP
#define SELFTEST_SPEC_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace selftest_spec {

/// No lower limit.
inline constexpr int32_t kAnyLo = std::numeric_limits<int32_t>::min();
/// No upper limit.
inline constexpr int32_t kAnyHi = std::numeric_limits<int32_t>::max();

/// What a check that cannot be measured counts as (see the file comment).
enum class Need : uint8_t {
  REQUIRED, ///< FAIL
  OPTIONAL, ///< SKIP
  REMOTE,   ///< FAIL on a run requested over RTPS, SKIP on a local one
};

// How a run is paced. Every check below is measured inside one of these.
/// Hands-off pause, so the at-rest joystick capture is at rest.
inline constexpr int kSettleMs = 1500;
/// Observation window: joystick, ADC cadence, RTPS, UI stall.
inline constexpr int kWindowMs = 6000;
/// One RTPS ping per this, through the window.
inline constexpr int kPingPeriodMs = 40;
/// Forced full-screen redraws timed by the render checks.
inline constexpr int kRenderFrames = 30;

/*
 * Where the less obvious limits come from (bench unit, v1.0.0-alpha-15):
 *
 * mem.*      End of a run: internal 30 KB free (least since boot 25), largest
 *            block 22. DMA-capable RAM is the tight one - the W5500's SPI
 *            bounce buffers come from it, and it running dry has boot-looped
 *            this board. 43 KB free when RTPS starts, 4..7 KB free at the end
 *            of a run (mem.dma_free, whole KB, so its limit sits well under
 *            that to keep rounding from flapping it), and
 *            its low-water mark is ~2.4 KB BEFORE the self test runs: normal
 *            operation alone takes it within a few hundred bytes of one
 *            Ethernet frame. mem.dma_min and mem.dma_block sit near their
 *            limits on a healthy unit; that closeness is the finding, not a
 *            limit to loosen.
 *
 * pwr.vbat   2S pack (BSP: 3.2..4.2 V per cell). The INA226 answers with or
 *            without a pack, so a reading under 5 V - one boot read 4.27 V,
 *            the bench unit's pack reads 8.39 - is taken as no pack fitted
 *            (SKIP) rather than as a pack no protection circuit would allow.
 *            Unexplained so far: the bench unit has read 8.39 V and then under
 *            5 V on the same boot, minutes apart. The SKIP detail carries the
 *            raw reading so that can be chased down.
 *
 * hap.*      Not the chip's own actuator diagnostic: on the bench unit it
 *            reported DIAG_RESULT in open- and closed-loop mode alike, so it
 *            cannot tell a missing motor from this one. hap.drv_play proves
 *            the I2C path, sequencer and output stage - not that a motor is
 *            attached.
 *
 * time.render_*  RENDER_START..RENDER_READY for one full-screen invalidate,
 *            flush and vsync wait included, on the screen that is up, with the results
 *            panel hidden and only its small blinking banner up: 95 ms mean,
 *            103 ms worst (88 / 91 with nothing up at all - the blink costs
 *            ~7 ms a frame). Depends on which screen is up, so compare runs
 *            started from the same place. Both limits are 180 ms: runs started
 *            from other screens kept failing the old 110 / 140.
 *
 * joy.*      Raw ADC mV. Nominal rest is 1650 (half the 3300 mV supply); real sticks rest
 *            up to ~150 mV away (1506 mV on the bench board), hence the width
 *            of the *_rest windows. The twist pot is on ADC2, sampled oneshot:
 *            ~60 mV peak-to-peak where X/Y read ~1 mV (twist_noise is after
 *            the ADC task averages 8 reads, before its lowpass).
 *
 * joy.*_cal_off  How far the at-rest reading has wandered from the centre the
 *            saved calibration measured. X/Y's 40 mV is well inside the
 *            stick's 0.10 radial deadzone (~150 mV), twist's 50 mV inside its
 *            60 mV centre deadband: past those, rest starts leaking out as
 *            motion. SKIP with nothing saved - joy.cal_saved fails for that.
 *
 * rtps.rtt_* The bench PC reaches the board over Tailscale: p50 5 ms, p99
 *            15..107 ms, worst 142 ms. On a direct LAN expect a fraction of
 *            that, and tighten these if that is the bench.
 *
 * net.wifi_rssi  Only on WiFi (SKIP on Ethernet). Below about -75 dBm 2.4 GHz
 *            starts losing frames, which shows up as rtps.mcb_loss and rtt.
 */

/// One check, in kChecks' order: the position is the report's check index.
/// Hand-written, not generated (no X-macros, CS-TYP-03); the static_asserts below
/// prove it lists exactly kChecks' rows in kChecks' order, then COUNT.
enum class Id : uint8_t {
  SYS_RESET,
  SYS_CPU,
  SYS_UPTIME,
  LOG_CAPTURE,
  NET_LINK,
  NET_IP,
  NET_WIFI_RSSI,
  RTPS_LINK,
  RTPS_MCB_PERIOD,
  RTPS_MCB_GAP,
  RTPS_MCB_LOSS,
  RTPS_ADC_HZ,
  RTPS_RTT_P50,
  RTPS_RTT_P99,
  RTPS_PING_LOSS,
  MEM_INT_FREE,
  MEM_INT_MIN,
  MEM_INT_BLOCK,
  MEM_DMA_FREE,
  MEM_DMA_MIN,
  MEM_DMA_BLOCK,
  MEM_PSRAM_FREE,
  MEM_HEAP_OK,
  MEM_STK_LVGL,
  MEM_STK_ADC,
  MEM_STK_RTPS,
  I2C_MISSING,
  I2C_COUNT,
  IMU_ACCEL,
  RTC_TICK,
  PWR_VBAT,
  HAP_DRV_ID,
  HAP_DRV_FAULT,
  HAP_DRV_PLAY,
  HAP_DA7280,
  DISP_DIRECT,
  DISP_BACKLIGHT,
  TIME_RENDER_AVG,
  TIME_RENDER_MAX,
  TIME_UI_STALL,
  TIME_ADC_AVG,
  TIME_ADC_MAX,
  JOY_VALID,
  JOY_X,
  JOY_Y,
  JOY_TWIST,
  JOY_X_NOISE,
  JOY_Y_NOISE,
  JOY_TWIST_NOISE,
  JOY_CAL,
  JOY_X_CAL,
  JOY_Y_CAL,
  JOY_TWIST_CAL,
  JOY_BUTTON,
  COUNT, ///< not a check: how many there are (asserted equal to kChecks.size())
};

/// One row of the spec: a check, its limits, and what it proves (TS-POST-03).
struct Check {
  Id id;
  std::string_view name; ///< "mem.int_free": the report's and the log's name
  std::string_view unit; ///< "KB"; empty for a count or a yes/no check
  int32_t lo;            ///< inclusive; kAnyLo = no lower limit
  int32_t hi;            ///< inclusive; kAnyHi = no upper limit
  Need need;
  std::string_view why; ///< what a PASS proves

  /// @brief A measured value's verdict: PASS when lo <= value <= hi.
  /// @param value the measurement, in `unit`
  /// @return true for PASS, false for FAIL
  [[nodiscard]] constexpr bool in_limits(int32_t value) const { return value >= lo && value <= hi; }

  /// @brief Whether this check, unmeasurable, counts as FAIL (true) or SKIP (false).
  /// @param remote the run was requested over RTPS
  /// @return true for FAIL, false for SKIP
  [[nodiscard]] constexpr bool required(bool remote) const {
    return need == Need::REQUIRED || (need == Need::REMOTE && remote);
  }

  /// @brief A yes/no check: lo == hi == 1 and no unit (shown as "yes"/"no").
  /// @return true for a yes/no check
  [[nodiscard]] constexpr bool is_yes_no() const { return lo == 1 && hi == 1 && unit.empty(); }
};

// clang-format off
// The table: one row per line (scraped by scripts/rammp_rtps.py, see the file comment).
inline constexpr std::array kChecks{
    // system
    Check{Id::SYS_RESET,       "sys.clean_reset",   "",     1,     1,      Need::REQUIRED, "Last reset not a panic/WDT/brownout"},
    Check{Id::SYS_CPU,         "sys.cpu_mhz",       "MHz",  360,   360,    Need::REQUIRED, "CPU runs at the configured clock"},
    Check{Id::SYS_UPTIME,      "sys.uptime",        "s",    0,     kAnyHi, Need::REQUIRED, "Seconds since boot (context)"},
    Check{Id::LOG_CAPTURE,     "log.capture",       "",     1,     1,      Need::REQUIRED, "Serial output reaches the log screen"},
    // network and RTPS - first, so the link is the first thing read on screen
    Check{Id::NET_LINK,        "net.link",          "",     1,     1,      Need::REQUIRED, "Network link up (Ethernet, or WiFi joined)"},
    Check{Id::NET_IP,          "net.ip",            "",     1,     1,      Need::REQUIRED, "DHCP lease held"},
    Check{Id::NET_WIFI_RSSI,   "net.wifi_rssi",     "dBm",  -75,   kAnyHi, Need::OPTIONAL, "WiFi signal at the AP"},
    Check{Id::RTPS_LINK,       "rtps.mcb_link",     "",     1,     1,      Need::REQUIRED, "MCB answering (McbStatus arriving)"},
    Check{Id::RTPS_MCB_PERIOD, "rtps.mcb_period",   "ms",   400,   600,    Need::REQUIRED, "McbStatus period, mean"},
    Check{Id::RTPS_MCB_GAP,    "rtps.mcb_gap",      "ms",   0,     1000,   Need::REQUIRED, "Longest McbStatus gap"},
    Check{Id::RTPS_MCB_LOSS,   "rtps.mcb_loss",     "0.1%", 0,     100,    Need::REQUIRED, "McbStatus lost (seq gaps)"},
    Check{Id::RTPS_ADC_HZ,     "rtps.adc_hz",       "Hz",   25,    32,     Need::REQUIRED, "Joystick samples published per s"},
    Check{Id::RTPS_RTT_P50,    "rtps.rtt_p50",      "us",   0,     20000,  Need::REMOTE,   "Ping round trip, median"},
    Check{Id::RTPS_RTT_P99,    "rtps.rtt_p99",      "us",   0,     250000, Need::REMOTE,   "Ping round trip, 99th percentile"},
    Check{Id::RTPS_PING_LOSS,  "rtps.ping_loss",    "0.1%", 0,     20,     Need::REMOTE,   "Pings with no pong"},
    // memory
    Check{Id::MEM_INT_FREE,    "mem.int_free",      "KB",   16,    kAnyHi, Need::REQUIRED, "Internal RAM free now"},
    Check{Id::MEM_INT_MIN,     "mem.int_min",       "KB",   12,    kAnyHi, Need::REQUIRED, "Least internal RAM since boot"},
    Check{Id::MEM_INT_BLOCK,   "mem.int_block",     "KB",   12,    kAnyHi, Need::REQUIRED, "Largest internal block"},
    Check{Id::MEM_DMA_FREE,    "mem.dma_free",      "KB",   2,     kAnyHi, Need::REQUIRED, "DMA-capable RAM free"},
    Check{Id::MEM_DMA_MIN,     "mem.dma_min",       "B",    1536,  kAnyHi, Need::REQUIRED, "Least DMA RAM since boot"},
    Check{Id::MEM_DMA_BLOCK,   "mem.dma_block",     "B",    1536,  kAnyHi, Need::REQUIRED, "Largest DMA block"},
    Check{Id::MEM_PSRAM_FREE,  "mem.psram_free",    "KB",   8192,  kAnyHi, Need::REQUIRED, "PSRAM free"},
    Check{Id::MEM_HEAP_OK,     "mem.heap_ok",       "",     1,     1,      Need::REQUIRED, "Heap metadata passes integrity check"},
    Check{Id::MEM_STK_LVGL,    "mem.stk_lvgl",      "B",    2048,  kAnyHi, Need::REQUIRED, "lv_task stack never used"},
    Check{Id::MEM_STK_ADC,     "mem.stk_adc",       "B",    1024,  kAnyHi, Need::REQUIRED, "ADC task stack never used"},
    Check{Id::MEM_STK_RTPS,    "mem.stk_rtps",      "B",    1024,  kAnyHi, Need::OPTIONAL, "rtps_pub stack never used"},
    // board
    Check{Id::I2C_MISSING,     "i2c.missing",       "",     0,     0,      Need::REQUIRED, "Boot-scan I2C devices still answering"},
    Check{Id::I2C_COUNT,       "i2c.devices",       "",     11,    kAnyHi, Need::REQUIRED, "Devices on the internal I2C bus"},
    Check{Id::IMU_ACCEL,       "imu.accel",         "mg",   850,   1150,   Need::REQUIRED, "Accelerometer reads 1 g at rest"},
    Check{Id::RTC_TICK,        "rtc.tick",          "s",    4,     8,      Need::REQUIRED, "RTC advanced across the window"},
    Check{Id::PWR_VBAT,        "pwr.vbat",          "mV",   6000,  8700,   Need::OPTIONAL, "Battery pack voltage (2S)"},
    // haptics
    Check{Id::HAP_DRV_ID,      "hap.drv_id",        "",     3,     7,      Need::REQUIRED, "DRV2605 answers with a DRV260x id"},
    Check{Id::HAP_DRV_FAULT,   "hap.drv_fault",     "",     1,     1,      Need::REQUIRED, "DRV2605 no over-current/over-temp"},
    Check{Id::HAP_DRV_PLAY,    "hap.drv_play",      "",     1,     1,      Need::REQUIRED, "DRV2605 plays a click to completion"},
    Check{Id::HAP_DA7280,      "hap.da7280",        "",     1,     1,      Need::OPTIONAL, "DA7280 answers on the bus"},
    // display and UI timing
    Check{Id::DISP_DIRECT,     "disp.direct",       "",     1,     1,      Need::REQUIRED, "LVGL draws into the DSI frame buffers"},
    Check{Id::DISP_BACKLIGHT,  "disp.backlight",    "%",    5,     100,    Need::REQUIRED, "Backlight on"},
    Check{Id::TIME_RENDER_AVG, "time.render_avg",   "us",   0,     180000, Need::REQUIRED, "Full-screen redraw, mean"},
    Check{Id::TIME_RENDER_MAX, "time.render_max",   "us",   0,     180000, Need::REQUIRED, "Full-screen redraw, worst"},
    Check{Id::TIME_UI_STALL,   "time.ui_stall",     "us",   0,     120000, Need::REQUIRED, "Longest wait for the UI lock"},
    // joystick and ADC, captured at rest
    Check{Id::TIME_ADC_AVG,    "time.adc_avg",      "us",   30000, 40000,  Need::REQUIRED, "ADC loop period, mean"},
    Check{Id::TIME_ADC_MAX,    "time.adc_max",      "us",   0,     70000,  Need::REQUIRED, "ADC loop period, worst"},
    Check{Id::JOY_VALID,       "joy.valid",         "%",    99,    100,    Need::REQUIRED, "ADC cycles with all three axes read"},
    Check{Id::JOY_X,           "joy.x_rest",        "mV",   1350,  1950,   Need::REQUIRED, "X axis rests near centre"},
    Check{Id::JOY_Y,           "joy.y_rest",        "mV",   1350,  1950,   Need::REQUIRED, "Y axis rests near centre"},
    Check{Id::JOY_TWIST,       "joy.twist_rest",    "mV",   1350,  1950,   Need::REQUIRED, "Twist rests near centre"},
    Check{Id::JOY_X_NOISE,     "joy.x_noise",       "mV",   0,     30,     Need::REQUIRED, "X peak-to-peak noise at rest"},
    Check{Id::JOY_Y_NOISE,     "joy.y_noise",       "mV",   0,     30,     Need::REQUIRED, "Y peak-to-peak noise at rest"},
    Check{Id::JOY_TWIST_NOISE, "joy.twist_noise",   "mV",   0,     120,    Need::REQUIRED, "Twist peak-to-peak noise"},
    Check{Id::JOY_CAL,         "joy.cal_saved",     "",     1,     1,      Need::REQUIRED, "Joystick calibration saved in flash"},
    Check{Id::JOY_X_CAL,       "joy.x_cal_off",     "mV",   0,     40,     Need::OPTIONAL, "X rest vs its calibrated centre"},
    Check{Id::JOY_Y_CAL,       "joy.y_cal_off",     "mV",   0,     40,     Need::OPTIONAL, "Y rest vs its calibrated centre"},
    Check{Id::JOY_TWIST_CAL,   "joy.twist_cal_off", "mV",   0,     50,     Need::OPTIONAL, "Twist rest vs saved centre"},
    Check{Id::JOY_BUTTON,      "joy.button_idle",   "",     1,     1,      Need::REQUIRED, "Stick button not stuck pressed"},
};
// clang-format on

/// @brief A check's position in kChecks, which is also its report index.
/// @param id the check
/// @return its index
[[nodiscard]] constexpr uint8_t index_of(Id id) { return static_cast<uint8_t>(id); }

/// @brief A check's row.
/// @param id the check
/// @return its row of kChecks
[[nodiscard]] constexpr const Check &check(Id id) { return kChecks[index_of(id)]; }

// --- invariants of the table (TS-UNIT-04 tests them again at run time) ---------------------

static_assert(kChecks.size() < 256, "the report carries the check index as a uint8_t");

/// @brief Each row's id is its position, and Id has one enumerator per row.
/// @return true when Id and kChecks agree, in count and order
consteval bool ids_match_rows() {
  for (std::size_t i = 0; i < kChecks.size(); ++i) {
    if (index_of(kChecks[i].id) != i) {
      return false;
    }
  }
  return static_cast<std::size_t>(Id::COUNT) == kChecks.size();
}
static_assert(ids_match_rows(), "Id and kChecks disagree: same checks, same order");

/// @brief Every row has lo <= hi, a name and a why, and a known need.
/// @return true when every row is well formed
consteval bool rows_well_formed() {
  return std::all_of(kChecks.begin(), kChecks.end(), [](const Check &c) {
    const bool need_ok =
        c.need == Need::REQUIRED || c.need == Need::OPTIONAL || c.need == Need::REMOTE;
    return c.lo <= c.hi && !c.name.empty() && !c.why.empty() && need_ok;
  });
}
static_assert(rows_well_formed(),
              "a selftest_spec.hpp row has lo > hi, no name or why, or a bad need");

/// @brief No two rows share a name: tools key the report on it.
/// @return true when the names are unique
consteval bool names_unique() {
  for (std::size_t i = 0; i < kChecks.size(); ++i) {
    for (std::size_t j = i + 1; j < kChecks.size(); ++j) {
      if (kChecks[i].name == kChecks[j].name) {
        return false;
      }
    }
  }
  return true;
}
static_assert(names_unique(), "two selftest_spec.hpp rows share a name");

} // namespace selftest_spec

#endif /* SELFTEST_SPEC_H */
