// The DA7280 bench run (CONFIG_HMI_BENCH_DA7280_TEST): a register probe, then the
// Da7280 driver's functional test. Moved from main.cpp's frag_da7280.inc.
#include "feedback/da7280_bench.hpp"

#include <algorithm>
#include <chrono>
#include <system_error>
#include <thread>

#include "feedback/config.hpp"
#include "feedback/da7280.hpp"

using namespace std::chrono_literals;

namespace hmi::feedback {

// DA7280 haptic driver bring-up test: read-only register probe, no driver
// class yet. kDa7280Address is in da7280_bench.hpp.
static constexpr uint8_t kDa7280RegChipRev = 0x00;

static void test_da7280(espp::Logger &logger, espp::I2c &i2c,
                        const std::vector<uint8_t> &found_addresses) {
  if (!std::ranges::contains(found_addresses, kDa7280Address)) {
    logger.warn("DA7280 not found at {:#02x} — check wiring", kDa7280Address);
    return;
  }
  uint8_t chip_rev = 0;
  if (!i2c.read_at_register(kDa7280Address, kDa7280RegChipRev, &chip_rev, 1)) {
    logger.error("DA7280 found at {:#02x} but CHIP_REV read failed", kDa7280Address);
    return;
  }
  logger.info("DA7280 CHIP_REV (reg {:#02x}) = {:#02x}", kDa7280RegChipRev, chip_rev);
}

// DA7280 driver functional test (SparkFun Qwiic Haptic Motor's bundled LRA
// electrical parameters). Exercises the driver's public API end to end:
//   1. Baseline smoke test (init + vibrate + stop)
//   2. Magnitude sweep across the DRO drive range
//   3. Acceleration mode: disabled, at high magnitudes (the "enabled at
//      high magnitude" case is invalid and is proven so at compile time
//      via static_assert, not exercised at runtime)
//   4. Rapid stop: enabled (snap stop) vs disabled (coast)
//   5. Frequency tracking: enabled vs disabled, with a fault-status peek
//   6. Custom Waveform Operation: SINE/SQUARE/TRIANGLE presets, then a raw
//      CUSTOM coefficient triple, then leaving the mode again
//
// Timing is deliberately generous: kDa7280DriveDuration is how long each
// vibration runs (long enough to unambiguously feel), kDa7280PauseDuration
// is silence between changes (long enough that "did it stop?" isn't a
// guess), and kDa7280StepPause gives time to read a step's header log
// before the first action in that step fires. These must be at namespace
// scope (not locals) since they're used as lambda default-argument values
// in test_da7280_functional() below.
static constexpr auto kDa7280DriveDuration = 750ms;
static constexpr auto kDa7280PauseDuration = 1500ms;
static constexpr auto kDa7280StepPause = 750ms;
static constexpr auto kDa7280FreqTrackDriveDuration = 1500ms;
static constexpr int kDa7280TotalTests = 15;

// Steps 4-5 and step 6 of test_da7280_functional, split off so each function
// stays within CS-FIL-01's 120 lines. `print_step_header` and `run_test` are that
// function's lambdas, so the tests keep one numbering across the split.
static void test_da7280_steps_4_5(espp::Da7280 &da7280, std::error_code &ec,
                                  const auto &print_step_header, const auto &run_test) {
  // 4. Rapid stop: enabled should snap to a stop, disabled should coast.
  // Magnitude 100 (not 150): acceleration is still enabled at this point
  // (left that way at the end of step 3), so anything above 127 would
  // produce no output here too.
  print_step_header("[DA7280 4/6] Rapid stop: enabled (snap) then disabled (coast)");
  std::this_thread::sleep_for(kDa7280StepPause);
  da7280.set_rapid_stop_enabled(true, ec);
  run_test("Rapid stop enabled", 100);

  da7280.set_rapid_stop_enabled(false, ec);
  std::this_thread::sleep_for(kDa7280PauseDuration);
  run_test("Rapid stop disabled", 100);
  da7280.set_rapid_stop_enabled(true, ec); // restore default

  // 5. Frequency tracking: enabled then disabled. A restrained/blocked
  // motor can trip a spurious fault with tracking enabled; run_test's
  // fault-status log is how to actually see that instead of guessing from
  // silence.
  print_step_header(
      "[DA7280 5/6] Frequency tracking: enabled then disabled, checking fault status");
  std::this_thread::sleep_for(kDa7280StepPause);
  da7280.set_frequency_tracking_enabled(true, ec);
  run_test("Freq tracking enabled", 80, kDa7280FreqTrackDriveDuration);

  da7280.set_frequency_tracking_enabled(false, ec);
  std::this_thread::sleep_for(kDa7280PauseDuration);
  run_test("Freq tracking disabled", 80, kDa7280FreqTrackDriveDuration);
  da7280.set_frequency_tracking_enabled(true, ec); // restore default
}

static void test_da7280_step_6(espp::Logger &logger, espp::Da7280 &da7280, std::error_code &ec,
                               const auto &print_step_header, const auto &run_test) {
  // 6. Custom Waveform Operation: SINE/SQUARE/TRIANGLE presets, then a raw
  // CUSTOM coefficient triple. enable_custom_waveform(true, ...) disables
  // acceleration/rapid-stop/amp-PID/frequency-tracking for us (required by
  // the chip for this mode) - we restore them ourselves at the end since
  // the driver won't do that automatically, see enable_custom_waveform()'s
  // doc comment. Magnitude 150 is safe here regardless: acceleration is
  // off for the whole step.
  print_step_header("[DA7280 6/6] Custom Waveform Operation: SINE, SQUARE, TRIANGLE, CUSTOM");
  std::this_thread::sleep_for(kDa7280StepPause);
  if (!da7280.enable_custom_waveform(true, ec)) {
    logger.error("[DA7280 6/6] enable_custom_waveform(true) failed: {}", ec.message());
  }

  da7280.set_wave_shape(espp::Da7280::WaveShape::SINE, ec);
  run_test("Custom waveform SINE", 150);

  da7280.set_wave_shape(espp::Da7280::WaveShape::SQUARE, ec);
  run_test("Custom waveform SQUARE", 150);

  da7280.set_wave_shape(espp::Da7280::WaveShape::TRIANGLE, ec);
  run_test("Custom waveform TRIANGLE", 150);

  // Arbitrary asymmetric shape: stays low, then jumps sharply near the end
  // of the quarter-period - clearly distinct from the three presets above.
  da7280.set_wave_coefficients(0x10, 0x30, 0xF8, ec);
  run_test("Custom waveform CUSTOM", 150);

  if (!da7280.enable_custom_waveform(false, ec)) {
    logger.error("[DA7280 6/6] enable_custom_waveform(false) failed: {}", ec.message());
  }
  // Restore the closed-loop defaults enable_custom_waveform(true) turned off
  da7280.set_acceleration_enabled(true, ec);
  da7280.set_rapid_stop_enabled(true, ec);
  da7280.set_frequency_tracking_enabled(true, ec);
  da7280.set_amp_pid_enabled(false, ec);
}

static void test_da7280_functional(espp::Logger &logger, espp::I2c &i2c) {
  std::error_code ec;
  auto da7280_i2c_device = i2c.add_device<uint8_t>(
      {
          .device_address = espp::Da7280::DEFAULT_ADDRESS,
          .timeout_ms = static_cast<int>(i2c.config().timeout_ms),
          .scl_speed_hz = i2c.config().clk_speed,
          .log_level = espp::Logger::Verbosity::WARN,
      },
      ec);
  if (!da7280_i2c_device) {
    logger.error("Could not create DA7280 I2C device: {}", ec.message());
    return;
  }

  espp::Da7280 da7280({
      .device_address = espp::Da7280::DEFAULT_ADDRESS,
      .motor_type = espp::Da7280::MotorType::LRA,
      .nominal_voltage = 2.106f,
      .abs_max_voltage = 2.26f,
      .max_current_ma = 165.4f,
      .impedance_ohms = 13.8f,
      .lra_freq_hz = 170.0f,
      .probe = espp::make_i2c_addressed_probe(da7280_i2c_device),
      .write = espp::make_i2c_addressed_write(da7280_i2c_device),
      .read_register = espp::make_i2c_addressed_read_register(da7280_i2c_device),
      .log_level = espp::Logger::Verbosity::INFO,
  });

  if (!da7280.initialize(ec)) {
    logger.error("DA7280 functional test: initialization failed: {}", ec.message());
    return;
  }

  // Every individual vibration is numbered [DA7280 TEST N/15] so a specific
  // failure (felt nothing, or an error) can be pinned to one line in the log
  // instead of just "somewhere in step 3". Status (IRQ_EVENT1/IRQ_STATUS1)
  // is checked after every test, not just the frequency-tracking ones, so
  // any future zero-output mystery has fault data attached instead of
  // needing a re-run to diagnose.
  // print_step_header draws a heavy divider (step boundary); run_test draws
  // a light divider (test boundary) before its own header line. Both are log
  // lines (CS-LOG-01), so the boundary is visually obvious when scrolling a
  // busy log.
  auto print_step_header = [&](const char *text) {
    logger.info("========================================");
    logger.info(text);
  };

  int test_index = 0;
  auto run_test = [&](const char *label, uint8_t magnitude,
                      std::chrono::milliseconds duration = kDa7280DriveDuration) {
    ++test_index;
    logger.info("----------------------------------------");
    logger.info("[DA7280 TEST {:02}/{}] {}: vibrate({})", test_index, kDa7280TotalTests, label,
                magnitude);
    da7280.vibrate(magnitude, ec);
    if (ec) {
      logger.error("[DA7280 TEST {:02}/{}] vibrate failed: {}", test_index, kDa7280TotalTests,
                   ec.message());
    }
    std::this_thread::sleep_for(duration);
    da7280.check_faults(ec);
    da7280.stop(ec);
    std::this_thread::sleep_for(kDa7280PauseDuration);
  };

  // 1. Baseline smoke test
  print_step_header("[DA7280 1/6] Baseline");
  std::this_thread::sleep_for(kDa7280StepPause);
  run_test("Baseline", 25);

  // 2. Magnitude sweep (0-127 only: acceleration is enabled by default at
  // this point, and above 127 with acceleration enabled produces no output
  // at all rather than a clamped value - see vibrate()'s doc comment. The
  // 128-255 range is exercised deliberately in step 3, with acceleration
  // disabled.)
  print_step_header("[DA7280 2/6] Magnitude sweep");
  std::this_thread::sleep_for(kDa7280StepPause);
  for (uint8_t magnitude : {32, 64, 96, 127}) {
    run_test("Magnitude sweep", magnitude);
  }

  // 3. Acceleration mode: disabled, at 200/255 (should drive at full
  // strength, current/voltage-limited near 255 - see check_faults()'s
  // WARNING decode). The "enabled + >127" combination is NOT exercised
  // here at runtime: it's proven invalid at COMPILE time instead, by the
  // static_asserts right below using Da7280::is_valid_dro_magnitude(). If
  // magnitude 200/255 were paired with AccelerationEnabled=true in one of
  // those asserts, the build itself would fail with a clear message rather
  // than needing a hardware run + log read to discover the mistake.
  static_assert(espp::Da7280::is_valid_dro_magnitude(/*acceleration_enabled=*/false, 200),
                "sanity check: 200 is valid with acceleration disabled");
  static_assert(espp::Da7280::is_valid_dro_magnitude(/*acceleration_enabled=*/false, 255),
                "sanity check: 255 is valid with acceleration disabled");
  static_assert(!espp::Da7280::is_valid_dro_magnitude(/*acceleration_enabled=*/true, 200),
                "sanity check: 200 must be rejected with acceleration enabled");
  static_assert(!espp::Da7280::is_valid_dro_magnitude(/*acceleration_enabled=*/true, 255),
                "sanity check: 255 must be rejected with acceleration enabled");
  print_step_header("[DA7280 3/6] Acceleration mode: disabled, at 200/255");
  std::this_thread::sleep_for(kDa7280StepPause);
  da7280.set_acceleration_enabled(false, ec);
  std::this_thread::sleep_for(kDa7280PauseDuration);
  for (uint8_t magnitude : {200, 255}) {
    run_test("Accel disabled", magnitude);
  }
  da7280.set_acceleration_enabled(true, ec); // restore default

  test_da7280_steps_4_5(da7280, ec, print_step_header, run_test);
  test_da7280_step_6(logger, da7280, ec, print_step_header, run_test);

  logger.info("DA7280 functional test complete");
}

// The DA7280 bench run at boot: test_da7280, then test_da7280_functional. Only
// with CONFIG_HMI_BENCH_DA7280_TEST (kBenchDa7280Test); otherwise nothing.
void run_da7280_bench(espp::Logger &logger, espp::I2c &i2c,
                      const std::vector<uint8_t> &found_addresses) {
  if constexpr (kBenchDa7280Test) {
    test_da7280(logger, i2c, found_addresses);
    test_da7280_functional(logger, i2c);
  }
}

} // namespace hmi::feedback
