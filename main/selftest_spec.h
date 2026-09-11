/**
 * @file selftest_spec.h
 * @brief The HMI self test: every check it runs, and the limits it is held to.
 *
 * THIS FILE IS THE SPEC. A firmware change is checked by running the self test
 * and reading PASS/FAIL against these rows, not by reading the code:
 *
 *   - on the device:  Settings -> R&D SELF TEST (results on screen and serial)
 *   - from a PC:      python scripts/rtps_selftest.py  (exit 0 = all passed)
 *
 * To tighten or loosen a check, change its limits here. To add one, add a row
 * here and its measurement in selftest.cpp. A row with no measurement behind
 * it reports FAIL ("not measured"), so the spec can never silently claim a
 * check the firmware does not run.
 *
 *   X(ID, "name", "unit", lo, hi, need, "what it proves")
 *
 * lo/hi are inclusive integers in `unit`; ST_ANY_LO/ST_ANY_HI means no limit on
 * that side. A check with lo == hi == 1 and no unit is a yes/no check.
 *
 * `need` decides what a check that cannot be measured counts as:
 *   ST_REQUIRED  FAIL - the hardware or feature is expected on every unit
 *   ST_OPTIONAL  SKIP - fitted on some units only
 *   ST_REMOTE    FAIL when the run was requested over RTPS (a self-test peer
 *                is known to be there), SKIP when started from the HMI's own
 *                settings row. Only the ping checks use it: they need a peer
 *                that answers self-test pings, which a production MCB does not.
 *                Everything that needs the MCB itself (McbStatus arriving, its
 *                timing, a subscriber for the joystick stream) is REQUIRED, so
 *                an unplugged cable or a silent MCB fails wherever it is run.
 *
 * The limits come from measurements on the bench unit, with margin for
 * unit-to-unit spread. A check that fails on a healthy unit means the limit
 * is wrong, and the fix belongs here rather than in the code.
 *
 * Plain C, one row per line, so a script can scrape it the way rammp_rtps.py
 * scrapes the actuator table.
 */

#ifndef SELFTEST_SPEC_H
#define SELFTEST_SPEC_H

#include <stdint.h>

#define ST_ANY_LO INT32_MIN
#define ST_ANY_HI INT32_MAX

enum {
  ST_REQUIRED = 0,
  ST_OPTIONAL = 1,
  ST_REMOTE = 2,
};

/* How a run is paced. Every check below is measured inside one of these. */
#define ST_SETTLE_MS 1500    /* hands-off pause, so the at-rest joystick capture is at rest */
#define ST_WINDOW_MS 6000    /* observation window: joystick, ADC cadence, RTPS, UI stall */
#define ST_PING_PERIOD_MS 40 /* one RTPS ping per this, through the window */
#define ST_RENDER_FRAMES 30  /* forced full-screen redraws timed by the render checks */

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
 *            flush and vsync wait included, on MainScreenFlex with the results
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
 */

#define SELFTEST_TABLE(X)                                                                          \
  /* system */                                                                                     \
  X(SYS_RESET, "sys.clean_reset", "", 1, 1, ST_REQUIRED, "Last reset not a panic/WDT/brownout")    \
  X(SYS_CPU, "sys.cpu_mhz", "MHz", 360, 360, ST_REQUIRED, "CPU runs at the configured clock")      \
  X(SYS_UPTIME, "sys.uptime", "s", 0, ST_ANY_HI, ST_REQUIRED, "Seconds since boot (context)")      \
  X(LOG_CAPTURE, "log.capture", "", 1, 1, ST_REQUIRED, "Serial output reaches the log screen")     \
  /* network and RTPS - first, so the link is the first thing read on screen */                    \
  X(NET_LINK, "net.eth_link", "", 1, 1, ST_REQUIRED, "Ethernet link up (W5500)")                   \
  X(NET_IP, "net.ip", "", 1, 1, ST_REQUIRED, "DHCP lease held")                                    \
  X(RTPS_LINK, "rtps.mcb_link", "", 1, 1, ST_REQUIRED, "MCB answering (McbStatus arriving)")       \
  X(RTPS_MCB_PERIOD, "rtps.mcb_period", "ms", 400, 600, ST_REQUIRED, "McbStatus period, mean")     \
  X(RTPS_MCB_GAP, "rtps.mcb_gap", "ms", 0, 1000, ST_REQUIRED, "Longest McbStatus gap")             \
  X(RTPS_MCB_LOSS, "rtps.mcb_loss", "0.1%", 0, 100, ST_REQUIRED, "McbStatus lost (seq gaps)")      \
  X(RTPS_ADC_HZ, "rtps.adc_hz", "Hz", 25, 32, ST_REQUIRED, "Joystick samples published per s")     \
  X(RTPS_RTT_P50, "rtps.rtt_p50", "us", 0, 20000, ST_REMOTE, "Ping round trip, median")            \
  X(RTPS_RTT_P99, "rtps.rtt_p99", "us", 0, 250000, ST_REMOTE, "Ping round trip, 99th percentile")  \
  X(RTPS_PING_LOSS, "rtps.ping_loss", "0.1%", 0, 20, ST_REMOTE, "Pings with no pong")              \
  /* memory */                                                                                     \
  X(MEM_INT_FREE, "mem.int_free", "KB", 16, ST_ANY_HI, ST_REQUIRED, "Internal RAM free now")       \
  X(MEM_INT_MIN, "mem.int_min", "KB", 12, ST_ANY_HI, ST_REQUIRED, "Least internal RAM since boot") \
  X(MEM_INT_BLOCK, "mem.int_block", "KB", 12, ST_ANY_HI, ST_REQUIRED, "Largest internal block")    \
  X(MEM_DMA_FREE, "mem.dma_free", "KB", 2, ST_ANY_HI, ST_REQUIRED, "DMA-capable RAM free")         \
  X(MEM_DMA_MIN, "mem.dma_min", "B", 1536, ST_ANY_HI, ST_REQUIRED, "Least DMA RAM since boot")     \
  X(MEM_DMA_BLOCK, "mem.dma_block", "B", 1536, ST_ANY_HI, ST_REQUIRED, "Largest DMA block")        \
  X(MEM_PSRAM_FREE, "mem.psram_free", "KB", 8192, ST_ANY_HI, ST_REQUIRED, "PSRAM free")            \
  X(MEM_HEAP_OK, "mem.heap_ok", "", 1, 1, ST_REQUIRED, "Heap metadata passes integrity check")     \
  X(MEM_STK_LVGL, "mem.stk_lvgl", "B", 2048, ST_ANY_HI, ST_REQUIRED, "lv_task stack never used")   \
  X(MEM_STK_ADC, "mem.stk_adc", "B", 1024, ST_ANY_HI, ST_REQUIRED, "ADC task stack never used")    \
  X(MEM_STK_RTPS, "mem.stk_rtps", "B", 1024, ST_ANY_HI, ST_OPTIONAL, "rtps_pub stack never used")  \
  /* board */                                                                                      \
  X(I2C_MISSING, "i2c.missing", "", 0, 0, ST_REQUIRED, "Boot-scan I2C devices still answering")    \
  X(I2C_COUNT, "i2c.devices", "", 11, ST_ANY_HI, ST_REQUIRED, "Devices on the internal I2C bus")   \
  X(IMU_ACCEL, "imu.accel", "mg", 850, 1150, ST_REQUIRED, "Accelerometer reads 1 g at rest")       \
  X(RTC_TICK, "rtc.tick", "s", 4, 8, ST_REQUIRED, "RTC advanced across the window")                \
  X(PWR_VBAT, "pwr.vbat", "mV", 6000, 8700, ST_OPTIONAL, "Battery pack voltage (2S)")              \
  /* haptics */                                                                                    \
  X(HAP_DRV_ID, "hap.drv_id", "", 3, 7, ST_REQUIRED, "DRV2605 answers with a DRV260x id")          \
  X(HAP_DRV_FAULT, "hap.drv_fault", "", 1, 1, ST_REQUIRED, "DRV2605 no over-current/over-temp")    \
  X(HAP_DRV_PLAY, "hap.drv_play", "", 1, 1, ST_REQUIRED, "DRV2605 plays a click to completion")    \
  X(HAP_DA7280, "hap.da7280", "", 1, 1, ST_OPTIONAL, "DA7280 answers on the bus")                  \
  /* display and UI timing */                                                                      \
  X(DISP_DIRECT, "disp.direct", "", 1, 1, ST_REQUIRED, "LVGL draws into the DSI frame buffers")    \
  X(DISP_BACKLIGHT, "disp.backlight", "%", 5, 100, ST_REQUIRED, "Backlight on")                    \
  X(TIME_RENDER_AVG, "time.render_avg", "us", 0, 180000, ST_REQUIRED, "Full-screen redraw, mean")  \
  X(TIME_RENDER_MAX, "time.render_max", "us", 0, 180000, ST_REQUIRED, "Full-screen redraw, worst") \
  X(TIME_UI_STALL, "time.ui_stall", "us", 0, 120000, ST_REQUIRED, "Longest wait for the UI lock")  \
  /* joystick and ADC, captured at rest */                                                         \
  X(TIME_ADC_AVG, "time.adc_avg", "us", 30000, 40000, ST_REQUIRED, "ADC loop period, mean")        \
  X(TIME_ADC_MAX, "time.adc_max", "us", 0, 70000, ST_REQUIRED, "ADC loop period, worst")           \
  X(JOY_VALID, "joy.valid", "%", 99, 100, ST_REQUIRED, "ADC cycles with all three axes read")      \
  X(JOY_X, "joy.x_rest", "mV", 1350, 1950, ST_REQUIRED, "X axis rests near centre")                \
  X(JOY_Y, "joy.y_rest", "mV", 1350, 1950, ST_REQUIRED, "Y axis rests near centre")                \
  X(JOY_TWIST, "joy.twist_rest", "mV", 1350, 1950, ST_REQUIRED, "Twist rests near centre")         \
  X(JOY_X_NOISE, "joy.x_noise", "mV", 0, 30, ST_REQUIRED, "X peak-to-peak noise at rest")          \
  X(JOY_Y_NOISE, "joy.y_noise", "mV", 0, 30, ST_REQUIRED, "Y peak-to-peak noise at rest")          \
  X(JOY_TWIST_NOISE, "joy.twist_noise", "mV", 0, 120, ST_REQUIRED, "Twist peak-to-peak noise")     \
  X(JOY_CAL, "joy.cal_saved", "", 1, 1, ST_REQUIRED, "Joystick calibration saved in flash")        \
  X(JOY_X_CAL, "joy.x_cal_off", "mV", 0, 40, ST_OPTIONAL, "X rest vs its calibrated centre")       \
  X(JOY_Y_CAL, "joy.y_cal_off", "mV", 0, 40, ST_OPTIONAL, "Y rest vs its calibrated centre")       \
  X(JOY_TWIST_CAL, "joy.twist_cal_off", "mV", 0, 50, ST_OPTIONAL, "Twist rest vs saved centre")    \
  X(JOY_BUTTON, "joy.button_idle", "", 1, 1, ST_REQUIRED, "Stick button not stuck pressed")

#endif /* SELFTEST_SPEC_H */
