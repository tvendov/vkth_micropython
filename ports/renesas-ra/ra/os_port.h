// Минимален os_port.h shim за axTLS AES на bare-metal Renesas RA.
//
// Стандартният extmod/axtls-include/axtls_os_port.h е проектиран за
// SSL stack на платформи с lwIP TCP/IP (включва <arpa/inet.h>, sys/time,
// py/stream — не съществуват на bare-metal Cortex-M без networking).
//
// За LoRaWAN ни трябва само AES-128 ECB/CBC/CTR — никакъв SSL.
// lib/axtls/crypto/aes.c използва от os_port.h:
//   - <string.h>     (memcpy, memset)
//   - стандартни C типове (uint8_t, uint32_t от <stdint.h>)
//
// Активира се чрез:
//   -DCONFIG_AXTLS_OS_PORT_H='"ra_axtls_os_port.h"'
// което bypass-ва axtls_os_port.h.

#ifndef RA_AXTLS_OS_PORT_H
#define RA_AXTLS_OS_PORT_H

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// axTLS uses these byte-swap functions; provide ARM-friendly versions.
// Rationale: RA is little-endian; htonl/htons need to swap to network byte order.
#define htonl(x) __builtin_bswap32(x)
#define htons(x) __builtin_bswap16(x)
#define ntohl(x) __builtin_bswap32(x)
#define ntohs(x) __builtin_bswap16(x)

#endif // RA_AXTLS_OS_PORT_H
