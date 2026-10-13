/// @file alloc_guard.cpp
/// @brief The on-chip allocation guard (alloc_guard.hpp): ESP-IDF's heap hook and the watched
///        tasks' stacks. Built only with CONFIG_HMI_BENCH_ALLOC_GUARD (CMakeLists.txt).

#include "alloc_guard/alloc_guard.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

#include <cstdio>

#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/freertos_debug.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

namespace {

using hmi::alloc_guard::AllocCounter;
using hmi::alloc_guard::MAX_TASKS;

// The guard's counts, written by the heap hook below.
// The one mutable object at namespace scope in this component, allowed as C-ABI hook state
// (owner decision T13; tools/l0/ratchet.py C_HOOK_STATE, CS-CMP-03). The hook ESP-IDF calls
// (esp_heap_trace_alloc_hook) takes no context argument, so what it writes can only be reached
// through a name; TS-DET-08 prescribes that hook on the chip. Constant-
// initialised (no constructor runs: G4) and in internal RAM (DRAM_ATTR), which the hook can
// reach with the flash cache off. Bench builds only (CONFIG_HMI_BENCH_ALLOC_GUARD).
DRAM_ATTR constinit AllocCounter<MAX_TASKS> counter;

// ---- DIAGNOSTIC ONLY (branch dev_ai_diag_heaptrace, finding T12; never merged) -------------
// For slot 0 (Read ADC) only: the call path of every allocation, from the frame-pointer chain
// (CONFIG_ESP_SYSTEM_USE_FRAME_POINTER) and, as a fallback, the code-looking words on the stack
// above the hook (a heuristic scan). Unique frame chains are counted with their bytes; the
// first kRecs allocations are kept whole. Written only by the Read ADC task (the slot's stack
// range picks it), read by the dump task (a torn read is acceptable for a diagnostic).
namespace diag {
constexpr std::size_t kFp = 14;
constexpr std::size_t kScan = 32;
constexpr std::size_t kRecs = 6;
constexpr std::size_t kPaths = 12;
struct Rec {
  std::uint32_t size;
  std::uint32_t cyc;
  std::uint32_t sp;
  std::uint32_t nfp;
  std::uint32_t fp[kFp];
  std::uint32_t nscan;
  std::uint32_t scan[kScan];
};
struct Path {
  std::uint32_t count;
  std::uint32_t bytes;
  std::uint32_t min_size;
  std::uint32_t max_size;
  std::uint32_t nfp;
  std::uint32_t fp[kFp];
};
DRAM_ATTR constinit std::atomic<std::uintptr_t> adc_lo{0};
DRAM_ATTR constinit std::atomic<std::uintptr_t> adc_hi{0};
DRAM_ATTR constinit Rec recs[kRecs]{};
DRAM_ATTR constinit Path paths[kPaths]{};
DRAM_ATTR constinit volatile std::uint32_t n_recs = 0;
DRAM_ATTR constinit volatile std::uint32_t n_paths = 0;
DRAM_ATTR constinit volatile std::uint32_t n_allocs = 0;
DRAM_ATTR constinit volatile std::uint32_t n_bytes = 0;
DRAM_ATTR constinit volatile std::uint32_t n_frees = 0;
DRAM_ATTR constinit volatile std::uint32_t n_unpathed = 0;

[[gnu::always_inline]] inline bool is_code(std::uint32_t w) {
  return (w >= 0x40000000U && w < 0x44000000U) || (w >= 0x4ff00000U && w < 0x4ffc0000U);
}

[[gnu::always_inline]] inline bool armed_and_adc(std::uintptr_t sp) {
  return counter.armed() && sp >= adc_lo.load(std::memory_order_relaxed) &&
         sp < adc_hi.load(std::memory_order_relaxed);
}

[[gnu::always_inline]] inline void note_adc(std::uintptr_t sp, std::size_t size,
                                            std::uintptr_t frame) {
  if (!armed_and_adc(sp)) {
    return;
  }
  const std::uintptr_t lo = adc_lo.load(std::memory_order_relaxed);
  const std::uintptr_t hi = adc_hi.load(std::memory_order_relaxed);
  const auto sz = static_cast<std::uint32_t>(size);
  n_allocs = n_allocs + 1;
  n_bytes = n_bytes + sz;
  std::uint32_t fp[kFp]{};
  std::uint32_t nfp = 0;
  std::uintptr_t f = frame;
  while (nfp < kFp && f >= lo + 8 && f <= hi && (f & 3U) == 0) {
    const auto *w = reinterpret_cast<const std::uint32_t *>(f);
    const std::uint32_t ra = w[-1];
    const std::uintptr_t next = w[-2];
    if (!is_code(ra)) {
      break;
    }
    fp[nfp++] = ra;
    if (next <= f) {
      break;
    }
    f = next;
  }
  if (n_recs < kRecs) {
    Rec &r = recs[n_recs];
    r.size = sz;
    r.cyc = static_cast<std::uint32_t>(esp_cpu_get_cycle_count());
    r.sp = static_cast<std::uint32_t>(sp);
    r.nfp = nfp;
    for (std::size_t i = 0; i < kFp; ++i) {
      r.fp[i] = fp[i];
    }
    std::uint32_t ns = 0;
    for (std::uintptr_t a = sp & ~std::uintptr_t{3}; a + 4 <= hi && ns < kScan; a += 4) {
      const std::uint32_t w = *reinterpret_cast<const std::uint32_t *>(a);
      if (is_code(w)) {
        r.scan[ns++] = w;
      }
    }
    r.nscan = ns;
    n_recs = n_recs + 1;
  }
  for (std::uint32_t p = 0; p < n_paths; ++p) {
    Path &q = paths[p];
    bool same = q.nfp == nfp;
    for (std::size_t i = 0; same && i < nfp; ++i) {
      same = q.fp[i] == fp[i];
    }
    if (same) {
      q.count = q.count + 1;
      q.bytes = q.bytes + sz;
      q.min_size = std::min(q.min_size, sz);
      q.max_size = std::max(q.max_size, sz);
      return;
    }
  }
  if (n_paths < kPaths) {
    Path &q = paths[n_paths];
    q.count = 1;
    q.bytes = sz;
    q.min_size = sz;
    q.max_size = sz;
    q.nfp = nfp;
    for (std::size_t i = 0; i < kFp; ++i) {
      q.fp[i] = fp[i];
    }
    n_paths = n_paths + 1;
    return;
  }
  n_unpathed = n_unpathed + 1;
}

void print_words(const char *label, const std::uint32_t *w, std::uint32_t n) {
  std::printf("%s", label);
  for (std::uint32_t i = 0; i < n; ++i) {
    std::printf("%s0x%08lx", i != 0 ? "," : "", static_cast<unsigned long>(w[i]));
  }
}

void dump_task(void * /*arg*/) {
  vTaskDelay(pdMS_TO_TICKS(5000));
  for (unsigned round = 0;; ++round) {
    std::printf("AGDIAG t_us=%lld allocs=%lu bytes=%lu frees=%lu paths=%lu unpathed=%lu "
                "guard=%lu recs=%lu\n",
                static_cast<long long>(esp_timer_get_time()), static_cast<unsigned long>(n_allocs),
                static_cast<unsigned long>(n_bytes), static_cast<unsigned long>(n_frees),
                static_cast<unsigned long>(n_paths), static_cast<unsigned long>(n_unpathed),
                static_cast<unsigned long>(counter.count(0).allocations),
                static_cast<unsigned long>(n_recs));
    for (std::uint32_t p = 0; p < n_paths; ++p) {
      const Path &q = paths[p];
      std::printf("AGDIAG path %lu count=%lu bytes=%lu size=%lu..%lu",
                  static_cast<unsigned long>(p), static_cast<unsigned long>(q.count),
                  static_cast<unsigned long>(q.bytes), static_cast<unsigned long>(q.min_size),
                  static_cast<unsigned long>(q.max_size));
      print_words(" fp=", q.fp, q.nfp);
      std::printf("\n");
    }
    if (round < 3 || round % 10 == 0) {
      for (std::uint32_t k = 0; k < n_recs; ++k) {
        const Rec &r = recs[k];
        std::printf("AGDIAG rec %lu size=%lu cyc=%lu sp=0x%08lx", static_cast<unsigned long>(k),
                    static_cast<unsigned long>(r.size), static_cast<unsigned long>(r.cyc),
                    static_cast<unsigned long>(r.sp));
        print_words(" fp=", r.fp, r.nfp);
        print_words(" scan=", r.scan, r.nscan);
        std::printf("\n");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}
} // namespace diag
// ---- end DIAGNOSTIC ONLY ----------------------------------------------------------------------

// Watches task @p name's stack in slot @p slot: from its start to its high end, as FreeRTOS
// recorded them when it made the task. False when no task has that name.
bool watch(std::size_t slot, std::string_view name) {
  std::array<char, configMAX_TASK_NAME_LEN> c_name{};
  if (name.empty() || name.size() >= c_name.size()) {
    return false;
  }
  std::copy(name.begin(), name.end(), c_name.begin());
  TaskHandle_t task = xTaskGetHandle(c_name.data());
  TaskSnapshot_t snapshot{};
  if (task == nullptr || vTaskGetSnapshot(task, &snapshot) != pdTRUE) {
    return false;
  }
  // The stack grows down: pxEndOfStack is its highest word.
  const auto lo = std::bit_cast<std::uintptr_t>(xTaskGetStackStart(task));
  const auto hi = std::bit_cast<std::uintptr_t>(snapshot.pxEndOfStack) + sizeof(StackType_t);
  counter.watch(slot, lo, hi);
  if (slot == 0) { // DIAGNOSTIC: Read ADC's range for diag::note_adc
    diag::adc_hi.store(0, std::memory_order_relaxed);
    diag::adc_lo.store(lo, std::memory_order_relaxed);
    diag::adc_hi.store(hi, std::memory_order_relaxed);
  }
  return true;
}

} // namespace

void hmi::alloc_guard::start(std::span<const std::string_view> tasks) {
  for (std::size_t slot = 0; slot < tasks.size() && slot < MAX_TASKS; ++slot) {
    static_cast<void>(watch(slot, tasks[slot])); // not there yet: count() looks again
  }
  counter.arm();
  // DIAGNOSTIC: the dump task (prints AGDIAG lines on the console every 10 s).
  static_cast<void>(
      xTaskCreatePinnedToCore(diag::dump_task, "agdiag", 4096, nullptr, 1, nullptr, 0));
}

std::optional<hmi::alloc_guard::Count> hmi::alloc_guard::count(std::size_t slot,
                                                               std::string_view task) {
  if (!counter.armed()) {
    return std::nullopt;
  }
  static_cast<void>(watch(slot, task)); // a task made after start(), or made again
  if (!counter.watched(slot)) {
    return std::nullopt;
  }
  return counter.count(slot);
}

// ESP-IDF's allocation hook (esp_heap_caps.h, CONFIG_HEAP_USE_HOOKS): called after every
// successful allocation, on the allocating task's stack (or an interrupt's), possibly with the
// flash cache disabled. In IRAM: the declaration in esp_heap_caps.h carries HEAP_IRAM_ATTR, the
// heap's own placement, which this definition takes (a second section attribute here would
// conflict with it). It reads the stack pointer and calls nothing else (AllocCounter::note is
// always inlined).
extern "C" void esp_heap_trace_alloc_hook(void * /*ptr*/, size_t size, uint32_t /*caps*/) {
  const auto sp = std::bit_cast<std::uintptr_t>(esp_cpu_get_sp());
  counter.note(sp, size);
  diag::note_adc(sp, size, std::bit_cast<std::uintptr_t>(__builtin_frame_address(0)));
}

// DIAGNOSTIC: frees made on Read ADC's stack.
extern "C" void esp_heap_trace_free_hook(void * /*ptr*/) {
  if (diag::armed_and_adc(std::bit_cast<std::uintptr_t>(esp_cpu_get_sp()))) {
    diag::n_frees = diag::n_frees + 1;
  }
}
