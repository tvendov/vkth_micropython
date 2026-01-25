/*
 * ra_ble.c — FSP v4.4.0 compliant MicroPython ↔ BLE glue
 *
 * Constraint: BLE module does not use I/O pins (no external IRQ model).
 * BLE is advanced by polling R_BLE_Execute(), and application events arrive via callbacks.
 */

#include <string.h>
#include "py/mpconfig.h"
#include "py/runtime.h"
#include "py/mphal.h"

#include "ra_ble.h"
#include "ra_ble_events.h"
#include "gatt_db.h"

#include "r_ble_api.h"
#include "rm_ble_abs.h"
#include "rm_ble_abs_api.h"

#ifndef BLE_GAP_DISCONN_REASON_REMOTE_USER_TERM_CONN
#define BLE_GAP_DISCONN_REASON_REMOTE_USER_TERM_CONN (0x13)
#endif

#ifndef RA_BLE_DEBUG
#define RA_BLE_DEBUG (0)
#endif
#if RA_BLE_DEBUG
#define RA_BLE_LOG(...) mp_printf(&mp_plat_print, __VA_ARGS__)
#else
#define RA_BLE_LOG(...) (void)0
#endif

extern void ra_ble_platform_fixup(void);

/* Expected to be provided by FSP/QE-generated config. */
extern ble_abs_instance_ctrl_t g_ble_abs0_ctrl;
extern const ble_abs_cfg_t g_ble_abs0_cfg;

static ra_ble_state_t g_ble_state = RA_BLE_STATE_OFF;
static bool g_ble_open;
static bool g_ble_stack_on;
static uint8_t g_ble_adv_hdl = 0;

/* Legacy advertising parameters (as used by QE for BLE generated projects). */
static ble_abs_legacy_advertising_parameter_t g_ble_legacy_adv_param;

#define RA_BLE_DEVICE_NAME_MAX_LEN 29
static char g_ble_device_name[RA_BLE_DEVICE_NAME_MAX_LEN + 1];
static uint8_t g_ble_device_name_len;

static uint8_t g_ble_bd_addr[6];
static volatile bool g_ble_bd_addr_valid;

/* Vendor specific callback (for BD address completion). */
void ra_ble_abs_vs_callback(uint16_t event_type, ble_status_t event_result, st_ble_vs_evt_data_t *p_event_data)
{
    (void)event_result;
    if (event_type == BLE_VS_EVENT_GET_ADDR_COMP && p_event_data && p_event_data->p_param)
    {
        st_ble_vs_get_bd_addr_comp_evt_t * get_address = (st_ble_vs_get_bd_addr_comp_evt_t *)p_event_data->p_param;
        memcpy(g_ble_bd_addr, get_address->addr.addr, BLE_BD_ADDR_LEN);
        g_ble_bd_addr_valid = true;
    }
}


static ra_ble_status_t ra_ble_status_from_fsp(ble_status_t status) {
    if (status == BLE_SUCCESS) {
        return RA_BLE_SUCCESS;
    }
    if (status == BLE_ERR_INVALID_ARG || status == BLE_ERR_INVALID_PTR) {
        return RA_BLE_ERR_INVALID_ARG;
    }
    if (status == BLE_ERR_INVALID_STATE) {
        return RA_BLE_ERR_INVALID_STATE;
    }
    if (status == BLE_ERR_MEM_ALLOC_FAILED) {
        return RA_BLE_ERR_NO_MEM;
    }
    return RA_BLE_ERR_INVALID_STATE;
}

static uint32_t ra_ble_adv_interval_units_from_ms(uint32_t interval_ms) {
    uint32_t units = (interval_ms * 1600U) / 1000U; /* 0.625ms units */
    if (units < 0x20U) {
        units = 0x20U;
    }
    if (units > 0x00FFFFFFU) {
        units = 0x00FFFFFFU;
    }
    return units;
}

/* GAP callback for RM_BLE_ABS */
void ra_ble_abs_gap_callback(uint16_t event_type, ble_status_t event_result, st_ble_evt_data_t *p_event_data) {
    (void)event_result;

    switch (event_type) {
        case BLE_GAP_EVENT_STACK_ON:
            g_ble_stack_on = true;
            ra_ble_event_push(BLE_EVT_STACK_READY, 0, 0, NULL, 0);
            break;

        case BLE_GAP_EVENT_ADV_ON:
            g_ble_state = RA_BLE_STATE_ADVERTISING;
            ra_ble_event_push(BLE_EVT_GAP_ADV_STARTED, 0, 0, NULL, 0);
            break;

        case BLE_GAP_EVENT_ADV_OFF:
            if (g_ble_state == RA_BLE_STATE_ADVERTISING) {
                g_ble_state = RA_BLE_STATE_IDLE;
            }
            ra_ble_event_push(BLE_EVT_GAP_ADV_STOPPED, 0, 0, NULL, 0);
            break;

        case BLE_GAP_EVENT_CONN_IND: {
            st_ble_gap_conn_evt_t *conn_evt = (st_ble_gap_conn_evt_t *)p_event_data->p_param;
            g_ble_state = RA_BLE_STATE_CONNECTED;
            ra_ble_event_push(BLE_EVT_GAP_CONNECTED, conn_evt->conn_hdl, 0, NULL, 0);
            break;
        }

        case BLE_GAP_EVENT_DISCONN_IND: {
            st_ble_gap_disconn_evt_t *disc_evt = (st_ble_gap_disconn_evt_t *)p_event_data->p_param;
            if (g_ble_state == RA_BLE_STATE_CONNECTED) {
                g_ble_state = RA_BLE_STATE_IDLE;
            }
            ra_ble_event_push(BLE_EVT_GAP_DISCONNECTED, disc_evt->conn_hdl, 0, &disc_evt->reason, 1);

            /* Auto-restart advertising after disconnect */
            if (g_ble_legacy_adv_param.advertising_data_length > 0) {
                RM_BLE_ABS_StartLegacyAdvertising(&g_ble_abs0_ctrl, &g_ble_legacy_adv_param);
            }
            break;
        }

        default:
            break;
    }
}

static void ra_ble_gatts_cb(uint16_t event_type, ble_status_t event_result, st_ble_gatts_evt_data_t *p_event_data) {
    (void)event_result;

    switch (event_type) {
        case BLE_GATTS_EVENT_WRITE_RSP_COMP: {
            if (p_event_data && p_event_data->p_param) {
                st_ble_gatts_write_rsp_evt_t *write_evt = (st_ble_gatts_write_rsp_evt_t *)p_event_data->p_param;
                st_ble_gatt_value_t attr_value;
                uint8_t value_buf[BLE_EVENT_MAX_PAYLOAD];
                attr_value.p_value = value_buf;
                attr_value.value_len = sizeof(value_buf);
                ble_status_t st = R_BLE_GATTS_GetAttr(p_event_data->conn_hdl, write_evt->attr_hdl, &attr_value);
                if (st == BLE_SUCCESS && attr_value.value_len) {
                    ra_ble_event_push(BLE_EVT_GATTS_WRITE, p_event_data->conn_hdl, write_evt->attr_hdl,
                                      attr_value.p_value, attr_value.value_len);
                } else {
                    ra_ble_event_push(BLE_EVT_GATTS_WRITE, p_event_data->conn_hdl, write_evt->attr_hdl, NULL, 0);
                }
            }
            break;
        }

        case BLE_GATTS_EVENT_READ_RSP_COMP: {
            if (p_event_data && p_event_data->p_param) {
                st_ble_gatts_read_rsp_evt_t *read_evt = (st_ble_gatts_read_rsp_evt_t *)p_event_data->p_param;
                ra_ble_event_push(BLE_EVT_GATTS_READ, p_event_data->conn_hdl, read_evt->attr_hdl, NULL, 0);
            }
            break;
        }

        case BLE_GATTS_EVENT_HDL_VAL_CNF: {
            if (p_event_data && p_event_data->p_param) {
                st_ble_gatts_cfm_evt_t *cfm_evt = (st_ble_gatts_cfm_evt_t *)p_event_data->p_param;
                ra_ble_event_push(BLE_EVT_GATTS_INDICATE_COMPLETE, p_event_data->conn_hdl, cfm_evt->attr_hdl, NULL, 0);
            }
            break;
        }

        default:
            break;
    }
}

static void ra_ble_pump_once(void) {
    if (!g_ble_open) {
        return;
    }
    (void)R_BLE_Execute();
}

static bool ra_ble_wait_stack_on(uint32_t max_iters) {
    while (!g_ble_stack_on && max_iters--) {
        ra_ble_pump_once();
    }
    return g_ble_stack_on;
}

static volatile int s_ble_init_in_progress = 0;
static volatile int s_ble_inited = 0;

ra_ble_status_t ra_ble_init(void) {
    if (s_ble_inited) {
        return RA_BLE_SUCCESS;
    }
    if (s_ble_init_in_progress) {
        while (1) { __WFI(); }
    }
    s_ble_init_in_progress = 1;

    if (g_ble_state != RA_BLE_STATE_OFF) {
        s_ble_init_in_progress = 0;
        return RA_BLE_ERR_INVALID_STATE;
    }

    ra_ble_event_queue_init();

    g_ble_open = false;
    g_ble_stack_on = false;
    g_ble_bd_addr_valid = false;
    memset(g_ble_bd_addr, 0, sizeof(g_ble_bd_addr));

    ra_ble_platform_fixup();

    /* Optional: these are BSP-level protections; keep if your port uses them. */
    R_BSP_RegisterProtectDisable(BSP_REG_PROTECT_CGC);
    R_BSP_RegisterProtectDisable(BSP_REG_PROTECT_OM_LPC_BATT);
    R_BSP_RegisterProtectDisable(BSP_REG_PROTECT_LVD);

    mp_hal_delay_ms(100);

    fsp_err_t fsp_err = RM_BLE_ABS_Open(&g_ble_abs0_ctrl, &g_ble_abs0_cfg);

    R_BSP_RegisterProtectEnable(BSP_REG_PROTECT_LVD);
    R_BSP_RegisterProtectEnable(BSP_REG_PROTECT_OM_LPC_BATT);
    R_BSP_RegisterProtectEnable(BSP_REG_PROTECT_CGC);

    if (fsp_err != FSP_SUCCESS) {
        s_ble_init_in_progress = 0;
        return RA_BLE_ERR_INVALID_STATE;
    }

    g_ble_open = true;

    /* GATTS init + DB wiring (required for connect + service discovery). */
    ble_status_t st;
    st = R_BLE_GATTS_Init(1);
    if (st != BLE_SUCCESS) {
        (void)RM_BLE_ABS_Close(&g_ble_abs0_ctrl);
        g_ble_open = false;
        s_ble_init_in_progress = 0;
        return ra_ble_status_from_fsp(st);
    }

    st = R_BLE_GATTS_SetDbInst(&g_gatt_db_table);
    if (st != BLE_SUCCESS) {
        (void)RM_BLE_ABS_Close(&g_ble_abs0_ctrl);
        g_ble_open = false;
        s_ble_init_in_progress = 0;
        return ra_ble_status_from_fsp(st);
    }

    st = R_BLE_GATTS_RegisterCb(ra_ble_gatts_cb, 1);
    if (st != BLE_SUCCESS) {
        (void)RM_BLE_ABS_Close(&g_ble_abs0_ctrl);
        g_ble_open = false;
        s_ble_init_in_progress = 0;
        return ra_ble_status_from_fsp(st);
    }

    (void)ra_ble_wait_stack_on(10000);

    g_ble_state = RA_BLE_STATE_IDLE;

    s_ble_inited = 1;
    s_ble_init_in_progress = 0;

    return RA_BLE_SUCCESS;
}

ra_ble_status_t ra_ble_deinit(void) {
    if (!g_ble_open) {
        return RA_BLE_SUCCESS;
    }

    (void)RM_BLE_ABS_Close(&g_ble_abs0_ctrl);
    g_ble_open = false;
    g_ble_stack_on = false;
    g_ble_state = RA_BLE_STATE_OFF;
    s_ble_inited = 0;
    g_ble_bd_addr_valid = false;
    memset(g_ble_bd_addr, 0, sizeof(g_ble_bd_addr));

    return RA_BLE_SUCCESS;
}

ra_ble_state_t ra_ble_get_state(void) {
    return g_ble_state;
}

ra_ble_status_t ra_ble_gap_set_device_name(const char *name, uint8_t len) {
    if (!name) {
        return RA_BLE_ERR_INVALID_ARG;
    }
    if (len > RA_BLE_DEVICE_NAME_MAX_LEN) {
        len = RA_BLE_DEVICE_NAME_MAX_LEN;
    }
    memcpy(g_ble_device_name, name, len);
    g_ble_device_name[len] = '\0';
    g_ble_device_name_len = len;
    return RA_BLE_SUCCESS;
}

const char *ra_ble_gap_get_device_name(uint8_t *out_len) {
    if (out_len) {
        *out_len = g_ble_device_name_len;
    }
    return g_ble_device_name;
}

ra_ble_status_t ra_ble_gap_get_address(uint8_t addr[6])
{
    if (!addr) {
        return RA_BLE_ERR_INVALID_ARG;
    }

    /* Address is provided by Vendor Specific API asynchronously (see QE-generated app_main.c).
       We request it and then pump until completion, or return cached if already valid. */
    if (!g_ble_bd_addr_valid) {
        (void)R_BLE_VS_GetBdAddr(BLE_VS_ADDR_AREA_REG, BLE_GAP_ADDR_PUBLIC);
        /* Pump a bounded number of iterations to allow BLE_VS_EVENT_GET_ADDR_COMP to arrive. */
        for (uint32_t i = 0; i < 50000U && !g_ble_bd_addr_valid; i++) {
            ra_ble_pump_once();
        }
    }

    if (!g_ble_bd_addr_valid) {
        return RA_BLE_ERR_TIMEOUT;
    }

    memcpy(addr, g_ble_bd_addr, 6);
    return RA_BLE_SUCCESS;
}


ra_ble_status_t ra_ble_gap_start_advertising(const ra_ble_adv_params_t *params)
{
    if (!g_ble_open) {
        return RA_BLE_ERR_INVALID_STATE;
    }
    if (!params) {
        return RA_BLE_ERR_INVALID_ARG;
    }

    /* Legacy ADV_IND payload limits. */
    if (params->adv_data_len > 31U || params->scan_rsp_len > 31U) {
        return RA_BLE_ERR_INVALID_ARG;
    }

    /* Ensure we have a valid local address to advertise with.
       If VS get address fails, advertising may appear to start but not be observable. */
    if (!g_ble_bd_addr_valid) {
        uint8_t tmp[6];
        ra_ble_status_t ast = ra_ble_gap_get_address(tmp);
        if (ast != RA_BLE_SUCCESS) {
            return ast;
        }
    }

    memset(&g_ble_legacy_adv_param, 0, sizeof(g_ble_legacy_adv_param));

    /* Convert interval_ms to 0.625ms units as QE uses. */
    uint32_t units = ra_ble_adv_interval_units_from_ms(params->interval_ms ? params->interval_ms : 100);

    g_ble_legacy_adv_param.p_peer_address             = NULL;
    g_ble_legacy_adv_param.slow_advertising_interval  = units;
    g_ble_legacy_adv_param.slow_advertising_period    = 0x0000;
    g_ble_legacy_adv_param.p_advertising_data         = params->adv_data;
    g_ble_legacy_adv_param.advertising_data_length    = params->adv_data_len;
    g_ble_legacy_adv_param.p_scan_response_data       = params->scan_rsp_data;
    g_ble_legacy_adv_param.scan_response_data_length  = params->scan_rsp_len;
    g_ble_legacy_adv_param.advertising_filter_policy  = BLE_ABS_ADVERTISING_FILTER_ALLOW_ANY;
    g_ble_legacy_adv_param.advertising_channel_map    = (BLE_GAP_ADV_CH_37 | BLE_GAP_ADV_CH_38 | BLE_GAP_ADV_CH_39);
    /* RM_BLE_ABS legacy advertising expects local address to be Public Identity (or RPA falling back to Public). */
    g_ble_legacy_adv_param.own_bluetooth_address_type = BLE_GAP_ADDR_PUBLIC;
    memcpy(g_ble_legacy_adv_param.own_bluetooth_address, g_ble_bd_addr, BLE_BD_ADDR_LEN);

    /* Start legacy advertising via BLE Abstraction. */
    fsp_err_t err = RM_BLE_ABS_StartLegacyAdvertising(&g_ble_abs0_ctrl, &g_ble_legacy_adv_param);
    if (err != FSP_SUCCESS) {
        return RA_BLE_ERR_INVALID_STATE;
    }
    return RA_BLE_SUCCESS;
}

ra_ble_status_t ra_ble_gap_stop_advertising(void) {
    if (!g_ble_open) {
        return RA_BLE_ERR_INVALID_STATE;
    }
    return ra_ble_status_from_fsp(R_BLE_GAP_StopAdv(g_ble_adv_hdl));
}

ra_ble_status_t ra_ble_gap_disconnect(uint16_t conn_handle) {
    if (!g_ble_open) {
        return RA_BLE_ERR_INVALID_STATE;
    }
    return ra_ble_status_from_fsp(R_BLE_GAP_Disconnect(conn_handle, BLE_GAP_DISCONN_REASON_REMOTE_USER_TERM_CONN));
}

ra_ble_status_t ra_ble_gatts_notify(uint16_t conn_handle, uint16_t attr_handle,
                                   const uint8_t *data, uint16_t len) {
    if (!g_ble_open) {
        return RA_BLE_ERR_INVALID_STATE;
    }
    st_ble_gatt_hdl_value_pair_t ntf;
    ntf.attr_hdl = attr_handle;
    ntf.value.p_value = (uint8_t *)data;
    ntf.value.value_len = len;
    ble_status_t st = R_BLE_GATTS_Notification(conn_handle, &ntf);
    if (st == BLE_SUCCESS) {
        /* Notification has no confirmation on air; treat as 'sent to stack'. */
        ra_ble_event_push(BLE_EVT_GATTS_NOTIFY_COMPLETE, conn_handle, attr_handle, NULL, 0);
    }
    return ra_ble_status_from_fsp(st);
}

ra_ble_status_t ra_ble_gatts_indicate(uint16_t conn_handle, uint16_t attr_handle,
                                     const uint8_t *data, uint16_t len) {
    if (!g_ble_open) {
        return RA_BLE_ERR_INVALID_STATE;
    }
    st_ble_gatt_hdl_value_pair_t ind;
    ind.attr_hdl = attr_handle;
    ind.value.p_value = (uint8_t *)data;
    ind.value.value_len = len;
    return ra_ble_status_from_fsp(R_BLE_GATTS_Indication(conn_handle, &ind));
}

void ra_ble_process_events(void) {
    ra_ble_pump_once();
}
