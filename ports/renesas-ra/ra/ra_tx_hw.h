/* RA6M3 TX: autonomous legacy CW/AM/raw-FM; bounded C audio AM/FM/SSB. */
#ifndef MICROPY_INCLUDED_RENESAS_RA_TX_HW_H
#define MICROPY_INCLUDED_RENESAS_RA_TX_HW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef MICROPY_HW_ENABLE_TX
#define MICROPY_HW_ENABLE_TX (0)
#endif

typedef enum {
    RA_TX_MODE_CW = 0,
    RA_TX_MODE_AM = 1,
    RA_TX_MODE_FM = 2,
    RA_TX_MODE_USB = 3,
    RA_TX_MODE_LSB = 4,
} ra_tx_mode_t;

typedef enum {
    RA_TX_ERROR_NONE = 0,
    RA_TX_ERROR_CONFIG,
    RA_TX_ERROR_BUSY,
    RA_TX_ERROR_TIMER,
    RA_TX_ERROR_ADC,
    RA_TX_ERROR_DTC,
    RA_TX_ERROR_STOP_TIMEOUT,
    RA_TX_ERROR_DSP_DEADLINE,
} ra_tx_error_t;

typedef struct {
    ra_tx_mode_t mode;
    uint32_t sample_rate_hz;
    uint16_t amplitude;
    uint16_t adc_mid;
    uint16_t i_zero;
    uint16_t q_zero;
    uint16_t ramp_samples;
    uint8_t fm_gain;
    uint16_t deviation_hz; /* FM: 0 = legacy DOC path; positive = C voice path */
    uint16_t mic_gain;     /* voice FM only: percent, 100 = unity */
    uint16_t audio_gain;   /* opt-in AM/SSB AF gain percent, 100 = unity */
    uint8_t am_depth;      /* normalized AM depth percent at full-scale AF */
    bool audio_controls;  /* false preserves legacy AM and SSB API behavior */
    bool file_source;     /* selected AF comes only from the SDRIQ decoder */
    bool gen_source;      /* direct AF DDS: no ADC and no FILE decoder */
    uint16_t gen_frequency_dhz; /* 0.1 Hz units */
    uint16_t gen_level;    /* percent of signed AF full scale */
    uint8_t gen_wave;      /* ra_tone_wave_t */
} ra_tx_config_t;

typedef struct {
    bool owned;
    bool running;
    bool keyed;
    bool quiesced;
    bool outputs_enabled;
    bool i_enabled;
    bool q_enabled;
    bool cpu_dsp;
    ra_tx_mode_t mode;
    ra_tx_error_t error;
    int32_t fsp_error;
    int8_t timer_channel;
    uint32_t requested_rate_hz;
    uint32_t timer_clock_hz;
    uint32_t timer_period;
    uint16_t last_adc;
    uint16_t phase;
    uint16_t i_code;
    uint16_t q_code;
    uint8_t transfer_count;
    uint32_t unexpected_irqs;
    uint32_t dsp_samples;
    uint32_t dsp_last_cycles;
    uint32_t dsp_max_cycles;
    uint32_t dsp_budget_cycles;
    uint32_t dsp_deadline_misses;
    uint32_t dsp_clips;
    uint32_t adc_rails;
    uint16_t dc_estimate;
    uint16_t audio_peak;
    uint32_t af_frames;      /* completed MIC frames claimed by the foreground */
    int32_t af_error;        /* monitor FSP error; independent of modulation */
    bool af_enabled;
    bool file_source;
    uint32_t file_underruns;
} ra_tx_status_t;

#define RA_TX_AM_LUT_BYTES (8192U)
#define RA_TX_AM_LUT_ALIGNMENT (8192U)
#define RA_TX_FM_LUT_BYTES (1024U)
#define RA_TX_FM_LUT_ALIGNMENT (1024U)
#define RA_TX_MAX_RAMP_SAMPLES (256U)
#define RA_TX_SSB_RATE (12000U)

static inline bool ra_tx_mode_is_ssb(ra_tx_mode_t mode) {
    return mode == RA_TX_MODE_USB || mode == RA_TX_MODE_LSB;
}

static inline bool ra_tx_is_voice_fm(const ra_tx_config_t *config) {
    return config->mode == RA_TX_MODE_FM && config->deviation_hz != 0;
}

static inline bool ra_tx_uses_cpu(const ra_tx_config_t *config) {
    return config->file_source || config->gen_source || ra_tx_mode_is_ssb(config->mode) || ra_tx_is_voice_fm(config) ||
           (config->mode == RA_TX_MODE_AM && config->audio_controls);
}

static inline bool ra_tx_software_source(const ra_tx_config_t *config) {
    return config->file_source || config->gen_source;
}

#if defined(RA6M3) && MICROPY_HW_ENABLE_TX
bool ra_tx_hw_init(const ra_tx_config_t *config, uint8_t *lut, size_t lut_bytes);
bool ra_tx_hw_start(void);
bool ra_tx_hw_stop(void);
bool ra_tx_hw_key(bool down);
bool ra_tx_hw_fm_configure(const ra_tx_config_t *config);
bool ra_tx_hw_audio_configure(const ra_tx_config_t *config);
bool ra_tx_hw_gen_configure(const ra_tx_config_t *config);
bool ra_tx_hw_deinit_checked(void);
bool ra_tx_hw_owns_adc(void);
bool ra_tx_hw_owns_dac(void);
bool ra_tx_hw_owns_resources(void);
uint32_t ra_tx_hw_adc_epoch(void);
void ra_tx_hw_get_status(ra_tx_status_t *status);
bool ra_tx_hw_scope_enable(bool on);
/* Borrowed signed AF: MIC codes around adc_mid or the decoded FILE audio
 * actually passed to the modulator. The next frame claim ends the loan. */
bool ra_tx_hw_scope_frame(const int16_t **samples, size_t *n, uint32_t *rate_hz);
#else
static inline bool ra_tx_hw_owns_adc(void) { return false; }
static inline bool ra_tx_hw_owns_dac(void) { return false; }
static inline bool ra_tx_hw_owns_resources(void) { return false; }
static inline uint32_t ra_tx_hw_adc_epoch(void) { return 0; }
static inline bool ra_tx_hw_deinit_checked(void) { return true; }
#endif

#endif
