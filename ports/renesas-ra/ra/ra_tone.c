#include "ra_tone.h"
#include <stddef.h>
#include <string.h>

/* Immutable quarter wave: 130 bytes of flash, shared by all contexts. */
static const int16_t sine_quarter[65] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602,
    6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793,
    12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
    18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
    27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971,
    32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
    32767,
};
static int32_t sine_index(uint32_t i) {
    i &= 255U;
    uint32_t q = i >> 6, k = i & 63U;
    int32_t v = sine_quarter[(q & 1U) ? 64U - k : k];
    return q >= 2U ? -v : v;
}
static int32_t sine_phase(uint32_t phase) {
    uint32_t i = phase >> 24;
    int32_t a = sine_index(i), b = sine_index(i + 1U);
    return a + ((b - a) * (int32_t)((phase >> 16) & 255U)) / 256;
}
bool ra_tone_configure(ra_tone_gen_t *g, uint32_t clock_hz, uint32_t period,
    uint32_t frequency_dhz, uint16_t peak, unsigned wave) {
    /* Bounding clock and period keeps the 64-bit numerator below 2^64. */
    if (!g || !period || period > 65536U || clock_hz < 8000U || clock_hz > 240000000U ||
        (uint64_t)clock_hz < 8000ULL * period || (uint64_t)clock_hz > 48000ULL * period ||
        !frequency_dhz || frequency_dhz > 30000U || peak > 2047U || wave > RA_TONE_TRIANGLE) {
        return false;
    }
    uint64_t denominator = (uint64_t)clock_hz * 10U;
    uint32_t step = (((uint64_t)frequency_dhz * period << 32) + denominator / 2U) / denominator;
    g->step = step;
    g->peak = peak;
    g->wave = wave;
    return true;
}
void ra_tone_reset(ra_tone_gen_t *g) {
    if (g) { g->phase = 0; }
}
int16_t ra_tone_next(ra_tone_gen_t *g) {
    int32_t unit;
    if (g->wave == RA_TONE_SQUARE) {
        unit = (g->phase & 0x80000000U) ? -32767 : 32767;
    } else if (g->wave == RA_TONE_TRIANGLE) {
        uint32_t ramp = g->phase >> 16;
        unit = ramp < 32768U ? (int32_t)ramp * 2 - 32767 : 98303 - (int32_t)ramp * 2;
    } else {
        unit = sine_phase(g->phase);
    }
    g->phase += g->step; /* modulo 2^32, including muted samples */
    return (int16_t)(unit * g->peak / 32767);
}
bool ra_tone_detector_init(ra_tone_detector_t *d, uint32_t fs,
    uint32_t frequency_dhz, uint16_t window_ms, uint16_t min_rms) {
    ra_tone_gen_t reference = {0};
    if (!d || window_ms < 50U || window_ms > 1000U || !min_rms || min_rms > 2047U ||
        (uint64_t)frequency_dhz * window_ms < 30000U || /* at least three cycles */
        !ra_tone_configure(&reference, fs, 1, frequency_dhz, 2047, RA_TONE_SINE)) {
        return false;
    }
    memset(d, 0, sizeof(*d));
    d->reference = reference;
    d->window = (fs * window_ms) / 1000U;
    d->min_rms = min_rms;
    return true;
}
void ra_tone_detector_reset(ra_tone_detector_t *d) {
    if (!d) { return; }
    ra_tone_gen_t reference = d->reference;
    uint32_t window = d->window;
    uint16_t min_rms = d->min_rms;
    memset(d, 0, sizeof(*d));
    reference.phase = 0;
    d->reference = reference;
    d->window = window;
    d->min_rms = min_rms;
}
bool ra_tone_detect(ra_tone_detector_t *d, int16_t sample) {
    if (!d || !d->window) { return false; }
    if (sample < -2048 || sample > 2047) {
        d->present = false;
        d->hits = d->misses = 0;
        d->count = 0;
        d->re = d->im = d->sum = d->energy = d->sum_re = d->sum_im = 0;
        d->purity_permille = 0;
        return false;
    }
    int32_t re = sine_phase(d->reference.phase + 0x40000000U) / 256;
    int32_t im = sine_phase(d->reference.phase) / 256;
    d->reference.phase += d->reference.step;
    d->re += (int32_t)sample * re;
    d->im += (int32_t)sample * im;
    d->sum += sample;
    d->sum_re += re;
    d->sum_im += im;
    d->energy += (int32_t)sample * sample;
    if (++d->count < d->window) { return false; }
    int64_t n = d->window;
    int64_t energy = (d->energy - d->sum * d->sum / n) / n;
    int64_t r = (d->re - d->sum * d->sum_re / n) / n;
    int64_t q = (d->im - d->sum * d->sum_im / n) / n;
    uint64_t power = 2U * (uint64_t)(r * r + q * q);
    uint64_t denominator = 127U * 127U * (uint64_t)(energy > 0 ? energy : 1);
    uint64_t purity = power * 1000U / denominator;
    d->purity_permille = purity > 1000U ? 1000U : purity;
    bool good = energy >= (int32_t)d->min_rms * d->min_rms &&
        d->purity_permille >= (d->present ? 500U : 700U);
    if (good) {
        d->misses = 0;
        if (d->hits < 2U) { ++d->hits; }
        if (d->hits == 2U) { d->present = true; }
    } else {
        d->hits = 0;
        if (d->misses < 2U) { ++d->misses; }
        if (d->misses == 2U) { d->present = false; }
    }
    ++d->windows;
    d->count = 0;
    d->re = d->im = d->sum = d->energy = d->sum_re = d->sum_im = 0;
    return true;
}
