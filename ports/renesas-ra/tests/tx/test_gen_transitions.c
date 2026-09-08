/* Real portable C generator/modulators. No target registers or RF proof. */
#include "ra_tone.h"
#include "ra_tx_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GEN_SMOOTH
#define GEN_SMOOTH 1
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)

#if GEN_SMOOTH
typedef ra_tone_smooth_t source_t;
#else
typedef ra_tone_gen_t source_t;
#endif

static void source_init(source_t *g, unsigned fs) {
    memset(g, 0, sizeof(*g));
    #if GEN_SMOOTH
    CHECK(ra_tone_smooth_init(g, fs, 1, 10000, 2047, RA_TONE_SINE));
    #else
    CHECK(ra_tone_configure(g, fs, 1, 10000, 2047, RA_TONE_SINE));
    #endif
}
static void request(source_t *g, unsigned fs, unsigned peak, unsigned wave, unsigned fd) {
    ra_tone_gen_t p = {0};
    CHECK(ra_tone_configure(&p, fs, 1, fd, peak, wave));
    #if GEN_SMOOTH
    ra_tone_smooth_request(g, &p);
    #else
    g->step = p.step; g->peak = p.peak; g->wave = p.wave;
    #endif
}
static int16_t source_next(source_t *g) {
    #if GEN_SMOOTH
    return ra_tone_smooth_next(g);
    #else
    return ra_tone_next(g);
    #endif
}

#if GEN_SMOOTH
static void envelope_vectors(void) {
    const unsigned clocks[] = {8000, 12000, 24000, 48000, 30000000};
    const unsigned periods[] = {1, 1, 1, 1, 682};
    CHECK(sizeof(ra_tone_gen_t) == 12 && sizeof(ra_tone_detector_t) == 80);
    for (unsigned r = 0; r < 5; ++r) {
        unsigned clk = clocks[r], per = periods[r];
        unsigned bound = (unsigned)(((uint64_t)clk * 20 + 1000 * per - 1) / (1000 * per));
        ra_tone_smooth_t g;
        CHECK(ra_tone_smooth_init(&g, clk, per, 10000, 2047, RA_TONE_SINE));
        CHECK(ra_tone_smooth_next(&g) == 0);
        for (unsigned n = 0; n <= bound; ++n) {
            uint32_t phase = g.current.phase, peak = g.peak_q16;
            ra_tone_smooth_next(&g);
            CHECK(g.peak_q16 >= peak && g.peak_q16 <= (2047U << 16));
            CHECK(g.current.phase == phase + g.current.step);
        }
        CHECK(g.peak_q16 == (2047U << 16));
        ra_tone_gen_t raw = g.current;
        for (unsigned n = 0; n < 513; ++n) {
            CHECK(ra_tone_smooth_next(&g) == ra_tone_next(&raw));
        }
        ra_tone_smooth_t saved = g;
        CHECK(!ra_tone_smooth_init(&g, clk, per, 30001, 2047, RA_TONE_SINE));
        CHECK(memcmp(&g, &saved, sizeof(g)) == 0);
        for (unsigned wave = 0; wave < 3; ++wave) {
            ra_tone_gen_t goal = {0};
            CHECK(ra_tone_configure(&goal, clk, per, 17500 + wave * 100, 1023, wave));
            uint32_t phase = g.current.phase, peak = g.peak_q16;
            ra_tone_smooth_request(&g, &goal);
            CHECK(g.current.phase == phase && g.peak_q16 == peak);
            unsigned switched = 0;
            for (unsigned n = 0; n < 2 * bound + 3; ++n) {
                unsigned was_wave = g.current.wave;
                uint32_t was_step = g.current.step;
                phase = g.current.phase;
                int sample = ra_tone_smooth_next(&g);
                CHECK(abs(sample) <= 2047 && g.current.phase == phase + g.current.step);
                if (g.current.wave != was_wave || g.current.step != was_step) {
                    CHECK(sample == 0); ++switched;
                }
                /* Repeating a GUI value must not restart the fade. */
                ra_tone_smooth_request(&g, &goal);
            }
            CHECK(switched == 1 && g.peak_q16 == (1023U << 16));
            CHECK(g.current.step == goal.step && g.current.wave == goal.wave);
        }
        for (unsigned n = 0; n < 300; ++n) {
            ra_tone_gen_t goal = {0};
            CHECK(ra_tone_configure(&goal, clk, per, n % 2 ? 10000 : 17500,
                n % 3 ? 2047 : 0, n % 3));
            uint32_t phase = g.current.phase;
            ra_tone_smooth_request(&g, &goal);
            CHECK(g.current.phase == phase);
            CHECK(abs(ra_tone_smooth_next(&g)) <= 2047);
        }
        ra_tone_gen_t zero = {0};
        CHECK(ra_tone_configure(&zero, clk, per, 10000, 0, RA_TONE_SINE));
        ra_tone_smooth_request(&g, &zero);
        for (unsigned n = 0; n <= 2 * bound + 3; ++n) { ra_tone_smooth_next(&g); }
        CHECK(g.peak_q16 == 0 && ra_tone_smooth_next(&g) == 0);
        uint32_t phase = g.current.phase;
        CHECK(ra_tone_smooth_next(&g) == 0 && g.current.phase == phase + g.current.step);
        ra_tone_smooth_reset(&g);
        CHECK(g.current.phase == 0 && g.peak_q16 == 0 && g.wanted_peak == 0);
    }
    printf("PASS envelope: rational Fs, 20-ms slew rounded to a sample, muted frequency/wave switch, phase, retarget, exact settled DDS; state=%zu\n", sizeof(ra_tone_smooth_t));
}
#endif

static void transition_vectors(void) {
    const unsigned levels[] = {100, 0, 25, 50, 75, 100, 0, 100};
    unsigned total = 0;
    for (unsigned mode = RA_TX_MODE_AM; mode <= RA_TX_MODE_LSB; ++mode) {
        unsigned fs = mode >= RA_TX_MODE_USB ? 12000 : 24000;
        ra_tx_config_t c = {.mode=mode, .sample_rate_hz=fs,
            .amplitude=818, .adc_mid=2048, .i_zero=2048, .q_zero=2048,
            .ramp_samples=220, .fm_gain=2, .deviation_hz=mode == RA_TX_MODE_FM ? 2500 : 0,
            .mic_gain=100, .audio_gain=100, .am_depth=50,
            .audio_controls=mode != RA_TX_MODE_FM,
            .gen_source=true, .gen_frequency_dhz=10000, .gen_level=100};
        CHECK(ra_tx_core_validate(&c));
        uint8_t lut[RA_TX_FM_LUT_BYTES];
        if (mode == RA_TX_MODE_FM) { CHECK(ra_tx_core_build_lut(&c, lut, sizeof(lut))); }
        unsigned mode_clips = 0;
        for (unsigned offset = 0; offset < 24; ++offset) {
            source_t gen;
            source_init(&gen, fs);
            ra_tx_ssb_state_t ssb;
            ra_tx_core_ssb_reset(&ssb);
            ra_tx_fm_state_t fm;
            if (mode == RA_TX_MODE_FM) { CHECK(ra_tx_core_fm_reset(&fm, &c, fs, 1)); }
            uint32_t clips = 0;
            for (unsigned stage = 0; stage < 14; ++stage) {
                /* High-level sine edges, then frequency/shape changes at50%. */
                unsigned peak = stage < 8 ? (2047 * levels[stage] + 50) / 100 : 1023;
                unsigned wave = stage < 8 ? RA_TONE_SINE : (stage - 8) % 3;
                unsigned fd = stage < 8 ? 10000 : (stage < 11 ? 17500 : 5000);
                request(&gen, fs, peak, wave, fd);
                for (unsigned n = 0; n < fs / 4 + offset; ++n) {
                    uint16_t raw = 2048 + source_next(&gen), i, q = 2048;
                    if (mode == RA_TX_MODE_AM) { i = ra_tx_core_am_sample(&c, raw, &clips); }
                    else if (mode == RA_TX_MODE_FM) {
                        ra_tx_core_fm_sample(&fm, &c, lut, raw, &i, &q); clips = fm.clips;
                    } else {
                        ra_tx_core_ssb_sample(&ssb, &c, raw, &i, &q); clips = ssb.clips;
                    }
                    CHECK(i <= 4095 && q <= 4095);
                }
            }
            mode_clips += clips;
        }
        printf("GEN transitions mode=%u smooth=%u clips=%u (24 phase offsets, 14 stages)\n",
            mode, GEN_SMOOTH, mode_clips);
        total += mode_clips;
        /* A full-scale square is not a band-limited sine. Its genuine filter
         * overload must remain visible; smoothing must not clear counters. */
        source_t square;
        source_init(&square, fs);
        request(&square, fs, 2047, RA_TONE_SQUARE, 10000);
        ra_tx_ssb_state_t ssb;
        ra_tx_core_ssb_reset(&ssb);
        ra_tx_fm_state_t fm;
        if (mode == RA_TX_MODE_FM) { CHECK(ra_tx_core_fm_reset(&fm, &c, fs, 1)); }
        uint32_t clips = 0;
        for (unsigned n = 0; n < fs / 4; ++n) {
            uint16_t raw = 2048 + source_next(&square), i, q;
            if (mode == RA_TX_MODE_AM) { (void)ra_tx_core_am_sample(&c, raw, &clips); }
            else if (mode == RA_TX_MODE_FM) {
                ra_tx_core_fm_sample(&fm, &c, lut, raw, &i, &q); clips = fm.clips;
            } else {
                ra_tx_core_ssb_sample(&ssb, &c, raw, &i, &q); clips = ssb.clips;
            }
        }
        CHECK(mode == RA_TX_MODE_AM ? clips == 0 : clips > 0);
        printf("GEN full-scale square mode=%u clips=%u (real overload retained)\n", mode, clips);
    }
    CHECK(total == 0);
}

int main(void) {
    #if GEN_SMOOTH
    envelope_vectors();
    #endif
    transition_vectors();
    printf("PASS GEN 1-kHz startup and level transitions; no board/analog/RF tested\n");
    return 0;
}
