#ifndef RA_RA_SCI_WS2812_H_
#define RA_RA_SCI_WS2812_H_

#include <stdbool.h>
#include <stdint.h>

bool ra_sci_ws2812_find_ch(uint32_t data_pin, uint8_t *ch);
bool ra_sci_ws2812_init(uint32_t ch, uint32_t data_pin, uint32_t baudrate);
void ra_sci_ws2812_write(uint32_t ch, uint32_t data_pin, const uint8_t *buf, uint32_t len, uint32_t latch_us);
void ra_sci_ws2812_deinit(uint32_t ch, uint32_t data_pin);

#endif /* RA_RA_SCI_WS2812_H_ */
