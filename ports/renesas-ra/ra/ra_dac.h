/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Vekatech Ltd.
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



#ifndef RA_RA_DAC_H_
#define RA_RA_DAC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    RA_DAC_TRANSFER_AUTO = 0,
    RA_DAC_TRANSFER_DMAC = 1,
    RA_DAC_TRANSFER_DTC = 2,
} ra_dac_transfer_t;

typedef enum {
    RA_DAC_STREAM_STATUS_OK = 0,
    RA_DAC_STREAM_STATUS_INVALID_CHANNEL,
    RA_DAC_STREAM_STATUS_INVALID_FREQ,
    RA_DAC_STREAM_STATUS_INVALID_LENGTH,
    RA_DAC_STREAM_STATUS_INVALID_TIMER,
    RA_DAC_STREAM_STATUS_TIMER_BUSY,
    RA_DAC_STREAM_STATUS_TRANSFER_BUSY,
    RA_DAC_STREAM_STATUS_LOOP_UNSUPPORTED,
    RA_DAC_STREAM_STATUS_HW_ERROR,
    RA_DAC_STREAM_STATUS_BUSY,
} ra_dac_stream_status_t;

typedef enum {
    RA_DAC_HW_STAGE_NONE = 0,
    RA_DAC_HW_STAGE_EVENT_MAP = 1,
    RA_DAC_HW_STAGE_DMAC_OPEN = 2,
    RA_DAC_HW_STAGE_DMAC_ENABLE = 3,
    RA_DAC_HW_STAGE_DTC_OPEN = 4,
    RA_DAC_HW_STAGE_DTC_ENABLE = 5,
    RA_DAC_HW_STAGE_DMAC_RUNTIME = 6,
    RA_DAC_HW_STAGE_AGT_RUNTIME = 7,
} ra_dac_hw_stage_t;

typedef bool (*ra_dac_stream_double_buffer_fill_t)(void *context, uint16_t *buf, size_t sample_count);
typedef void (*ra_dac_stream_double_buffer_stop_t)(void *context);

void ra_dac_start(uint8_t ch);
void ra_dac_stop(uint8_t ch);
uint8_t ra_dac_is_running(uint8_t ch);
uint16_t ra_dac_read(uint8_t ch);
void ra_dac_write(uint8_t ch, uint16_t val);
void ra_dac_init(uint32_t dac_pin, uint8_t ch);
void ra_dac_deinit(uint32_t dac_pin, uint8_t ch);
bool ra_dac_is_dac_pin(uint32_t pin);
ra_dac_stream_status_t ra_dac_write_timed(uint8_t ch, const uint16_t *buf, size_t sample_count, uint32_t freq,
    bool loop, ra_dac_transfer_t transfer, int8_t timer_ch);
ra_dac_stream_status_t ra_dac_write_timed_double_buffered(uint8_t ch, uint16_t *buf_a, uint16_t *buf_b,
    bool buf_b_ready, size_t sample_count, uint32_t freq, ra_dac_stream_double_buffer_fill_t fill_cb,
    ra_dac_stream_double_buffer_stop_t stop_cb, void *context, int8_t timer_ch);
/* Re-arm two already-open double-buffered DMAC streams at buffer index 0 while
 * their common AGT is stopped, then start that AGT once.  This is the physical
 * sample-index barrier required for coherent DAC0=I / DAC1=Q output. */
bool ra_dac_stream_sync_pair(uint8_t ch_a, uint8_t ch_b);
/* Stop and release a stream.  False means hardware did not acknowledge the
 * bounded quiesce; ownership is retained so a later stop can retry safely. */
bool ra_dac_stream_stop(uint8_t ch);
bool ra_dac_stream_is_active(uint8_t ch);
int8_t ra_dac_stream_timer(uint8_t ch);
ra_dac_hw_stage_t ra_dac_stream_last_stage(uint8_t ch);
int32_t ra_dac_stream_last_error(uint8_t ch);

/* Exclusive, CPU-fed DAC0/DAC1 pair. Foreground lifecycle calls are serialized.
 * Open reserves the pair before touching hardware and leaves AGT stopped.
 * Any partial failure after acquisition retains the owner until close succeeds.
 * Four disjoint 4-byte-aligned buffers remain alive until successful close.
 * Fill is called only on the completed channel's inactive buffer. False faults
 * BOTH channels; fault is bounded/IRQ-safe and must not close/free anything. */
typedef struct {
    uint16_t *buffers[2][2]; /* [channel][ping/pong] */
    size_t count;
    uint32_t rate;
    uint16_t i_zero, q_zero;
    bool (*fill)(void *context, uint8_t channel, uint16_t *buffer, size_t count);
    void (*fault)(void *context);
    void *context;
    /* Alternative to fill: both completed planes are writable in one call.
     * Exactly one of fill/fill_pair must be supplied. */
    bool (*fill_pair)(void *context, uint16_t *i, uint16_t *q, size_t count);
} ra_dac_pair_config_t;
/* Logical reservation survives close; release only after all coupled hardware
 * has closed. A failed reservation has no peripheral side effects. */
bool ra_dac_pair_reserve(const void *owner);
bool ra_dac_pair_release(const void *owner);
bool ra_dac_pair_reserved_by(const void *owner);
bool ra_dac_pair_is_owned(void);
bool ra_dac_pair_owned_by(const void *owner);
ra_dac_stream_status_t ra_dac_pair_open(void *owner, const ra_dac_pair_config_t *config);
bool ra_dac_pair_timing(const void *owner, uint32_t *clock_hz, uint32_t *period);
bool ra_dac_pair_start(const void *owner);
bool ra_dac_pair_running(const void *owner);
/* Bounded fault stop, retains ownership and buffers; safe from a capture ISR. */
void ra_dac_pair_abort(const void *owner);
bool ra_dac_pair_close(const void *owner);

#endif /* RA_RA_DAC_H_ */
