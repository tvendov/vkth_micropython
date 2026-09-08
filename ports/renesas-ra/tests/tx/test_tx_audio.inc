/* Actual C math: normalized AM, separate SSB gain, no opt-in MIC/FILE AM bank. */
static void test_audio_controls(void) {
    ra_tx_config_t c = defaults(RA_TX_MODE_AM);
    CHECK(!ra_tx_uses_cpu(&c));
    CHECK(ra_tx_core_lut_bytes(&c) == RA_TX_AM_LUT_BYTES);
    CHECK(ra_tx_core_lut_alignment(&c) == RA_TX_AM_LUT_ALIGNMENT);
    c.audio_controls = true;
    c.audio_gain = 100;
    c.am_depth = 50;
    uint8_t bank[RA_TX_AM_LUT_BYTES];
    memset(bank, 0xa5, sizeof(bank));
    CHECK(ra_tx_uses_cpu(&c));
    CHECK(ra_tx_core_lut_bytes(&c) == 0);
    CHECK(ra_tx_core_lut_alignment(&c) == 1);
    CHECK(ra_tx_core_build_lut(&c, NULL, 0));
    CHECK(ra_tx_core_build_lut(&c, bank, sizeof(bank)));
    for (unsigned k = 0; k < sizeof(bank); ++k) { CHECK(bank[k] == 0xa5); }
    uint32_t clips = 0;
    for (unsigned level = 0; level <= 100; ++level) {
        c.amplitude = (uint16_t)((1023U * level + 50U) / 100U);
        CHECK(ra_tx_core_validate(&c));
        CHECK(ra_tx_core_build_lut(&c, NULL, 0));
        for (unsigned raw = 0; raw <= 4095; ++raw) {
            int audio = ((int)raw - 2048) / 2;
            unsigned expected = 2048U + ((2048 + audio) * c.amplitude + 1024) / 2048;
            uint16_t sample = ra_tx_core_am_sample(&c, (uint16_t)raw, &clips);
            CHECK(sample == expected);
        }
    }
    CHECK(clips == 0);
    c.file_source = true;
    c.sample_rate_hz = 24000;
    CHECK(ra_tx_core_lut_bytes(&c) == 0);
    CHECK(ra_tx_core_lut_alignment(&c) == 1);
    CHECK(ra_tx_core_build_lut(&c, NULL, 0));
    for (unsigned depth = 0; depth <= 100; depth += 10) {
        c.am_depth = (uint8_t)depth;
        for (unsigned gain = 0; gain <= 1600; gain += 100) {
            c.audio_gain = (uint16_t)gain;
            CHECK(ra_tx_core_validate(&c));
            for (unsigned raw = 0; raw <= 4095; ++raw) {
                uint16_t sample = ra_tx_core_am_sample(&c, (uint16_t)raw, &clips);
                CHECK(sample >= c.i_zero && sample <= c.i_zero + 2 * c.amplitude);
                if (gain == 0 || depth == 0) { CHECK(sample == c.i_zero + c.amplitude); }
            }
        }
    }
    CHECK(clips > 0);
    c.audio_gain = 1601;
    CHECK(!ra_tx_core_validate(&c));
    c.audio_gain = 100;
    c.am_depth = 101;
    CHECK(!ra_tx_core_validate(&c));
    c.am_depth = 50;
    c.mode = RA_TX_MODE_FM;
    CHECK(!ra_tx_core_validate(&c));
    puts("PASS normalized AM: all ADC codes and 101 levels, depth/gain bounds, mute, MIC/FILE no bank and legacy LUT preserved");

    for (unsigned mode = RA_TX_MODE_USB; mode <= RA_TX_MODE_LSB; ++mode) {
        ra_tx_config_t full = defaults((ra_tx_mode_t)mode);
        full.sample_rate_hz = RA_TX_SSB_RATE;
        full.audio_controls = true;
        full.audio_gain = 100;
        full.amplitude = 1600;
        ra_tx_ssb_state_t s;
        ra_tx_core_ssb_reset(&s);
        for (unsigned n = 0; n < 4096; ++n) {
            uint16_t raw = (uint16_t)(2048 + lround(700 * sin(2 * PI * 1000 * n / 12000)));
            ra_tx_ssb_state_t half_state = s, zero_state = s, gain_state = s;
            ra_tx_config_t half = full, zero = full, gain = full;
            half.amplitude = 800;
            zero.amplitude = 0;
            gain.audio_gain = 50;
            uint16_t i, q, hi, hq, zi, zq, gi, gq;
            CHECK(ra_tx_core_validate(&zero));
            ra_tx_core_ssb_sample(&s, &full, raw, &i, &q);
            ra_tx_core_ssb_sample(&half_state, &half, raw, &hi, &hq);
            ra_tx_core_ssb_sample(&zero_state, &zero, raw, &zi, &zq);
            ra_tx_core_ssb_sample(&gain_state, &gain, raw, &gi, &gq);
            CHECK(abs(2 * ((int)hi - 2048) - ((int)i - 2048)) <= 1);
            CHECK(abs(2 * ((int)hq - 2048) - ((int)q - 2048)) <= 1);
            CHECK(zi == 2048 && zq == 2048);
            CHECK(abs((int)gi - hi) <= 1 && abs((int)gq - hq) <= 1);
            CHECK(memcmp(&s, &half_state, sizeof(s)) == 0);
        }
        CHECK(s.clips == 0);
    }
    puts("PASS USB/LSB separate AF gain and LEVEL: proportional I/Q, zero output, unchanged stream history");
}
