/*
 * SCI Simple IIC master driver for Renesas RA MCUs.
 *
 * The driver uses the shared SCI IRQ dispatch and ownership implemented in
 * ra_sci.c. SDA is an SCI TXD pin and SCL is the matching SCI RXD pin.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hal_data.h"
#include "ra_config.h"
#include "ra_gpio.h"
#include "ra_sci.h"
#include "ra_sci_i2c.h"

#if !defined(RA_PRI_SCI_I2C)
#define RA_PRI_SCI_I2C (2)
#endif

#define SCI_I2C_SIMR3_START       (0x51U)
#define SCI_I2C_SIMR3_RESTART     (0x52U)
#define SCI_I2C_SIMR3_STOP        (0x54U)
#define SCI_I2C_SIMR3_SERIAL      (0x00U)
#define SCI_I2C_SIMR3_RELEASE     (0xF0U)
#define SCI_I2C_SCMR_INIT         (0xFAU)
#define SCI_I2C_SIMR2_INIT        (0x23U)
#define SCI_I2C_SIMR2_ACK         (0x00U)
#define SCI_I2C_SIMR2_NACK        (0x20U)
#define SCI_I2C_SCR_TRANSFER      (0xB4U)
#define SCI_I2C_MDDR_MIN          (0x80U)

extern volatile uint32_t uwTick;

typedef enum {
    SCI_I2C_STATE_IDLE = 0,
    SCI_I2C_STATE_START,
    SCI_I2C_STATE_ADDRESS,
    SCI_I2C_STATE_WRITE,
    SCI_I2C_STATE_READ,
    SCI_I2C_STATE_STOP,
    SCI_I2C_STATE_DONE,
    SCI_I2C_STATE_ERROR,
} sci_i2c_state_t;

typedef struct {
    volatile sci_i2c_state_t state;
    volatile int error;
    R_SCI0_Type *reg;
    const uint8_t *tx_buf;
    uint8_t *rx_buf;
    uint32_t sda_pin;
    uint32_t scl_pin;
    uint32_t len;
    volatile uint32_t pos;
    uint16_t addr;
    bool read;
    bool stop;
    bool bus_active;
    bool initialized;
} sci_i2c_context_t;

typedef struct {
    uint8_t cks;
    uint8_t brr;
    uint8_t mddr;
    bool modulation;
} sci_i2c_baud_t;

static sci_i2c_context_t sci_i2c_context[SCI_CH_MAX];

static R_SCI0_Type *sci_i2c_reg(uint32_t ch) {
    switch (ch) {
        #if defined(VECTOR_NUMBER_SCI0_RXI) && defined(VECTOR_NUMBER_SCI0_TXI) && defined(VECTOR_NUMBER_SCI0_TEI) && defined(VECTOR_NUMBER_SCI0_ERI)
        case 0: return R_SCI0;
        #endif
        #if defined(VECTOR_NUMBER_SCI1_RXI) && defined(VECTOR_NUMBER_SCI1_TXI) && defined(VECTOR_NUMBER_SCI1_TEI) && defined(VECTOR_NUMBER_SCI1_ERI)
        case 1: return R_SCI1;
        #endif
        #if defined(VECTOR_NUMBER_SCI2_RXI) && defined(VECTOR_NUMBER_SCI2_TXI) && defined(VECTOR_NUMBER_SCI2_TEI) && defined(VECTOR_NUMBER_SCI2_ERI)
        case 2: return R_SCI2;
        #endif
        #if defined(VECTOR_NUMBER_SCI3_RXI) && defined(VECTOR_NUMBER_SCI3_TXI) && defined(VECTOR_NUMBER_SCI3_TEI) && defined(VECTOR_NUMBER_SCI3_ERI)
        case 3: return R_SCI3;
        #endif
        #if defined(VECTOR_NUMBER_SCI4_RXI) && defined(VECTOR_NUMBER_SCI4_TXI) && defined(VECTOR_NUMBER_SCI4_TEI) && defined(VECTOR_NUMBER_SCI4_ERI)
        case 4: return R_SCI4;
        #endif
        #if defined(VECTOR_NUMBER_SCI5_RXI) && defined(VECTOR_NUMBER_SCI5_TXI) && defined(VECTOR_NUMBER_SCI5_TEI) && defined(VECTOR_NUMBER_SCI5_ERI)
        case 5: return R_SCI5;
        #endif
        #if defined(VECTOR_NUMBER_SCI6_RXI) && defined(VECTOR_NUMBER_SCI6_TXI) && defined(VECTOR_NUMBER_SCI6_TEI) && defined(VECTOR_NUMBER_SCI6_ERI)
        case 6: return R_SCI6;
        #endif
        #if defined(VECTOR_NUMBER_SCI7_RXI) && defined(VECTOR_NUMBER_SCI7_TXI) && defined(VECTOR_NUMBER_SCI7_TEI) && defined(VECTOR_NUMBER_SCI7_ERI)
        case 7: return R_SCI7;
        #endif
        #if defined(VECTOR_NUMBER_SCI8_RXI) && defined(VECTOR_NUMBER_SCI8_TXI) && defined(VECTOR_NUMBER_SCI8_TEI) && defined(VECTOR_NUMBER_SCI8_ERI)
        case 8: return R_SCI8;
        #endif
        #if defined(VECTOR_NUMBER_SCI9_RXI) && defined(VECTOR_NUMBER_SCI9_TXI) && defined(VECTOR_NUMBER_SCI9_TEI) && defined(VECTOR_NUMBER_SCI9_ERI)
        case 9: return R_SCI9;
        #endif
        default: return NULL;
    }
}

static bool sci_i2c_calc_baud(uint32_t freq, sci_i2c_baud_t *setting) {
    if (freq == 0 || freq > RA_SCI_I2C_MAX_FREQ) {
        return false;
    }

    uint32_t pclk = R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_PCLKA);
    uint32_t best_rate = 0;
    sci_i2c_baud_t best = {0};

    for (uint32_t cks = 0; cks < 4; ++cks) {
        uint32_t clock_divisor = 16U << (2U * cks);
        for (uint32_t divider = 1; divider <= 256; ++divider) {
            uint32_t base_rate = pclk / (clock_divisor * divider);
            if (base_rate == 0) {
                continue;
            }

            uint32_t actual_rate;
            uint32_t mddr = 0xff;
            bool modulation = false;
            if (base_rate <= freq) {
                actual_rate = base_rate;
            } else {
                mddr = (uint32_t)(((uint64_t)freq * 256U) / base_rate);
                if (mddr < SCI_I2C_MDDR_MIN || mddr > 0xff) {
                    continue;
                }
                actual_rate = (uint32_t)(((uint64_t)base_rate * mddr) / 256U);
                modulation = true;
            }

            if (actual_rate > best_rate) {
                best_rate = actual_rate;
                best.cks = (uint8_t)cks;
                best.brr = (uint8_t)(divider - 1U);
                best.mddr = (uint8_t)mddr;
                best.modulation = modulation;
            }
        }
    }

    if (best_rate == 0) {
        return false;
    }
    *setting = best;
    return true;
}

static void sci_i2c_finish_without_stop(sci_i2c_context_t *context) {
    context->reg->SCR &= (uint8_t)~(R_SCI0_SCR_TIE_Msk | R_SCI0_SCR_TEIE_Msk);
    context->bus_active = true;
    context->state = SCI_I2C_STATE_DONE;
}

static void sci_i2c_request_stop(sci_i2c_context_t *context) {
    context->state = SCI_I2C_STATE_STOP;
    context->reg->SCR &= (uint8_t)~R_SCI0_SCR_TIE_Msk;
    context->reg->SCR |= R_SCI0_SCR_TEIE_Msk;
    context->reg->SIMR3 = SCI_I2C_SIMR3_STOP;
}

static void sci_i2c_complete_data(sci_i2c_context_t *context) {
    if (context->stop) {
        sci_i2c_request_stop(context);
    } else {
        sci_i2c_finish_without_stop(context);
    }
}

static void sci_i2c_tei_callback(uint32_t ch) {
    sci_i2c_context_t *context = &sci_i2c_context[ch];
    R_SCI0_Type *reg = context->reg;
    reg->SIMR3_b.IICSTIF = 0;

    if (context->state == SCI_I2C_STATE_START) {
        reg->SIMR3 = SCI_I2C_SIMR3_SERIAL;
        context->state = SCI_I2C_STATE_ADDRESS;
        reg->TDR = (uint8_t)((context->addr << 1) | (context->read ? 1U : 0U));
    } else if (context->state == SCI_I2C_STATE_STOP) {
        reg->SIMR3 = SCI_I2C_SIMR3_RELEASE;
        reg->SCR = 0;
        context->bus_active = false;
        context->state = SCI_I2C_STATE_DONE;
    } else {
        context->error = RA_SCI_I2C_ERR;
        context->state = SCI_I2C_STATE_ERROR;
    }
}

static void sci_i2c_txi_callback(uint32_t ch) {
    sci_i2c_context_t *context = &sci_i2c_context[ch];
    R_SCI0_Type *reg = context->reg;

    if (reg->SISR & R_SCI0_SISR_IICACKR_Msk) {
        context->error = RA_SCI_I2C_NACK;
        sci_i2c_request_stop(context);
        return;
    }

    switch (context->state) {
        case SCI_I2C_STATE_ADDRESS:
            if (context->len == 0) {
                sci_i2c_complete_data(context);
            } else if (context->read) {
                reg->SIMR2 = context->len == 1 ? SCI_I2C_SIMR2_NACK : SCI_I2C_SIMR2_ACK;
                context->state = SCI_I2C_STATE_READ;
                reg->TDR = 0xff;
            } else {
                context->state = SCI_I2C_STATE_WRITE;
                reg->TDR = context->tx_buf[0];
            }
            break;

        case SCI_I2C_STATE_WRITE:
            ++context->pos;
            if (context->pos == context->len) {
                sci_i2c_complete_data(context);
            } else {
                reg->TDR = context->tx_buf[context->pos];
            }
            break;

        case SCI_I2C_STATE_READ:
            context->rx_buf[context->pos++] = reg->RDR;
            if (context->pos == context->len) {
                sci_i2c_complete_data(context);
            } else {
                reg->SIMR2 = context->pos == context->len - 1 ? SCI_I2C_SIMR2_NACK : SCI_I2C_SIMR2_ACK;
                reg->TDR = 0xff;
            }
            break;

        default:
            context->error = RA_SCI_I2C_ERR;
            context->state = SCI_I2C_STATE_ERROR;
            break;
    }
}

bool ra_sci_i2c_find_pins(uint32_t sda_pin, uint32_t scl_pin, uint32_t *ch) {
    uint32_t sda_ch;
    uint32_t scl_ch;
    uint32_t sda_af;
    uint32_t scl_af;
    if (!ra_sci_find_tx_ch_af(sda_pin, &sda_ch, &sda_af) ||
        !ra_sci_find_rx_ch_af(scl_pin, &scl_ch, &scl_af) ||
        sda_ch != scl_ch || sda_af != scl_af || !ra_sci_channel_available(sda_ch)) {
        return false;
    }
    if (ch != NULL) {
        *ch = sda_ch;
    }
    return true;
}

bool ra_sci_i2c_init(uint32_t ch, uint32_t sda_pin, uint32_t scl_pin, uint32_t freq) {
    uint32_t pin_ch;
    uint32_t af;
    uint32_t ignored_ch;
    sci_i2c_baud_t baud;
    R_SCI0_Type *reg = sci_i2c_reg(ch);

    if (reg == NULL || !ra_sci_i2c_find_pins(sda_pin, scl_pin, &pin_ch) ||
        pin_ch != ch || !ra_sci_find_tx_ch_af(sda_pin, &ignored_ch, &af) ||
        !sci_i2c_calc_baud(freq, &baud)) {
        return false;
    }

    if (sci_i2c_context[ch].initialized) {
        ra_sci_i2c_deinit(ch);
    }
    if (!ra_sci_owner_acquire(ch, RA_SCI_OWNER_I2C)) {
        return false;
    }

    ra_sci_irq_disable(ch);
    ra_sci_module_start(ch);
    reg->SCR = 0;
    while (reg->SCR != 0) {
    }

    ra_gpio_config(sda_pin, GPIO_MODE_AF_OD, GPIO_PULLUP, GPIO_LOW_POWER, af);
    ra_gpio_config(scl_pin, GPIO_MODE_AF_OD, GPIO_PULLUP, GPIO_LOW_POWER, af);

    reg->FCR = 0;
    reg->SIMR3 = SCI_I2C_SIMR3_RELEASE;
    reg->SMR = baud.cks;
    reg->SCMR = SCI_I2C_SCMR_INIT;
    reg->BRR = baud.brr;
    reg->MDDR = baud.mddr;
    reg->SEMR = (uint8_t)(R_SCI0_SEMR_NFEN_Msk |
        (baud.modulation ? R_SCI0_SEMR_BRME_Msk : 0));
    reg->SNFR = 1;
    reg->SIMR1 = R_SCI0_SIMR1_IICM_Msk;
    reg->SIMR2 = SCI_I2C_SIMR2_INIT;
    reg->SPMR = 0;

    sci_i2c_context_t *context = &sci_i2c_context[ch];
    memset(context, 0, sizeof(*context));
    context->state = SCI_I2C_STATE_IDLE;
    context->reg = reg;
    context->sda_pin = sda_pin;
    context->scl_pin = scl_pin;
    context->initialized = true;

    ra_sci_set_txi_callback(ch, RA_SCI_OWNER_I2C, sci_i2c_txi_callback);
    ra_sci_set_tei_callback(ch, RA_SCI_OWNER_I2C, sci_i2c_tei_callback);
    ra_sci_irq_priority(ch, RA_PRI_SCI_I2C);
    ra_sci_irq_enable(ch);
    return true;
}

void ra_sci_i2c_deinit(uint32_t ch) {
    if (ch >= SCI_CH_MAX || !sci_i2c_context[ch].initialized) {
        return;
    }

    sci_i2c_context_t *context = &sci_i2c_context[ch];
    context->reg->SCR = 0;
    context->reg->SIMR3 = SCI_I2C_SIMR3_RELEASE;
    ra_sci_irq_disable(ch);
    ra_sci_clear_txi_callback(ch);
    ra_sci_clear_tei_callback(ch);
    ra_sci_module_stop(ch);
    ra_sci_owner_release(ch, RA_SCI_OWNER_I2C);
    ra_gpio_config(context->sda_pin, GPIO_MODE_INPUT, GPIO_NOPULL, GPIO_LOW_POWER, AF_GPIO);
    ra_gpio_config(context->scl_pin, GPIO_MODE_INPUT, GPIO_NOPULL, GPIO_LOW_POWER, AF_GPIO);
    memset(context, 0, sizeof(*context));
}

static int sci_i2c_transfer(uint32_t ch, uint32_t timeout_ms) {
    sci_i2c_context_t *context = &sci_i2c_context[ch];
    R_SCI0_Type *reg = context->reg;

    context->state = SCI_I2C_STATE_START;
    context->error = RA_SCI_I2C_OK;
    context->pos = 0;
    reg->SIMR2 = SCI_I2C_SIMR2_INIT;
    reg->SCR = SCI_I2C_SCR_TRANSFER;
    reg->SIMR3 = context->bus_active ? SCI_I2C_SIMR3_RESTART : SCI_I2C_SIMR3_START;

    uint32_t start = uwTick;
    while (context->state != SCI_I2C_STATE_DONE && context->state != SCI_I2C_STATE_ERROR) {
        if ((uwTick - start) >= timeout_ms) {
            context->error = RA_SCI_I2C_TIMEOUT;
            context->state = SCI_I2C_STATE_ERROR;
            break;
        }
        __WFI();
    }

    int result;
    if (context->state == SCI_I2C_STATE_ERROR || context->error != RA_SCI_I2C_OK) {
        reg->SCR = 0;
        reg->SIMR3 = SCI_I2C_SIMR3_RELEASE;
        context->bus_active = false;
        result = context->error;
    } else {
        result = (int)context->len;
    }
    context->state = SCI_I2C_STATE_IDLE;
    return result;
}

int ra_sci_i2c_write(uint32_t ch, uint16_t addr, const uint8_t *src,
    uint32_t len, bool stop, uint32_t timeout_ms) {
    if (ch >= SCI_CH_MAX || !sci_i2c_context[ch].initialized ||
        sci_i2c_context[ch].state != SCI_I2C_STATE_IDLE) {
        return RA_SCI_I2C_ERR;
    }
    sci_i2c_context_t *context = &sci_i2c_context[ch];
    context->addr = addr;
    context->tx_buf = src;
    context->rx_buf = NULL;
    context->len = len;
    context->read = false;
    context->stop = stop;
    return sci_i2c_transfer(ch, timeout_ms);
}

int ra_sci_i2c_read(uint32_t ch, uint16_t addr, uint8_t *dest,
    uint32_t len, bool stop, uint32_t timeout_ms) {
    if (ch >= SCI_CH_MAX || !sci_i2c_context[ch].initialized ||
        sci_i2c_context[ch].state != SCI_I2C_STATE_IDLE) {
        return RA_SCI_I2C_ERR;
    }
    sci_i2c_context_t *context = &sci_i2c_context[ch];
    context->addr = addr;
    context->tx_buf = NULL;
    context->rx_buf = dest;
    context->len = len;
    context->read = true;
    context->stop = stop;
    return sci_i2c_transfer(ch, timeout_ms);
}
