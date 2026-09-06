/* Exercise actual core C plus independent DOC/address mathematical models.
 * No target headers, register access, MicroPython runtime or board execution.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ra_tx_core.h"

#define PI (3.14159265358979323846264338327950288)
static unsigned long checks;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static ra_tx_config_t defaults(ra_tx_mode_t mode) {
    ra_tx_config_t config = {
        .mode = mode, .sample_rate_hz = 44000, .amplitude = 800,
        .adc_mid = 2048, .i_zero = 2048, .q_zero = 2048,
        .ramp_samples = 220, .fm_gain = 2,
    };
    return config;
}

static uint16_t read_u16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_u32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8)
           | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void test_validation(void) {
    CHECK(!ra_tx_core_validate(NULL));
    for (int mode = RA_TX_MODE_CW; mode <= RA_TX_MODE_FM; ++mode) {
        ra_tx_config_t config = defaults((ra_tx_mode_t)mode);
        CHECK(ra_tx_core_validate(&config));
        const uint32_t rates[] = {0, 7999, 8000, 48000, 48001, UINT32_MAX};
        for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
            config.sample_rate_hz = rates[i];
            CHECK(ra_tx_core_validate(&config) == (rates[i] >= 8000 && rates[i] <= 48000));
        }
        config = defaults((ra_tx_mode_t)mode);
        for (uint32_t value = 0; value <= UINT16_MAX; ++value) {
            config.adc_mid = (uint16_t)value;
            CHECK(ra_tx_core_validate(&config) == (value >= 1 && value <= 4094));
        }
        config = defaults((ra_tx_mode_t)mode);
        for (uint32_t value = 0; value <= 257; ++value) {
            config.ramp_samples = (uint16_t)value;
            CHECK(ra_tx_core_validate(&config) == (value >= 1 && value <= 256));
        }
        config = defaults((ra_tx_mode_t)mode);
        for (uint32_t value = 0; value <= UINT8_MAX; ++value) {
            config.fm_gain = (uint8_t)value;
            CHECK(ra_tx_core_validate(&config) == (value == 1 || value == 2));
        }
        config = defaults((ra_tx_mode_t)mode);
        uint32_t limit = mode == RA_TX_MODE_AM ? 1023U : 2047U;
        for (uint32_t value = 0; value <= UINT16_MAX; ++value) {
            config.amplitude = (uint16_t)value;
            CHECK(ra_tx_core_validate(&config) == (value >= 1 && value <= limit));
        }
    }
    ra_tx_config_t config = defaults(RA_TX_MODE_FM);
    config.mode = (ra_tx_mode_t)-1;
    CHECK(!ra_tx_core_validate(&config));
    config.mode = (ra_tx_mode_t)5;
    CHECK(!ra_tx_core_validate(&config));

    for (int mode = RA_TX_MODE_CW; mode <= RA_TX_MODE_FM; ++mode) {
        config = defaults((ra_tx_mode_t)mode);
        config.amplitude = 1;
        for (uint32_t zero = 0; zero <= UINT16_MAX; ++zero) {
            config.i_zero = (uint16_t)zero;
            bool valid = mode == RA_TX_MODE_AM ? zero <= 4093
                         : mode == RA_TX_MODE_CW ? zero <= 4094 : zero >= 1 && zero <= 4094;
            CHECK(ra_tx_core_validate(&config) == valid);
        }
        config.i_zero = 2048;
        for (uint32_t zero = 0; zero <= UINT16_MAX; ++zero) {
            config.q_zero = (uint16_t)zero;
            bool valid = mode == RA_TX_MODE_FM ? zero >= 1 && zero <= 4094 : zero <= 4095;
            CHECK(ra_tx_core_validate(&config) == valid);
        }
    }
    puts("PASS validation: full 16-bit ADC midpoint/amplitude/offset domains and mode/rate/gain/ramp boundaries");
}

static void test_lut_contract(void) {
    uint8_t bank[RA_TX_AM_LUT_BYTES + 2U];
    memset(bank, 0xa5, sizeof(bank));

    CHECK(ra_tx_core_lut_bytes(NULL) == 0U);
    CHECK(ra_tx_core_lut_alignment(NULL) == 1U);
    struct {
        ra_tx_mode_t mode;
        size_t bytes;
        size_t alignment;
        size_t allocation;
    } cases[] = {
        {RA_TX_MODE_CW, 0U, 1U, 0U},
        {RA_TX_MODE_AM, 8192U, 8192U, 16383U},
        {RA_TX_MODE_FM, 1024U, 1024U, 2047U},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        ra_tx_config_t requirements = defaults(cases[i].mode);
        size_t bytes = ra_tx_core_lut_bytes(&requirements);
        size_t alignment = ra_tx_core_lut_alignment(&requirements);
        CHECK(bytes == cases[i].bytes);
        CHECK(alignment == cases[i].alignment);
        CHECK((alignment & (alignment - 1U)) == 0U);
        if (bytes != 0U) {
            CHECK(bytes + alignment - 1U == cases[i].allocation);
            /* Over-allocation by alignment-1 must fit the complete table for
             * every possible original-pointer residue. */
            for (size_t residue = 0; residue < alignment; ++residue) {
                size_t padding = (alignment - residue) & (alignment - 1U);
                CHECK(padding + bytes <= cases[i].allocation);
            }
            /* An aligned compact table never carries out of the low 16-bit
             * source-address halfword patched by the hardware backend. */
            for (uint32_t low = 0; low <= UINT16_MAX; low += (uint32_t)alignment) {
                CHECK(low + bytes - 1U <= UINT16_MAX);
            }
        }
    }

    ra_tx_config_t config = defaults(RA_TX_MODE_CW);
    CHECK(ra_tx_core_build_lut(&config, NULL, 0));
    CHECK(ra_tx_core_build_lut(&config, bank, sizeof(bank)));
    for (size_t j = 0; j < sizeof(bank); ++j) {
        CHECK(bank[j] == 0xa5);
    }
    config.mode = RA_TX_MODE_AM;
    CHECK(!ra_tx_core_build_lut(&config, NULL, RA_TX_AM_LUT_BYTES));
    CHECK(!ra_tx_core_build_lut(&config, bank, RA_TX_AM_LUT_BYTES - 1U));
    config.mode = RA_TX_MODE_FM;
    CHECK(!ra_tx_core_build_lut(&config, NULL, RA_TX_FM_LUT_BYTES));
    CHECK(!ra_tx_core_build_lut(&config, bank, RA_TX_FM_LUT_BYTES - 1U));
    CHECK(!ra_tx_core_build_lut(NULL, bank, sizeof(bank)));
    config.amplitude = 0;
    CHECK(!ra_tx_core_build_lut(&config, bank, sizeof(bank)));
    for (size_t j = 0; j < sizeof(bank); ++j) {
        CHECK(bank[j] == 0xa5);
    }
    config = defaults(RA_TX_MODE_FM);
    config.mode = (ra_tx_mode_t)-1;
    CHECK(ra_tx_core_lut_bytes(&config) == 0U);
    CHECK(ra_tx_core_lut_alignment(&config) == 1U);
    config.mode = (ra_tx_mode_t)3;
    CHECK(ra_tx_core_lut_bytes(&config) == 0U);
    CHECK(ra_tx_core_lut_alignment(&config) == 1U);
    puts("PASS bank contract: mode sizes, alignment/allocation bounds, CW no LUT, invalid calls preserve memory");
}

static void test_am_lut(void) {
    uint8_t storage[RA_TX_AM_LUT_BYTES + 2U];
    uint8_t *bank = storage + 1;
    const uint16_t midpoints[] = {1, 2048, 4094};
    const uint16_t zeros[] = {0, 2048, 4093};
    for (size_t m = 0; m < 3; ++m) {
        for (size_t z = 0; z < 3; ++z) {
            ra_tx_config_t config = defaults(RA_TX_MODE_AM);
            config.adc_mid = midpoints[m];
            config.i_zero = zeros[z];
            config.amplitude = (uint16_t)((4095 - config.i_zero) / 2);
            memset(storage, 0xa5, sizeof(storage));
            CHECK(ra_tx_core_build_lut(&config, bank, RA_TX_AM_LUT_BYTES));
            uint16_t previous = config.i_zero;
            for (uint32_t adc = 0; adc < 4096; ++adc) {
                uint16_t code = read_u16(bank + 2 * adc);
                int32_t low = (int32_t)config.adc_mid - config.amplitude;
                int32_t high = (int32_t)config.adc_mid + config.amplitude;
                uint16_t expected = (int32_t)adc < low ? config.i_zero
                                    : (int32_t)adc > high ? config.i_zero + 2 * config.amplitude
                                    : (uint16_t)((int32_t)config.i_zero + config.amplitude + (int32_t)adc - config.adc_mid);
                CHECK(code == expected);
                CHECK(code >= previous && code >= config.i_zero && code <= config.i_zero + 2 * config.amplitude);
                previous = code;
            }
            CHECK(read_u16(bank + 2 * config.adc_mid) == config.i_zero + config.amplitude);
            CHECK(storage[0] == 0xa5 && storage[sizeof(storage) - 1] == 0xa5);
        }
    }
    ra_tx_config_t config = defaults(RA_TX_MODE_AM);
    CHECK(ra_tx_core_build_lut(&config, bank, RA_TX_AM_LUT_BYTES));
    const uint16_t adc[] = {0, 1248, 1249, 2048, 2847, 2848, 4095};
    const uint16_t dac[] = {2048, 2048, 2049, 2848, 3647, 3648, 3648};
    for (size_t j = 0; j < sizeof(adc) / sizeof(adc[0]); ++j) {
        CHECK(read_u16(bank + 2 * adc[j]) == dac[j]);
    }
    for (uint32_t raw = 0; raw <= 4095U; ++raw) {
        uint16_t offset = (uint16_t)(raw + raw);
        uint8_t sar[] = {(uint8_t)offset, (uint8_t)(offset >> 8), 0x34, 0x12};
        CHECK(offset < RA_TX_AM_LUT_BYTES);
        CHECK(read_u32(sar) == 0x12340000U + 2U * raw);
    }
    puts("PASS AM: compact 4096-entry LUT, all ADC codes, calibration/headroom extremes and guards");
}

static void test_fm_lut_and_address_model(void) {
    uint8_t storage[RA_TX_FM_LUT_BYTES + 2U];
    uint8_t *bank = storage + 1;
    const uint32_t q_base = RA_TX_FM_LUT_BYTES / 2U;
    ra_tx_config_t config = defaults(RA_TX_MODE_FM);
    const uint16_t amplitudes[] = {1, 800, 2047};
    for (size_t a = 0; a < 3; ++a) {
        config.amplitude = amplitudes[a];
        config.i_zero = 2047;
        config.q_zero = 2048;
        memset(storage, 0xa5, sizeof(storage));
        CHECK(ra_tx_core_build_lut(&config, bank, RA_TX_FM_LUT_BYTES));
        for (uint32_t phase = 0; phase <= UINT16_MAX; ++phase) {
            /* Synthetic little-endian SAR bytes, not a target DTC descriptor. */
            uint8_t i_sar[] = {0, 0, 0x34, 0x12};
            uint8_t q_sar[] = {0, 0, 0x34, 0x12};
            uint32_t index = phase >> 8;
            uint32_t offset = 2U * index;
            uint32_t q_offset = q_base + offset;
            i_sar[0] = (uint8_t)offset;
            i_sar[1] = (uint8_t)(offset >> 8);
            q_sar[0] = (uint8_t)q_offset;
            q_sar[1] = (uint8_t)(q_offset >> 8);
            CHECK(read_u32(i_sar) == 0x12340000U + offset);
            CHECK(read_u32(q_sar) == 0x12340000U + q_offset);
            int i = read_u16(bank + offset) - config.i_zero;
            int q = read_u16(bank + q_offset) - config.q_zero;
            double angle = 2.0 * PI * index / 256.0;
            CHECK(fabs(i - config.amplitude * cos(angle)) <= 0.501);
            CHECK(fabs(q - config.amplitude * sin(angle)) <= 0.501);
            CHECK(read_u16(bank + offset) <= 4095 && read_u16(bank + q_offset) <= 4095);
        }
        const int16_t x[] = {1, 0, -1, 0};
        const int16_t y[] = {0, 1, 0, -1};
        for (uint32_t quadrant = 0; quadrant < 4; ++quadrant) {
            uint32_t offset = 2U * quadrant * 64U;
            CHECK(read_u16(bank + offset) == config.i_zero + x[quadrant] * config.amplitude);
            CHECK(read_u16(bank + q_base + offset) == config.q_zero + y[quadrant] * config.amplitude);
        }
        CHECK(2U * 255U + 1U < q_base);
        CHECK(q_base + 2U * 255U + 1U == RA_TX_FM_LUT_BYTES - 1U);
        CHECK(storage[0] == 0xa5 && storage[sizeof(storage) - 1] == 0xa5);
    }
    puts("PASS FM planar LUT and SAR model: every 16-bit phase, I/Q planes, axis points and guards");
}

static void test_doc_bias_and_phase_model(void) {
    const uint16_t phases[] = {0, 1, 255, 256, 32767, 32768, 65534, 65535};
    const uint16_t midpoints[] = {1, 2048, 4094};
    ra_tx_config_t config = defaults(RA_TX_MODE_FM);
    CHECK(ra_tx_core_fm_bias(NULL) == 0);
    for (uint8_t gain = 1; gain <= 2; ++gain) {
        config.fm_gain = gain;
        for (uint16_t mid = 1; mid < 4095; ++mid) {
            config.adc_mid = mid;
            CHECK(ra_tx_core_fm_bias(&config) == (uint16_t)(65536 - gain * mid));
        }
        for (size_t m = 0; m < 3; ++m) {
            config.adc_mid = midpoints[m];
            uint16_t bias = ra_tx_core_fm_bias(&config);
            for (size_t p = 0; p < sizeof(phases) / sizeof(phases[0]); ++p) {
                for (uint32_t adc = 0; adc < 4096; ++adc) {
                    uint16_t actual = (uint16_t)(phases[p] + adc);
                    if (gain == 2) {
                        actual = (uint16_t)(actual + adc);
                    }
                    actual = (uint16_t)(actual + bias);
                    int32_t expected = phases[p] + gain * ((int32_t)adc - config.adc_mid);
                    if (expected < 0) {
                        expected += 65536;
                    }
                    if (expected >= 65536) {
                        expected -= 65536;
                    }
                    CHECK(actual == expected);
                    if (gain == 2) {
                        CHECK((actual & 1) == (phases[p] & 1));
                    }
                }
            }
        }
    }
    CHECK((uint16_t)(0xfffaU + 8U) == 2);
    uint16_t phase = 0;
    for (uint32_t n = 1; n <= 65536; ++n) {
        phase = (uint16_t)(phase + 1);
        CHECK(phase == (uint16_t)n);
        CHECK((phase >> 8) == ((n / 256) % 256));
    }
    CHECK((uint16_t)(phase - 1) == 65535);
    puts("PASS FM bias and DOC model: all midpoint biases, 196608 ADC/phase cases, wrap and low-bit retention");
}

static void test_ramps(void) {
    uint16_t guarded[RA_TX_MAX_RAMP_SAMPLES + 2];
    uint16_t reverse[RA_TX_MAX_RAMP_SAMPLES];
    const uint16_t pairs[][2] = {{2048, 2848}, {2848, 2048}, {2048, 2048}, {0, 4095}, {4095, 0}, {0, 65535}, {65535, 0}};
    for (size_t p = 0; p < sizeof(pairs) / sizeof(pairs[0]); ++p) {
        uint16_t from = pairs[p][0];
        uint16_t to = pairs[p][1];
        for (uint16_t n = 1; n <= RA_TX_MAX_RAMP_SAMPLES; ++n) {
            for (size_t j = 0; j < sizeof(guarded) / sizeof(guarded[0]); ++j) {
                guarded[j] = 0xa55a;
            }
            ra_tx_core_ramp(guarded + 1, n, from, to);
            CHECK(guarded[0] == 0xa55a && guarded[n + 1] == 0xa55a);
            CHECK(guarded[n] == to);
            uint16_t previous = from;
            for (uint16_t j = 1; j <= n; ++j) {
                uint16_t code = guarded[j];
                CHECK(from <= to ? code >= previous && code <= to : code <= previous && code >= to);
                double expected = from + ((double)to - from) * 0.5 * (1.0 - cos(PI * j / n));
                CHECK(fabs(code - expected) <= 0.511);
                previous = code;
            }
            /* Reversal starts from the current code, never from the old target. */
            uint16_t current = guarded[1 + n / 2];
            ra_tx_core_ramp(reverse, n, current, from);
            previous = current;
            for (uint16_t j = 0; j < n; ++j) {
                CHECK(current <= from ? reverse[j] >= previous && reverse[j] <= from
                      : reverse[j] <= previous && reverse[j] >= from);
                previous = reverse[j];
            }
            CHECK(reverse[n - 1] == from);
        }
    }
    guarded[0] = 123;
    ra_tx_core_ramp(guarded, 0, 0, 4095);
    CHECK(guarded[0] == 123);
    ra_tx_core_ramp(NULL, 0, 0, 4095);
    ra_tx_core_ramp(NULL, 220, 0, 4095);
    puts("PASS CW ramps: every length 1..256, rising/falling/constant, exact endpoints, reversal and no-op");
}

static void test_integer_ramp_scaling(void) {
    uint16_t shape[RA_TX_MAX_RAMP_SAMPLES];
    uint16_t guarded[RA_TX_MAX_RAMP_SAMPLES + 2];
    uint16_t reverse[RA_TX_MAX_RAMP_SAMPLES];
    const uint16_t pairs[][2] = {
        {2048, 2848}, {2848, 2048}, {2048, 2048}, {0, 4095}, {4095, 0},
        {0, 1}, {1, 0}, {0, 65535}, {65535, 0},
    };
    for (uint16_t n = 1; n <= RA_TX_MAX_RAMP_SAMPLES; ++n) {
        ra_tx_core_ramp(shape, n, 0, UINT16_MAX);
        CHECK(shape[n - 1] == UINT16_MAX);
        for (uint16_t j = 1; j < n; ++j) {
            CHECK(shape[j] >= shape[j - 1]);
        }
        for (size_t p = 0; p < sizeof(pairs) / sizeof(pairs[0]); ++p) {
            uint16_t from = pairs[p][0];
            uint16_t to = pairs[p][1];
            guarded[0] = 0xa55a;
            guarded[n + 1] = 0xa55a;
            ra_tx_core_scale_ramp(guarded + 1, shape, n, from, to);
            ra_tx_core_scale_ramp(reverse, shape, n, to, from);
            CHECK(guarded[0] == 0xa55a && guarded[n + 1] == 0xa55a);
            CHECK(guarded[n] == to && reverse[n - 1] == from);
            uint16_t previous = from;
            for (uint16_t j = 0; j < n; ++j) {
                uint16_t code = guarded[j + 1];
                long expected = from + lround(((double)to - from) * shape[j] / 65535.0);
                CHECK(code == expected);
                CHECK(from <= to ? code >= previous && code <= to : code <= previous && code >= to);
                CHECK((uint32_t)code + reverse[j] == (uint32_t)from + to);
                previous = code;
            }
            uint16_t current = guarded[1 + n / 2];
            ra_tx_core_scale_ramp(reverse, shape, n, current, from);
            previous = current;
            for (uint16_t j = 0; j < n; ++j) {
                CHECK(current <= from ? reverse[j] >= previous && reverse[j] <= from
                      : reverse[j] <= previous && reverse[j] >= from);
                previous = reverse[j];
            }
            CHECK(reverse[n - 1] == from);
        }
    }

    /* Every possible normalized value exercises the divider rounding,
     * including values immediately below and above the midpoint.
     */
    const uint16_t deltas[] = {0, 1, 2, 799, 800, 4095, 65535};
    uint16_t sample_shape[2] = {0, UINT16_MAX};
    uint16_t up[2];
    uint16_t down[2];
    for (size_t d = 0; d < sizeof(deltas) / sizeof(deltas[0]); ++d) {
        uint16_t delta = deltas[d];
        for (uint32_t value = 0; value <= UINT16_MAX; ++value) {
            sample_shape[0] = (uint16_t)value;
            ra_tx_core_scale_ramp(up, sample_shape, 2, 0, delta);
            ra_tx_core_scale_ramp(down, sample_shape, 2, delta, 0);
            CHECK(up[0] == lround((double)delta * value / 65535.0));
            CHECK((uint32_t)up[0] + down[0] == delta);
            CHECK(up[1] == delta && down[1] == 0);
        }
    }
    guarded[0] = 123;
    ra_tx_core_scale_ramp(guarded, shape, 0, 0, 4095);
    CHECK(guarded[0] == 123);
    ra_tx_core_scale_ramp(guarded, NULL, 220, 0, 4095);
    CHECK(guarded[0] == 123);
    ra_tx_core_scale_ramp(NULL, shape, 220, 0, 4095);
    sample_shape[0] = 0;
    ra_tx_core_scale_ramp(up, sample_shape, 1, 2048, 2848);
    CHECK(up[0] == 2848);
    puts("PASS integer CW scaling: all lengths/shapes, symmetric rounding, DAC/full-word extremes and reversal");
}

static void test_ssb_validation_and_dc(void) {
    for (int mode = RA_TX_MODE_USB; mode <= RA_TX_MODE_LSB; ++mode) {
        ra_tx_config_t c = defaults((ra_tx_mode_t)mode);
        CHECK(!ra_tx_core_validate(&c)); /* No silently mistuned FIR at 44 kHz. */
        c.sample_rate_hz = RA_TX_SSB_RATE;
        CHECK(ra_tx_core_validate(&c));
        CHECK(ra_tx_core_lut_bytes(&c) == 0 && ra_tx_core_lut_alignment(&c) == 1);
        CHECK(ra_tx_core_build_lut(&c, NULL, 0));
        for (unsigned rate = 8000; rate <= 48000; ++rate) {
            c.sample_rate_hz = rate;
            CHECK(ra_tx_core_validate(&c) == (rate == RA_TX_SSB_RATE));
        }
        c.sample_rate_hz = RA_TX_SSB_RATE;
        c.i_zero = 799;
        CHECK(!ra_tx_core_validate(&c));
        c.i_zero = 2048;
        c.q_zero = 3296;
        CHECK(!ra_tx_core_validate(&c));
        c.q_zero = 2048;
        for (unsigned adc = 0; adc <= 4095; adc += 273) {
            struct { uint32_t pre; ra_tx_ssb_state_t state; uint32_t post; } guarded;
            guarded.pre = guarded.post = 0xa55ac33c;
            ra_tx_core_ssb_reset(&guarded.state);
            for (unsigned n = 0; n < 600; ++n) {
                uint16_t i, q;
                ra_tx_core_ssb_sample(&guarded.state, &c, (uint16_t)adc, &i, &q);
                CHECK(i == c.i_zero && q == c.q_zero);
            }
            CHECK(guarded.state.clips == 0);
            CHECK(guarded.pre == 0xa55ac33c && guarded.post == 0xa55ac33c);
        }
    }
    puts("PASS SSB validation/DC: fixed rate, both DAC headrooms, no LUT, biased-input priming and state guards");
}

static void test_ssb_sidebands(void) {
    double worst = 1000.0;
    for (int mode = RA_TX_MODE_USB; mode <= RA_TX_MODE_LSB; ++mode) {
        ra_tx_config_t c = defaults((ra_tx_mode_t)mode);
        c.sample_rate_hz = RA_TX_SSB_RATE;
        for (unsigned hz = 300; hz <= 3000; hz += 100) {
            ra_tx_ssb_state_t s;
            ra_tx_core_ssb_reset(&s);
            double pr = 0, pi = 0, nr = 0, ni = 0;
            double mean_i = 0, mean_q = 0;
            unsigned count = RA_TX_SSB_RATE;
            for (unsigned n = 0; n < count + 1024U; ++n) {
                double angle = 2.0 * PI * hz * n / RA_TX_SSB_RATE;
                uint16_t adc = (uint16_t)(2048 + lround(1200 * cos(angle)));
                uint16_t ic, qc;
                ra_tx_core_ssb_sample(&s, &c, adc, &ic, &qc);
                CHECK(ic <= 4095 && qc <= 4095);
                if (n >= 1024U) {
                    double i = (double)ic - c.i_zero, q = (double)qc - c.q_zero;
                    double co = cos(angle), si = sin(angle);
                    pr += i * co + q * si;
                    pi += q * co - i * si;
                    nr += i * co - q * si;
                    ni += q * co + i * si;
                    mean_i += i;
                    mean_q += q;
                }
            }
            double positive = hypot(pr, pi) / count, negative = hypot(nr, ni) / count;
            double desired = mode == RA_TX_MODE_USB ? positive : negative;
            double image = mode == RA_TX_MODE_USB ? negative : positive;
            double rejection = 20 * log10(desired / (image + 1e-12));
            if (rejection < worst) {
                worst = rejection;
            }
            CHECK(rejection >= 55.0);
            CHECK(fabs(desired / (800.0 * 1200 / 2048) - 1.0) < .015);
            if (fabs(mean_i / count) >= .1 || fabs(mean_q / count) >= .1) {
                printf("SSB quantisation mean mode=%d Hz=%u I=%.6f Q=%.6f DAC codes\n",
                    mode, hz, mean_i / count, mean_q / count);
            }
            /* Two symmetric roundings contribute at most .5*800/2048+.5
             * DAC codes to the mean; a non-symmetric sampled cosine period
             * need not have zero mean AFTER those roundings. Constant input
             * itself must reject exactly (the separate DC test above). */
            CHECK(fabs(mean_i / count) <= .70 && fabs(mean_q / count) <= .70);
            CHECK(s.clips == 0);
        }
    }
    printf("PASS SSB actual C output: 56 USB/LSB tones 300..3000 Hz, worst image rejection %.2f dB\n", worst);
}

static void test_ssb_stream_continuity_and_limits(void) {
    enum { COUNT = 9000 };
    static uint16_t input[COUNT], expected_i[COUNT], expected_q[COUNT];
    ra_tx_config_t usb = defaults(RA_TX_MODE_USB), lsb = defaults(RA_TX_MODE_LSB);
    usb.sample_rate_hz = lsb.sample_rate_hz = RA_TX_SSB_RATE;
    ra_tx_ssb_state_t whole, chunked, mirror;
    ra_tx_core_ssb_reset(&whole);
    ra_tx_core_ssb_reset(&chunked);
    ra_tx_core_ssb_reset(&mirror);
    uint32_t random = 12345;
    for (unsigned n = 0; n < COUNT; ++n) {
        random = random * 1664525U + 1013904223U;
        input[n] = (uint16_t)((random >> 16) & 4095U);
        ra_tx_core_ssb_sample(&whole, &usb, input[n], &expected_i[n], &expected_q[n]);
    }
    const unsigned chunks[] = {1, 7, 128, 3, 256, 511, 17, 64};
    unsigned offset = 0, part = 0;
    while (offset < COUNT) {
        unsigned end = offset + chunks[part++ % (sizeof(chunks) / sizeof(chunks[0]))];
        if (end > COUNT) {
            end = COUNT;
        }
        for (; offset < end; ++offset) {
            uint16_t i, q, mi, mq;
            ra_tx_core_ssb_sample(&chunked, &usb, input[offset], &i, &q);
            ra_tx_core_ssb_sample(&mirror, &lsb, input[offset], &mi, &mq);
            CHECK(i == expected_i[offset] && q == expected_q[offset]);
            CHECK(i == mi && (unsigned)q + mq == 4096U);
        }
    }
    CHECK(memcmp(&whole, &chunked, sizeof(whole)) == 0);
    /* Explicit restart resets history; first constant sample stays neutral. */
    ra_tx_core_ssb_reset(&whole);
    uint16_t i, q;
    ra_tx_core_ssb_sample(&whole, &usb, 3210, &i, &q);
    CHECK(i == 2048 && q == 2048 && whole.clips == 0);
    /* Overdrive including invalid ADC words must never wrap DAC codes. */
    usb.amplitude = 2047;
    usb.i_zero = 2047;
    usb.q_zero = 2048;
    ra_tx_core_ssb_reset(&whole);
    for (unsigned n = 0; n < COUNT; ++n) {
        ra_tx_core_ssb_sample(&whole, &usb, (n / 3U) % 2U ? UINT16_MAX : 0, &i, &q);
        CHECK(i <= 4094 && q >= 1 && q <= 4095);
    }
    CHECK(whole.clips > 0);
    puts("PASS SSB stream: arbitrary chunk boundaries identical, USB/LSB conjugate, restart, overdrive and DAC limits");
}

static void test_ssb_stopband_and_delay(void) {
    ra_tx_config_t c = defaults(RA_TX_MODE_USB);
    c.sample_rate_hz = RA_TX_SSB_RATE;
    const unsigned frequencies[] = {3500, 4000, 5000, 5500};
    for (unsigned f = 0; f < sizeof(frequencies) / sizeof(frequencies[0]); ++f) {
        ra_tx_ssb_state_t s;
        ra_tx_core_ssb_reset(&s);
        double energy = 0;
        for (unsigned n = 0; n < 4096; ++n) {
            uint16_t i, q;
            uint16_t adc = (uint16_t)(2048 + lround(1200 * cos(2 * PI * frequencies[f] * n / RA_TX_SSB_RATE)));
            ra_tx_core_ssb_sample(&s, &c, adc, &i, &q);
            if (n >= 1024) {
                energy += ((double)i - 2048) * ((double)i - 2048) + ((double)q - 2048) * ((double)q - 2048);
            }
        }
        CHECK(sqrt(energy / 3072) < 1.0);
    }
    ra_tx_ssb_state_t s;
    ra_tx_core_ssb_reset(&s);
    uint16_t i, q, impulse_i[RA_TX_SSB_TAPS], impulse_q[RA_TX_SSB_TAPS];
    ra_tx_core_ssb_sample(&s, &c, 2048, &i, &q);
    for (unsigned n = 0; n < RA_TX_SSB_TAPS; ++n) {
        ra_tx_core_ssb_sample(&s, &c, n == 0 ? 3072 : 2048, &impulse_i[n], &impulse_q[n]);
    }
    for (unsigned n = 0; n < RA_TX_SSB_TAPS; ++n) {
        CHECK(impulse_i[n] == impulse_i[RA_TX_SSB_TAPS - 1U - n]);
        CHECK((unsigned)impulse_q[n] + impulse_q[RA_TX_SSB_TAPS - 1U - n] == 4096U);
    }
    puts("PASS SSB stopband and matched I/Q delay: 3.5..5.5 kHz rejection, 127-sample group delay");
}

int main(void) {
    test_validation();
    test_lut_contract();
    test_am_lut();
    test_fm_lut_and_address_model();
    test_doc_bias_and_phase_model();
    test_ramps();
    test_integer_ramp_scaling();
    test_ssb_validation_and_dc();
    test_ssb_sidebands();
    test_ssb_stream_continuity_and_limits();
    test_ssb_stopband_and_delay();
    printf("PASS actual C TX core: 11 groups, %lu checks\n", checks);
    return 0;
}
