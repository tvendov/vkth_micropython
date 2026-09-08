#ifndef RA_IQ_CAPTURE_H
#define RA_IQ_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Raw-capture contract, independent of FSP register/type headers. Implemented
 * by ra_iq_adc.c when MICROPY_HW_ENABLE_MEASUREMENT is enabled. */
#define RA_IQ_RAW_DATA_LOSS (1U)
typedef void (*ra_iq_raw_consumer_t)(void *context, const uint16_t *a,
    const uint16_t *b, size_t count, uint32_t flags, bool fatal);
bool ra_iq_adc_set_raw_consumer(ra_iq_raw_consumer_t consumer, void *context);
bool ra_iq_adc_raw_owned(void);
float ra_iq_adc_actual_rate(void);

/* Foreground, single-core serialized lifecycle. Reservation is software-only,
 * visible to ordinary ADC/IQADC/TX clients, and survives checked deinit. */
bool ra_iq_capture_reserve(const void *owner);
bool ra_iq_capture_reserved_by(const void *owner);
bool ra_iq_capture_release(const void *owner);
bool ra_iq_capture_open(const void *owner, uint32_t a_pin, uint32_t b_pin,
    uint32_t rate, size_t block, ra_iq_raw_consumer_t sink, void *context);
bool ra_iq_capture_start(const void *owner);
bool ra_iq_capture_running(const void *owner);
bool ra_iq_capture_close(const void *owner);
/* Bounded IRQ-safe halt only; close/release remain foreground operations. */
void ra_iq_capture_halt(const void *owner);

#endif
