/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2025 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * FSP BLE Compact Library Configuration
 *
 * This file provides the configuration symbols required by the prebuilt
 * FSP BLE Compact library (libr_ble.a). These symbols are normally generated
 * by e2 studio / FSP Configurator but we define them manually here.
 *
 * Reference: Renesas FSP BLE documentation and example projects
 */

#include <stdint.h>
#include <stdbool.h>
#include "r_ble_api.h"

// Timer instance required by rf_cmt.o inside Renesas BLE compact library.
// We provide an AGT-based timer instance and a small API compatibility shim.
#include "r_timer_api.h"
#include "r_agt.h"

// For RA4W1 BLE RF interrupt wiring (ICU IRQ8 is reserved for BLE middleware).
#include "common_data.h"
#include "vector_data.h"

// Use MicroPython's lightweight ICU IRQ dispatcher (ports/renesas-ra/ra/ra_icu.c)
// instead of pulling in the full FSP ICU external_irq driver.
#include "ra_icu.h"
#include "r_external_irq_api.h"

// BLE Connection Configuration
// Maximum number of simultaneous connections (Compact library supports 1)
const uint8_t g_ble_conn_max = 1;

// Maximum connection data size (251 bytes for BLE 4.2+ DLE)
const uint16_t g_ble_conn_data_max = 251;

// BLE Host Connection Configuration structure
// This is used by R_BLE_GAP_Init to configure the host stack
typedef struct {
    uint8_t conn_max;
    uint16_t mtu_max;
    uint8_t tx_buffer_count;
    uint8_t rx_buffer_count;
} st_ble_host_conn_config_t;

const st_ble_host_conn_config_t ble_host_conn_config = {
    .conn_max = 1,
    .mtu_max = 247,
    .tx_buffer_count = 4,
    .rx_buffer_count = 4,
};

// BLE Controller Connection Entity storage
// Size: approximately 32 bytes per connection (minimal)
static uint8_t s_ble_cntl_conn_ent_storage[32];
uint8_t *g_ble_cntl_conn_ent = s_ble_cntl_conn_ent_storage;

// BLE Controller Heap (main heap for BLE stack)
// Size: 1KB (minimal for basic advertising)
static uint8_t s_ble_cntl_heap_storage[1024];
uint8_t *g_ble_cntl_heap = s_ble_cntl_heap_storage;

// BLE Controller Heap 2 (secondary heap)
static uint8_t s_ble_cntl_heap2_storage[256];
uint8_t *g_ble_cntl_heap2 = s_ble_cntl_heap2_storage;

// BLE Controller Data storage
static uint8_t s_ble_cntl_data_storage[64];
uint8_t *g_ble_cntl_data = s_ble_cntl_data_storage;

// BLE RF Notify callback (called by RF driver for power management)
void (*g_ble_rf_notify)(uint32_t) = NULL;

// BLE External IRQ instance (for RF interrupt handling)
// libr_ble.a expects this to point at a valid external_irq_instance_t that opens ICU channel 8.
// We provide a small external_irq API wrapper backed by MicroPython's ra_icu dispatcher so:
//  - IRQ8 uses the existing r_icu_isr vector handler (from ra_icu.c)
//  - we don't pull in lib/fsp/.../r_icu.c (whose ISR expects an FSP context pointer)
extern void r_ble_icu_intout_interrupt(void);

static void ra_ble_rf_intout_callback(external_irq_callback_args_t * p_args)
{
    (void) p_args;
    r_ble_icu_intout_interrupt();
}

typedef struct
{
    bool open;
    uint8_t channel;
    uint8_t ipl;
    IRQn_Type irq;
    external_irq_trigger_t trigger;
    void (* p_callback)(external_irq_callback_args_t * p_args);
    void const * p_context;
    external_irq_callback_args_t * p_callback_memory;
    external_irq_callback_args_t cb_args;
} ra_ble_external_irq_ctrl_t;

static void ra_ble_external_irq_icu_cb(void * p_param)
{
    ra_ble_external_irq_ctrl_t * p_ctrl = (ra_ble_external_irq_ctrl_t *)p_param;
    if ((NULL == p_ctrl) || (!p_ctrl->open) || (NULL == p_ctrl->p_callback))
    {
        return;
    }

    external_irq_callback_args_t * p_args = p_ctrl->p_callback_memory;
    if (NULL == p_args)
    {
        p_args = &p_ctrl->cb_args;
    }
    p_args->p_context = p_ctrl->p_context;
    p_args->channel   = p_ctrl->channel;

    p_ctrl->p_callback(p_args);
}

static fsp_err_t ra_ble_external_irq_open(external_irq_ctrl_t * const p_api_ctrl, external_irq_cfg_t const * const p_cfg)
{
    ra_ble_external_irq_ctrl_t * p_ctrl = (ra_ble_external_irq_ctrl_t *)p_api_ctrl;

    FSP_ASSERT(NULL != p_ctrl);
    FSP_ASSERT(NULL != p_cfg);

    p_ctrl->channel          = p_cfg->channel;
    p_ctrl->ipl              = p_cfg->ipl;
    p_ctrl->irq              = p_cfg->irq;
    p_ctrl->trigger          = p_cfg->trigger;
    p_ctrl->p_callback       = p_cfg->p_callback;
    p_ctrl->p_context        = p_cfg->p_context;
    p_ctrl->p_callback_memory = NULL;
    p_ctrl->open             = true;

    // Configure trigger for ICU IRQ line.
    ra_icu_trigger_irq_no(p_ctrl->channel, (uint32_t)p_ctrl->trigger);

    // Register callback with the MicroPython ICU dispatcher.
    ra_icu_set_callback(p_ctrl->channel, ra_ble_external_irq_icu_cb, p_ctrl);

    // Configure NVIC priority if this IRQ is present in the vector table.
    if (p_ctrl->irq >= 0)
    {
        R_BSP_IrqCfg(p_ctrl->irq, p_ctrl->ipl, (void *)NULL);
    }

    return FSP_SUCCESS;
}

static fsp_err_t ra_ble_external_irq_enable(external_irq_ctrl_t * const p_api_ctrl)
{
    ra_ble_external_irq_ctrl_t * p_ctrl = (ra_ble_external_irq_ctrl_t *)p_api_ctrl;

    FSP_ASSERT(NULL != p_ctrl);
    FSP_ERROR_RETURN(p_ctrl->open, FSP_ERR_NOT_OPEN);
    FSP_ERROR_RETURN(p_ctrl->irq >= 0, FSP_ERR_IRQ_BSP_DISABLED);

    R_BSP_IrqStatusClear(p_ctrl->irq);
    R_BSP_IrqEnable(p_ctrl->irq);

    return FSP_SUCCESS;
}

static fsp_err_t ra_ble_external_irq_disable(external_irq_ctrl_t * const p_api_ctrl)
{
    ra_ble_external_irq_ctrl_t * p_ctrl = (ra_ble_external_irq_ctrl_t *)p_api_ctrl;

    FSP_ASSERT(NULL != p_ctrl);
    FSP_ERROR_RETURN(p_ctrl->open, FSP_ERR_NOT_OPEN);
    FSP_ERROR_RETURN(p_ctrl->irq >= 0, FSP_ERR_IRQ_BSP_DISABLED);

    R_BSP_IrqDisable(p_ctrl->irq);

    return FSP_SUCCESS;
}

static fsp_err_t ra_ble_external_irq_callback_set(external_irq_ctrl_t * const          p_api_ctrl,
                                                  void (                             * p_callback)(external_irq_callback_args_t *),
                                                  void const * const                   p_context,
                                                  external_irq_callback_args_t * const p_callback_memory)
{
    ra_ble_external_irq_ctrl_t * p_ctrl = (ra_ble_external_irq_ctrl_t *)p_api_ctrl;

    FSP_ASSERT(NULL != p_ctrl);
    FSP_ERROR_RETURN(p_ctrl->open, FSP_ERR_NOT_OPEN);

    p_ctrl->p_callback        = p_callback;
    p_ctrl->p_context         = p_context;
    p_ctrl->p_callback_memory = p_callback_memory;

    return FSP_SUCCESS;
}

static fsp_err_t ra_ble_external_irq_close(external_irq_ctrl_t * const p_api_ctrl)
{
    ra_ble_external_irq_ctrl_t * p_ctrl = (ra_ble_external_irq_ctrl_t *)p_api_ctrl;

    FSP_ASSERT(NULL != p_ctrl);
    if (p_ctrl->open)
    {
        if (p_ctrl->irq >= 0)
        {
            R_BSP_IrqDisable(p_ctrl->irq);
        }
        ra_icu_set_callback(p_ctrl->channel, NULL, NULL);
        p_ctrl->open = false;
    }

    return FSP_SUCCESS;
}

static const external_irq_api_t g_ra_ble_external_irq_on_icu =
{
    .open        = ra_ble_external_irq_open,
    .enable      = ra_ble_external_irq_enable,
    .disable     = ra_ble_external_irq_disable,
    .callbackSet = ra_ble_external_irq_callback_set,
    .close       = ra_ble_external_irq_close,
};

static ra_ble_external_irq_ctrl_t g_ble_external_irq8_ctrl;

static const external_irq_cfg_t g_ble_external_irq8_cfg =
{
    .channel       = 8,
    .ipl           = 12,
    .irq           = VECTOR_NUMBER_ICU_IRQ8,
    .trigger       = EXTERNAL_IRQ_TRIG_RISING,
    .pclk_div      = EXTERNAL_IRQ_PCLK_DIV_BY_1,
    .filter_enable = false,
    .p_callback    = ra_ble_rf_intout_callback,
    .p_context     = NULL,
    .p_extend      = NULL,
};

static const external_irq_instance_t g_ble_external_irq8 =
{
    .p_ctrl = &g_ble_external_irq8_ctrl,
    .p_cfg  = &g_ble_external_irq8_cfg,
    .p_api  = &g_ra_ble_external_irq_on_icu,
};

external_irq_instance_t const * g_ble_external_irq = &g_ble_external_irq8;

// BLE Platform Timer instance (for BLE timing)
// The prebuilt BLE compact library (rf_cmt.o) expects `g_ble_pl_timer` to be a
// global variable holding a pointer to an FSP-like timer instance.
//
// NOTE: rf_cmt.o uses a slightly different `timer_api_t` layout (it reads the
// close() function pointer at offset 0x30). To keep compatibility, we create a
// shim API structure with an extra reserved word before close().

extern void r_rf_host_timer_interrupt(void);

static void ra_ble_pl_timer_callback(timer_callback_args_t * p_args)
{
    (void) p_args;
    r_rf_host_timer_interrupt();
}

typedef struct st_ra_ble_timer_api_compat
{
    fsp_err_t (* open)(timer_ctrl_t * const p_ctrl, timer_cfg_t const * const p_cfg);
    fsp_err_t (* start)(timer_ctrl_t * const p_ctrl);
    fsp_err_t (* stop)(timer_ctrl_t * const p_ctrl);
    fsp_err_t (* reset)(timer_ctrl_t * const p_ctrl);
    fsp_err_t (* enable)(timer_ctrl_t * const p_ctrl);
    fsp_err_t (* disable)(timer_ctrl_t * const p_ctrl);
    fsp_err_t (* periodSet)(timer_ctrl_t * const p_ctrl, uint32_t const period);
    fsp_err_t (* dutyCycleSet)(timer_ctrl_t * const p_ctrl, uint32_t const duty_cycle_counts, uint32_t const pin);
    fsp_err_t (* infoGet)(timer_ctrl_t * const p_ctrl, timer_info_t * const p_info);
    fsp_err_t (* statusGet)(timer_ctrl_t * const p_ctrl, timer_status_t * const p_status);
    fsp_err_t (* callbackSet)(timer_ctrl_t * const p_api_ctrl,
                              void (* p_callback)(timer_callback_args_t *),
                              void const * const p_context,
                              timer_callback_args_t * const p_callback_memory);
    void const * reserved;
    fsp_err_t (* close)(timer_ctrl_t * const p_ctrl);
} ra_ble_timer_api_compat_t;

static const ra_ble_timer_api_compat_t g_ra_ble_timer_on_agt_compat =
{
    .open         = R_AGT_Open,
    .start        = R_AGT_Start,
    .stop         = R_AGT_Stop,
    .reset        = R_AGT_Reset,
    .enable       = R_AGT_Enable,
    .disable      = R_AGT_Disable,
    .periodSet    = R_AGT_PeriodSet,
    .dutyCycleSet = R_AGT_DutyCycleSet,
    .infoGet      = R_AGT_InfoGet,
    .statusGet    = R_AGT_StatusGet,
    .callbackSet  = R_AGT_CallbackSet,
    .reserved     = NULL,
    .close        = R_AGT_Close,
};

static agt_instance_ctrl_t g_ble_pl_timer_ctrl;

static const agt_extended_cfg_t g_ble_pl_timer_extend =
{
    .count_source = AGT_CLOCK_PCLKB,
    .agto         = AGT_PIN_CFG_DISABLED,
    .agtoab_settings_b.agtoa = AGT_PIN_CFG_DISABLED,
    .agtoab_settings_b.agtob = AGT_PIN_CFG_DISABLED,
    .measurement_mode = AGT_MEASURE_DISABLED,
    .agtio_filter     = AGT_AGTIO_FILTER_NONE,
    .enable_pin       = AGT_ENABLE_PIN_NOT_USED,
    .trigger_edge     = AGT_TRIGGER_EDGE_RISING,
};

static const timer_cfg_t g_ble_pl_timer_cfg =
{
    .mode             = TIMER_MODE_PERIODIC,
    // Initial value is not critical: rf_cmt.o will call periodSet() before start().
    .period_counts    = (uint32_t) 0x10000,
    .source_div       = TIMER_SOURCE_DIV_8,
    .duty_cycle_counts = (uint32_t) 0x8000,
    .channel          = 1,
    .cycle_end_ipl    = 5,
  #if defined(VECTOR_NUMBER_AGT1_INT)
    .cycle_end_irq    = VECTOR_NUMBER_AGT1_INT,
  #else
    .cycle_end_irq    = FSP_INVALID_VECTOR,
  #endif
    .p_callback       = ra_ble_pl_timer_callback,
    .p_context        = NULL,
    .p_extend         = &g_ble_pl_timer_extend,
};

static const timer_instance_t g_ble_pl_timer_inst =
{
    .p_ctrl = &g_ble_pl_timer_ctrl,
    .p_cfg  = &g_ble_pl_timer_cfg,
    .p_api  = (timer_api_t const *) &g_ra_ble_timer_on_agt_compat,
};

timer_instance_t const * g_ble_pl_timer = &g_ble_pl_timer_inst;

// BLE Main Clock frequency in kHz
const uint32_t g_ble_main_clk_khz = 48000;  // 48 MHz

// BLE MCU Clock Change callback
void (*g_ble_mcu_clock_change_fp)(uint32_t) = NULL;

// BLE Device Data Flash addresses (for storing BLE configuration)
const uint32_t g_ble_dev_data_df_addr = 0x40100000;  // Data flash start
const uint32_t g_ble_dev_data_cf_addr = 0x00000000;  // Code flash (not used)

// BLE Debug addresses (for development)
uint8_t g_ble_dbg_rand_addr[6] = {0};
uint8_t g_ble_dbg_pub_addr[6] = {0};

// BLE RF Configuration
typedef struct {
    uint8_t tx_power;
    uint8_t rf_mode;
} st_ble_rf_config_t;

const st_ble_rf_config_t g_ble_rf_config = {
    .tx_power = 0,  // 0 dBm
    .rf_mode = 0,   // Normal mode
};

// ============================================================================
// GATT Database Configuration (Device Information Service)
// ============================================================================

// UUID table (packed bytes)
// Contains all UUIDs used in GATT database
static const uint8_t g_ble_uuid_table[] = {
    // Offset 0-1: 0x180A (Device Information Service)
    0x0A, 0x18,
    // Offset 2-3: 0x2A29 (Manufacturer Name String)
    0x29, 0x2A,
    // Offset 4-5: 0x2A23 (System ID)
    0x23, 0x2A,
};

// Attribute values (must be defined before attr_cfg)
static uint8_t g_ble_attr_val_table[] = {
    'R', 'e', 'n', 'e', 's', 'a', 's', 0x00,  // Manufacturer Name at offset 0
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,  // System ID at offset 8
};

// Attribute configurations
// Each entry defines one attribute (service or characteristic)
static const st_ble_gatts_db_attr_cfg_t g_ble_attr_cfg[] = {
    // Handle 0: Service declaration (Device Information Service)
    {
        .desc_prop = BLE_GATT_DB_READ,  // Service property
        .next = 0xFFFF,                 // No more attributes with this UUID
        .uuid_offset = 0,               // Points to 0x180A in uuid_table
        .p_data_offset = NULL,          // Service declaration has no value
    },
    // Handle 1: Characteristic (Manufacturer Name String)
    {
        .desc_prop = BLE_GATT_DB_READ,  // Read property
        .next = 0xFFFF,                 // No more attributes with this UUID
        .uuid_offset = 2,               // Points to 0x2A29 in uuid_table
        .p_data_offset = &g_ble_attr_val_table[0],  // Manufacturer Name at offset 0
    },
    // Handle 2: Characteristic (System ID)
    {
        .desc_prop = BLE_GATT_DB_READ,  // Read property
        .next = 0xFFFF,                 // No more attributes with this UUID
        .uuid_offset = 4,               // Points to 0x2A23 in uuid_table
        .p_data_offset = &g_ble_attr_val_table[8],  // System ID at offset 8
    },
};

// UUID configuration (index for fast UUID lookup)
static const st_ble_gatts_db_uuid_cfg_t g_ble_uuid_cfg[] = {
    // UUID 0x180A (Device Information Service)
    {
        .offset = 0,           // Offset in uuid_table
        .first = 0,            // First handle with this UUID
        .last = 0,             // Last handle with this UUID
    },
    // UUID 0x2A29 (Manufacturer Name String)
    {
        .offset = 2,           // Offset in uuid_table
        .first = 1,            // First handle with this UUID
        .last = 1,             // Last handle with this UUID
    },
    // UUID 0x2A23 (System ID)
    {
        .offset = 4,           // Offset in uuid_table
        .first = 2,            // First handle with this UUID
        .last = 2,             // Last handle with this UUID
    },
};

// GATT Database configuration structure
// This is passed to R_BLE_GATTS_SetDbInst() to configure the GATT server
st_ble_gatts_db_cfg_t g_ble_gatts_db_cfg = {
    .p_uuid_table = g_ble_uuid_table,
    .p_attr_val_table = g_ble_attr_val_table,
    .p_const_attr_val_table = NULL,
    .p_rem_spec_val_table = NULL,
    .p_const_rem_spec_val_table = NULL,
    .p_uuid_cfg = g_ble_uuid_cfg,
    .p_attr_cfg = g_ble_attr_cfg,
    .p_char_cfg = NULL,
    .p_serv_cfg = NULL,
    .serv_cnt = 0,
    .char_cnt = 0,
    .uuid_type_cnt = 3,        // Total number of unique UUIDs
    .peer_spec_val_cnt = 0,
};

