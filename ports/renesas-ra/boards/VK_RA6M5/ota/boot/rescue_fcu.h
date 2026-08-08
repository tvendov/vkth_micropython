/* Rescue stub FCU code-flash API.  All functions execute from RAM
 * (.ramcode section) so the rescue stub can program code flash while
 * itself living in code flash. */
#ifndef VK_RA6M5_OTA_RESCUE_FCU_H
#define VK_RA6M5_OTA_RESCUE_FCU_H

#include <stdint.h>

/* Erase all blocks covering [start, start+length).  Refuses any range
 * that touches the first 32 KB (where the rescue stub itself lives). */
int rescue_cf_erase_range(uint32_t start, uint32_t length);

/* Program code flash.  `addr` and `len` must be 128-byte aligned;
 * destination cells must already be erased (0xFF). */
int rescue_cf_write(uint32_t addr, const void *src, uint32_t len);

#endif
