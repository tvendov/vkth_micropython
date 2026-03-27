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
} ra_dac_stream_status_t;

typedef enum {
    RA_DAC_HW_STAGE_NONE = 0,
    RA_DAC_HW_STAGE_EVENT_MAP = 1,
    RA_DAC_HW_STAGE_DMAC_OPEN = 2,
    RA_DAC_HW_STAGE_DMAC_ENABLE = 3,
    RA_DAC_HW_STAGE_DTC_OPEN = 4,
    RA_DAC_HW_STAGE_DTC_ENABLE = 5,
    RA_DAC_HW_STAGE_DMAC_RUNTIME = 6,
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
void ra_dac_stream_stop(uint8_t ch);
bool ra_dac_stream_is_active(uint8_t ch);
int8_t ra_dac_stream_timer(uint8_t ch);
ra_dac_hw_stage_t ra_dac_stream_last_stage(uint8_t ch);
int32_t ra_dac_stream_last_error(uint8_t ch);

#endif /* RA_RA_DAC_H_ */
