#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

#include "base_peripheral.hpp"

namespace espp {
/**
 * @brief Class for controlling the Renesas/Dialog DA7280 Haptic Motor
 *        Driver in DRO (Direct Register Override / I2C) mode. Drives ERM
 *        (eccentric rotating mass) and LRA (linear resonant actuator) motors.
 *        The datasheet for the DA7280 can be found here:
 *        https://www.renesas.com/en/document/dst/da7280-datasheet.
 *
 * @note This driver only implements DRO mode (writing a drive magnitude
 *       directly over I2C). PWM control, waveform-memory playback, and
 *       edge/level triggered modes are not implemented.
 */
class Da7280 : public BasePeripheral<uint8_t, true> {
public:
  static constexpr uint8_t DEFAULT_ADDRESS =
      (0x4A); ///< 7-bit I2C address of the DA7280. NOTE: this cannot be changed, as the DA7280
              ///< does not support changing its I2C address.
  static constexpr uint8_t EXPECTED_CHIP_REV = 0xBA; ///< Expected value of the CHIP_REV register

  /**
   * @brief The type of actuator (motor) connected to the DA7280.
   */
  enum class MotorType : uint8_t {
    LRA = 0, ///< Linear Resonant Actuator
    ERM = 1, ///< Eccentric Rotating Mass
  };

  /**
   * @brief Named per-period drive shapes for Custom Waveform Operation, see
   *        enable_custom_waveform() and set_wave_shape(). SINE/SQUARE/
   *        TRIANGLE are preset SWG_C1-C3 coefficient triples; CUSTOM means
   *        set_wave_coefficients() was called with caller-chosen values
   *        instead of a preset.
   */
  enum class WaveShape : uint8_t {
    SINE,     ///< Datasheet default coefficients: {0x61, 0xB4, 0xEC}
    SQUARE,   ///< {0xFF, 0xFF, 0xFF}
    TRIANGLE, ///< {0x40, 0x80, 0xC0}
    CUSTOM,   ///< Set via set_wave_coefficients(), not a preset
  };

  /**
   * @brief Configuration structure for the DA7280.
   */
  struct Config {
    uint8_t device_address = DEFAULT_ADDRESS; /**< I2C address of the device. */
    MotorType motor_type{MotorType::LRA};     /**< Type of actuator connected. */
    float nominal_voltage{2.106f};            /**< Motor nominal (RMS) voltage, in volts. */
    float abs_max_voltage{2.26f};             /**< Motor absolute max voltage, in volts. */
    float max_current_ma{165.4f};             /**< Motor rated max current, in milliamps. */
    float impedance_ohms{13.8f};              /**< Motor rated impedance, in ohms. */
    float lra_freq_hz{170.0f};       /**< LRA resonant frequency, in Hz. Ignored for ERM motors. */
    bool acceleration_enabled{true}; /**< Closed-loop acceleration (overshoot/undershoot
                                          compensation). Limits the DRO drive range to 0-127 when
                                          enabled instead of 0-255. */
    bool rapid_stop_enabled{true};   /**< Actively brakes the actuator when the drive magnitude
                                          returns to 0, instead of coasting. */
    bool frequency_tracking_enabled{true};   /**< Tracks the LRA's actual resonant frequency at
                                                  runtime. NOTE: a mechanically restrained motor can
                                                  trip a spurious fault with this enabled - disable
                                                  it if you see that. Only meaningful for LRA
                                                  motors. */
    bool amp_pid_enabled{false};             /**< Closed-loop amplitude PID control. */
    BasePeripheral::probe_fn probe{nullptr}; /**< Function to probe the peripheral. */
    BasePeripheral::write_fn write{nullptr}; /**< Function for writing a byte to a register on the
                                                  Da7280. */
    BasePeripheral::read_register_fn
        read_register;    /**< Function for reading a register from the Da7280. */
    bool auto_init{true}; /**< If true, the driver will initialize the DA7280 on construction. */
    espp::Logger::Verbosity log_level{
        espp::Logger::Verbosity::WARN}; /**< Log verbosity for the Da7280.  */
  };

  /**
   * @brief Construct and initialize the DA7280.
   */
  explicit Da7280(const Config &config)
      : BasePeripheral({.address = config.device_address,
                        .probe = config.probe,
                        .write = config.write,
                        .read_register = config.read_register},
                       "Da7280", config.log_level)
      , motor_type_(config.motor_type)
      , acceleration_enabled_(config.acceleration_enabled)
      , config_(config) {
    if (config.auto_init) {
      std::error_code ec;
      initialize(ec);
      if (ec) {
        logger_.error("Failed to initialize: {}", ec.message());
      }
    }
  }

  /**
   * @brief Initialize the DA7280: verify chip presence, program the actuator
   *        electrical parameters, and select DRO (I2C direct-drive) mode.
   * @param ec Error code to set if there is an error.
   * @return true if the initialization was successful, false if there was an
   *         error.
   */
  bool initialize(std::error_code &ec) { return init(config_, ec); }

  /**
   * @brief Read the CHIP_REV identification register.
   * @param ec Error code to set if there is an error.
   * @return CHIP_REV value (0xBA if this is a genuine DA7280), or 0 on error.
   */
  uint8_t chip_revision(std::error_code &ec) const {
    return read_u8_from_register((uint8_t)Register::CHIP_REV, ec);
  }

  /**
   * @brief Drive the actuator at the given magnitude (DRO mode).
   * @note If acceleration mode is enabled (the default), only 0-127 is
   *       valid. Empirically (not just per clamped-range assumption), values
   *       128-255 produce NO output at all while acceleration is enabled -
   *       the chip does not clamp them down to a lesser drive. Disable
   *       acceleration (set_acceleration_enabled(false, ec)) if you need the
   *       full 0-255 range.
   * @param magnitude Drive strength: 0 (off) to 255 (max, only reachable
   *                  with acceleration disabled).
   * @param ec Error code to set if there is an error.
   * @return true if the write was successful, false if there was an error.
   */
  bool vibrate(uint8_t magnitude, std::error_code &ec) {
    if (acceleration_enabled_ && magnitude > 127) {
      logger_.warn("vibrate({}) requested while acceleration mode is enabled - the DA7280 "
                   "produces no output above 127 in this mode, not a clamped value. Call "
                   "set_acceleration_enabled(false, ec) first if you need this magnitude.",
                   magnitude);
    }
    logger_.info("Setting vibration magnitude to {}", magnitude);
    write_u8_to_register((uint8_t)Register::TOP_CTL2, magnitude, ec);
    return !ec; // return true if no error
  }

  /**
   * @brief Stop the actuator. Equivalent to vibrate(0, ec).
   * @param ec Error code to set if there is an error.
   * @return true if the write was successful, false if there was an error.
   */
  bool stop(std::error_code &ec) { return vibrate(0, ec); }

  /**
   * @brief IRQ_EVENT1 bit definitions (register 0x03).
   */
  enum class Event : uint8_t {
    SEQ_CONTINUE = 0x01,   ///< Waveform-memory sequence still running
    UVLO = 0x02,           ///< Under-voltage lockout
    SEQ_DONE = 0x04,       ///< Waveform-memory sequence finished
    OVERTEMP_CRIT = 0x08,  ///< Critical overtemperature
    SEQ_FAULT = 0x10,      ///< Waveform-memory sequence fault
    WARNING = 0x20,        ///< A warning bit is set in IRQ_EVENT_WARN_DIAG; see enum WarnDiag
    ACTUATOR_FAULT = 0x40, ///< Actuator (open/short circuit) fault
    OC_FAULT = 0x80,       ///< Over-current fault
  };

  /**
   * @brief IRQ_EVENT_WARN_DIAG bit definitions (register 0x04) - the detail
   *        behind Event::WARNING. Only bits confirmed against the datasheet
   *        are named here; any other bit is surfaced as raw hex by
   *        check_faults() rather than guessed at.
   */
  enum class WarnDiag : uint8_t {
    LIM_DRIVE_ACC = 0x40, ///< Acceleration is limited: the supply voltage is lower than
                          ///< required for the requested acceleration target
  };

  /**
   * @brief Read IRQ_EVENT1, log a warning/error for any concerning bits,
   *        then clear them by writing the read value back (IRQ_EVENT1 is
   *        write-1-to-clear, not clear-on-read - without this write-back
   *        every call would keep re-reporting the same stale latched
   *        bit(s) from the first time they were ever set, regardless of
   *        what's happening on the current call).
   * @param ec Error code to set if there is an error.
   * @return true if no fault/warning bits were set, false otherwise (ec is
   *         only set for an I2C error, not for a set fault bit).
   */
  bool check_faults(std::error_code &ec) {
    uint8_t events = read_u8_from_register((uint8_t)Register::IRQ_EVENT1, ec);
    if (ec)
      return false;

    bool ok = true;
    if (events & (uint8_t)Event::WARNING) {
      // Event::WARNING is just a summary flag; the actual reason lives in
      // IRQ_EVENT_WARN_DIAG. Read + log + clear (write-1-to-clear, per
      // datasheet) that too so the real cause is visible instead of just
      // "a warning happened".
      std::error_code diag_ec;
      uint8_t diag = read_u8_from_register((uint8_t)Register::IRQ_EVENT_WARN_DIAG, diag_ec);
      if (diag_ec) {
        logger_.warn("DA7280 WARNING event (IRQ_EVENT1=0x{:02X}), failed to read "
                     "IRQ_EVENT_WARN_DIAG for detail: {}",
                     events, diag_ec.message());
      } else if (diag & (uint8_t)WarnDiag::LIM_DRIVE_ACC) {
        logger_.warn("DA7280 WARNING: acceleration limited (E_LIM_DRIVE_ACC, "
                     "IRQ_EVENT_WARN_DIAG=0x{:02X}) - supply voltage is too low for the "
                     "requested acceleration target. Check the DA7280's supply headroom, "
                     "lower the drive magnitude, or disable acceleration mode.",
                     diag);
      } else {
        logger_.warn("DA7280 WARNING event (IRQ_EVENT1=0x{:02X}), IRQ_EVENT_WARN_DIAG=0x{:02X} - "
                     "no named bit matched, see datasheet for that register's bit table",
                     events, diag);
      }
      if (!diag_ec && diag != 0) {
        write_u8_to_register((uint8_t)Register::IRQ_EVENT_WARN_DIAG, diag, diag_ec);
      }
      ok = false;
    }
    if (events & (uint8_t)Event::UVLO) {
      logger_.warn("DA7280 UVLO event (under-voltage lockout, IRQ_EVENT1=0x{:02X})", events);
      ok = false;
    }
    if (events & (uint8_t)Event::SEQ_FAULT) {
      logger_.warn("DA7280 SEQ_FAULT event (IRQ_EVENT1=0x{:02X})", events);
      ok = false;
    }
    if (events & (uint8_t)Event::OVERTEMP_CRIT) {
      logger_.error("DA7280 OVERTEMP_CRIT event (IRQ_EVENT1=0x{:02X})", events);
      ok = false;
    }
    if (events & (uint8_t)Event::ACTUATOR_FAULT) {
      logger_.error("DA7280 ACTUATOR_FAULT event (IRQ_EVENT1=0x{:02X})", events);
      ok = false;
    }
    if (events & (uint8_t)Event::OC_FAULT) {
      logger_.error("DA7280 OC_FAULT event (over-current, IRQ_EVENT1=0x{:02X})", events);
      ok = false;
    }
    if (ok) {
      logger_.info("DA7280 status clean (IRQ_EVENT1=0x{:02X})", events);
    } else {
      write_u8_to_register((uint8_t)Register::IRQ_EVENT1, events, ec);
      if (ec) {
        logger_.warn("DA7280 failed to clear IRQ_EVENT1: {}", ec.message());
      }
    }
    return ok;
  }

  /**
   * @brief Compile-time check for whether a DRO magnitude is valid given the
   *        acceleration mode setting. With acceleration enabled, the DA7280
   *        produces no output for magnitude > 127 (see vibrate()'s note).
   *        Use in a static_assert at call sites where both the magnitude
   *        and the acceleration setting are compile-time constants, to
   *        catch the mistake at build time instead of only at runtime.
   */
  static constexpr bool is_valid_dro_magnitude(bool acceleration_enabled, uint8_t magnitude) {
    return !acceleration_enabled || magnitude <= 127;
  }

  /**
   * @brief Set the actuator (motor) type.
   * @param motor_type Type of actuator connected.
   * @param ec Error code to set if there is an error.
   * @return true if the write was successful, false if there was an error.
   */
  bool set_motor_type(MotorType motor_type, std::error_code &ec) {
    std::lock_guard<std::recursive_mutex> lock(base_mutex_);
    logger_.info("Setting motor type to {}", motor_type);
    motor_type_ = motor_type;

    static constexpr uint8_t MOTOR_TYPE_BIT_MASK = 0x20; // bit 5
    if (motor_type == MotorType::ERM) {
      set_bits_in_register((uint8_t)Register::TOP_CFG1, MOTOR_TYPE_BIT_MASK, ec);
    } else {
      clear_bits_in_register((uint8_t)Register::TOP_CFG1, MOTOR_TYPE_BIT_MASK, ec);
    }
    return !ec; // return true if no error
  }

  /**
   * @brief Enable or disable closed-loop acceleration (overshoot/undershoot
   *        compensation).
   * @note While enabled, the effective vibrate() drive range is 0-127
   *       instead of 0-255.
   * @param enabled true to enable acceleration mode, false to disable it.
   * @param ec Error code to set if there is an error.
   * @return true if the write was successful, false if there was an error.
   */
  bool set_acceleration_enabled(bool enabled, std::error_code &ec) {
    std::lock_guard<std::recursive_mutex> lock(base_mutex_);
    logger_.info("{} acceleration mode", enabled ? "Enabling" : "Disabling");
    acceleration_enabled_ = enabled;
    static constexpr uint8_t ACCEL_EN_BIT_MASK = 0x04; // bit 2
    if (enabled) {
      set_bits_in_register((uint8_t)Register::TOP_CFG1, ACCEL_EN_BIT_MASK, ec);
    } else {
      clear_bits_in_register((uint8_t)Register::TOP_CFG1, ACCEL_EN_BIT_MASK, ec);
    }
    return !ec; // return true if no error
  }

  /**
   * @brief Enable or disable rapid stop (active braking when the drive
   *        magnitude returns to 0, instead of coasting to a stop).
   * @param enabled true to enable rapid stop, false to disable it.
   * @param ec Error code to set if there is an error.
   * @return true if the write was successful, false if there was an error.
   */
  bool set_rapid_stop_enabled(bool enabled, std::error_code &ec) {
    std::lock_guard<std::recursive_mutex> lock(base_mutex_);
    logger_.info("{} rapid stop", enabled ? "Enabling" : "Disabling");
    static constexpr uint8_t RAPID_STOP_EN_BIT_MASK = 0x02; // bit 1
    if (enabled) {
      set_bits_in_register((uint8_t)Register::TOP_CFG1, RAPID_STOP_EN_BIT_MASK, ec);
    } else {
      clear_bits_in_register((uint8_t)Register::TOP_CFG1, RAPID_STOP_EN_BIT_MASK, ec);
    }
    return !ec; // return true if no error
  }

  /**
   * @brief Enable or disable LRA resonant-frequency tracking.
   * @note A mechanically restrained LRA can trip a spurious fault with this
   *       enabled; disable it if you see unexpected faults during bring-up.
   * @param enabled true to enable frequency tracking, false to disable it.
   * @param ec Error code to set if there is an error.
   * @return true if the write was successful, false if there was an error.
   */
  bool set_frequency_tracking_enabled(bool enabled, std::error_code &ec) {
    std::lock_guard<std::recursive_mutex> lock(base_mutex_);
    if (motor_type_ != MotorType::LRA) {
      logger_.warn("Setting frequency tracking while motor type is set to {}", motor_type_.load());
    }
    logger_.info("{} frequency tracking", enabled ? "Enabling" : "Disabling");
    static constexpr uint8_t FREQ_TRACK_EN_BIT_MASK = 0x08; // bit 3
    if (enabled) {
      set_bits_in_register((uint8_t)Register::TOP_CFG1, FREQ_TRACK_EN_BIT_MASK, ec);
    } else {
      clear_bits_in_register((uint8_t)Register::TOP_CFG1, FREQ_TRACK_EN_BIT_MASK, ec);
    }
    return !ec; // return true if no error
  }

  /**
   * @brief Enable or disable closed-loop amplitude PID control.
   * @param enabled true to enable amplitude PID, false to disable it.
   * @param ec Error code to set if there is an error.
   * @return true if the write was successful, false if there was an error.
   */
  bool set_amp_pid_enabled(bool enabled, std::error_code &ec) {
    std::lock_guard<std::recursive_mutex> lock(base_mutex_);
    logger_.info("{} amplitude PID", enabled ? "Enabling" : "Disabling");
    static constexpr uint8_t AMP_PID_EN_BIT_MASK = 0x01; // bit 0
    if (enabled) {
      set_bits_in_register((uint8_t)Register::TOP_CFG1, AMP_PID_EN_BIT_MASK, ec);
    } else {
      clear_bits_in_register((uint8_t)Register::TOP_CFG1, AMP_PID_EN_BIT_MASK, ec);
    }
    return !ec; // return true if no error
  }

  /**
   * @brief Set the LRA resonant frequency.
   * @note Only meaningful when the motor type is MotorType::LRA.
   * @param freq_hz Resonant frequency of the LRA actuator, in Hz.
   * @param ec Error code to set if there is an error.
   * @return true if the write was successful, false if there was an error.
   */
  bool set_lra_frequency_hz(float freq_hz, std::error_code &ec) {
    std::lock_guard<std::recursive_mutex> lock(base_mutex_);
    if (motor_type_ != MotorType::LRA) {
      logger_.warn("Setting LRA resonant frequency while motor type is set to {}",
                   motor_type_.load());
    }
    logger_.info("Setting LRA resonant frequency to {} Hz", freq_hz);
    // LRA period = 1 / (freq_hz * 1333.32ns), split MSB (bits[14:7]) / LSB
    // (bits[6:0], bit[7] reserved) across two registers
    uint16_t period = static_cast<uint16_t>(1.0f / (freq_hz * LRA_PERIOD_SCALE) + 0.5f);
    write_u8_to_register((uint8_t)Register::FRQ_LRA_PER_H, (period >> 7) & 0xFF, ec);
    if (ec)
      return false;
    static constexpr uint8_t FRQ_LRA_PER_L_MASK = 0x7F; // bits 0-6, bit 7 reserved
    set_bits_in_register_by_mask((uint8_t)Register::FRQ_LRA_PER_L, FRQ_LRA_PER_L_MASK,
                                 period & 0x7F, ec);
    return !ec; // return true if no error
  }

  /**
   * @brief Enable or disable Custom Waveform Operation (datasheet Section
   *        5.7.6): an open-loop mode where one resonant period is driven as
   *        a 4-point shape (see set_wave_shape()/set_wave_coefficients())
   *        instead of the default closed-loop BEMF-sensed drive. vibrate()
   *        still works the same way while this is active - it's the shape
   *        within each period that changes, not the amplitude-streaming API.
   * @note Enabling this REQUIRES acceleration, rapid stop, amplitude PID,
   *       and frequency tracking to all be off; this method disables all
   *       four for you when enabling. It does NOT re-enable them when
   *       disabling - call the individual set_*_enabled() methods
   *       afterward if you want them back, since this driver has no way to
   *       know which of them (if any) you actually want restored.
   * @param enabled true to enter Custom Waveform Operation, false to return
   *                to standard closed-loop operation.
   * @param ec Error code to set if there is an error.
   * @return true if all the writes succeeded, false if any failed.
   */
  bool enable_custom_waveform(bool enabled, std::error_code &ec) {
    std::lock_guard<std::recursive_mutex> lock(base_mutex_);
    logger_.info("{} custom waveform operation", enabled ? "Entering" : "Leaving");

    if (enabled) {
      // Custom Waveform Operation requires all closed-loop features off
      if (!set_acceleration_enabled(false, ec))
        return false;
      if (!set_rapid_stop_enabled(false, ec))
        return false;
      if (!set_amp_pid_enabled(false, ec))
        return false;
      if (!set_frequency_tracking_enabled(false, ec))
        return false;
    }

    // BEMF_SENSE_EN: TOP_CFG1 bit 4. 0 = custom waveform, 1 = standard operation
    static constexpr uint8_t BEMF_SENSE_EN_BIT_MASK = 0x10; // bit 4
    if (enabled) {
      clear_bits_in_register((uint8_t)Register::TOP_CFG1, BEMF_SENSE_EN_BIT_MASK, ec);
    } else {
      set_bits_in_register((uint8_t)Register::TOP_CFG1, BEMF_SENSE_EN_BIT_MASK, ec);
    }
    if (ec)
      return false;

    // WAVEGEN_MODE: SEQ_CTL1 bit 1. 0 = normal (step/ramp), 1 = custom wave
    static constexpr uint8_t WAVEGEN_MODE_BIT_MASK = 0x02; // bit 1
    if (enabled) {
      set_bits_in_register((uint8_t)Register::SEQ_CTL1, WAVEGEN_MODE_BIT_MASK, ec);
    } else {
      clear_bits_in_register((uint8_t)Register::SEQ_CTL1, WAVEGEN_MODE_BIT_MASK, ec);
    }
    if (ec)
      return false;

    // V2I_FACTOR_FREEZE: TOP_CFG4 bit 7. 1 = freeze automatic updates (required here)
    static constexpr uint8_t V2I_FACTOR_FREEZE_BIT_MASK = 0x80; // bit 7
    if (enabled) {
      set_bits_in_register((uint8_t)Register::TOP_CFG4, V2I_FACTOR_FREEZE_BIT_MASK, ec);
    } else {
      clear_bits_in_register((uint8_t)Register::TOP_CFG4, V2I_FACTOR_FREEZE_BIT_MASK, ec);
    }
    if (ec)
      return false;

    // DELAY_H (full byte): datasheet-documented settings are 0x00 for
    // wideband/custom-waveform mode, 0x25 for closed-loop frequency
    // tracking mode
    write_u8_to_register((uint8_t)Register::FRQ_PHASE_H, enabled ? 0x00 : 0x25, ec);
    if (ec)
      return false;

    // DELAY_FREEZE (bit 7) + DELAY_SHIFT_L (bits[2:0]) share FRQ_PHASE_L.
    // enabled: freeze=1, shift=0 (0x80). disabled: freeze=0, shift=5 (0x05)
    write_u8_to_register((uint8_t)Register::FRQ_PHASE_L, enabled ? 0x80 : 0x05, ec);
    if (ec)
      return false;

    custom_waveform_enabled_ = enabled;
    return true;
  }

  /**
   * @brief Set the 3 coefficients that shape one resonant period during
   *        Custom Waveform Operation. Point 0 (0%) and point 4 (100% of
   *        IMAX) are fixed by the chip; these set points 1-3 (and are
   *        mirrored/repeated to build the full period, see datasheet
   *        Section 5.7.6 Figure 18). Only takes effect once
   *        enable_custom_waveform(true, ec) has been called.
   * @param coeff1 Point 1 amplitude, 0 (0% of IMAX) to 255 (100%).
   * @param coeff2 Point 2 amplitude, 0-255.
   * @param coeff3 Point 3 amplitude, 0-255.
   * @param ec Error code to set if there is an error.
   * @return true if all the writes succeeded, false if any failed.
   */
  bool set_wave_coefficients(uint8_t coeff1, uint8_t coeff2, uint8_t coeff3, std::error_code &ec) {
    std::lock_guard<std::recursive_mutex> lock(base_mutex_);
    logger_.info("Setting custom waveform coefficients to {{0x{:02X}, 0x{:02X}, 0x{:02X}}}", coeff1,
                 coeff2, coeff3);
    write_u8_to_register((uint8_t)Register::SWG_C1, coeff1, ec);
    if (ec)
      return false;
    write_u8_to_register((uint8_t)Register::SWG_C2, coeff2, ec);
    if (ec)
      return false;
    write_u8_to_register((uint8_t)Register::SWG_C3, coeff3, ec);
    if (ec)
      return false;
    wave_shape_ = WaveShape::CUSTOM;
    return true;
  }

  /**
   * @brief Set one of the named coefficient presets (SINE, SQUARE,
   *        TRIANGLE). For an arbitrary shape, call set_wave_coefficients()
   *        directly instead - WaveShape::CUSTOM has no preset and is
   *        rejected here.
   * @param shape SINE, SQUARE, or TRIANGLE.
   * @param ec Error code to set if there is an error.
   * @return true if the write was successful, false if there was an error
   *         (including if shape is WaveShape::CUSTOM).
   */
  bool set_wave_shape(WaveShape shape, std::error_code &ec) {
    uint8_t coeff1 = 0, coeff2 = 0, coeff3 = 0;
    switch (shape) {
    case WaveShape::SINE:
      coeff1 = 0x61;
      coeff2 = 0xB4;
      coeff3 = 0xEC;
      break;
    case WaveShape::SQUARE:
      coeff1 = 0xFF;
      coeff2 = 0xFF;
      coeff3 = 0xFF;
      break;
    case WaveShape::TRIANGLE:
      coeff1 = 0x40;
      coeff2 = 0x80;
      coeff3 = 0xC0;
      break;
    default:
      logger_.error("set_wave_shape(CUSTOM) has no preset - call set_wave_coefficients() "
                    "directly instead");
      ec = std::make_error_code(std::errc::invalid_argument);
      return false;
    }
    if (!set_wave_coefficients(coeff1, coeff2, coeff3, ec))
      return false;
    wave_shape_ = shape; // overwrite the CUSTOM tag set_wave_coefficients() just applied
    logger_.info("Set wave shape to {}", shape);
    return true;
  }

protected:
  bool init(const Config &c, std::error_code &ec) {
    std::lock_guard<std::recursive_mutex> lock(base_mutex_);
    logger_.info("Initializing DA7280");

    uint8_t chip_rev = chip_revision(ec);
    if (ec)
      return false;
    if (chip_rev != EXPECTED_CHIP_REV) {
      logger_.error("DA7280 CHIP_REV mismatch: expected 0x{:02X}, got 0x{:02X}", EXPECTED_CHIP_REV,
                    chip_rev);
      ec = std::make_error_code(std::errc::no_such_device);
      return false;
    }

    if (!set_motor_type(c.motor_type, ec))
      return false;
    if (!set_acceleration_enabled(c.acceleration_enabled, ec))
      return false;
    if (!set_rapid_stop_enabled(c.rapid_stop_enabled, ec))
      return false;
    if (!set_frequency_tracking_enabled(c.frequency_tracking_enabled, ec))
      return false;
    if (!set_amp_pid_enabled(c.amp_pid_enabled, ec))
      return false;

    // Nominal / absolute max voltage, 23.4 mV/LSB
    write_u8_to_register((uint8_t)Register::ACTUATOR1, volts_to_reg(c.nominal_voltage), ec);
    if (ec)
      return false;
    write_u8_to_register((uint8_t)Register::ACTUATOR2, volts_to_reg(c.abs_max_voltage), ec);
    if (ec)
      return false;

    // Max current: IMAX(mA) = 28.6 + 7.2 * reg, reg is bits[4:0] of ACTUATOR3
    int imax_reg_i = static_cast<int>((c.max_current_ma - IMAX_OFFSET_MA) / IMAX_STEP_MA + 0.5f);
    uint8_t imax_reg = static_cast<uint8_t>(std::clamp(imax_reg_i, 0, 0x1F));
    static constexpr uint8_t IMAX_BIT_MASK = 0x1F; // bits 0-4
    set_bits_in_register_by_mask((uint8_t)Register::ACTUATOR3, IMAX_BIT_MASK, imax_reg, ec);
    if (ec)
      return false;

    // Motor impedance calibration factor: V2I_FACTOR = impedance * (IMAX_reg + 4) / 1.6104
    uint16_t v2i_factor =
        static_cast<uint16_t>(c.impedance_ohms * (imax_reg + 4) / V2I_FACTOR_SCALE + 0.5f);
    write_u8_to_register((uint8_t)Register::CALIB_V2I_H, (v2i_factor >> 8) & 0xFF, ec);
    if (ec)
      return false;
    write_u8_to_register((uint8_t)Register::CALIB_V2I_L, v2i_factor & 0xFF, ec);
    if (ec)
      return false;

    // LRA resonant frequency (only meaningful for LRA motors)
    if (c.motor_type == MotorType::LRA) {
      if (!set_lra_frequency_hz(c.lra_freq_hz, ec))
        return false;
    }

    // Select DRO (I2C direct-drive) operation mode: TOP_CTL1 bits[2:0]
    static constexpr uint8_t OPERATION_MODE_MASK = 0x07; // bits 0-2
    static constexpr uint8_t OPERATION_MODE_DRO = 0x01;  // 0=INACTIVE, 1=DRO
    set_bits_in_register_by_mask((uint8_t)Register::TOP_CTL1, OPERATION_MODE_MASK,
                                 OPERATION_MODE_DRO, ec);
    if (ec)
      return false;

    return true; // initialization successful
  }

  static uint8_t volts_to_reg(float volts) {
    int reg = static_cast<int>(volts / VOLTAGE_LSB + 0.5f);
    return static_cast<uint8_t>(std::clamp(reg, 0, 0xFF));
  }

  // Register map (subset needed for DRO mode; see datasheet for full map)
  enum class Register : uint8_t {
    CHIP_REV = 0x00,            ///< Chip revision / WHOAMI, expected 0xBA
    IRQ_EVENT1 = 0x03,          ///< Latched fault/warning events, see enum Event
    IRQ_EVENT_WARN_DIAG = 0x04, ///< Detail bits behind Event::WARNING; datasheet has the bit table
    IRQ_STATUS1 = 0x06,         ///< Live fault/warning state (same bit layout as IRQ_EVENT1)
    FRQ_LRA_PER_H = 0x0A,       ///< LRA period[14:7]
    FRQ_LRA_PER_L = 0x0B,       ///< LRA period[6:0] in bits[6:0], bit[7] reserved
    ACTUATOR1 = 0x0C,           ///< Nominal voltage, 23.4mV/LSB
    ACTUATOR2 = 0x0D,           ///< Absolute max voltage, 23.4mV/LSB
    ACTUATOR3 = 0x0E,           ///< bits[4:0]: IMAX, mA = 28.6 + 7.2 * reg
    CALIB_V2I_H = 0x0F,         ///< V2I_FACTOR[9:8]
    CALIB_V2I_L = 0x10,         ///< V2I_FACTOR[7:0]
    TOP_CFG1 = 0x13, ///< Actuator type, BEMF_SENSE_EN, accel/rapid-stop/freq-track/amp-pid enables
    TOP_CFG4 = 0x16, ///< bit[7]: V2I_FACTOR_FREEZE
    TOP_CTL1 = 0x22, ///< bits[2:0]: operation mode
    TOP_CTL2 = 0x23, ///< DRO drive magnitude
    SEQ_CTL1 = 0x24, ///< bit[1]: WAVEGEN_MODE (custom waveform enable)
    SWG_C1 = 0x25,   ///< Custom waveform coefficient 1 (point 1 of 4, % of IMAX)
    SWG_C2 = 0x26,   ///< Custom waveform coefficient 2 (point 2 of 4)
    SWG_C3 = 0x27,   ///< Custom waveform coefficient 3 (point 3 of 4)
    FRQ_PHASE_H = 0x48, ///< DELAY_H, full byte; used by custom waveform operation
    FRQ_PHASE_L = 0x49, ///< bit[7]: DELAY_FREEZE, bits[2:0]: DELAY_SHIFT_L
  };

  static constexpr float VOLTAGE_LSB = 23.4e-3f;         ///< Volts per LSB (ACTUATOR1/2)
  static constexpr float IMAX_OFFSET_MA = 28.6f;         ///< IMAX formula offset, mA
  static constexpr float IMAX_STEP_MA = 7.2f;            ///< IMAX formula step, mA/LSB
  static constexpr float V2I_FACTOR_SCALE = 1.6104f;     ///< V2I_FACTOR formula scale
  static constexpr float LRA_PERIOD_SCALE = 1333.32e-9f; ///< LRA period formula scale, s

  std::atomic<MotorType> motor_type_;
  std::atomic<bool> acceleration_enabled_;
  std::atomic<bool> custom_waveform_enabled_{false};
  std::atomic<WaveShape> wave_shape_{WaveShape::SINE}; // matches the chip's reset coefficients
  Config config_{};
};
} // namespace espp

// for easy printing of the enum with the libfmt library:
template <> struct fmt::formatter<espp::Da7280::MotorType> {
  constexpr auto parse(format_parse_context &ctx) const { return ctx.begin(); }

  template <typename FormatContext>
  auto format(espp::Da7280::MotorType mt, FormatContext &ctx) const {
    switch (mt) {
    case espp::Da7280::MotorType::LRA:
      return fmt::format_to(ctx.out(), "LRA");
    case espp::Da7280::MotorType::ERM:
      return fmt::format_to(ctx.out(), "ERM");
    default:
      return fmt::format_to(ctx.out(), "UNKNOWN");
    }
  }
};

template <> struct fmt::formatter<espp::Da7280::WaveShape> {
  constexpr auto parse(format_parse_context &ctx) const { return ctx.begin(); }

  template <typename FormatContext>
  auto format(espp::Da7280::WaveShape s, FormatContext &ctx) const {
    switch (s) {
    case espp::Da7280::WaveShape::SINE:
      return fmt::format_to(ctx.out(), "SINE");
    case espp::Da7280::WaveShape::SQUARE:
      return fmt::format_to(ctx.out(), "SQUARE");
    case espp::Da7280::WaveShape::TRIANGLE:
      return fmt::format_to(ctx.out(), "TRIANGLE");
    case espp::Da7280::WaveShape::CUSTOM:
      return fmt::format_to(ctx.out(), "CUSTOM");
    default:
      return fmt::format_to(ctx.out(), "UNKNOWN");
    }
  }
};
