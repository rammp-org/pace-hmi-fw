#pragma once

/**
 * @file heap_watch.hpp
 * @brief Debug only (CONFIG_HMI_DEBUG_HEAP_WATCH): watches the internal heaps
 *        for a corrupt block, to find what corrupted one at boot.
 *
 * Why it exists: release final-a9a040f panicked once in 51 boots with a load
 * access fault in tlsf_walk_pool. fw_info's first esp_partition_read walked the
 * internal heaps (spi_flash's bounce buffer asks for the largest free block) and
 * met a used block whose size word read 0xef041100. A walk only finds such a
 * block; this watch finds it sooner, and says when and where.
 *
 * A static task (no heap: the watch does not move what it watches) walks every
 * internal heap each CONFIG_HMI_DEBUG_HEAP_WATCH_PERIOD_MS and checks that each
 * block lies inside its heap. Optionally, every Nth walk, it also runs
 * heap_caps_check_integrity (which, with CONFIG_HEAP_POISONING_COMPREHENSIVE,
 * names the overrun block's address). On the first bad block it prints, through
 * the ROM console and with "HEAPWATCH" on every line: the time, the last clean
 * walk, the heap, the bad block and the blocks before it (eight words from each
 * block's header: code and data pointers in them name the object), then aborts.
 * At 2.5 s and 20 s it prints the blocks between CONFIG_HMI_DEBUG_HEAP_WATCH_MAP_LO
 * and _MAP_HI, a map of who lives there.
 *
 * Never in sdkconfig.defaults, which ships (CS-LAY-09).
 */

/// Starts the watch task, first thing in app_main. Does nothing unless
/// CONFIG_HMI_DEBUG_HEAP_WATCH.
void heap_watch_start();
