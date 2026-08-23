#ifndef MICROPY_INCLUDED_RENESAS_RA_STORM_ADC_H
#define MICROPY_INCLUDED_RENESAS_RA_STORM_ADC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RA_STORM_ADC_MAX_FRAME_SAMPLES (128)

typedef struct {
    uint8_t initialised;
    uint8_t running;
    uint8_t overflow;
    uint32_t pin;
    uint32_t sample_rate_hz;
    uint16_t frame_samples;
    uint16_t ring_samples;
    uint32_t frame_sequence;
    uint32_t dropped_frames;
    uint16_t last_raw;
    uint16_t write_index;
    uint8_t ready_mask;
    uint8_t active_half;
} ra_storm_adc_status_t;

bool ra_storm_adc_init(uint32_t pin, uint32_t sample_rate_hz, size_t frame_samples);
void ra_storm_adc_deinit(void);
bool ra_storm_adc_deinit_checked(void);
bool ra_storm_adc_start(void);
void ra_storm_adc_stop(void);
bool ra_storm_adc_ready(void);
const int16_t *ra_storm_adc_acquire_ready_buffer(size_t *frame_samples, uint32_t *sequence);
void ra_storm_adc_get_status(ra_storm_adc_status_t *status);

#endif
