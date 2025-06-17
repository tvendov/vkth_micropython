/*  Общи структури / макроси, споделяни между gc.c и gc_ospi.c.
 *  *НЕ* слагайте тук "weak" символи – това са чисти helpers.               */

#include "py/gc.h"
#include "py/mpstate.h"

/* --- статистики --------------------------------------------------------- */
typedef struct {
    size_t alloc_count;
    size_t free_count;
    size_t total_allocated;
    size_t current_allocated;
    size_t max_free_block;
} ospi_gc_stats_t;

typedef struct {
    size_t alloc_fail_fast;
    size_t alloc_fail_full;
} gc_pressure_stats_t;

/*  Константи за спешен режим                                              */
#define OSPI_EMERGENCY_RESERVE   (8 * 1024)

/*  Декларации за helper-функциите (реализациите са в gc_ospi.c)           */
bool   is_ospi_area     (struct _mp_state_mem_area_t *area);
size_t ospi_align_blocks(size_t blocks);
void   ospi_update_stats(size_t bytes, bool is_alloc);
void   ospi_update_free_stats(size_t size);
void   update_pressure_metrics(size_t bytes, bool success);

