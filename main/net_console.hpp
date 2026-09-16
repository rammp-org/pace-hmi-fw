#pragma once

/**
 * @file net_console.hpp
 * @brief The serial console over TCP (rammp::kConsolePort), one client at a time.
 *
 * Out: everything printed, as it is printed, after the lines the LogScreen
 * still holds. In: a line is a command (`help` lists them). Raw bytes, no
 * framing, so `idf.py monitor -p socket://<ip>:3333 --no-reset` attaches.
 * Off unless CONFIG_HMI_NET_CONSOLE; it has no authentication.
 */

/// After rtps_comms_start() has brought the network stack up.
void net_console_start();
