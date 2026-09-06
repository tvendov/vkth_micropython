/* Floating-point work runs at setup; key-edge scaling uses integer math. */
#include <math.h>
#include <string.h>

#include "ra_tx_core.h"
#include "ra_tx_ssb_coeffs.h"

#define RA_TX_DAC_MAX (4095U)
#define RA_TX_PI_F (3.14159265358979323846f)

static void ra_tx_core_put_u16(uint8_t *where, uint16_t value) {
    where[0] = (uint8_t)value;
    where[1] = (uint8_t)(value >> 8);
}

bool ra_tx_core_validate(const ra_tx_config_t *config) {
    if (config == NULL || config->sample_rate_hz < 8000U || config->sample_rate_hz > 48000U
        || config->amplitude == 0U || config->adc_mid == 0U || config->adc_mid >= RA_TX_DAC_MAX
        || config->i_zero > RA_TX_DAC_MAX || config->q_zero > RA_TX_DAC_MAX
        || config->ramp_samples == 0U || config->ramp_samples > RA_TX_MAX_RAMP_SAMPLES
        || (config->fm_gain != 1U && config->fm_gain != 2U)) {
        return false;
    }

    uint32_t amplitude = config->amplitude;
    switch (config->mode) {
        case RA_TX_MODE_CW:
            return amplitude <= RA_TX_DAC_MAX - config->i_zero;
        case RA_TX_MODE_AM:
            return 2U * amplitude <= RA_TX_DAC_MAX - config->i_zero;
        case RA_TX_MODE_FM:
            return amplitude <= config->i_zero && amplitude <= RA_TX_DAC_MAX - config->i_zero
                   && amplitude <= config->q_zero && amplitude <= RA_TX_DAC_MAX - config->q_zero;
        case RA_TX_MODE_USB:
        case RA_TX_MODE_LSB:
            return config->sample_rate_hz == RA_TX_SSB_RATE
                   && amplitude <= config->i_zero && amplitude <= RA_TX_DAC_MAX - config->i_zero
                   && amplitude <= config->q_zero && amplitude <= RA_TX_DAC_MAX - config->q_zero;
        default:
            return false;
    }
}

size_t ra_tx_core_lut_bytes(const ra_tx_config_t *config) {
    if (config == NULL) {
        return 0;
    }
    if (config->mode == RA_TX_MODE_AM) {
        return RA_TX_AM_LUT_BYTES;
    }
    if (config->mode == RA_TX_MODE_FM) {
        return RA_TX_FM_LUT_BYTES;
    }
    return 0;
}

size_t ra_tx_core_lut_alignment(const ra_tx_config_t *config) {
    if (config == NULL) {
        return 1;
    }
    if (config->mode == RA_TX_MODE_AM) {
        return RA_TX_AM_LUT_ALIGNMENT;
    }
    if (config->mode == RA_TX_MODE_FM) {
        return RA_TX_FM_LUT_ALIGNMENT;
    }
    return 1;
}

bool ra_tx_core_build_lut(const ra_tx_config_t *config, uint8_t *bank, size_t bytes) {
    if (!ra_tx_core_validate(config)) {
        return false;
    }
    if (config->mode == RA_TX_MODE_CW || ra_tx_mode_is_ssb(config->mode)) {
        return true;
    }
    size_t required = ra_tx_core_lut_bytes(config);
    if (bank == NULL || bytes < required) {
        return false;
    }

    if (config->mode == RA_TX_MODE_AM) {
        for (uint32_t adc = 0; adc <= RA_TX_DAC_MAX; ++adc) {
            int32_t envelope = (int32_t)config->amplitude + (int32_t)adc - config->adc_mid;
            int32_t maximum = 2 * (int32_t)config->amplitude;
            if (envelope < 0) {
                envelope = 0;
            } else if (envelope > maximum) {
                envelope = maximum;
            }
            ra_tx_core_put_u16(bank + 2U * adc, (uint16_t)(config->i_zero + envelope));
        }
    } else {
        for (uint32_t index = 0; index < 256U; ++index) {
            float angle = (2.0f * RA_TX_PI_F * (float)index) / 256.0f;
            int32_t i_code = (int32_t)config->i_zero + lroundf(config->amplitude * cosf(angle));
            int32_t q_code = (int32_t)config->q_zero + lroundf(config->amplitude * sinf(angle));
            /* Planar layout keeps the DTC address builder short: I occupies
             * the first 512 bytes and Q the second 512 bytes. */
            ra_tx_core_put_u16(bank + 2U * index, (uint16_t)i_code);
            ra_tx_core_put_u16(bank + 0x200U + 2U * index, (uint16_t)q_code);
        }
    }
    return true;
}

void ra_tx_core_ramp(uint16_t *out, uint16_t n, uint16_t from, uint16_t to) {
    if (out == NULL || n == 0U) {
        return;
    }
    int32_t change = (int32_t)to - from;
    int32_t minimum = from < to ? from : to;
    int32_t maximum = from > to ? from : to;
    for (uint32_t j = 1; j < n; ++j) {
        float position = (float)j / n;
        float weight = 0.5f * (1.0f - cosf(RA_TX_PI_F * position));
        int32_t code = (int32_t)from + lroundf(change * weight);
        if (code < minimum) {
            code = minimum;
        } else if (code > maximum) {
            code = maximum;
        }
        out[j - 1U] = (uint16_t)code;
    }
    out[n - 1U] = to;
}

void ra_tx_core_scale_ramp(uint16_t *out, const uint16_t *shape, uint16_t n, uint16_t from, uint16_t to) {
    if (out == NULL || shape == NULL || n == 0U) {
        return;
    }
    int32_t change = (int32_t)to - from;
    uint32_t magnitude = change < 0 ? (uint32_t)(-change) : (uint32_t)change;
    for (uint32_t j = 0; j + 1U < n; ++j) {
        /* Magnitude arithmetic also handles the full uint16_t endpoint
         * range: 65535*65535+32767 fits uint32_t. The DAC range is smaller.
         * Apply the sign after rounding, so up/down transitions are symmetric.
         */
        uint32_t scaled = (magnitude * shape[j] + 32767U) / 65535U;
        out[j] = change < 0 ? (uint16_t)(from - scaled) : (uint16_t)(from + scaled);
    }
    out[n - 1U] = to;
}

uint16_t ra_tx_core_fm_bias(const ra_tx_config_t *config) {
    if (config == NULL) {
        return 0;
    }
    return (uint16_t)(0U - (uint32_t)config->fm_gain * config->adc_mid);
}

void ra_tx_core_ssb_reset(ra_tx_ssb_state_t *state) {
    memset(state, 0, sizeof(*state));
}

static int32_t tx_ssb_round_shift(int32_t value, unsigned bits) {
    /* Symmetric rounding, without relying on negative right-shift semantics. */
    int32_t half = (int32_t)1 << (bits - 1U);
    return value >= 0 ? (value + half) >> bits : -((-value + half) >> bits);
}

void ra_tx_core_ssb_sample(ra_tx_ssb_state_t *state, const ra_tx_config_t *config,
    uint16_t adc, uint16_t *i_code, uint16_t *q_code) {
    if (adc > RA_TX_DAC_MAX) {
        adc = RA_TX_DAC_MAX;
    }
    int16_t input = (int16_t)((int32_t)adc - config->adc_mid);
    if (!state->primed) {
        /* Suppress startup transients from the microphone's constant bias. */
        for (unsigned k = 0; k < RA_TX_SSB_RING; ++k) {
            state->history[k] = input;
        }
        state->primed = true;
    }
    unsigned pos = state->next;
    state->history[pos] = input;
    state->next = (uint16_t)((pos + 1U) & (RA_TX_SSB_RING - 1U));
    int32_t acc_i = 0, acc_q = 0;
    for (unsigned k = 0; k < RA_TX_SSB_TAPS / 2U; ++k) {
        int32_t a = state->history[(pos - k) & (RA_TX_SSB_RING - 1U)];
        int32_t b = state->history[(pos - (RA_TX_SSB_TAPS - 1U - k)) & (RA_TX_SSB_RING - 1U)];
        acc_i += tx_ssb_i_q15[k] * (a + b);
        acc_q += tx_ssb_q_q15[k] * (a - b);
    }
    acc_i += tx_ssb_i_q15[RA_TX_SSB_TAPS / 2U]
             * state->history[(pos - RA_TX_SSB_TAPS / 2U) & (RA_TX_SSB_RING - 1U)];
    int32_t i = tx_ssb_round_shift(acc_i, 15);
    int32_t q = tx_ssb_round_shift(acc_q, 15);
    int32_t peak_i = i < 0 ? -i : i;
    int32_t peak_q = q < 0 ? -q : q;
    int32_t peak = peak_i > peak_q ? peak_i : peak_q;
    if (peak > 2048) {
        i = i * 2048 / peak;
        q = q * 2048 / peak;
        ++state->clips;
    }
    if (config->mode == RA_TX_MODE_LSB) {
        q = -q;
    }
    *i_code = (uint16_t)(config->i_zero + tx_ssb_round_shift(i * config->amplitude, 11));
    *q_code = (uint16_t)(config->q_zero + tx_ssb_round_shift(q * config->amplitude, 11));
}
