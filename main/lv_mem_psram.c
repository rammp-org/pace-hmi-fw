/**
 * @file lv_mem_psram.c
 * @brief LVGL's allocator (LV_STDLIB_CUSTOM), taking every allocation from PSRAM.
 *
 * With plain malloc (LV_STDLIB_CLIB) and SPIRAM_MALLOC_ALWAYSINTERNAL=512,
 * nearly every widget and style - each well under 512 B - landed in internal
 * RAM. On the P4 all of internal RAM is DMA-capable, and that is the pool the
 * W5500's SPI driver takes a bounce buffer from on almost every frame it reads,
 * with no NULL check (spi_master.c setup_dma_priv_buffer). Each new screen
 * pushed that pool closer to empty, and it ran dry twice: once with the
 * generic-actions screens resident, once with the Diagnostics screen open while
 * RTPS started its tasks. Either time the board boot-looped.
 *
 * So LVGL, and only LVGL, allocates from PSRAM: the widget tree no longer
 * competes with the network, however many screens exist. Everything else keeps
 * the ALWAYSINTERNAL placement. Selected by LV_USE_STDLIB_MALLOC in the
 * top-level CMakeLists; this mirrors LVGL's clib allocator otherwise.
 */

#include "lvgl.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#include "esp_heap_caps.h"

static const uint32_t kLvglCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

void lv_mem_init(void) {}

void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes) {
  /* Not supported: the heap is the pool. */
  LV_UNUSED(mem);
  LV_UNUSED(bytes);
  return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool) { LV_UNUSED(pool); }

void *lv_malloc_core(size_t size) { return heap_caps_malloc(size, kLvglCaps); }

void *lv_realloc_core(void *p, size_t new_size) {
  return heap_caps_realloc(p, new_size, kLvglCaps);
}

void lv_free_core(void *p) { heap_caps_free(p); }

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p) {
  multi_heap_info_t info;
  heap_caps_get_info(&info, kLvglCaps);
  mon_p->total_size = info.total_free_bytes + info.total_allocated_bytes;
  mon_p->free_size = info.total_free_bytes;
  mon_p->free_biggest_size = info.largest_free_block;
  mon_p->used_cnt = info.allocated_blocks;
  mon_p->free_cnt = info.free_blocks;
  mon_p->used_pct = mon_p->total_size != 0
                        ? (uint8_t)(100 - (100 * info.total_free_bytes) / mon_p->total_size)
                        : 0;
}

lv_result_t lv_mem_test_core(void) {
  return heap_caps_check_integrity(kLvglCaps, false) ? LV_RESULT_OK : LV_RESULT_INVALID;
}

#endif /* LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM */
