/* RA6M3 autonomous baseband transmitter. No per-sample CPU processing. */
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
} ra_tx_mode_t;

typedef enum {
    RA_TX_ERROR_NONE = 0,
    RA_TX_ERROR_CONFIG,
    RA_TX_ERROR_BUSY,
    RA_TX_ERROR_TIMER,
    RA_TX_ERROR_ADC,
    RA_TX_ERROR_DTC,
    RA_TX_ERROR_STOP_TIMEOUT,
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
} ra_tx_config_t;

typedef struct {
    bool owned;
    bool running;
    bool keyed;
    bool quiesced;
    bool outputs_enabled;
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
} ra_tx_status_t;

#define RA_TX_LUT_BYTES (65536U)
#define RA_TX_LUT_ALLOCATION_BYTES (2U * RA_TX_LUT_BYTES - 1U)
#define RA_TX_MAX_RAMP_SAMPLES (256U)

#if defined(RA6M3) && MICROPY_HW_ENABLE_TX
bool ra_tx_hw_init(const ra_tx_config_t *config, uint8_t *lut, size_t lut_bytes);
bool ra_tx_hw_start(void);
bool ra_tx_hw_stop(void);
bool ra_tx_hw_key(bool down);
bool ra_tx_hw_deinit_checked(void);
bool ra_tx_hw_owns_adc(void);
bool ra_tx_hw_owns_dac(void);
bool ra_tx_hw_owns_resources(void);
uint32_t ra_tx_hw_adc_epoch(void);
void ra_tx_hw_get_status(ra_tx_status_t *status);
#else
static inline bool ra_tx_hw_owns_adc(void) { return false; }
static inline bool ra_tx_hw_owns_dac(void) { return false; }
static inline bool ra_tx_hw_owns_resources(void) { return false; }
static inline uint32_t ra_tx_hw_adc_epoch(void) { return 0; }
static inline bool ra_tx_hw_deinit_checked(void) { return true; }
#endif

#endif
