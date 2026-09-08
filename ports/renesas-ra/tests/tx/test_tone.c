/* Host-only independent vectors for the actual shared C tone core. */
#include "ra_tone.h"
#include "ra_tx_core.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PI 3.14159265358979323846
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void generator(void) {
    const unsigned rates[] = {8000, 12000, 24000, 44000, 48000};
    const unsigned frequencies[] = {1, 670, 885, 1000, 10000, 17500, 30000};
    for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r) {
        for (unsigned f = 0; f < sizeof(frequencies) / sizeof(frequencies[0]); ++f) {
            ra_tone_gen_t a = {0};
            unsigned fs = rates[r], fd = frequencies[f];
            CHECK(ra_tone_configure(&a, fs, 1, fd, 2047, RA_TONE_SINE));
            for (unsigned n = 0; n < fs; ++n) {
                double expected = 2047.0 * sin(2.0 * PI * fd * n / (10.0 * fs));
                CHECK(fabs(ra_tone_next(&a) - expected) < 2.0);
            }
            ra_tone_gen_t b = a;
            /* Different producer partition sizes must yield the same samples. */
            for (unsigned block = 0; block < 17; ++block) {
                for (unsigned j = 0; j < 513; ++j) { CHECK(ra_tone_next(&a) == ra_tone_next(&b)); }
            }
            uint32_t phase = a.phase;
            CHECK(ra_tone_configure(&a, fs, 1, 17500, 0, RA_TONE_SINE));
            CHECK(a.phase == phase);
            CHECK(ra_tone_next(&a) == 0 && a.phase == phase + a.step);
            ra_tone_gen_t old = a;
            CHECK(!ra_tone_configure(&a, fs, 1, 30001, 2047, RA_TONE_SINE));
            CHECK(memcmp(&old, &a, sizeof(a)) == 0);
            ra_tone_reset(&a);
            CHECK(a.phase == 0);
        }
    }
    ra_tone_gen_t a = {0};
    CHECK(ra_tone_configure(&a, 30000000, 682, 10000, 2047, RA_TONE_SINE));
    CHECK(fabs((double)a.step * 30000000.0 / (4294967296.0 * 682) - 1000.0) < 0.00001);
    for (unsigned wave = 0; wave < 3; ++wave) {
        CHECK(ra_tone_configure(&a, 24000, 1, 10000, 1000, wave));
        ra_tone_reset(&a);
        int sum = 0, lo = 0, hi = 0;
        for (unsigned i = 0; i < 24000; ++i) {
            int v = ra_tone_next(&a);
            CHECK(abs(v) <= 1000);
            sum += v;
            if (v > hi) { hi = v; }
            if (v < lo) { lo = v; }
        }
        CHECK(abs(sum) < 2500 && hi > 990 && lo < -990);
    }
}

static void detector(void) {
    const unsigned frequencies[] = {670, 885, 1000, 2541, 10000, 17500};
    const unsigned rates[] = {8000, 12000, 24000, 48000};
    for (unsigned r = 0; r < 4; ++r) {
        unsigned fs = rates[r];
        for (unsigned f = 0; f < 6; ++f) {
            unsigned fd = frequencies[f];
            ra_tone_detector_t d;
            CHECK(ra_tone_detector_init(&d, fs, fd, 500, 8));
            /* Independent libm source, arbitrary phase, DC, small deterministic noise. */
            for (unsigned n = 0; n < fs * 2; ++n) {
                int v = (int)(600 * sin(2.0 * PI * fd * n / (10.0 * fs) + 0.7));
                ra_tone_detect(&d, (int16_t)(v + 300 + ((n * 37) % 41) - 20));
            }
            CHECK(d.present && d.windows == 4 && d.purity_permille > 900);
            /* Pure DC cannot hold lock; release requires two completed windows. */
            for (unsigned n = 0; n < fs; ++n) { ra_tone_detect(&d, 500); }
            CHECK(!d.present && d.purity_permille == 0);
            /* A 5-Hz neighbor is not the selected frequency in this 500-ms detector. */
            for (unsigned n = 0; n < fs * 2; ++n) {
                ra_tone_detect(&d, (int16_t)(600 * sin(2.0 * PI * (fd + 50) * n / (10.0 * fs))));
            }
            CHECK(!d.present);
            ra_tone_detect(&d, 3000);
            CHECK(!d.present && d.count == 0);
        }
    }
    ra_tone_detector_t d;
    CHECK(ra_tone_detector_init(&d, 24000, 10000, 100, 8));
    uint32_t noise = 1;
    for (unsigned n = 0; n < 240000; ++n) {
        noise = 1664525U * noise + 1013904223U;
        ra_tone_detect(&d, (int16_t)((noise >> 20) - 2048));
        CHECK(!d.present);
    }
}

static void modulation(void) {
    for (unsigned mode = RA_TX_MODE_AM; mode <= RA_TX_MODE_LSB; ++mode) {
        ra_tx_config_t c = {.mode=mode, .sample_rate_hz=mode >= RA_TX_MODE_USB ? 12000 : 24000,
            .amplitude=800, .adc_mid=2048, .i_zero=2048, .q_zero=2048, .ramp_samples=220,
            .fm_gain=2, .deviation_hz=mode == RA_TX_MODE_FM ? 2500 : 0, .mic_gain=100,
            .audio_gain=100, .am_depth=50, .audio_controls=mode != RA_TX_MODE_FM,
            .gen_source=true, .gen_frequency_dhz=10000, .gen_level=50, .gen_wave=0};
        CHECK(ra_tx_core_validate(&c) && ra_tx_uses_cpu(&c) && ra_tx_software_source(&c));
        CHECK(ra_tx_core_lut_bytes(&c) == (mode == RA_TX_MODE_FM ? 1024U : 0U));
        c.file_source = true;
        CHECK(!ra_tx_core_validate(&c));
        c.file_source = false;
        c.gen_level = 101;
        CHECK(!ra_tx_core_validate(&c));
    }
}
static void fm_silence(void) {
    ra_tx_config_t c = {.mode=RA_TX_MODE_FM, .sample_rate_hz=24000,
        .amplitude=1024, .adc_mid=2048, .i_zero=2048, .q_zero=2048,
        .ramp_samples=220, .fm_gain=2, .deviation_hz=2500, .mic_gain=100,
        .gen_source=true, .gen_frequency_dhz=10000, .gen_level=50};
    uint8_t lut[RA_TX_FM_LUT_BYTES];
    CHECK(ra_tx_core_build_lut(&c, lut, sizeof(lut)));
    double worst = 0;
    for (unsigned stop = 0; stop < 24; ++stop) {
        ra_tx_fm_state_t fm;
        ra_tone_gen_t g = {0};
        uint16_t i, q;
        CHECK(ra_tx_core_fm_reset(&fm, &c, 30000000, 1250));
        CHECK(ra_tone_configure(&g, 24000, 1, 10000, 1024, 0));
        for (unsigned n = 0; n < 24000 + stop; ++n) {
            ra_tx_core_fm_sample(&fm, &c, lut, 2048 + ra_tone_next(&g), &i, &q);
        }
        CHECK(ra_tone_configure(&g, 24000, 1, 10000, 0, 0));
        for (unsigned n = 0; n < 24000; ++n) {
            CHECK(ra_tone_next(&g) == 0);
            ra_tx_core_fm_sample(&fm, &c, lut, 2048, &i, &q);
        }
        uint32_t before = fm.phase;
        for (unsigned n = 0; n < 1680; ++n) { ra_tx_core_fm_sample(&fm, &c, lut, 2048, &i, &q); }
        double turns = (double)(int32_t)(fm.phase - before) / 4294967296.0;
        double drift = fabs(turns / 0.07);
        if (drift > worst) { worst = drift; }
        CHECK(drift < 0.1 && fm.last_audio == 0);
    }
    printf("FM existing DC-servo residual at GEN mute: worst %.6f Hz (2500-Hz deviation, unity gain)\n", worst);
}
int main(void) {
    generator(); detector(); modulation(); fm_silence();
    printf("PASS tone DDS: sine error<2 codes; rational rate; phase/mute; waveforms; GEN mode validation\n");
    printf("PASS selected-tone detector: independent tones/DC/noise/neighbor/acquire/release; not protocol certification\n");
    printf("State bytes: generator=%zu detector=%zu; no board/DAC/RF tested\n", sizeof(ra_tone_gen_t), sizeof(ra_tone_detector_t));
    return 0;
}
