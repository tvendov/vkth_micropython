#ifndef MICROPY_INCLUDED_RENESAS_RA_STORM_DETECT_H
#define MICROPY_INCLUDED_RENESAS_RA_STORM_DETECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    RA_STORM_DETECT_STATE_IDLE = 0,
    RA_STORM_DETECT_STATE_TRACKING,
    RA_STORM_DETECT_STATE_COOLDOWN,
} ra_storm_detect_state_t;

typedef struct {
    uint8_t initialised;
    uint8_t possible;
    uint8_t detected;
    uint8_t tracking;
    uint8_t overflow;
    uint32_t sample_rate_hz;
    uint16_t frame_samples;
    uint32_t processed_frames;
    uint32_t event_count;
    uint32_t last_sequence;
    uint32_t dropped_sequences;
    uint8_t state;
    float noise_floor;
    float rms;
    float peak;
    float lf_energy;
    float hf_energy;
    float lf_hf;
    float score;
    float event_duration_s;
    float event_peak_rms;
} ra_storm_detect_status_t;

void ra_storm_detect_reset(void);
void ra_storm_detect_process_frame(const int16_t *samples, size_t frame_samples, uint32_t sample_rate_hz, uint32_t sequence);
size_t ra_storm_detect_process_ready_adc(void);
bool ra_storm_detected(void);
void ra_storm_detect_clear(void);
void ra_storm_detect_get_status(ra_storm_detect_status_t *status);

#endif
