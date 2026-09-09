/* Real fixed-point core, not a Python filter model. No target/HIL proof. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "ra_tx_core.h"

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
#define PI 3.14159265358979323846

static double response(unsigned fs, unsigned cutoff, double frequency) {
    ra_tx_af_state_t af;
    CHECK(ra_tx_core_af_reset(&af, cutoff, fs, 1));
    double in = 0, out = 0;
    for (unsigned n = 0; n < 2 * fs; ++n) {
        int raw = 2048 + (int)lround(500 * sin(2 * PI * frequency * n / fs));
        int sample = ra_tx_core_af_sample(&af, raw, 2048) - 2048;
        if (n >= fs) { in += (raw - 2048) * (raw - 2048); out += sample * sample; }
    }
    CHECK(!af.clips);
    return sqrt(out / in);
}

int main(void) {
    CHECK(sizeof(ra_tx_ssb_state_t) + sizeof(ra_tx_af_state_t) <= 1024);
    ra_tx_af_state_t af;
    ra_tx_af_config_t config;
    CHECK(!ra_tx_core_af_reset(NULL, 3000, 24000, 1));
    CHECK(!ra_tx_core_af_reset(&af, 3000, 0, 1));
    CHECK(!ra_tx_core_af_prepare(&config, 6000, 12000, 1));
    CHECK(!ra_tx_core_af_prepare(&config, 1000, 24000, 1));
    for (unsigned mode = RA_TX_MODE_CW; mode <= RA_TX_MODE_LSB; ++mode) {
        ra_tx_config_t c = {.mode=mode, .sample_rate_hz=mode >= RA_TX_MODE_USB ? 12000 : 24000,
            .amplitude=400, .adc_mid=2048, .i_zero=2048, .q_zero=2048,
            .fm_gain=2, .ramp_samples=220, .audio_gain=100, .am_depth=50,
            .audio_controls=mode == RA_TX_MODE_AM || mode >= RA_TX_MODE_USB};
        CHECK(ra_tx_core_validate(&c));
        c.audio_cutoff = 2400;
        CHECK(ra_tx_core_validate(&c) == c.audio_controls);
        c.audio_cutoff = 6000;
        CHECK(ra_tx_core_validate(&c) == (mode == RA_TX_MODE_AM));
        c.audio_cutoff = 1799; CHECK(!ra_tx_core_validate(&c));
        c.audio_cutoff = 6001; CHECK(!ra_tx_core_validate(&c));
    }
    for (unsigned mid = 1; mid < 4095; mid += 127) {
        CHECK(ra_tx_core_af_reset(&af, 0, 44000, 1));
        for (unsigned raw = 0; raw <= 4095; ++raw) {
            CHECK(ra_tx_core_af_sample(&af, raw, mid) == raw);
        }
    }
    for (unsigned f = 0; f < 4; ++f) {
        unsigned fs = (unsigned[]){12000,24000,44000,48000}[f];
        for (unsigned c = 0; c < 7; ++c) {
            unsigned hz = (unsigned[]){1800,2000,2400,2700,3000,4500,6000}[c];
            if (hz * 2 >= fs) { continue; }
            double pass = response(fs, hz, 500);
            double corner = response(fs, hz, hz);
            double stop = response(fs, hz, (hz + fs / 2.0) / 2.0);
            CHECK(pass > .97 && pass < 1.02);
            CHECK(corner > .69 && corner < .72);
            CHECK(stop < .19);
            CHECK(response(fs, hz, 25) < .065);
            CHECK(ra_tx_core_af_reset(&af, hz, fs, 1));
            for (unsigned n = 0; n < fs; ++n) {
                CHECK(ra_tx_core_af_sample(&af, 3071, 2048) == 2048);
            }
        }
    }
    /* Exact 20 ms fade at the actual 30MHz/682 timer ratio; last target wins. */
    CHECK(ra_tx_core_af_reset(&af, 0, 30000000, 682));
    CHECK(af.fade_length == 880);
    CHECK(ra_tx_core_af_prepare(&config, 3000, 30000000, 682));
    ra_tx_core_af_request(&af, &config);
    ra_tx_core_af_sample(&af, 2048, 2048);
    CHECK(af.fade_position == 2 && af.bank[af.active].config.cutoff == 0);
    for (unsigned c = 0; c < 3; ++c) {
        CHECK(ra_tx_core_af_prepare(&config, (unsigned[]){4500,2000,6000}[c], 30000000, 682));
        ra_tx_core_af_request(&af, &config);
    }
    for (unsigned n = 1; n < af.fade_length; ++n) { ra_tx_core_af_sample(&af, 2048, 2048); }
    CHECK(!af.fade_position && af.bank[af.active].config.cutoff == 3000 && af.pending_valid);
    for (unsigned n = 0; n < af.fade_length; ++n) { ra_tx_core_af_sample(&af, 2048, 2048); }
    CHECK(!af.fade_position && af.bank[af.active].config.cutoff == 6000 && !af.pending_valid);
    /* Filter state never depends on producer block size; full-range stress. */
    ra_tx_af_state_t chunked;
    CHECK(ra_tx_core_af_reset(&af, 1800, 48000, 1));
    chunked = af;
    uint32_t rng = 1;
    for (unsigned block = 0; block < 1000; ++block) {
        for (unsigned n = 0; n < 512; ++n) {
            rng = rng * 1664525U + 1013904223U;
            uint16_t raw = block < 500 ? (rng >> 20) : (n & 1 ? 4095 : 0);
            uint16_t a = ra_tx_core_af_sample(&af, raw, 2048);
            CHECK(a <= 4095 && a == ra_tx_core_af_sample(&chunked, raw, 2048));
        }
    }
    CHECK(memcmp(&af, &chunked, sizeof af) == 0);
    /* A stale or zero target is not confused with 'no request'. */
    CHECK(ra_tx_core_af_prepare(&config, 0, 48000, 1));
    ra_tx_core_af_request(&af, &config);
    for (unsigned n = 0; n < af.fade_length; ++n) { ra_tx_core_af_sample(&af, 2000, 2048); }
    CHECK(ra_tx_core_af_sample(&af, 1234, 2048) == 1234);
    printf("PASS TX AF core: %u checks; response/DC/bypass/queued fades/stream history; state=%zu B\n", checks, sizeof af);
    return 0;
}
