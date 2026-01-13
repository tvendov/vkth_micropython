/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Renesas Electronics Corporation
 */

#include "py/runtime.h"
#include "py/obj.h"
#include <string.h>
#include "modble_renesas.h"

#if MICROPY_HW_ENABLE_BLE

#include "ble/ra_ble.h"
#include "ble/ra_ble_events.h"

// Callback storage
static mp_obj_t g_ble_callbacks[BLE_EVT_MAX];

// Persistent advertising payload buffers (FSP API requires memory to remain valid while setting data).
static uint8_t g_adv_data[31];
static uint8_t g_scan_rsp_data[31];

static void build_adv_payload(const char *name, uint8_t *adv_len, uint8_t *sr_len) {
    // Minimal payload:
    //  - Flags (LE General Discoverable + BR/EDR Not Supported)
    //  - Complete/Shortened local name (may be in scan response if it doesn't fit)
    uint8_t a = 0;
    uint8_t s = 0;

    // Flags AD structure.
    g_adv_data[a++] = 2;      // length (type + 1 byte)
    g_adv_data[a++] = 0x01;   // AD type: Flags
    g_adv_data[a++] = 0x06;   // LE General Discoverable + BR/EDR not supported

    size_t name_len = strlen(name);

    // Try to fit the (complete) name into advertising data.
    size_t adv_name_max = 31 - a - 2;
    if (name_len <= adv_name_max) {
        g_adv_data[a++] = (uint8_t)(name_len + 1);
        g_adv_data[a++] = 0x09; // Complete Local Name
        memcpy(&g_adv_data[a], name, name_len);
        a += (uint8_t)name_len;
    } else {
        // Put a shortened name in advertising data and (if possible) the full name in scan response.
        size_t short_len = adv_name_max;
        if (short_len > 0) {
            g_adv_data[a++] = (uint8_t)(short_len + 1);
            g_adv_data[a++] = 0x08; // Shortened Local Name
            memcpy(&g_adv_data[a], name, short_len);
            a += (uint8_t)short_len;
        }

        size_t sr_name_max = 31 - 2;
        size_t sr_use = (name_len <= sr_name_max) ? name_len : sr_name_max;
        if (sr_use > 0) {
            g_scan_rsp_data[s++] = (uint8_t)(sr_use + 1);
            g_scan_rsp_data[s++] = 0x09; // Complete Local Name
            memcpy(&g_scan_rsp_data[s], name, sr_use);
            s += (uint8_t)sr_use;
        }
    }

    *adv_len = a;
    *sr_len = s;
}

// Initialize callbacks to None
static void init_callbacks(void) {
    for (int i = 0; i < BLE_EVT_MAX; i++) {
        g_ble_callbacks[i] = mp_const_none;
    }
}

// renesas_ble.active(state)
static mp_obj_t modble_active(mp_obj_t state_obj) {
    bool state = mp_obj_is_true(state_obj);

    if (state) {
        init_callbacks();  // Initialize callback storage
        ra_ble_status_t status = ra_ble_init();
        if (status != RA_BLE_SUCCESS) {
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("BLE init failed"));
        }
    } else {
        ra_ble_deinit();
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(modble_active_obj, modble_active);

// renesas_ble.advertise(name, interval_ms)
static mp_obj_t modble_advertise(size_t n_args, const mp_obj_t *args) {
    const char *name = mp_obj_str_get_str(args[0]);
    mp_int_t interval_ms = mp_obj_get_int(args[1]);
    
    // Set device name
    ra_ble_gap_set_device_name(name, strlen(name));
    
    // Start advertising
    uint8_t adv_len = 0;
    uint8_t sr_len = 0;
    build_adv_payload(name, &adv_len, &sr_len);

    ra_ble_adv_params_t adv_params = {
        .interval_ms = interval_ms,
        .adv_data = g_adv_data,
        .adv_data_len = adv_len,
        .scan_rsp_data = g_scan_rsp_data,
        .scan_rsp_len = sr_len,
    };
    
    ra_ble_status_t status = ra_ble_gap_start_advertising(&adv_params);
    if (status != RA_BLE_SUCCESS) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Advertising failed"));
    }
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(modble_advertise_obj, 2, 2, modble_advertise);

// renesas_ble.on(event_name, callback)
static mp_obj_t modble_on(mp_obj_t event_name_obj, mp_obj_t callback_obj) {
    const char *event_name = mp_obj_str_get_str(event_name_obj);
    
    // Map event name to event type
    ble_event_type_t evt_type = BLE_EVT_NONE;
    
    if (strcmp(event_name, "connect") == 0) {
        evt_type = BLE_EVT_GAP_CONNECTED;
    } else if (strcmp(event_name, "disconnect") == 0) {
        evt_type = BLE_EVT_GAP_DISCONNECTED;
    } else if (strcmp(event_name, "write") == 0) {
        evt_type = BLE_EVT_GATTS_WRITE;
    } else if (strcmp(event_name, "notify_complete") == 0) {
        evt_type = BLE_EVT_GATTS_NOTIFY_COMPLETE;
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("Unknown event"));
    }
    
    // Store callback
    g_ble_callbacks[evt_type] = callback_obj;
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(modble_on_obj, modble_on);

// renesas_ble.notify(conn_handle, attr_handle, data)
static mp_obj_t modble_notify(mp_obj_t conn_hdl_obj, mp_obj_t attr_hdl_obj, mp_obj_t data_obj) {
    mp_int_t conn_handle = mp_obj_get_int(conn_hdl_obj);
    mp_int_t attr_handle = mp_obj_get_int(attr_hdl_obj);
    
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_obj, &bufinfo, MP_BUFFER_READ);
    
    ra_ble_status_t status = ra_ble_gatts_notify(conn_handle, attr_handle, 
                                                  bufinfo.buf, bufinfo.len);
    if (status != RA_BLE_SUCCESS) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Notify failed"));
    }
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(modble_notify_obj, modble_notify);

// renesas_ble.get_stats()
static mp_obj_t modble_get_stats(void) {
    ble_event_stats_t stats;
    ra_ble_event_get_stats(&stats);
    
    mp_obj_t dict = mp_obj_new_dict(3);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_pushed), mp_obj_new_int(stats.pushed));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_popped), mp_obj_new_int(stats.popped));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_dropped), mp_obj_new_int(stats.dropped));
    
    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_0(modble_get_stats_obj, modble_get_stats);

// Module globals
static const mp_rom_map_elem_t modble_renesas_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_renesas_ble) },
    { MP_ROM_QSTR(MP_QSTR_active), MP_ROM_PTR(&modble_active_obj) },
    { MP_ROM_QSTR(MP_QSTR_advertise), MP_ROM_PTR(&modble_advertise_obj) },
    { MP_ROM_QSTR(MP_QSTR_on), MP_ROM_PTR(&modble_on_obj) },
    { MP_ROM_QSTR(MP_QSTR_notify), MP_ROM_PTR(&modble_notify_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_stats), MP_ROM_PTR(&modble_get_stats_obj) },
};
static MP_DEFINE_CONST_DICT(modble_renesas_globals, modble_renesas_globals_table);

const mp_obj_module_t mp_module_renesas_ble = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&modble_renesas_globals,
};

MP_REGISTER_MODULE(MP_QSTR_renesas_ble, mp_module_renesas_ble);

// Process BLE events from main loop
void modble_renesas_process_events(void) {
    ble_event_t evt;

    // Run the FSP BLE host stack state machine.
    ra_ble_process_events();
    
    while (ra_ble_event_pop(&evt)) {
        mp_obj_t callback = g_ble_callbacks[evt.type];
        
        if (callback != mp_const_none) {
            // Call Python callback with event data
            mp_obj_t args[3];
            args[0] = mp_obj_new_int(evt.conn_handle);
            args[1] = mp_obj_new_int(evt.attr_handle);
            args[2] = mp_obj_new_bytes(evt.data, evt.data_len);
            
            mp_call_function_n_kw(callback, 3, 0, args);
        }
    }
}

#endif // MICROPY_HW_ENABLE_BLE

