#include "py/obj.h"
#include "py/mphal.h"
#include "ospi_heap.h"
#include <string.h>

static uint8_t *g_base = (uint8_t *)OSPI_START_ADDR;
static uint8_t *bitmap;
static size_t   total_blocks;

void ospi_heap_init(void) {
    total_blocks = OSPI_SIZE_BYTES / OSPI_BLOCK_SIZE;
    bitmap = m_new(uint8_t, (total_blocks + 7) / 8);
    memset(bitmap, 0, (total_blocks + 7) / 8);
}

static inline bool is_free(size_t blk) {
    return !(bitmap[blk >> 3] & (1 << (blk & 7)));
}

static inline void mark_used(size_t blk, size_t cnt, int used) {
    while (cnt--) {
        if (used)
            bitmap[blk >> 3] |=  1 << (blk & 7);
        else
            bitmap[blk >> 3] &= ~(1 << (blk & 7));
        blk++;
    }
}

void *ospi_malloc(size_t n_bytes) {
    if (n_bytes == 0 || n_bytes > OSPI_SIZE_BYTES) {
        return NULL;
    }
    size_t req_blocks = (n_bytes + OSPI_BLOCK_SIZE - 1) / OSPI_BLOCK_SIZE;
    size_t run = 0, start = 0;
    for (size_t i = 0; i < total_blocks; ++i) {
        if (is_free(i)) {
            if (run == 0) start = i;
            if (++run == req_blocks) {
                mark_used(start, req_blocks, 1);
                return (void *)(g_base + start * OSPI_BLOCK_SIZE);
            }
        } else {
            run = 0;
        }
    }
    return NULL; // no room
}

void ospi_free(void *ptr) {
    if (!ptr) return;
    uintptr_t off = (uint8_t *)ptr - g_base;
    if (off >= OSPI_SIZE_BYTES) return; // not ours
    size_t blk = off / OSPI_BLOCK_SIZE;
    // naive: free until next used block
    while (blk < total_blocks && !is_free(blk)) {
        mark_used(blk, 1, 0);
        blk++;
    }
}

size_t ospi_available(void) {
    size_t free_cnt = 0;
    for (size_t i = 0; i < total_blocks; ++i) {
        if (is_free(i)) free_cnt++;
    }
    return free_cnt * OSPI_BLOCK_SIZE;
}
