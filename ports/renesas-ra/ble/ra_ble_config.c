/*
 * ra_ble_config.c — Minimal BLE configuration for MicroPython
 *
 * This file provides ONLY the symbols required by the prebuilt
 * FSP BLE Compact library (libr_ble.a) and RM_BLE_ABS.
 *
 * BLE works with polling model:
 *   - MicroPython main loop calls R_BLE_Execute() periodically
 *   - No external IRQ needed (that's for demo buttons)
 *   - No GPT/AGT timers needed (that's for demo LED patterns)
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "r_ble_api.h"
#include "r_ble_cfg.h"

// BLE Abstraction Layer (RM_BLE_ABS)
#include "rm_ble_abs.h"
#include "rm_ble_abs_api.h"

// HAL data for flash instance and timer
#include "hal_data.h"
#include "r_external_irq_api.h"
#include "r_timer_api.h"
#include "r_icu.h"
#include "r_gpt.h"
#include "vector_data.h"

// -----------------------------------------------------------------------------
// NOTE about HardFaults during BLE init
// -----------------------------------------------------------------------------
// The prebuilt Renesas BLE compact library consumes several *global pointer*
// symbols (e.g. g_ble_external_irq, g_ble_pl_timer, g_ble_flash). We have
// observed cases where these RAM-resident pointers can be corrupted before BLE
// initialisation runs (e.g. due to unrelated RAM writes / very tight RAM).
//
// Mitigation: place the pointers in a NOINIT section and always re-assert them
// immediately before RM_BLE_ABS_Open() (via ra_ble_platform_fixup()).
#if defined(__GNUC__)
#define RA_BLE_NOINIT __attribute__((section(".noinit"), used))
#else
#define RA_BLE_NOINIT
#endif

// -----------------------------------------------------------------------------
// Connection parameters (must be 16-bit wide; library reads with LDRH)
// -----------------------------------------------------------------------------
const uint16_t g_ble_conn_max      = BLE_CFG_RF_CONNECTION_MAXIMUM;
const uint16_t g_ble_conn_data_max = BLE_CFG_RF_CONNECTION_DATA_MAXIMUM;

// -----------------------------------------------------------------------------
// Advertising / Sync limits (required by libr_ble.a "all" variant)
// -----------------------------------------------------------------------------
const uint16_t g_ble_adv_data_max = BLE_CFG_RF_ADVERTISING_DATA_MAXIMUM;
const uint16_t g_ble_adv_set_max  = BLE_CFG_RF_ADVERTISING_SET_MAXIMUM;
const uint16_t g_ble_sync_set_max = BLE_CFG_RF_SYNC_SET_MAXIMUM;

// -----------------------------------------------------------------------------
// Controller storage regions (MUST match QE-generated hal_data.c expectations)
//
// NOTE:
// The prebuilt Renesas BLE library declares these as *arrays* (e.g.
//   extern uint32_t g_ble_cntl_heap[];
// ) and writes into them directly.
//
// Defining them as pointers (uint8_t *g_ble_cntl_heap) or undersizing them will
// cause out-of-bounds writes and typically HardFault during RM_BLE_ABS_Open().
//
// Sizes follow the same formulas used by QE (TryBT hal_data.c), but are scaled
// by BLE_CFG_RF_* macros (we use 1 connection, compact library).
// -----------------------------------------------------------------------------

/* BLE controller data area (2byte)
 * CRITICAL: These values MUST match QE-generated hal_data.c exactly!
 * Wrong values cause out-of-bounds writes and "advertises but won't connect".
 */
#if (BLE_CFG_LIBRARY_TYPE != 0)
/* Compact/Balance library (type 1 or 2) */
#define BLE_CNTL_DATA_MIN           (392)
#define BLE_CNTL_DATA_CONN          (65)
#define BLE_CNTL_DATA_ADV           (0)
#define BLE_CNTL_DATA_SYNC          (0)
#else /* BLE_CFG_LIBRARY_TYPE == 0 (Full/All library) */
#define BLE_CNTL_DATA_MIN           (279)
#define BLE_CNTL_DATA_CONN          (65)
#define BLE_CNTL_DATA_ADV           (78)
#define BLE_CNTL_DATA_SYNC          (33)
#endif

#define BLE_CNTL_DATA_MAX                           \
(                                                   \
    (BLE_CNTL_DATA_MIN) +                           \
    (BLE_CNTL_DATA_CONN * BLE_CFG_RF_CONNECTION_MAXIMUM) +          \
    (BLE_CNTL_DATA_ADV  * BLE_CFG_RF_ADVERTISING_SET_MAXIMUM) +     \
    (BLE_CNTL_DATA_SYNC * BLE_CFG_RF_SYNC_SET_MAXIMUM) +            \
    (0)                                                             \
)

/* BLE stack event heap area (1byte) */
#if defined(BLE_CFG_HCI_MODE_EN) && (BLE_CFG_HCI_MODE_EN)
#define BLE_HOST_HEAP_MIN         (0)
#else
#define BLE_HOST_HEAP_MIN         (3032)
#endif

#if (BLE_CFG_LIBRARY_TYPE != 0)
#define BLE_CNTL_HEAP_MIN         (88)
#define BLE_CNTL_HEAP_EVENT       (720)
#else
#define BLE_CNTL_HEAP_MIN         (280)
#define BLE_CNTL_HEAP_EVENT       (3784)
#endif

#define BLE_CNTL_HEAP_CONN        (388)
#define BLE_CNTL_ALIGN4(base)     ((((base) + 3) >> 2) << 2)
#define BLE_CNTL_HEAP_TX_DATA     (BLE_CNTL_ALIGN4(BLE_CFG_RF_CONNECTION_DATA_MAXIMUM + 4) + 20)
#define BLE_CNTL_HEAP_RX_DATA     (BLE_CNTL_ALIGN4(BLE_CFG_RF_CONNECTION_DATA_MAXIMUM + 8) + 4)
#define BLE_CNTL_HEAP_TX2_DATA    (BLE_CFG_RF_CONNECTION_DATA_MAXIMUM + 8)
#define BLE_CNTL_TXRX_MAX         (4)

#if (BLE_CFG_LIBRARY_TYPE != 0)
#define BLE_CNTL_ADV_DATA_MAX     (0)
#else
#define BLE_ADV_DATA_BLOCKS_LIMIT (36)
#define BLE_ADV_DATA_BLOCKS       ((((BLE_CFG_RF_ADVERTISING_DATA_MAXIMUM + 251) / 252) * BLE_CFG_RF_ADVERTISING_SET_MAXIMUM) * 2)
#if (BLE_ADV_DATA_BLOCKS > BLE_ADV_DATA_BLOCKS_LIMIT)
#define BLE_CNTL_ADV_DATA_MAX     (BLE_ADV_DATA_BLOCKS_LIMIT * 256)
#else
#define BLE_CNTL_ADV_DATA_MAX     (BLE_ADV_DATA_BLOCKS * 256)
#endif
#endif

#define BLE_CNTL_HEAP_MAX                           \
(                                                   \
    (BLE_CNTL_HEAP_MIN) +                           \
    (BLE_HOST_HEAP_MIN) +                           \
    (BLE_CNTL_HEAP_EVENT) +                         \
    (BLE_CNTL_HEAP_CONN * BLE_CFG_RF_CONNECTION_MAXIMUM) +          \
    (BLE_CNTL_HEAP_TX_DATA * BLE_CNTL_TXRX_MAX) +   \
    (BLE_CNTL_HEAP_RX_DATA * BLE_CNTL_TXRX_MAX) +   \
    (BLE_CNTL_ADV_DATA_MAX) +                       \
    (0)                                             \
)

/* LL connection entry area (1byte) */
#if (BLE_CFG_LIBRARY_TYPE == 1)
#define BLE_CNTL_CONN_ENT         (328)
#elif (BLE_CFG_LIBRARY_TYPE == 2)
#define BLE_CNTL_CONN_ENT         (316)
#else
#define BLE_CNTL_CONN_ENT         (336)
#endif

#define BLE_CNTL_CONN_ENT_MAX                       \
(                                                   \
    (BLE_CNTL_CONN_ENT * BLE_CFG_RF_CONNECTION_MAXIMUM) +           \
    (0)                                             \
)

/* LL Advertising set area (1byte)
 * NOTE: Even with BLE_CFG_LIBRARY_TYPE=2, the prebuilt libr_ble.a (compact)
 * still references g_ble_cntl_adv_set, so we must provide it.
 */
#define BLE_CNTL_ADV_SET          (152)
#define BLE_CNTL_ADV_SET_MAX                        \
(                                                   \
    (BLE_CNTL_ADV_SET * BLE_CFG_RF_ADVERTISING_SET_MAXIMUM) +       \
    (0)                                             \
)

/* Required symbols consumed by libr_ble.a */
uint16_t g_ble_cntl_data[BLE_CNTL_DATA_MAX];
uint32_t g_ble_cntl_heap[(BLE_CNTL_HEAP_MAX + 3) / 4];
uint32_t g_ble_cntl_heap2[(BLE_CNTL_HEAP_TX2_DATA + 3) / 4];
uint32_t g_ble_cntl_conn_ent[(BLE_CNTL_CONN_ENT_MAX + 3) / 4];

/* Required by libr_ble.a (even for compact library build). */
uint32_t g_ble_cntl_adv_set[(BLE_CNTL_ADV_SET_MAX + 3) / 4];

/* Advertising block count (extended advertising data blocks).
 * For compact builds this should be safe as 0.
 */
#if (BLE_CFG_LIBRARY_TYPE == 0)
const uint16_t g_ble_adv_block = (uint16_t)(BLE_CNTL_ADV_DATA_MAX / 256);
#else
const uint16_t g_ble_adv_block = 0;
#endif

// -----------------------------------------------------------------------------
// MINT buffer management heap
// -----------------------------------------------------------------------------
#ifndef BLE_CFG_TOTAL_HEAP_SIZE
#define BLE_CFG_TOTAL_HEAP_SIZE (8192)
#endif

static uint8_t s_mint_buf_mgmt_heap_storage[BLE_CFG_TOTAL_HEAP_SIZE] __attribute__((aligned(4)));

/* Provided by the prebuilt Renesas BLE library (libr_ble.a). */
extern uint8_t * g_mint_buf_mgmt_heap_memory;

// -----------------------------------------------------------------------------
// RF notify hooks (optional power hooks)
// -----------------------------------------------------------------------------
const st_ble_rf_notify_t g_ble_rf_notify =
{
    .enable    = 0,
    .start_cb  = NULL,
    .close_cb  = NULL,
    .dsleep_cb = NULL,
};

// -----------------------------------------------------------------------------
// Clock and RF configuration symbols used by compact library
// CRITICAL: g_ble_main_clk_khz must match actual MCU clock for correct BLE timing!
// Wrong value causes "advertises but won't connect" (timing mismatch).
// -----------------------------------------------------------------------------
#include "bsp_cfg.h"

#if (BSP_CFG_CLKOUT_RF_MAIN == 1) && (BSP_CFG_XTAL_HZ == 4000000) && (BLE_CFG_RF_CLKOUT_EN == 5)
extern void R_BSP_ConfigClockSetting(void);
const uint16_t g_ble_main_clk_khz = (uint16_t)(BSP_CFG_XTAL_HZ / 1000);
const ble_mcu_clock_change_cb_t g_ble_mcu_clock_change_fp = R_BSP_ConfigClockSetting;
#elif defined(BSP_CFG_CLKOUT_RF_MAIN) && (BSP_CFG_CLKOUT_RF_MAIN == 0)
const uint16_t g_ble_main_clk_khz = (uint16_t)BLE_CFG_MCU_MAIN_CLK_KHZ;
const ble_mcu_clock_change_cb_t g_ble_mcu_clock_change_fp = NULL;
#else
/* Fallback: use BLE_CFG_MCU_MAIN_CLK_KHZ from r_ble_cfg.h */
const uint16_t g_ble_main_clk_khz = (uint16_t)BLE_CFG_MCU_MAIN_CLK_KHZ;
const ble_mcu_clock_change_cb_t g_ble_mcu_clock_change_fp = NULL;
#endif

const uint32_t g_ble_dev_data_df_addr = 0x40100000;
const uint32_t g_ble_dev_data_cf_addr = 0x00000000;

uint8_t g_ble_dbg_rand_addr[6] = {0};
uint8_t g_ble_dbg_pub_addr[6]  = {0};

const uint8_t g_ble_rf_config[] =
{
    (BLE_CFG_RF_CLVAL << 0) | 0x00,
    (BLE_CFG_RF_EXT32K_EN << 0) |
    (BLE_CFG_RF_MCU_CLKOUT_FREQ << 1) |
    (BLE_CFG_RF_MCU_CLKOUT_PORT << 2) |
    (0x01 << 4) | 0x00,
    (BLE_CFG_RF_MAX_TX_POW << 0) | 0x00,
    (BLE_CFG_RF_DCDC_CONVERTER_ENABLE << 0) |
    (BLE_CFG_RF_DEF_TX_POW << 1) |
    (BLE_CFG_RF_CLKOUT_EN << 4) | 0x00
};

// =============================================================================
// External IRQ for BLE RF interrupt (IRQ8)
// Required by libr_ble.a - the library uses this for RF interrupt handling
// =============================================================================

// Callback provided by libr_ble.a
extern void r_rf_ble_interrupt(external_irq_callback_args_t *p_args);

static icu_instance_ctrl_t g_ble_external_irq_ctrl;

static const external_irq_cfg_t g_ble_external_irq_cfg =
{
    .channel = 8,  // IRQ8 for BLE RF
    .trigger = EXTERNAL_IRQ_TRIG_FALLING,
    .filter_enable = false,
    .pclk_div = EXTERNAL_IRQ_PCLK_DIV_BY_64,
    .p_callback = r_rf_ble_interrupt,
    .p_context = NULL,
    .p_extend = NULL,
    .ipl = (12),
    .irq = VECTOR_NUMBER_ICU_IRQ8,
};

static const external_irq_instance_t g_ble_external_irq_inst =
{
    .p_ctrl = &g_ble_external_irq_ctrl,
    .p_cfg = &g_ble_external_irq_cfg,
    .p_api = &g_external_irq_on_icu,
};

// Pointer expected by libr_ble.a
RA_BLE_NOINIT external_irq_instance_t const * g_ble_external_irq;

// =============================================================================
// Platform timer for BLE RF timing (GPT1)
// Required by libr_ble.a rf_cmt.o for RF host timing
// =============================================================================

// Callback provided by libr_ble.a
extern void r_rf_host_timer_interrupt(timer_callback_args_t *p_args);

static gpt_instance_ctrl_t g_ble_pl_timer_ctrl;

static const gpt_extended_cfg_t g_ble_pl_timer_extend =
{
    .gtioca = { .output_enabled = false, .stop_level = GPT_PIN_LEVEL_LOW },
    .gtiocb = { .output_enabled = false, .stop_level = GPT_PIN_LEVEL_LOW },
    .start_source = (gpt_source_t)(GPT_SOURCE_NONE),
    .stop_source = (gpt_source_t)(GPT_SOURCE_NONE),
    .clear_source = (gpt_source_t)(GPT_SOURCE_NONE),
    .count_up_source = (gpt_source_t)(GPT_SOURCE_NONE),
    .count_down_source = (gpt_source_t)(GPT_SOURCE_NONE),
    .capture_a_source = (gpt_source_t)(GPT_SOURCE_NONE),
    .capture_b_source = (gpt_source_t)(GPT_SOURCE_NONE),
    .capture_a_ipl = (BSP_IRQ_DISABLED),
    .capture_b_ipl = (BSP_IRQ_DISABLED),
    .capture_a_irq = FSP_INVALID_VECTOR,
    .capture_b_irq = FSP_INVALID_VECTOR,
    .capture_filter_gtioca = GPT_CAPTURE_FILTER_NONE,
    .capture_filter_gtiocb = GPT_CAPTURE_FILTER_NONE,
    .p_pwm_cfg = NULL,
    .gtior_setting.gtior = 0U,
};

static const timer_cfg_t g_ble_pl_timer_cfg =
{
    .mode = TIMER_MODE_PERIODIC,
    .period_counts = (uint32_t) 0x4e200,  // 320000 - matches TryBT exactly
    .duty_cycle_counts = 0x27100,         // 160000 - matches TryBT exactly
    .source_div = (timer_source_div_t) 0,
    .channel = 1,  // GPT1
    .p_callback = r_rf_host_timer_interrupt,
    .p_context = NULL,
    .p_extend = &g_ble_pl_timer_extend,
    .cycle_end_ipl = (2),
    .cycle_end_irq = VECTOR_NUMBER_GPT1_COUNTER_OVERFLOW,
};

static const timer_instance_t g_ble_pl_timer_inst =
{
    .p_ctrl = &g_ble_pl_timer_ctrl,
    .p_cfg = &g_ble_pl_timer_cfg,
    .p_api = &g_timer_on_gpt,
};

// Pointer expected by libr_ble.a (rf_cmt.o)
RA_BLE_NOINIT timer_instance_t const * g_ble_pl_timer;

// Flash instance pointer expected by libr_ble.a
RA_BLE_NOINIT flash_instance_t const * g_ble_flash;

// =============================================================================
// Platform fix-up (called before RM_BLE_ABS_Open)
// =============================================================================
void ra_ble_platform_fixup(void)
{
    // Ensure the BLE library sees valid platform pointers.
    g_ble_external_irq = &g_ble_external_irq_inst;
    g_ble_pl_timer     = &g_ble_pl_timer_inst;
    g_ble_flash        = &g_flash0;

    g_mint_buf_mgmt_heap_memory = s_mint_buf_mgmt_heap_storage;
}

// =============================================================================
// BLE Host Stack settings (required by libr_ble.a)
// =============================================================================
#ifndef ENABLE_HCI_MODE
#define BLE_HOST_L2_SIG_TBL_LEN                  24
#define BLE_HOST_L2_CH_PARAM_TBL_LEN              2
#define BLE_HOST_HCI_REM_TBL_LEN                  6
#define BLE_HOST_SMP_CONFIG_LEN                 108
#define BLE_HOST_GAP_CONN_TBL_LEN                12
#define BLE_HOST_DEV_Q_TBL_LEN                   14
#define BLE_HOST_ATT_CONN_TBL_LEN                16
#define BLE_HOST_GATTS_CNF_TBL_LEN                2

uint32_t g_ble_host_dev_q_tbl[(BLE_CFG_RF_CONNECTION_MAXIMUM * BLE_HOST_DEV_Q_TBL_LEN + 3) / 4];
uint32_t g_ble_host_hci_rem_tbl[(BLE_CFG_RF_CONNECTION_MAXIMUM * BLE_HOST_HCI_REM_TBL_LEN + 3) / 4];
uint32_t g_ble_host_l2_sig_tbl[(BLE_CFG_RF_CONNECTION_MAXIMUM * BLE_HOST_L2_SIG_TBL_LEN + 3) / 4];
uint32_t g_ble_host_l2_ch_param_tbl[(BLE_CFG_RF_CONNECTION_MAXIMUM * BLE_HOST_L2_CH_PARAM_TBL_LEN + 3) / 4];
uint32_t g_ble_host_smp_config_tbl[(BLE_CFG_RF_CONNECTION_MAXIMUM * BLE_HOST_SMP_CONFIG_LEN + 3) / 4];
uint32_t g_ble_host_att_conn_tbl[(BLE_CFG_RF_CONNECTION_MAXIMUM * BLE_HOST_ATT_CONN_TBL_LEN + 3) / 4];
uint32_t g_ble_host_gap_conn_tbl[(BLE_CFG_RF_CONNECTION_MAXIMUM * BLE_HOST_GAP_CONN_TBL_LEN + 3) / 4];
uint32_t g_ble_host_gatts_cnf_tbl[(BLE_CFG_RF_CONNECTION_MAXIMUM * BLE_HOST_GATTS_CNF_TBL_LEN + 3) / 4];

static const uint32_t g_p_ble_host_config_tbls[] = {
    (uint32_t)g_ble_host_dev_q_tbl,
    (uint32_t)g_ble_host_hci_rem_tbl,
    (uint32_t)g_ble_host_l2_sig_tbl,
    (uint32_t)g_ble_host_l2_ch_param_tbl,
    (uint32_t)g_ble_host_smp_config_tbl,
    (uint32_t)g_ble_host_att_conn_tbl,
    (uint32_t)g_ble_host_gap_conn_tbl,
    (uint32_t)g_ble_host_gatts_cnf_tbl
};

void ble_host_conn_config(uint32_t **pp_host_conn_config_table) {
    *pp_host_conn_config_table = (uint32_t*)g_p_ble_host_config_tbls;
}
#endif /* !ENABLE_HCI_MODE */

// =============================================================================
// Pairing Parameters (required by RM_BLE_ABS)
// =============================================================================
static ble_abs_pairing_parameter_t gs_abs_pairing_param = {
    .io_capabilitie_local_device = BLE_GAP_IOCAP_NOINPUT_NOOUTPUT,
    .mitm_protection_policy = BLE_GAP_SEC_MITM_BEST_EFFORT,
    .secure_connection_only = BLE_GAP_SC_BEST_EFFORT,
    .local_key_distribute = (uint8_t)(0),
    .remote_key_distribute = (uint8_t)(0),
    .maximum_key_size = 16,
};

// =============================================================================
// BLE Abstraction Layer (RM_BLE_ABS) Instance
// =============================================================================
extern void ra_ble_abs_gap_callback(uint16_t event_type, ble_status_t event_result,
                                    st_ble_evt_data_t *p_event_data);
extern void ra_ble_abs_vs_callback(uint16_t event_type, ble_status_t event_result,
                                   st_ble_vs_evt_data_t *p_event_data);

ble_abs_instance_ctrl_t g_ble_abs0_ctrl;

const ble_abs_cfg_t g_ble_abs0_cfg = {
    .gap_callback = ra_ble_abs_gap_callback,
    .vendor_specific_callback = ra_ble_abs_vs_callback,
    .p_gatt_server_callback_list = NULL,
    .gatt_server_callback_list_number = 0,
    .p_gatt_client_callback_list = NULL,
    .gatt_client_callback_list_number = 0,
    .p_pairing_parameter = &gs_abs_pairing_param,
    .p_flash_instance = &g_flash0,
    .p_timer_instance = &g_timer_ble,
    .p_callback = NULL,
    .p_context = NULL,
    .p_extend = NULL,
};

const ble_abs_instance_t g_ble_abs0 = {
    .p_ctrl = &g_ble_abs0_ctrl,
    .p_cfg = &g_ble_abs0_cfg,
    .p_api = &g_ble_abs_on_ble,
};
