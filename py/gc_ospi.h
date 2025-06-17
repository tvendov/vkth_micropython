#pragma once
#include "py/mpconfig.h"

#if defined(MICROPY_PORT_RA6M5_OSPI) && MICROPY_ENABLE_GC

#ifndef MICROPY_GC_WEAK
#define MICROPY_GC_WEAK  __attribute__((weak))
#endif

/*  Public aliases to the strong symbols в gc.c
 *  (използваме ги, за да делегираме към "оригинала", когато е нужно)        */
void *gc_alloc_default  (size_t n_bytes, unsigned int alloc_flags);
void  gc_free_default   (void *ptr);
void *gc_realloc_default(void *ptr, size_t n_bytes, bool allow_move);

/*  Hook, извикан от порта след gc_init(), за да инициализира
 *  вътрешния OSPI-счетоводител.                                            */
void gc_ospi_on_init(void);

#endif  /* MICROPY_PORT_RA6M5_OSPI */
