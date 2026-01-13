/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Renesas Electronics Corporation
 */

#include <string.h>
#include "py/mpconfig.h"
#include "ra_ble.h"
#include "ra_ble_events.h"

// FSP BLE stack API
#include "r_ble_api.h"

// BLE state
static ra_ble_state_t g_ble_state = RA_BLE_STATE_OFF;

// FSP stack runtime flags
static bool g_ble_open;
static bool g_ble_stack_on;
static bool g_ble_set_rand_addr_pending;
static uint8_t g_ble_adv_hdl = 0;

// Own identity address (little-endian). For now this is only stored/returned by the wrapper.
// Applying it to the controller will be done when the real FSP BLE stack init path is implemented.
static uint8_t g_ble_own_addr_le[6];
static bool g_ble_own_addr_valid;

// Device name storage for advertising payload
#define RA_BLE_DEVICE_NAME_MAX_LEN 29  // Max for adv payload (31 - 2 for type/len)
static char g_ble_device_name[RA_BLE_DEVICE_NAME_MAX_LEN + 1];
static uint8_t g_ble_device_name_len;

// Default advertising payload buffer (built dynamically)
#define RA_BLE_ADV_PAYLOAD_MAX_LEN 31
static uint8_t g_ble_default_adv_data[RA_BLE_ADV_PAYLOAD_MAX_LEN];
static uint8_t g_ble_default_adv_data_len;

#if defined(MICROPY_HW_BLE_STATIC_RANDOM_ADDR_LE)
static const uint8_t g_ble_cfg_static_random_addr_le[6] = MICROPY_HW_BLE_STATIC_RANDOM_ADDR_LE;
#endif

static void ra_ble_set_static_random_bits_le(uint8_t addr_le[6]) {
    // Static Random address: top two bits of MSB must be 1.
    // With little-endian storage MSB is addr_le[5].
    addr_le[5] = (uint8_t)(addr_le[5] | 0xC0);
}

static void ra_ble_load_own_address(void) {
    memset(g_ble_own_addr_le, 0, sizeof(g_ble_own_addr_le));
    g_ble_own_addr_valid = false;

    #if defined(MICROPY_HW_BLE_USE_STATIC_RANDOM_ADDR) && MICROPY_HW_BLE_USE_STATIC_RANDOM_ADDR
    #if defined(MICROPY_HW_BLE_STATIC_RANDOM_ADDR_LE)
    memcpy(g_ble_own_addr_le, g_ble_cfg_static_random_addr_le, sizeof(g_ble_own_addr_le));
    ra_ble_set_static_random_bits_le(g_ble_own_addr_le);
    g_ble_own_addr_valid = true;
    #endif
    #endif
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

static void ra_ble_try_set_rand_addr(void) {
    if (!g_ble_set_rand_addr_pending) {
        return;
    }
    if (!g_ble_own_addr_valid) {
        g_ble_set_rand_addr_pending = false;
        return;
    }

    ble_status_t st = R_BLE_GAP_SetRandAddr(g_ble_own_addr_le);
    if (st == BLE_SUCCESS) {
        g_ble_set_rand_addr_pending = false;
    }
}

static void ra_ble_gap_cb(uint16_t event_type, ble_status_t event_result, st_ble_evt_data_t *p_event_data) {
    (void)event_result;
    (void)p_event_data;

    switch (event_type) {
        case BLE_GAP_EVENT_STACK_ON:
            g_ble_stack_on = true;
            ra_ble_try_set_rand_addr();
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
            // Connection established - extract conn_hdl from event data
            st_ble_gap_conn_evt_t *conn_evt = (st_ble_gap_conn_evt_t *)p_event_data->p_param;
            g_ble_state = RA_BLE_STATE_CONNECTED;
            ra_ble_event_push(BLE_EVT_GAP_CONNECTED, conn_evt->conn_hdl, 0, NULL, 0);
            break;
        }

        case BLE_GAP_EVENT_DISCONN_IND: {
            // Disconnection - extract conn_hdl and reason from event data
            st_ble_gap_disconn_evt_t *disc_evt = (st_ble_gap_disconn_evt_t *)p_event_data->p_param;
            if (g_ble_state == RA_BLE_STATE_CONNECTED) {
                g_ble_state = RA_BLE_STATE_IDLE;
            }
            ra_ble_event_push(BLE_EVT_GAP_DISCONNECTED, disc_evt->conn_hdl, 0, &disc_evt->reason, 1);
            break;
        }

        default:
            break;
    }
}

// GATT Server callback - handles write completions, read completions, and indication confirmations
// Note: FSP Compact library sends *_RSP_COMP events (after response sent), not request events
static void ra_ble_gatts_cb(uint16_t event_type, ble_status_t event_result, st_ble_gatts_evt_data_t *p_event_data) {
    (void)event_result;

    switch (event_type) {
        case BLE_GATTS_EVENT_WRITE_RSP_COMP: {
            // Write Response has been sent - client wrote to a characteristic
            if (p_event_data && p_event_data->p_param) {
                st_ble_gatts_write_rsp_evt_t *write_evt = (st_ble_gatts_write_rsp_evt_t *)p_event_data->p_param;

                // Try to read the written value from FSP attribute storage
                st_ble_gatt_value_t attr_value;
                uint8_t value_buf[BLE_EVENT_MAX_PAYLOAD];
                attr_value.p_value = value_buf;
                attr_value.value_len = sizeof(value_buf);

                ble_status_t st = R_BLE_GATTS_GetAttr(p_event_data->conn_hdl, write_evt->attr_hdl, &attr_value);
                if (st == BLE_SUCCESS && attr_value.value_len > 0) {
                    // Push event with the actual written data
                    ra_ble_event_push(BLE_EVT_GATTS_WRITE,
                                      p_event_data->conn_hdl,
                                      write_evt->attr_hdl,
                                      attr_value.p_value,
                                      attr_value.value_len);
                } else {
                    // Push event without data (GetAttr failed or unsupported)
                    ra_ble_event_push(BLE_EVT_GATTS_WRITE,
                                      p_event_data->conn_hdl,
                                      write_evt->attr_hdl,
                                      NULL, 0);
                }
            }
            break;
        }

        case BLE_GATTS_EVENT_READ_RSP_COMP: {
            // Read Response has been sent
            if (p_event_data && p_event_data->p_param) {
                st_ble_gatts_read_rsp_evt_t *read_evt = (st_ble_gatts_read_rsp_evt_t *)p_event_data->p_param;
                ra_ble_event_push(BLE_EVT_GATTS_READ,
                                  p_event_data->conn_hdl,
                                  read_evt->attr_hdl,
                                  NULL, 0);
            }
            break;
        }

        case BLE_GATTS_EVENT_HDL_VAL_CNF: {
            // Indication confirmation received from client
            if (p_event_data && p_event_data->p_param) {
                st_ble_gatts_cfm_evt_t *cfm_evt = (st_ble_gatts_cfm_evt_t *)p_event_data->p_param;
                ra_ble_event_push(BLE_EVT_GATTS_INDICATE_COMPLETE,
                                  p_event_data->conn_hdl,
                                  cfm_evt->attr_hdl,
                                  NULL, 0);
            }
            break;
        }

        case BLE_GATTS_EVENT_CONN_IND:
        case BLE_GATTS_EVENT_DISCONN_IND:
            // These are also sent to GATTS callback but we handle them in GAP callback
            break;

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

static uint32_t ra_ble_adv_interval_units_from_ms(uint32_t interval_ms) {
    // Units are 0.625ms (Core Spec). Convert ms -> units.
    // units = ms / 0.625 = ms * 1600 / 1000
    uint32_t units = (interval_ms * 1600U) / 1000U;
    if (units < 0x20U) {
        units = 0x20U;
    }
    if (units > 0x00FFFFFFU) {
        units = 0x00FFFFFFU;
    }
    return units;
}

// Initialize BLE stack
ra_ble_status_t ra_ble_init(void) {
    if (g_ble_state != RA_BLE_STATE_OFF) {
        return RA_BLE_ERR_INVALID_STATE;
    }
    
    // Initialize event queue
    ra_ble_event_queue_init();

    // Load configured own-address (hardcoded Static Random by default on EK_RA4W1).
    ra_ble_load_own_address();

    g_ble_open = false;
    g_ble_stack_on = false;
    g_ble_set_rand_addr_pending = g_ble_own_addr_valid;

    ble_status_t st = R_BLE_Open();
    if (st != BLE_SUCCESS) {
        return ra_ble_status_from_fsp(st);
    }
    g_ble_open = true;

    st = R_BLE_GAP_Init(ra_ble_gap_cb);
    if (st != BLE_SUCCESS) {
        (void)R_BLE_Close();
        g_ble_open = false;
        return ra_ble_status_from_fsp(st);
    }

    // Initialize GATT Server and register callback for write/read/indication events
    st = R_BLE_GATTS_Init(1);  // 1 callback slot
    if (st != BLE_SUCCESS) {
        (void)R_BLE_GAP_Terminate();
        (void)R_BLE_Close();
        g_ble_open = false;
        return ra_ble_status_from_fsp(st);
    }

    // Configure GATT Database with Device Information Service
    extern st_ble_gatts_db_cfg_t g_ble_gatts_db_cfg;
    st = R_BLE_GATTS_SetDbInst(&g_ble_gatts_db_cfg);
    if (st != BLE_SUCCESS) {
        (void)R_BLE_GAP_Terminate();
        (void)R_BLE_Close();
        g_ble_open = false;
        return ra_ble_status_from_fsp(st);
    }

    st = R_BLE_GATTS_RegisterCb(ra_ble_gatts_cb, 1);  // priority 1
    if (st != BLE_SUCCESS) {
        // Non-fatal: continue without GATTS callback (notifications/writes won't generate events)
        // Some FSP library variants may not support this
    }

    // Try to set the configured static random address early. If the stack isn't ready yet,
    // we'll retry when we receive BLE_GAP_EVENT_STACK_ON.
    ra_ble_try_set_rand_addr();

    g_ble_state = RA_BLE_STATE_IDLE;

    // Ensure pending startup events are processed (some libraries signal STACK_ON immediately).
    ra_ble_pump_once();

    return RA_BLE_SUCCESS;
}

// Deinitialize BLE stack
ra_ble_status_t ra_ble_deinit(void) {
    if (g_ble_state == RA_BLE_STATE_OFF) {
        return RA_BLE_SUCCESS;
    }

    if (g_ble_state == RA_BLE_STATE_ADVERTISING) {
        (void)ra_ble_gap_stop_advertising();
    }

    if (g_ble_open) {
        (void)R_BLE_GAP_Terminate();
        (void)R_BLE_Close();
        g_ble_open = false;
    }

    g_ble_stack_on = false;
    g_ble_set_rand_addr_pending = false;
    g_ble_state = RA_BLE_STATE_OFF;
    return RA_BLE_SUCCESS;
}

// Get current BLE state
ra_ble_state_t ra_ble_get_state(void) {
    return g_ble_state;
}

// Build default advertising payload with Flags + Complete Local Name
// Returns pointer to static buffer and sets *out_len to payload length
static uint8_t *ra_ble_build_default_adv_payload(uint8_t *out_len) {
    uint8_t *p = g_ble_default_adv_data;
    uint8_t remaining = RA_BLE_ADV_PAYLOAD_MAX_LEN;

    // AD Type 0x01: Flags (3 bytes total: len=2, type=0x01, flags)
    // Flags: LE General Discoverable + BR/EDR Not Supported
    if (remaining >= 3) {
        *p++ = 2;           // Length of this AD structure (type + data)
        *p++ = 0x01;        // AD Type: Flags
        *p++ = 0x06;        // LE General Discoverable (0x02) | BR/EDR Not Supported (0x04)
        remaining -= 3;
    }

    // AD Type 0x09: Complete Local Name (if set and fits)
    if (g_ble_device_name_len > 0 && remaining >= (2 + g_ble_device_name_len)) {
        *p++ = 1 + g_ble_device_name_len;  // Length: type + name
        *p++ = 0x09;                        // AD Type: Complete Local Name
        memcpy(p, g_ble_device_name, g_ble_device_name_len);
        p += g_ble_device_name_len;
        remaining -= (2 + g_ble_device_name_len);
    } else if (g_ble_device_name_len > 0 && remaining >= 3) {
        // Use Shortened Local Name (0x08) if full name doesn't fit
        uint8_t short_len = remaining - 2;  // How much name we can fit
        *p++ = 1 + short_len;
        *p++ = 0x08;  // AD Type: Shortened Local Name
        memcpy(p, g_ble_device_name, short_len);
        p += short_len;
        remaining -= (2 + short_len);
    }

    g_ble_default_adv_data_len = (uint8_t)(p - g_ble_default_adv_data);
    *out_len = g_ble_default_adv_data_len;
    return g_ble_default_adv_data;
}

// Start advertising
ra_ble_status_t ra_ble_gap_start_advertising(const ra_ble_adv_params_t *params) {
    if (!params) {
        return RA_BLE_ERR_INVALID_ARG;
    }
    
    if (g_ble_state != RA_BLE_STATE_IDLE) {
        return RA_BLE_ERR_INVALID_STATE;
    }

    if (!g_ble_open) {
        return RA_BLE_ERR_INVALID_STATE;
    }

    // Make sure the host stack is ready before starting advertising.
    if (!ra_ble_wait_stack_on(50000)) {
        return RA_BLE_ERR_TIMEOUT;
    }

    // Configure legacy advertising set 0.
    st_ble_gap_adv_param_t adv_param;
    memset(&adv_param, 0, sizeof(adv_param));
    adv_param.adv_hdl = g_ble_adv_hdl;
    adv_param.adv_prop_type = BLE_GAP_LEGACY_PROP_ADV_IND;

    uint32_t adv_units = ra_ble_adv_interval_units_from_ms(params->interval_ms);
    adv_param.adv_intv_min = adv_units;
    adv_param.adv_intv_max = adv_units;

    adv_param.adv_ch_map = BLE_GAP_ADV_CH_ALL;
    adv_param.filter_policy = BLE_GAP_ADV_ALLOW_SCAN_ANY_CONN_ANY;
    adv_param.adv_phy = BLE_GAP_ADV_PHY_1M;
    adv_param.sec_adv_max_skip = 0;
    adv_param.sec_adv_phy = BLE_GAP_ADV_PHY_1M;
    adv_param.scan_req_ntf_flag = BLE_GAP_SCAN_REQ_NTF_DISABLE;

    if (g_ble_own_addr_valid) {
        adv_param.o_addr_type = BLE_GAP_ADDR_RAND;
        memcpy(adv_param.o_addr, g_ble_own_addr_le, BLE_BD_ADDR_LEN);
    } else {
        adv_param.o_addr_type = BLE_GAP_ADDR_PUBLIC;
    }

    ble_status_t st = R_BLE_GAP_SetAdvParam(&adv_param);
    if (st != BLE_SUCCESS) {
        return ra_ble_status_from_fsp(st);
    }

    // Set advertising data.
    // If caller provides custom data, use it; otherwise build default payload with device name.
    {
        st_ble_gap_adv_data_t adv_data;
        memset(&adv_data, 0, sizeof(adv_data));
        adv_data.adv_hdl = g_ble_adv_hdl;
        adv_data.data_type = BLE_GAP_ADV_DATA_MODE;

        if (params->adv_data && params->adv_data_len) {
            // Use caller-provided advertising data
            adv_data.data_length = params->adv_data_len;
            adv_data.p_data = params->adv_data;
        } else {
            // Build default payload: Flags + Complete Local Name
            uint8_t default_len;
            uint8_t *default_data = ra_ble_build_default_adv_payload(&default_len);
            adv_data.data_length = default_len;
            adv_data.p_data = default_data;
        }

        adv_data.zero_length_flag = (adv_data.data_length == 0) ? 1 : 0;
        st = R_BLE_GAP_SetAdvSresData(&adv_data);
        if (st != BLE_SUCCESS) {
            return ra_ble_status_from_fsp(st);
        }
    }

    // Set scan response data.
    if (params->scan_rsp_data && params->scan_rsp_len) {
        st_ble_gap_adv_data_t sr_data;
        memset(&sr_data, 0, sizeof(sr_data));
        sr_data.adv_hdl = g_ble_adv_hdl;
        sr_data.data_type = BLE_GAP_SCAN_RSP_DATA_MODE;
        sr_data.data_length = params->scan_rsp_len;
        sr_data.p_data = params->scan_rsp_data;
        sr_data.zero_length_flag = 0;
        st = R_BLE_GAP_SetAdvSresData(&sr_data);
        if (st != BLE_SUCCESS) {
            return ra_ble_status_from_fsp(st);
        }
    }

    st = R_BLE_GAP_StartAdv(g_ble_adv_hdl, 0x0000, 0x00);
    if (st != BLE_SUCCESS) {
        return ra_ble_status_from_fsp(st);
    }

    // State will be updated and event queued on BLE_GAP_EVENT_ADV_ON.
    ra_ble_pump_once();
    return RA_BLE_SUCCESS;
}

// Stop advertising
ra_ble_status_t ra_ble_gap_stop_advertising(void) {
    if (g_ble_state != RA_BLE_STATE_ADVERTISING) {
        return RA_BLE_ERR_INVALID_STATE;
    }

    if (!g_ble_open) {
        return RA_BLE_ERR_INVALID_STATE;
    }

    ble_status_t st = R_BLE_GAP_StopAdv(g_ble_adv_hdl);
    if (st != BLE_SUCCESS) {
        return ra_ble_status_from_fsp(st);
    }

    // State will be updated and event queued on BLE_GAP_EVENT_ADV_OFF.
    ra_ble_pump_once();
    return RA_BLE_SUCCESS;
}

// Disconnect from peer
ra_ble_status_t ra_ble_gap_disconnect(uint16_t conn_handle) {
    if (g_ble_state == RA_BLE_STATE_OFF || !g_ble_stack_on) {
        return RA_BLE_ERR_INVALID_STATE;
    }

    // Reason 0x13 = "Remote User Terminated Connection" (standard BLE HCI reason)
    ble_status_t st = R_BLE_GAP_Disconnect(conn_handle, 0x13);
    return ra_ble_status_from_fsp(st);
}

// Send GATT notification
ra_ble_status_t ra_ble_gatts_notify(uint16_t conn_handle, uint16_t attr_handle,
                                    const uint8_t *data, uint16_t len) {
    if (!g_ble_stack_on) {
        return RA_BLE_ERR_INVALID_STATE;
    }
    if (data == NULL || len == 0) {
        return RA_BLE_ERR_INVALID_ARG;
    }

    // Build notification data structure
    st_ble_gatt_hdl_value_pair_t ntf;
    ntf.attr_hdl = attr_handle;
    ntf.value.value_len = len;
    ntf.value.p_value = (uint8_t *)data;  // FSP does not modify the data

    ble_status_t st = R_BLE_GATTS_Notification(conn_handle, &ntf);
    return ra_ble_status_from_fsp(st);
}

// Send GATT indication
ra_ble_status_t ra_ble_gatts_indicate(uint16_t conn_handle, uint16_t attr_handle,
                                      const uint8_t *data, uint16_t len) {
    if (!g_ble_stack_on) {
        return RA_BLE_ERR_INVALID_STATE;
    }
    if (data == NULL || len == 0) {
        return RA_BLE_ERR_INVALID_ARG;
    }

    // Build indication data structure
    st_ble_gatt_hdl_value_pair_t ind;
    ind.attr_hdl = attr_handle;
    ind.value.value_len = len;
    ind.value.p_value = (uint8_t *)data;  // FSP does not modify the data

    // Note: Indication confirmation is received via BLE_GATTS_EVENT_HDL_VAL_CNF
    // if a GATTS callback is registered with R_BLE_GATTS_RegisterCb
    ble_status_t st = R_BLE_GATTS_Indication(conn_handle, &ind);
    return ra_ble_status_from_fsp(st);
}

// Set GAP device name
// Note: FSP Compact BLE library does not have R_BLE_GAP_SetDevName.
// The name is stored locally and used when building advertising payload.
ra_ble_status_t ra_ble_gap_set_device_name(const char *name, uint8_t len) {
    if (name == NULL) {
        return RA_BLE_ERR_INVALID_ARG;
    }

    // Clamp length to max allowed
    if (len > RA_BLE_DEVICE_NAME_MAX_LEN) {
        len = RA_BLE_DEVICE_NAME_MAX_LEN;
    }

    memcpy(g_ble_device_name, name, len);
    g_ble_device_name[len] = '\0';
    g_ble_device_name_len = len;

    return RA_BLE_SUCCESS;
}

// Get stored device name (for advertising payload builder)
const char *ra_ble_gap_get_device_name(uint8_t *out_len) {
    if (out_len != NULL) {
        *out_len = g_ble_device_name_len;
    }
    return g_ble_device_name;
}

// Get GAP device address
ra_ble_status_t ra_ble_gap_get_address(uint8_t addr[6]) {
    // TODO: Get address
    // st_ble_dev_addr_t dev_addr;
    // R_BLE_GAP_GetBdAddr(&dev_addr);
    
    if (addr) {
        if (g_ble_own_addr_valid) {
            memcpy(addr, g_ble_own_addr_le, 6);
        } else {
            memset(addr, 0, 6);
        }
    }
    return RA_BLE_SUCCESS;
}

// Process BLE events (call from main loop)
void ra_ble_process_events(void) {
    ra_ble_pump_once();
}

