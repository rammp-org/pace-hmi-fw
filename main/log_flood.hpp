#pragma once

/**
 * @file log_flood.hpp
 * @brief Debug only (CONFIG_HMI_DEBUG_LOG_FLOOD): reproduces the boot heap
 *        corruption of final-a9a040f through stdout.
 *
 * The suspect: log_capture's stdout is a picolibc bufio FILE from funopen(),
 * fully buffered in a 128-byte buffer that is the last 128 bytes of its 208-byte
 * heap block. picolibc's fwrite can leave that buffer exactly full (len == size,
 * not flushed), and __bufio_put stores the next character before it checks for
 * a full buffer: one byte lands just past the buffer, in the next heap block's
 * size word. An IDF log line (vprintf, character by character) right after an
 * espp line (fmt, one fwrite) that filled the buffer to the byte writes its 'E'
 * there. The panic boot printed ~112 such espp -> IDF "E (...)" pairs (the failed
 * I2C scan); a normal boot prints about one.
 *
 * Two modes, both from static tasks (no heap, the layout HEAPWATCH maps stays):
 *  - EXACT: at 3 s, under the stdout lock, fflush, one fwrite of exactly 128
 *    bytes, then one printf'd "E (...)" line. Deterministic.
 *  - otherwise: from 3 s, HMI_DEBUG_LOG_FLOOD_PAIRS pairs of an espp error line
 *    of varying length and an IDF-style "E (...)" line, every 50 ms, from
 *    HMI_DEBUG_LOG_FLOOD_TASKS tasks: the failed I2C scan, repeated.
 *
 * Never in sdkconfig.defaults, which ships (CS-LAY-09).
 */

/// Starts the flood task(s), in app_main after the first log line. Does
/// nothing unless CONFIG_HMI_DEBUG_LOG_FLOOD.
void log_flood_start();
