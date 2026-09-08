/* Shared, heap-free audio DDS and selected-tone detector; no hardware ownership. */
#ifndef MICROPY_INCLUDED_RA_TONE_H
#define MICROPY_INCLUDED_RA_TONE_H
#include <stdbool.h>
#include <stdint.h>

typedef enum { RA_TONE_SINE, RA_TONE_SQUARE, RA_TONE_TRIANGLE } ra_tone_wave_t;
typedef struct {
    uint32_t phase, step;
    uint16_t peak; /* signed AF peak, 0..2047 */
    uint8_t wave;
} ra_tone_gen_t;

/* frequency in 0.1 Hz; actual Fs=clock_hz/period. No phase reset on configure.
 * Supported audio range 0.1..3000 Hz, Fs 8..48 kHz. reset explicitly at stream start.
 * SQUARE/TRIANGLE are sampled diagnostic waveforms, NOT band-limited sources. */
bool ra_tone_configure(ra_tone_gen_t *g, uint32_t clock_hz, uint32_t period,
    uint32_t frequency_dhz, uint16_t peak, unsigned wave);
void ra_tone_reset(ra_tone_gen_t *g);
int16_t ra_tone_next(ra_tone_gen_t *g);

/* Experimental selected-frequency correlator, not a complete CTCSS/DTMF/DCS decoder.
 * Caller supplies signed 12-bit AF (-2048..2047), at configured Fs, continuously.
 * No decimation, gating or voice filtering may silently change that Fs.
 * Two good windows acquire, two bad windows release. DC is removed per window.
 * For CTCSS+speech a subaudio input filter is REQUIRED before this component.
 * Does not scan unknown tones or operate a squelch. Invalid input clears lock. */
typedef struct {
    ra_tone_gen_t reference;
    uint32_t window, count, windows;
    int64_t re, im, sum, energy, sum_re, sum_im;
    uint16_t purity_permille, min_rms;
    uint8_t hits, misses;
    bool present;
} ra_tone_detector_t;
bool ra_tone_detector_init(ra_tone_detector_t *d, uint32_t fs,
    uint32_t frequency_dhz, uint16_t window_ms, uint16_t min_rms);
bool ra_tone_detect(ra_tone_detector_t *d, int16_t sample);
#endif
