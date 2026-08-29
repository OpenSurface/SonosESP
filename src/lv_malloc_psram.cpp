/**
 * LVGL PSRAM allocator — LV_STDLIB_CUSTOM hooks
 *
 * LVGL v9 requires these symbols when LV_USE_STDLIB_MALLOC = LV_STDLIB_CUSTOM:
 *   lv_mem_init / lv_mem_deinit   — lifecycle (no-ops for heap-based allocator)
 *   lv_malloc_core                — allocate
 *   lv_realloc_core               — reallocate
 *   lv_free_core                  — free
 *   lv_mem_monitor_core           — optional stats (stub)
 *
 * Routes all LVGL object allocations to PSRAM (MALLOC_CAP_SPIRAM) instead of
 * internal SRAM, freeing ~50-100KB of DMA-capable SRAM for the SDIO RX buffer
 * pool (esp_hosted) and JPEG HW decoder DMA rxlink.
 *
 * Required because IDF v5.5.4 (pioarduino 55.03.38) increased the esp_hosted
 * internal buffer pool size, exhausting DMA when LVGL used internal SRAM.
 *
 * Fallback to malloc() if PSRAM allocation fails (PSRAM full edge case).
 * heap_caps_free() handles pointers from any heap region.
 */

#include "esp_heap_caps.h"
#include "lvgl.h"
#include <stdlib.h>
#include <string.h>

extern "C" {

void lv_mem_init(void) {
    // No-op: PSRAM heap is managed by ESP-IDF, no init needed
}

void lv_mem_deinit(void) {
    // No-op
}

void lv_mem_monitor_core(lv_mem_monitor_t* mon_p) {
    if (!mon_p) return;
    memset(mon_p, 0, sizeof(*mon_p));
    mon_p->free_size     = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    mon_p->total_size    = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    // Guard against divide-by-zero if PSRAM is somehow unregistered / not yet init.
    mon_p->used_pct      = (mon_p->total_size == 0) ? 0
                           : (uint8_t)(100 - (mon_p->free_size * 100 / mon_p->total_size));
    mon_p->frag_pct      = 0;
}

void* lv_malloc_core(size_t size) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(size);  // fallback to internal SRAM
    return p;
}

void* lv_realloc_core(void* p, size_t new_size) {
    // heap_caps_realloc handles cross-region moves (internal→PSRAM) automatically
    void* np = heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    // The "&& p" meant a realloc-as-malloc (p == NULL) skipped the fallback that
    // lv_malloc_core has, returning NULL while internal SRAM was still available.
    if (!np) np = realloc(p, new_size);  // fallback to internal SRAM
    return np;
}

void lv_free_core(void* p) {
    heap_caps_free(p);  // works for both PSRAM and internal SRAM pointers
}

} // extern "C"
