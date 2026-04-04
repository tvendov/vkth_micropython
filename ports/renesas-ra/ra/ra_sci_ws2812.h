#ifndef RA_RA_SCI_WS2812_H_
#define RA_RA_SCI_WS2812_H_

#include <stdbool.h>
#include <stdint.h>

bool ra_sci_ws2812_find_ch(uint32_t data_pin, uint8_t *ch);
bool ra_sci_ws2812_init(uint32_t ch, uint32_t data_pin, uint32_t baudrate, uint32_t polarity, uint32_t phase);
bool ra_sci_ws2812_write(uint32_t ch, uint32_t data_pin, const uint8_t *buf, uint32_t len, uint32_t latch_us);
void ra_sci_ws2812_deinit(uint32_t ch, uint32_t data_pin);

// Async API — non-blocking transmit via DTC, Timer(-1) notification pattern.
// Usage: write_async() → Timer(-1).init(ONE_SHOT,2,cb) → [adc/other work] → sync()
bool ra_sci_ws2812_write_async(uint32_t ch, uint32_t data_pin, const uint8_t *buf, uint32_t len);
bool ra_sci_ws2812_busy(uint32_t ch);
void ra_sci_ws2812_sync(uint32_t ch, uint32_t data_pin);

#endif /* RA_RA_SCI_WS2812_H_ */
