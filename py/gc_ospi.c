// gc_ospi.c – OSPI‑optimized overrides for MicroPython GC
// Split out of gc.c to avoid duplicate definitions and ease maintenance.
//
// Build system: add this file to SRC_C/CMake list when MICROPY_PORT_RA6M5_OSPI is enabled.
// The original py/gc.c must remain in the build; the weak symbols below will replace
// their default (non‑OSPI) counterparts automatically.

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "py/gc.h"               // core GC types/macros
#include "py/gc_ospi_internal.h" // shared helpers/typedefs moved from patches
#include "py/runtime.h"
#include "py/mpstate.h"
#include "py/mphal.h"

// -----------------------------------------------------------------------------
// Build‑time switch – compile this file *only* for the RA6M5 + OSPI RAM target
// -----------------------------------------------------------------------------
#ifndef MICROPY_PORT_RA6M5_OSPI
#error "gc_ospi.c should only be compiled when MICROPY_PORT_RA6M5_OSPI is defined"
#endif

// -----------------------------------------------------------------------------
// Weak‑override attribute – makes linker pick these implementations over gc.c
// -----------------------------------------------------------------------------
// Note: MICROPY_GC_WEAK is now defined in gc_ospi_internal.h

// -----------------------------------------------------------------------------
// 1. **Global state & helpers**  (moved verbatim from the patch)
// -----------------------------------------------------------------------------

#ifndef gc_free_bytes
STATIC size_t gc_free_bytes = 0;          // live free‑byte counter
#endif

#ifndef last_validate_ms
STATIC mp_uint_t last_validate_ms = 0;   // debounced validator timestamp
#endif

#ifndef ospi_emergency_mode
STATIC bool ospi_emergency_mode = false; // low‑mem flag
#endif

#ifndef ospi_gc_stats
STATIC ospi_gc_stats_t   ospi_gc_stats   = {0};
#endif

#ifndef gc_pressure_stats
STATIC gc_pressure_stats_t gc_pressure_stats = {0};
#endif

// --- Helper predicates -------------------------------------------------------
bool is_ospi_area(mp_state_mem_area_t *area) {
    return (uintptr_t)area->gc_pool_start >= 0x90000000; // adapt to map
}

size_t ospi_align_blocks(size_t blocks) {
    return (blocks + 7) & ~7; // 128‑byte / 8‑block alignment
}

void ospi_update_stats(size_t bytes, bool is_alloc) {
    if (is_alloc) {
        ospi_gc_stats.alloc_count++;
        ospi_gc_stats.current_allocated += bytes;
        ospi_gc_stats.total_allocated   += bytes;
    } else {
        ospi_gc_stats.free_count++;
        if (bytes > ospi_gc_stats.current_allocated) {
            ospi_gc_stats.current_allocated = 0;
        } else {
            ospi_gc_stats.current_allocated -= bytes;
        }
    }
}

void ospi_update_free_stats(size_t size) {
    if (size > ospi_gc_stats.max_free_block) {
        ospi_gc_stats.max_free_block = size;
    }
}

// --- Free‑byte counter helpers ----------------------------------------------
static inline void gc_update_free_bytes(ssize_t delta) {
    if (delta < 0 && (size_t)(-delta) > gc_free_bytes) {
        gc_free_bytes = 0; // underflow guard
    } else {
        gc_free_bytes += delta;
    }
}

static inline bool ospi_check_critical_memory_fast(void) {
    return gc_free_bytes < OSPI_EMERGENCY_RESERVE && !ospi_emergency_mode;
}

// Forward declarations – see bottom for implementations
static void *ospi_alloc_from_area(mp_state_mem_area_t *, size_t, unsigned);

// -----------------------------------------------------------------------------
// 2. **Weak overrides for public GC API** (gc_alloc / gc_free / gc_realloc)
// -----------------------------------------------------------------------------

// Forward declarations to original functions (renamed to avoid recursion)
extern void gc_init_original(void *start, void *end);
extern void *gc_alloc_original(size_t n_bytes, unsigned int alloc_flags);
extern void gc_free_original(void *ptr);
extern void *gc_realloc_original(void *ptr, size_t n_bytes, bool allow_move);

void MICROPY_GC_WEAK gc_init(void *start, void *end) {
    // call through to original to keep common logic
    gc_init_original(start, end);        // run default init first

    // afterwards initialise OSPI additions
    gc_free_bytes      = (end > start) ? (size_t)((uint8_t*)end - (uint8_t*)start) : 0;
    last_validate_ms   = mp_hal_ticks_ms();
    ospi_emergency_mode = false;
    memset(&ospi_gc_stats, 0, sizeof(ospi_gc_stats));
    memset(&gc_pressure_stats, 0, sizeof(gc_pressure_stats));
}

// ------------------  gc_alloc  -----------------------------------------------
void *MICROPY_GC_WEAK gc_alloc(size_t n_bytes, unsigned int alloc_flags) {
    // 1) large requests → try OSPI first
    const size_t ospi_threshold = 32 * 1024;
    if (n_bytes >= ospi_threshold) {
        for (mp_state_mem_area_t *a = &MP_STATE_MEM(area); a; a = a->next) {
            if (!is_ospi_area(a)) continue;
            void *p = ospi_alloc_from_area(a, n_bytes, alloc_flags);
            if (p) {
                gc_update_free_bytes(-(ssize_t)((n_bytes + BYTES_PER_BLOCK - 1) & ~(BYTES_PER_BLOCK - 1)));
                return p;
            }
        }
    }

    // 2) fallback to default allocator (internal RAM)
    void *ptr = gc_alloc_original(n_bytes, alloc_flags);
    if (ptr) {
        gc_update_free_bytes(-(ssize_t)((n_bytes + BYTES_PER_BLOCK - 1) & ~(BYTES_PER_BLOCK - 1)));
    }
    return ptr;
}

// ------------------  gc_free  ------------------------------------------------
void MICROPY_GC_WEAK gc_free(void *ptr) {
    if (ptr == NULL) return;
    size_t before = gc_nbytes(ptr);
    gc_free_original(ptr);
    gc_update_free_bytes((ssize_t)before);
}

// ------------------  gc_realloc  --------------------------------------------
void *MICROPY_GC_WEAK gc_realloc(void *ptr, size_t n_bytes, bool allow_move) {
    size_t old = ptr ? gc_nbytes(ptr) : 0;
    void *newp = gc_realloc_original(ptr, n_bytes, allow_move);
    if (newp) {
        size_t new_sz = gc_nbytes(newp);
        gc_update_free_bytes((ssize_t)old - (ssize_t)new_sz); // negative if grown
    }
    return newp;
}

// -----------------------------------------------------------------------------
// 3. **OSPI‑specific small helpers**  (private to this translation unit)
// -----------------------------------------------------------------------------
static void *ospi_alloc_from_area(mp_state_mem_area_t *area, size_t n_bytes, unsigned int flags) {
    size_t n_blocks = (n_bytes + BYTES_PER_BLOCK - 1) / BYTES_PER_BLOCK;
    n_blocks = ospi_align_blocks(n_blocks);
    if (n_blocks > MICROPY_GC_MAX_BLOCKS_PER_ALLOC) {
        return NULL;
    }

    size_t consecutive = 0;
    size_t start_block = 0;
    for (size_t block = 0; block < area->gc_alloc_table_byte_len * BLOCKS_PER_ATB; ++block) {
        if (ATB_GET_KIND(area, block) == AT_FREE) {
            if (consecutive++ == 0) start_block = block;
            if (consecutive >= n_blocks) {
                // mark HEAD/TAIL
                ATB_FREE_TO_HEAD(area, start_block);
                for (size_t i = 1; i < n_blocks; ++i) {
                    ATB_FREE_TO_TAIL(area, start_block + i);
                }
                void *ptr = (void*)(area->gc_pool_start + start_block * BYTES_PER_BLOCK);
                ospi_update_stats(n_blocks * BYTES_PER_BLOCK, true);
                return ptr;
            }
        } else {
            consecutive = 0;
        }
    }
    return NULL; // no room
}

#endif // MICROPY_PORT_RA6M5_OSPI
