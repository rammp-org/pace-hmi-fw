#include "heap_watch.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>

#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace {

constexpr bool kEnabled = CONFIG_HMI_DEBUG_HEAP_WATCH_AS_INT != 0;
constexpr uint32_t kPeriodMs = CONFIG_HMI_DEBUG_HEAP_WATCH_PERIOD_MS;
constexpr uint32_t kIntegrityEvery = CONFIG_HMI_DEBUG_HEAP_WATCH_INTEGRITY_EVERY;
constexpr uintptr_t kMapLo = CONFIG_HMI_DEBUG_HEAP_WATCH_MAP_LO;
constexpr uintptr_t kMapHi = CONFIG_HMI_DEBUG_HEAP_WATCH_MAP_HI;
constexpr std::array<int64_t, 2> kMapAtUs = {2'500'000, 20'000'000};
constexpr uint32_t kCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
constexpr size_t kWords = 8;   // copied from 8 bytes before the block's data: its header first
constexpr size_t kHistory = 4; // blocks remembered before a bad one
constexpr size_t kMapMax = 64; // blocks a map holds
constexpr uint32_t kStackBytes = 4096;
constexpr UBaseType_t kPriority = 20; // above every app task: it samples while they run

struct Block {
  uintptr_t ptr;
  uint32_t size;
  bool used;
  std::array<uint32_t, kWords> words;
};

struct Walk {
  uintptr_t heap_start;
  uintptr_t heap_end;
  uintptr_t last_end; // end of the last block walked in this heap
  uint32_t index;
  std::array<Block, kHistory> history;
  bool bad;
  const char *bad_reason;
  Block bad_block;
  uint32_t bad_index;
  uintptr_t bad_heap_start;
  uintptr_t bad_heap_end;
  std::array<Block, kHistory> bad_history;
  bool mapping;
  size_t map_count;
  std::array<Block, kMapMax> map;
};

// Only the watch task touches these (the walker runs on it, under each heap's lock).
Walk walk;
StaticTask_t task_tcb;
std::array<StackType_t, kStackBytes / sizeof(StackType_t)> task_stack;

Block snapshot(uintptr_t ptr, size_t size, bool used, uintptr_t heap_end) {
  Block b{.ptr = ptr, .size = static_cast<uint32_t>(size), .used = used, .words = {}};
  const uintptr_t from = ptr - 8; // prev_phys, size, then the data
  for (size_t i = 0; i < kWords; ++i) {
    const uintptr_t at = from + i * 4;
    if (at + 4 <= heap_end) {
      b.words[i] = *reinterpret_cast<const volatile uint32_t *>(at);
    }
  }
  return b;
}

void mark_bad(const char *reason, const Block &b) {
  if (walk.bad) {
    return;
  }
  walk.bad = true;
  walk.bad_reason = reason;
  walk.bad_block = b;
  walk.bad_index = walk.index;
  walk.bad_heap_start = walk.heap_start;
  walk.bad_heap_end = walk.heap_end;
  walk.bad_history = walk.history;
}

// A heap's walk must reach its end: a wrong size can also end it early, quietly
// (a size of 0 reads as the last block).
constexpr uintptr_t kEndSlack = 32;

void check_heap_end() {
  if (walk.heap_start != 0 && walk.last_end + kEndSlack < walk.heap_end) {
    const Block last{.ptr = walk.last_end, .size = 0, .used = false, .words = {}};
    mark_bad("walk ended early (block at ptr is where it stopped)", last);
  }
}

bool on_block(walker_heap_into_t heap, walker_block_info_t block, void *) {
  if (static_cast<uintptr_t>(heap.start) != walk.heap_start) {
    check_heap_end();
    walk.heap_start = static_cast<uintptr_t>(heap.start);
    walk.heap_end = static_cast<uintptr_t>(heap.end);
    walk.last_end = walk.heap_start;
    walk.index = 0;
  }
  const auto ptr = reinterpret_cast<uintptr_t>(block.ptr);
  const Block b = snapshot(ptr, block.size, block.used, walk.heap_end);
  const bool inside = ptr >= walk.heap_start && ptr < walk.heap_end && block.size % 4 == 0 &&
                      block.size <= walk.heap_end - ptr;
  if (!inside) {
    mark_bad("size runs outside the heap", b);
    return false; // the next block would be found through this size: stop this heap here
  }
  // TLSF keeps the next block's prev_free bit (bit 1 of its size word, at
  // ptr + size) equal to this block being free.
  if (ptr + block.size + 4 <= walk.heap_end) {
    const uint32_t next_size = *reinterpret_cast<const volatile uint32_t *>(ptr + block.size);
    if (((next_size & 2U) != 0) != !block.used) {
      mark_bad("next block's prev_free bit disagrees with this block", b);
      return false;
    }
  }
  if (walk.mapping && ptr >= kMapLo && ptr < kMapHi && walk.map_count < kMapMax) {
    walk.map[walk.map_count++] = b;
  }
  walk.history[walk.index % kHistory] = b;
  walk.index++;
  walk.last_end = ptr + block.size;
  return true;
}

void print_block(const char *what, uint32_t index, const Block &b) {
  esp_rom_printf("HEAPWATCH %s #%u ptr=0x%08x size=0x%08x %s words@ptr-8:", what,
                 static_cast<unsigned>(index), static_cast<unsigned>(b.ptr),
                 static_cast<unsigned>(b.size), b.used ? "used" : "free");
  for (uint32_t w : b.words) {
    esp_rom_printf(" %08x", static_cast<unsigned>(w));
  }
  esp_rom_printf("\n");
}

void print_map(int64_t now_us) {
  esp_rom_printf("HEAPWATCH map t=%u ms: blocks in [0x%08x, 0x%08x), %u shown\n",
                 static_cast<unsigned>(now_us / 1000), static_cast<unsigned>(kMapLo),
                 static_cast<unsigned>(kMapHi), static_cast<unsigned>(walk.map_count));
  for (size_t i = 0; i < walk.map_count; ++i) {
    print_block("map", static_cast<uint32_t>(i), walk.map[i]);
  }
}

void report(int64_t now_us, int64_t last_ok_us) {
  esp_rom_printf("HEAPWATCH CORRUPT t=%u ms, last clean walk t=%u ms (window %u us)\n",
                 static_cast<unsigned>(now_us / 1000), static_cast<unsigned>(last_ok_us / 1000),
                 static_cast<unsigned>(now_us - last_ok_us));
  if (!walk.bad) {
    esp_rom_printf("HEAPWATCH walk clean; heap_caps_check_integrity failed (its lines above)\n");
    return;
  }
  esp_rom_printf("HEAPWATCH heap [0x%08x, 0x%08x)\n", static_cast<unsigned>(walk.bad_heap_start),
                 static_cast<unsigned>(walk.bad_heap_end));
  const uint32_t first = walk.bad_index > kHistory ? walk.bad_index - kHistory : 0;
  for (uint32_t i = first; i < walk.bad_index; ++i) {
    print_block("before", i, walk.bad_history[i % kHistory]);
  }
  print_block("BAD", walk.bad_index, walk.bad_block);
}

void watch_task(void *) {
  esp_rom_printf("HEAPWATCH started: every %u ms, integrity every %u walks\n",
                 static_cast<unsigned>(kPeriodMs), static_cast<unsigned>(kIntegrityEvery));
  int64_t last_ok_us = esp_timer_get_time();
  size_t next_map = 0;
  uint32_t walks = 0;
  while (true) {
    const int64_t now_us = esp_timer_get_time();
    walk.mapping = next_map < kMapAtUs.size() && now_us >= kMapAtUs[next_map];
    walk.map_count = 0;
    walk.heap_start = 0;
    walk.index = 0;
    heap_caps_walk(kCaps, on_block, nullptr);
    check_heap_end(); // the last heap walked
    bool ok = !walk.bad;
    ++walks;
    if (ok && kIntegrityEvery != 0 && walks % kIntegrityEvery == 0) {
      ok = heap_caps_check_integrity(kCaps, true);
    }
    if (walk.mapping) {
      print_map(now_us);
      ++next_map;
    }
    if (!ok) {
      report(now_us, last_ok_us);
      abort(); // debug image: stop here, so the culprit has not moved on
    }
    last_ok_us = now_us;
    vTaskDelay(pdMS_TO_TICKS(kPeriodMs));
  }
}

} // namespace

void heap_watch_start() {
  if constexpr (kEnabled) {
    const TaskHandle_t task =
        xTaskCreateStaticPinnedToCore(watch_task, "heapwatch", kStackBytes, nullptr, kPriority,
                                      task_stack.data(), &task_tcb, tskNO_AFFINITY);
    if (task == nullptr) {
      esp_rom_printf("HEAPWATCH could not start its task\n");
    }
  }
}
