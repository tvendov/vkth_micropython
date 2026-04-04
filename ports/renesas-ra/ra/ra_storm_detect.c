#include <math.h>
#include <string.h>

#include "ra_storm_adc.h"
#include "ra_storm_detect.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

#define RA_STORM_MIN_TRACK_FRAMES         (6U)
#define RA_STORM_MAX_TRACK_FRAMES         (96U)
#define RA_STORM_NOISE_INIT               (64.0f)
#define RA_STORM_NOISE_ALPHA              (0.02f)
#define RA_STORM_TRIGGER_RMS_FACTOR       (3.5f)
#define RA_STORM_TRIGGER_PEAK_FACTOR      (4.0f)
#define RA_STORM_TRIGGER_LFHF             (1.2f)
#define RA_STORM_CONFIRM_MIN_SCORE        (2.7f)
#define RA_STORM_POSSIBLE_MIN_SCORE       (1.6f)
#define RA_STORM_CONFIRM_MIN_DURATION_S   (0.09f)
#define RA_STORM_COOLDOWN_S               (0.25f)

static const float s_goertzel_freqs[] = {63.0f, 125.0f, 188.0f, 250.0f, 375.0f, 500.0f, 625.0f};

static ra_storm_detect_status_t s_status;
static float s_peak_rms;
static bool s_decay_seen;
static uint32_t s_track_frames;
static uint32_t s_cooldown_frames;

static float ra_storm_goertzel(const int16_t *samples, size_t count, uint32_t sample_rate_hz, float target_hz) {
    float coeff = 2.0f * cosf(2.0f * (float)M_PI * target_hz / (float)sample_rate_hz);
    float q0 = 0.0f;
    float q1 = 0.0f;
    float q2 = 0.0f;

    for (size_t i = 0; i < count; ++i) {
        q0 = (float)samples[i] + coeff * q1 - q2;
        q2 = q1;
        q1 = q0;
    }

    float power = q1 * q1 + q2 * q2 - coeff * q1 * q2;
    return power > 0.0f ? power : 0.0f;
}

static void ra_storm_finish_event(bool detected) {
    if (detected) {
        s_status.detected = 1U;
        s_status.event_count++;
    }
    s_status.state = RA_STORM_DETECT_STATE_COOLDOWN;
    s_status.tracking = 0U;
    s_status.possible = 0U;
    s_cooldown_frames = 0U;
    s_track_frames = 0U;
    s_peak_rms = 0.0f;
    s_decay_seen = false;
}

void ra_storm_detect_reset(void) {
    memset(&s_status, 0, sizeof(s_status));
    s_status.initialised = 1U;
    s_status.noise_floor = RA_STORM_NOISE_INIT;
    s_peak_rms = 0.0f;
    s_decay_seen = false;
    s_track_frames = 0U;
    s_cooldown_frames = 0U;
}

void ra_storm_detect_process_frame(const int16_t *samples, size_t frame_samples, uint32_t sample_rate_hz, uint32_t sequence) {
    if (samples == NULL || frame_samples == 0 || sample_rate_hz == 0) {
        return;
    }
    if (!s_status.initialised) {
        ra_storm_detect_reset();
    }

    if (s_status.last_sequence != 0U && sequence > (s_status.last_sequence + 1U)) {
        s_status.overflow = 1U;
        s_status.dropped_sequences += sequence - (s_status.last_sequence + 1U);
    }
    s_status.last_sequence = sequence;
    s_status.sample_rate_hz = sample_rate_hz;
    s_status.frame_samples = (uint16_t)frame_samples;
    s_status.processed_frames++;

    float sum_sq = 0.0f;
    float peak = 0.0f;
    for (size_t i = 0; i < frame_samples; ++i) {
        float v = (float)samples[i];
        float av = fabsf(v);
        sum_sq += v * v;
        if (av > peak) {
            peak = av;
        }
    }

    float rms = sqrtf(sum_sq / (float)frame_samples);
    float lf = 0.0f;
    float hf = 0.0f;
    for (size_t i = 0; i < sizeof(s_goertzel_freqs) / sizeof(s_goertzel_freqs[0]); ++i) {
        float e = ra_storm_goertzel(samples, frame_samples, sample_rate_hz, s_goertzel_freqs[i]);
        if (i < 4U) {
            lf += e;
        } else {
            hf += e;
        }
    }

    s_status.rms = rms;
    s_status.peak = peak;
    s_status.lf_energy = lf;
    s_status.hf_energy = hf;
    s_status.lf_hf = lf / (hf + 1.0f);
    s_status.score = 0.0f;

    if (s_status.state == RA_STORM_DETECT_STATE_IDLE && rms < (s_status.noise_floor * 2.0f)) {
        s_status.noise_floor = (1.0f - RA_STORM_NOISE_ALPHA) * s_status.noise_floor + RA_STORM_NOISE_ALPHA * rms;
    }

    if (rms > (s_status.noise_floor * RA_STORM_TRIGGER_RMS_FACTOR)) {
        s_status.score += 1.2f;
    }
    if (peak > (s_status.noise_floor * RA_STORM_TRIGGER_PEAK_FACTOR)) {
        s_status.score += 0.8f;
    }
    if (s_status.lf_hf > RA_STORM_TRIGGER_LFHF) {
        s_status.score += 0.9f;
    }

    if (s_status.state == RA_STORM_DETECT_STATE_IDLE) {
        s_status.event_duration_s = 0.0f;
        s_status.event_peak_rms = 0.0f;
        if (s_status.score >= RA_STORM_POSSIBLE_MIN_SCORE) {
            s_status.state = RA_STORM_DETECT_STATE_TRACKING;
            s_status.tracking = 1U;
            s_status.possible = 1U;
            s_track_frames = 1U;
            s_peak_rms = rms;
            s_status.event_duration_s = (float)frame_samples / (float)sample_rate_hz;
            s_status.event_peak_rms = rms;
        }
        return;
    }

    if (s_status.state == RA_STORM_DETECT_STATE_TRACKING) {
        float frame_s = (float)frame_samples / (float)sample_rate_hz;
        s_track_frames++;
        s_status.event_duration_s += frame_s;
        if (rms > s_peak_rms) {
            s_peak_rms = rms;
            s_status.event_peak_rms = rms;
        }
        if (rms < (s_peak_rms * 0.92f)) {
            s_decay_seen = true;
        }

        bool strong_now = s_status.score >= RA_STORM_POSSIBLE_MIN_SCORE;
        bool confirmed = s_status.score >= RA_STORM_CONFIRM_MIN_SCORE &&
            s_status.event_duration_s >= RA_STORM_CONFIRM_MIN_DURATION_S &&
            (s_decay_seen || s_track_frames >= RA_STORM_MIN_TRACK_FRAMES);

        if (confirmed) {
            ra_storm_finish_event(true);
        } else if (!strong_now && s_track_frames >= RA_STORM_MIN_TRACK_FRAMES) {
            ra_storm_finish_event(false);
        } else if (s_track_frames >= RA_STORM_MAX_TRACK_FRAMES) {
            ra_storm_finish_event(s_decay_seen);
        }

        return;
    }

    if (s_status.state == RA_STORM_DETECT_STATE_COOLDOWN) {
        s_cooldown_frames++;
        s_status.event_duration_s = 0.0f;
        if (((float)s_cooldown_frames * (float)frame_samples / (float)sample_rate_hz) >= RA_STORM_COOLDOWN_S) {
            s_status.state = RA_STORM_DETECT_STATE_IDLE;
        }
    }
}

size_t ra_storm_detect_process_ready_adc(void) {
    size_t processed = 0;
    size_t frame_samples;
    uint32_t sequence;
    const int16_t *samples;
    ra_storm_adc_status_t adc_status;

    while ((samples = ra_storm_adc_acquire_ready_buffer(&frame_samples, &sequence)) != NULL) {
        ra_storm_adc_get_status(&adc_status);
        ra_storm_detect_process_frame(samples, frame_samples, adc_status.sample_rate_hz, sequence);
        processed++;
    }

    return processed;
}

bool ra_storm_detected(void) {
    return s_status.detected != 0U;
}

void ra_storm_detect_clear(void) {
    s_status.detected = 0U;
}

void ra_storm_detect_get_status(ra_storm_detect_status_t *status) {
    if (status != NULL) {
        *status = s_status;
    }
}
