#pragma once

/**
 * @file console.hpp
 * @brief The console's commands, shared by every way in: the network console
 * (TCP) and the serial over RTPS. Answers go to stdout, so they reach wherever
 * the log goes.
 */

#include <functional>
#include <string>
#include <string_view>

/// One typed line (`help` lists the commands). Surrounding blanks and '\r' are
/// ignored; an empty line does nothing.
void console_run(std::string_view line);

/// Asked before a reboot (the `reboot` command, a serial RESET): why not now, or "".
/// Set once, before any console starts.
void console_set_reboot_guard(std::function<std::string()> guard);

/// Reboots unless the guard refuses; says which on stdout. False if refused.
bool console_reboot();
