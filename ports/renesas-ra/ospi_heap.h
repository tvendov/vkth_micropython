#pragma once
#include <stdint.h>

// 32‑byte blocks → 8 MiB / 32 = 262 144 bits → 32 768 bytes bitmap
#define OSPI_START_ADDR   (0x68000000u)
#define OSPI_SIZE_BYTES   (8 * 1024 * 1024u)
#define OSPI_BLOCK_SIZE   (32u)

void  ospi_heap_init(void);
void *ospi_malloc(size_t n_bytes);
void  ospi_free(void *ptr);
size_t ospi_available(void);
