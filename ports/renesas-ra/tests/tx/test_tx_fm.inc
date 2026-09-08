/* Included by test_tx_core.c: actual portable voice-FM implementation tests. */
static ra_tx_config_t voice_fm(void) {
    ra_tx_config_t c = defaults(RA_TX_MODE_FM);
    c.deviation_hz = 2500;
    c.mic_gain = 100;
    return c;
}

static double fm_delta(uint32_t now, uint32_t before, double fs) {
    uint32_t word = now - before;
    int64_t signed_word = word <= INT32_MAX ? (int64_t)word : (int64_t)word - 4294967296LL;
    return signed_word * fs / 4294967296.0;
}

static void test_voice_fm_validation_dc(void) {
    ra_tx_config_t c = voice_fm();
    ra_tx_fm_state_t s;
    uint8_t lut[RA_TX_FM_LUT_BYTES];
    CHECK(ra_tx_core_validate(&c));
    CHECK(ra_tx_core_build_lut(&c, lut, sizeof(lut)));
    CHECK(sizeof(s) <= 1024);
    c.amplitude = 0;
    CHECK(ra_tx_core_validate(&c));
    c.mic_gain = 1601;
    CHECK(!ra_tx_core_validate(&c));
    c = voice_fm();
    c.deviation_hz = 5001;
    CHECK(!ra_tx_core_validate(&c));
    c.deviation_hz = 5000;
    c.sample_rate_hz = 16000;
    CHECK(!ra_tx_core_validate(&c));
    CHECK(ra_tx_core_fm_step(30000000, 682, 5000) != 0);
    CHECK(ra_tx_core_fm_step(16000000, 1000, 5000) == 0);
    CHECK(ra_tx_core_fm_step(0, 682, 2500) == 0);
    CHECK(ra_tx_core_fm_step(30000000, 0, 2500) == 0);
    CHECK(ra_tx_core_fm_step(30000000, UINT32_MAX, 2500) == 0);
    c = voice_fm();
    for (unsigned bias = 0; bias <= 4095; bias += 195) {
        CHECK(ra_tx_core_fm_reset(&s, &c, 30000000, 682));
        for (unsigned n = 0; n < 1024; ++n) {
            uint16_t i, q;
            ra_tx_core_fm_sample(&s, &c, lut, bias, &i, &q);
            CHECK(s.phase == 0 && s.last_audio == 0);
            CHECK(i == 2848 && q == 2048); /* carrier, not mute */
        }
    }
    /* A changed DC bias has a transient, then must settle, not leave a beat. */
    CHECK(ra_tx_core_fm_reset(&s, &c, 30000000, 682));
    uint16_t i, q;
    ra_tx_core_fm_sample(&s, &c, lut, 700, &i, &q);
    for (unsigned n = 0; n < 30000; ++n) {
        uint32_t before = s.phase;
        ra_tx_core_fm_sample(&s, &c, lut, 3000, &i, &q);
        if (n > 25000) {
            CHECK(fabs(fm_delta(s.phase, before, 30000000.0 / 682)) < 0.1);
        }
    }
    puts("PASS voice FM validation/DC: actual-rate guards, arbitrary bias, DC-step settling, carrier at silence");
}

static void test_voice_fm_tone_and_level(void) {
    const unsigned periods[] = {625, 682, 1250};
    const unsigned deviations[] = {100, 2500, 4000, 5000};
    uint8_t lut[RA_TX_FM_LUT_BYTES];
    for (unsigned p = 0; p < 3; ++p) {
        double fs = 30000000.0 / periods[p];
        for (unsigned d = 0; d < 4; ++d) {
            ra_tx_config_t c = voice_fm(), quiet = c;
            c.sample_rate_hz = (uint32_t)(fs + 0.5);
            c.deviation_hz = deviations[d];
            quiet = c;
            quiet.amplitude = 0;
            ra_tx_fm_state_t s, muted;
            CHECK(ra_tx_core_fm_reset(&s, &c, 30000000, periods[p]));
            CHECK(ra_tx_core_fm_reset(&muted, &quiet, 30000000, periods[p]));
            CHECK(ra_tx_core_build_lut(&c, lut, sizeof(lut)));
            double sum = 0, power = 0;
            for (unsigned n = 0; n < 14000; ++n) {
                uint16_t adc = (uint16_t)(2048 + lround(1024 * sin(2 * PI * 1000 * n / fs)));
                uint16_t i, q, mi, mq;
                uint32_t before = s.phase;
                ra_tx_core_fm_sample(&s, &c, lut, adc, &i, &q);
                ra_tx_core_fm_sample(&muted, &quiet, lut, adc, &mi, &mq);
                CHECK(s.phase == muted.phase && mi == 2048 && mq == 2048);
                CHECK(fabs(hypot((double)i - 2048, (double)q - 2048) - 800) < 1.5);
                double df = fm_delta(s.phase, before, fs);
                CHECK(fabs(df) <= c.deviation_hz + 0.001);
                if (n >= 4000) { sum += df; power += df * df; }
            }
            /* +/-1024 codes at unity = half the selected peak deviation.
             * 1-kHz conditioning loss is <1.5%; finite-window error included. */
            double measured = sqrt(2 * power / 10000);
            CHECK(fabs(measured / (0.5 * c.deviation_hz) - 1.0) < 0.015);
            CHECK(fabs(sum / 10000) < c.deviation_hz * 0.002);
            CHECK(s.clips == 0 && s.adc_rails == 0);
        }
    }
    puts("PASS voice FM tones: 1 kHz, deviations 100/2500/4000/5000 Hz at three actual rates; independent DAC level");
}

static void test_voice_fm_stream_limiter(void) {
    ra_tx_config_t c = voice_fm();
    ra_tx_fm_state_t a, b;
    uint8_t lut[RA_TX_FM_LUT_BYTES];
    CHECK(ra_tx_core_fm_reset(&a, &c, 30000000, 682));
    CHECK(ra_tx_core_fm_reset(&b, &c, 30000000, 682));
    CHECK(ra_tx_core_build_lut(&c, lut, sizeof(lut)));
    for (unsigned base = 0; base < 12000;) {
        unsigned count = 1 + (base * 17U % 131U);
        for (unsigned k = 0; k < count && base < 12000; ++k, ++base) {
            int tone = (int)lround(700 * sin(2 * PI * 1000 * base / (30000000.0 / 682)));
            uint16_t ai, aq, bi, bq;
            ra_tx_core_fm_sample(&a, &c, lut, 1000 + tone, &ai, &aq);
            ra_tx_core_fm_sample(&b, &c, lut, 3000 + tone, &bi, &bq);
            CHECK(a.phase == b.phase && ai == bi && aq == bq);
        }
    }
    uint32_t phase = a.phase;
    c.mic_gain = 1600;
    c.deviation_hz = 5000;
    a.max_step = ra_tx_core_fm_step(30000000, 682, c.deviation_hz);
    CHECK(a.phase == phase); /* control updates must not re-prime or reset */
    for (unsigned n = 0; n < 12000; ++n) {
        uint16_t i, q;
        uint32_t before = a.phase;
        ra_tx_core_fm_sample(&a, &c, lut, (n / 22) % 2 ? 4095 : 0, &i, &q);
        CHECK(fabs(fm_delta(a.phase, before, 30000000.0 / 682)) <= 5000.001);
        CHECK(i >= 1248 && i <= 2848 && q >= 1248 && q <= 2848);
    }
    CHECK(a.clips > 0 && a.adc_rails == 12000 && a.audio_peak == 2048);
    CHECK(ra_tx_core_fm_reset(&a, &c, 30000000, 682));
    CHECK(a.phase == 0 && a.clips == 0 && a.adc_rails == 0 && !a.primed);
    puts("PASS voice FM stream: arbitrary chunks, bias invariance, live phase continuity, limiter, ADC rails and restart");
}

static void test_voice_fm_filter(void) {
    const unsigned frequencies[] = {1000, 3000, 10000};
    double levels[3];
    ra_tx_config_t c = voice_fm();
    uint8_t lut[RA_TX_FM_LUT_BYTES];
    CHECK(ra_tx_core_build_lut(&c, lut, sizeof(lut)));
    for (unsigned f = 0; f < 3; ++f) {
        ra_tx_fm_state_t s;
        CHECK(ra_tx_core_fm_reset(&s, &c, 30000000, 682));
        double energy = 0;
        for (unsigned n = 0; n < 16000; ++n) {
            uint16_t i, q;
            uint16_t adc = (uint16_t)(2048 + lround(1000 * sin(2 * PI * frequencies[f] * n / (30000000.0 / 682))));
            ra_tx_core_fm_sample(&s, &c, lut, adc, &i, &q);
            if (n >= 4000) { energy += (double)s.last_audio * s.last_audio; }
        }
        levels[f] = sqrt(energy / 12000);
    }
    CHECK(levels[1] / levels[0] > 0.69 && levels[1] / levels[0] < 0.73);
    CHECK(levels[2] / levels[0] < 0.08);
    puts("PASS voice FM conditioning: 3-kHz Butterworth knee and 10-kHz rejection; no pre-emphasis claimed");
}
