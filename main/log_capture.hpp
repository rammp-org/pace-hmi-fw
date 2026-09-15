#pragma once

/**
 * @file log_capture.hpp
 * @brief Keeps what the firmware prints, for the LogScreen.
 *
 * log_capture_start() points stdout and stderr at a stream that passes every
 * byte on to the serial console unchanged and also keeps the last
 * kLogCaptureLines lines, in PSRAM. printf, fmt::print (every espp Logger) and
 * ESP_LOGx all write to stdout, so all of them are caught. Not caught: the
 * bootloader and anything printed before app_main, and panic dumps, which go
 * straight to the UART through esp_rom_printf.
 *
 * Nothing may print from inside the capture path: it holds the ring's lock.
 */

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

inline constexpr size_t kLogCaptureLines = 500;
/// Longest line kept; a longer one is cut short and ends in "...".
inline constexpr size_t kLogCaptureLineLen = 159;

/// How a line is shown: from the colour the espp loggers print it in, or from
/// the level letter IDF's own log lines start with.
enum class LogLevel : uint8_t { Plain, Warning, Error };

/// First thing in app_main. If there is no PSRAM to spare it leaves stdout
/// alone: the console always keeps working.
void log_capture_start();

/// Whether stdout is currently going through the capture.
bool log_capture_active();

/// Lines captured since boot; keeps counting after the ring starts dropping
/// its oldest, so a change in it means new lines.
uint32_t log_capture_count();

/// Visit the lines kept, oldest first. Holds the ring's lock throughout, so
/// `visit` must be quick and must not print. Returns log_capture_count() as of
/// the lines visited.
uint32_t log_capture_visit(const std::function<void(LogLevel, std::string_view)> &visit);
