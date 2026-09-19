#pragma once

/**
 * @file rtps_serial.hpp
 * @brief The serial console over RTPS ("Serial over RTPS" in hmi_rtps_spec.hpp).
 *
 * Out, on kSerialTx: to each host that attaches, the lines the LogScreen still
 * holds, then everything printed, as it is printed - what the USB port shows.
 * In, on kSerialRx: typed bytes, echoed back, a line being a console command
 * (console.hpp); and RESET, which reboots. scripts/rtps_serial.py is the host.
 *
 * Nothing the network stack prints goes out this way (see the spec for why),
 * nor anything printed on the task that sends: both still reach the UART and
 * the LogScreen. Off unless CONFIG_HMI_RTPS_SERIAL; it has no authentication.
 */

/// Before rtps_comms_start(): it registers the kSerialRx handler.
void rtps_serial_start();
