/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2026
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef RA_RA_SCI_SPI_H_
#define RA_RA_SCI_SPI_H_

#include <stdbool.h>
#include <stdint.h>

bool ra_sci_spi_find_af_ch(uint32_t mosi, uint32_t miso, uint32_t sck, uint8_t *ch);
bool ra_sci_spi_init(uint32_t ch, uint32_t mosi, uint32_t miso, uint32_t sck, uint32_t baud, uint32_t polarity, uint32_t phase, uint32_t firstbit);
void ra_sci_spi_deinit(uint32_t ch);
void ra_sci_spi_transfer(uint32_t ch, const uint8_t *src, uint8_t *dst, uint32_t count);

// Non-blocking submit API (AD5.1 foundation).
//
// `cb` (optional) fires from the SCI RX-end IRQ when the DTC drains the last
// byte. The callback runs in hard-IRQ context — keep the body lean: no
// allocations, no Python objects, no blocking calls. Callers that prefer
// polling can pass NULL and use ra_sci_spi_is_done() instead.
//
// Semantics:
//   - One call = one bulk transfer. src/dst NULL = idle byte / discard
//     (matches ra_sci_spi_transfer).
//   - Buffers must stay valid until is_done() returns true (or the callback
//     fires).
//   - No internal locking: callers must serialise per channel.
//   - Returns false on illegal channel / inactive bus / count==0. On success
//     the DTC pair has been armed and SCI started; control returns immediately.
typedef void (*ra_sci_spi_done_cb_t)(uint32_t ch, void *user);

bool ra_sci_spi_submit(uint32_t ch, const uint8_t *src, uint8_t *dst, uint32_t count,
                       ra_sci_spi_done_cb_t cb, void *user);
bool ra_sci_spi_is_done(uint32_t ch);

#endif /* RA_RA_SCI_SPI_H_ */
