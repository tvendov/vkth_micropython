/* Float only at setup; sample processing and live FM updates use integer math. */
#include <math.h>
#include <string.h>

#include "ra_tx_core.h"
#include "ra_tx_ssb_coeffs.h"
#include "ra_tone.h"

#define RA_TX_DAC_MAX (4095U)
#define RA_TX_PI_F (3.14159265358979323846f)

static void ra_tx_core_put_u16(uint8_t *where, uint16_t value) {
    where[0] = (uint8_t)value;
    where[1] = (uint8_t)(value >> 8);
}

bool ra_tx_core_validate(const ra_tx_config_t *config) {
    if (config == NULL || config->sample_rate_hz < 8000U || config->sample_rate_hz > 48000U
        || (config->amplitude == 0U && !ra_tx_is_voice_fm(config) && !config->audio_controls)
        || config->adc_mid == 0U || config->adc_mid >= RA_TX_DAC_MAX
        || config->i_zero > RA_TX_DAC_MAX || config->q_zero > RA_TX_DAC_MAX
        || config->ramp_samples == 0U || config->ramp_samples > RA_TX_MAX_RAMP_SAMPLES
        || (config->fm_gain != 1U && config->fm_gain != 2U)) {
        return false;
    }
    if (config->audio_controls &&
        ((config->mode != RA_TX_MODE_AM && !ra_tx_mode_is_ssb(config->mode)) ||
        config->audio_gain > 1600U || config->am_depth > 100U)) {
        return false;
    }
    if (config->audio_cutoff && (!config->audio_controls ||
        config->audio_cutoff < 1800U || config->audio_cutoff > 6000U ||
        (ra_tx_mode_is_ssb(config->mode) && config->audio_cutoff > 3000U) ||
        2U * config->audio_cutoff >= config->sample_rate_hz)) {
        return false;
    }
    if (config->deviation_hz != 0 &&
        (config->mode != RA_TX_MODE_FM || config->deviation_hz < 100U ||
        config->deviation_hz > 5000U || config->mic_gain > 1600U ||
        2U * (config->deviation_hz + 3000U) >= config->sample_rate_hz)) {
        return false;
    }

    uint32_t amplitude = config->amplitude;
    if (config->gen_source && (config->file_source || config->adc_mid != 2048U ||
        config->mode == RA_TX_MODE_CW || !config->gen_frequency_dhz ||
        config->gen_frequency_dhz > 30000U || config->gen_level > 100U ||
        config->gen_wave > RA_TONE_TRIANGLE ||
        (config->mode == RA_TX_MODE_AM && !config->audio_controls) ||
        (config->mode == RA_TX_MODE_FM && !ra_tx_is_voice_fm(config)))) {
        return false;
    }
    if (config->file_source && (config->adc_mid != 2048U || config->mode == RA_TX_MODE_CW ||
        (ra_tx_mode_is_ssb(config->mode) ? config->sample_rate_hz != RA_TX_SSB_RATE :
         config->sample_rate_hz != 24000U) ||
        (config->mode == RA_TX_MODE_FM && !ra_tx_is_voice_fm(config)))) {
        return false;
    }
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
    if (config->mode == RA_TX_MODE_AM && !ra_tx_uses_cpu(config)) {
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
    if (config->mode == RA_TX_MODE_AM && !ra_tx_uses_cpu(config)) {
        return RA_TX_AM_LUT_ALIGNMENT;
    }
    if (config->mode == RA_TX_MODE_FM) {
        return RA_TX_FM_LUT_ALIGNMENT;
    }
    return 1;
}

static bool tx_build_iq_lut(uint8_t *bank, size_t bytes, int32_t magnitude,
    int32_t i_zero, int32_t q_zero) {
    if (bank == NULL || bytes < RA_TX_FM_LUT_BYTES) {
        return false;
    }
    for (uint32_t index = 0; index < 256U; ++index) {
        float angle = (2.0f * RA_TX_PI_F * (float)index) / 256.0f;
        int32_t i_code = i_zero + lroundf(magnitude * cosf(angle));
        int32_t q_code = q_zero + lroundf(magnitude * sinf(angle));
        /* I and Q occupy separate 512-byte planes, including for the DTC path. */
        ra_tx_core_put_u16(bank + 2U * index, (uint16_t)i_code);
        ra_tx_core_put_u16(bank + 0x200U + 2U * index, (uint16_t)q_code);
    }
    return true;
}

bool ra_tx_core_build_iq_lut(uint8_t *bank, size_t bytes) {
    return tx_build_iq_lut(bank, bytes, 32767, 0, 0);
}

uint16_t ra_tx_core_am_sample(const ra_tx_config_t *config, uint16_t adc, uint32_t *clips) {
    int32_t audio = (int32_t)(adc > RA_TX_DAC_MAX ? RA_TX_DAC_MAX : adc) - config->adc_mid;
    int32_t envelope;
    if (config->audio_controls) {
        /* Keep intermediates bounded in int32: <=4094*1600, then <=65504*100.
         * Limit modulation BEFORE output scaling, so LEVEL never changes depth. */
        audio = (audio * config->audio_gain / 100) * config->am_depth / 100;
        if (audio < -2048 || audio > 2048) {
            if (clips != NULL) { ++*clips; }
            audio = audio < 0 ? -2048 : 2048;
        }
        envelope = ((2048 + audio) * config->amplitude + 1024) / 2048;
    } else {
        envelope = (int32_t)config->amplitude + audio;
        int32_t maximum = 2 * (int32_t)config->amplitude;
        if (envelope < 0 || envelope > maximum) {
            if (clips != NULL) { ++*clips; }
            envelope = envelope < 0 ? 0 : maximum;
        }
    }
    return (uint16_t)(config->i_zero + envelope);
}

bool ra_tx_core_build_lut(const ra_tx_config_t *config, uint8_t *bank, size_t bytes) {
    if (!ra_tx_core_validate(config)) {
        return false;
    }
    if (ra_tx_core_lut_bytes(config) == 0) {
        return true;
    }
    size_t required = ra_tx_core_lut_bytes(config);
    if (bank == NULL || bytes < required) {
        return false;
    }

    if (config->mode == RA_TX_MODE_AM) {
        for (uint32_t adc = 0; adc <= RA_TX_DAC_MAX; ++adc) {
            ra_tx_core_put_u16(bank + 2U * adc, ra_tx_core_am_sample(config, (uint16_t)adc, NULL));
        }
    } else {
        return ra_tx_is_voice_fm(config) ? ra_tx_core_build_iq_lut(bank, bytes) :
               tx_build_iq_lut(bank, bytes, config->amplitude, config->i_zero, config->q_zero);
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

static int32_t tx_fm_round(int64_t value, unsigned bits) {
    int64_t half = (int64_t)1 << (bits - 1U);
    return (int32_t)(value >= 0 ? (value + half) >> bits : -((-value + half) >> bits));
}

bool ra_tx_core_af_prepare(ra_tx_af_config_t *out, uint16_t cutoff,
    uint32_t clock_hz, uint32_t period) {
    if (!out || !clock_hz || !period || period > 65536U ||
        (cutoff && (cutoff < 1800U || cutoff > 6000U ||
                    2ULL * cutoff * period >= clock_hz))) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->cutoff = cutoff;
    if (!cutoff) { return true; }
    const float q[3] = {0.70710678118f, 0.54119610015f, 1.30656296488f};
    float fs = (float)clock_hz / period;
    for (unsigned k = 0; k < 3; ++k) {
        float w = 2.0f * RA_TX_PI_F * (k ? cutoff : 100.0f) / fs;
        float cosine = cosf(w), alpha = sinf(w) / (2.0f * q[k]);
        float norm = 1.0f / (1.0f + alpha);
        int32_t *c = out->coefficient[k];
        c[0] = lroundf((1.0f + (k ? -cosine : cosine)) * 0.5f * norm * 268435456.0f);
        c[1] = (k ? 2 : -2) * c[0];
        c[2] = c[0];
        c[3] = lroundf(-2.0f * cosine * norm * 268435456.0f);
        c[4] = lroundf((1.0f - alpha) * norm * 268435456.0f);
    }
    return true;
}

bool ra_tx_core_af_reset(ra_tx_af_state_t *state, uint16_t cutoff,
    uint32_t clock_hz, uint32_t period) {
    if (!state) { return false; }
    memset(state, 0, sizeof(*state));
    if (!ra_tx_core_af_prepare(&state->bank[0].config, cutoff, clock_hz, period)) {
        return false;
    }
    uint64_t divisor = 50ULL * period;
    uint64_t length = ((uint64_t)clock_hz + divisor - 1U) / divisor;
    if (!length || length > UINT16_MAX) { return false; }
    state->fade_length = (uint16_t)length;
    return true;
}

void ra_tx_core_af_request(ra_tx_af_state_t *state, const ra_tx_af_config_t *prepared) {
    /* Caller serializes this bounded copy against the sample ISR. */
    state->pending = *prepared;
    state->pending_valid = true;
}

static int32_t tx_af_bank_sample(ra_tx_af_bank_t *bank, int32_t input) {
    if (!bank->config.cutoff) { return input; }
    if (!bank->primed) {
        memset(bank->history, 0, sizeof(bank->history));
        bank->history[0][0] = bank->history[0][1] = input;
        bank->primed = true; /* high-pass starts at the input's DC equilibrium */
    }
    for (unsigned k = 0; k < 3; ++k) {
        const int32_t *c = bank->config.coefficient[k];
        int32_t *h = bank->history[k];
        int32_t y = tx_fm_round((int64_t)c[0] * input + (int64_t)c[1] * h[0] +
            (int64_t)c[2] * h[1] - (int64_t)c[3] * h[2] - (int64_t)c[4] * h[3], 28);
        h[1] = h[0]; h[0] = input;
        h[3] = h[2]; h[2] = y;
        input = y;
    }
    return input;
}

uint16_t ra_tx_core_af_sample(ra_tx_af_state_t *state, uint16_t raw, uint16_t midpoint) {
    if (!state->fade_position && state->pending_valid) {
        if (state->pending.cutoff != state->bank[state->active].config.cutoff) {
            ra_tx_af_bank_t *next = &state->bank[state->active ^ 1U];
            next->config = state->pending;
            next->primed = false;
            state->fade_position = 1;
        }
        state->pending_valid = false;
    }
    /* Bypass is bit-identical to the old stream, including custom ADC midpoint. */
    if (!state->fade_position && !state->bank[state->active].config.cutoff) {
        return raw;
    }
    int32_t input = ((int32_t)(raw > RA_TX_DAC_MAX ? RA_TX_DAC_MAX : raw) - midpoint) * 4096;
    int32_t output = tx_af_bank_sample(&state->bank[state->active], input);
    if (state->fade_position) {
        int32_t next = tx_af_bank_sample(&state->bank[state->active ^ 1U], input);
        output += (int32_t)((int64_t)(next - output) * state->fade_position / state->fade_length);
        if (state->fade_position++ == state->fade_length) {
            state->active ^= 1U;
            state->fade_position = 0;
        }
    }
    int32_t code = midpoint + tx_fm_round(output, 12);
    if (code < 0 || code > (int32_t)RA_TX_DAC_MAX) {
        ++state->clips;
        code = code < 0 ? 0 : RA_TX_DAC_MAX;
    }
    return (uint16_t)code;
}

uint32_t ra_tx_core_fm_step(uint32_t clock_hz, uint32_t period, uint16_t deviation_hz) {
    /* Validate the actual timer ratio, not just the requested rate. This margin
     * is a rate/voice-band guard, not a guarantee of RF spectral compliance. */
    if (clock_hz == 0 || period == 0 || period > 65536U ||
        deviation_hz < 100U || deviation_hz > 5000U ||
        2ULL * (deviation_hz + 3000U) * period >= clock_hz) {
        return 0;
    }
    return (uint32_t)((((uint64_t)deviation_hz * period << 32) + clock_hz / 2U) / clock_hz);
}

bool ra_tx_core_fm_reset(ra_tx_fm_state_t *state, const ra_tx_config_t *config,
    uint32_t clock_hz, uint32_t period) {
    if (state == NULL || !ra_tx_core_validate(config) || !ra_tx_is_voice_fm(config)) {
        return false;
    }
    uint32_t step = ra_tx_core_fm_step(clock_hz, period, config->deviation_hz);
    if (step == 0) {
        return false;
    }
    memset(state, 0, sizeof(*state));
    state->max_step = step;
    float fs = (float)clock_hz / period;
    state->dc_alpha = lroundf((1.0f - expf(-2.0f * RA_TX_PI_F * 30.0f / fs)) * 1073741824.0f);
    float w = 2.0f * RA_TX_PI_F * 3000.0f / fs;
    float cosine = cosf(w), alpha = sinf(w) * 0.70710678118f;
    float norm = 1.0f / (1.0f + alpha);
    state->b0 = lroundf((1.0f - cosine) * 0.5f * norm * 268435456.0f);
    state->b1 = 2 * state->b0;
    state->b2 = state->b0;
    state->a1 = lroundf(-2.0f * cosine * norm * 268435456.0f);
    state->a2 = lroundf((1.0f - alpha) * norm * 268435456.0f);
    return true;
}

static int32_t tx_fm_lut(const uint8_t *plane, unsigned index) {
    unsigned code = (unsigned)plane[2 * index] | ((unsigned)plane[2 * index + 1] << 8);
    return code >= 32768U ? (int32_t)code - 65536 : (int32_t)code;
}

static uint16_t tx_fm_output(const uint8_t *plane, uint32_t phase, uint16_t zero, uint16_t amplitude) {
    unsigned index = phase >> 24;
    int32_t first = tx_fm_lut(plane, index);
    int32_t next = tx_fm_lut(plane, (index + 1U) & 255U);
    int32_t wave = first + tx_fm_round((int64_t)(next - first) * ((phase >> 8) & 65535U), 16);
    return (uint16_t)(zero + tx_fm_round((int64_t)wave * amplitude, 15));
}

void ra_tx_core_iq_sample(const uint8_t *lut, uint32_t phase,
    uint16_t i_zero, uint16_t q_zero, uint16_t amplitude,
    uint16_t *i_code, uint16_t *q_code) {
    *i_code = tx_fm_output(lut, phase, i_zero, amplitude);
    *q_code = tx_fm_output(lut + 512, phase, q_zero, amplitude);
}

void ra_tx_core_fm_sample(ra_tx_fm_state_t *state, const ra_tx_config_t *config,
    const uint8_t *lut, uint16_t adc, uint16_t *i_code, uint16_t *q_code) {
    /* Called only with a prepared context/validated config and immutable LUT.
     * First input primes DC, suppressing startup from arbitrary microphone bias.
     * This does not reset phase or history at later buffer/control boundaries. */
    if (adc >= RA_TX_DAC_MAX || adc == 0) {
        ++state->adc_rails;
        if (adc > RA_TX_DAC_MAX) {
            adc = RA_TX_DAC_MAX;
        }
    }
    int32_t input = (int32_t)adc * 4096;
    if (!state->primed) {
        state->dc = input;
        state->primed = true;
    }
    state->dc += tx_fm_round((int64_t)(input - state->dc) * state->dc_alpha, 30);
    int32_t x = input - state->dc;
    int32_t y = tx_fm_round((int64_t)state->b0 * x + (int64_t)state->b1 * state->x1 +
        (int64_t)state->b2 * state->x2 - (int64_t)state->a1 * state->y1 -
        (int64_t)state->a2 * state->y2, 28);
    state->x2 = state->x1;
    state->x1 = x;
    state->y2 = state->y1;
    state->y1 = y;
    int32_t audio = (int32_t)((int64_t)y * config->mic_gain / 100);
    const int32_t limit = 2048 * 4096;
    if (audio > limit || audio < -limit) {
        audio = audio > 0 ? limit : -limit;
        ++state->clips;
    }
    state->last_audio = (int16_t)tx_fm_round(audio, 12);
    unsigned peak = state->last_audio < 0 ? -state->last_audio : state->last_audio;
    if (peak > state->audio_peak) {
        state->audio_peak = peak;
    }
    int32_t delta = tx_fm_round((int64_t)audio * state->max_step, 23);
    state->phase += (uint32_t)delta;
    /* Linear interpolation improves phase resolution without increasing LUT RAM.
     * Output level never enters the phase/deviation calculation. */
    ra_tx_core_iq_sample(lut, state->phase, config->i_zero, config->q_zero,
        config->amplitude, i_code, q_code);
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
    if (config->audio_controls) {
        /* Gain before the shared I/Q limiter; output LEVEL remains independent.
         * Do not reset the Hilbert history when a control changes. */
        i = i * config->audio_gain / 100;
        q = q * config->audio_gain / 100;
    }
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
