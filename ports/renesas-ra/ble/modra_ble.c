/*
 * modra_ble.c — MicroPython binding for Renesas RA FSP 4.4 BLE glue (ra_ble_*.c)
 *
 * Design:
 *  - No external IRQ model.
 *  - BLE advanced by polling ra_ble_process_events() (which calls R_BLE_Execute()).
 *  - GAP/GATTS callbacks push events into ra_ble_events ring buffer.
 *
 * Python API (module: ra_ble)
 *  - init()
 *  - deinit()
 *  - adv_start(interval_ms, adv_data: bytes, scan_rsp: bytes|None)
 *  - adv_stop()
 *  - poll() -> None  (pumps BLE once)
 *  - get_event() -> (type, conn_handle, attr_handle, data: bytes) | None
 *  - get_state() -> int
 *  - set_name(name: str/bytes)
 *  - get_addr() -> bytes(6)
 */

#include "py/runtime.h"
#include "py/objstr.h"

#include "ra_ble.h"
#include "ra_ble_events.h"

/* ---------- helpers ---------- */

static mp_obj_t mp_ra_ble_init(void) {
    ra_ble_status_t st = ra_ble_init();
    if (st != RA_BLE_SUCCESS) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("BLE init failed: %d"), (int)st);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_ra_ble_init_obj, mp_ra_ble_init);

static mp_obj_t mp_ra_ble_deinit(void) {
    ra_ble_status_t st = ra_ble_deinit();
    if (st != RA_BLE_SUCCESS) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("BLE deinit failed: %d"), (int)st);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_ra_ble_deinit_obj, mp_ra_ble_deinit);

static mp_obj_t mp_ra_ble_poll(void) {
    ra_ble_process_events();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_ra_ble_poll_obj, mp_ra_ble_poll);

static mp_obj_t mp_ra_ble_get_state(void) {
    return mp_obj_new_int((int)ra_ble_get_state());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_ra_ble_get_state_obj, mp_ra_ble_get_state);

static mp_obj_t mp_ra_ble_set_name(mp_obj_t name_in) {
    size_t len = 0;
    const char *buf = NULL;

    if (mp_obj_is_str(name_in)) {
        buf = mp_obj_str_get_data(name_in, &len);
    } else {
        buf = (const char *)mp_obj_str_get_data(name_in, &len); // bytes
    }

    if (len > 255) {
        len = 255;
    }
    ra_ble_status_t st = ra_ble_gap_set_device_name(buf, (uint8_t)len);
    if (st != RA_BLE_SUCCESS) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("set_name failed: %d"), (int)st);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_ra_ble_set_name_obj, mp_ra_ble_set_name);

static mp_obj_t mp_ra_ble_get_addr(void) {
    uint8_t addr[6];
    ra_ble_status_t st = ra_ble_gap_get_address(addr);
    if (st != RA_BLE_SUCCESS) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("get_addr failed: %d"), (int)st);
    }
    return mp_obj_new_bytes(addr, 6);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_ra_ble_get_addr_obj, mp_ra_ble_get_addr);

static mp_obj_t mp_ra_ble_adv_start(size_t n_args, const mp_obj_t *args) {
    // adv_start(interval_ms, adv_data: bytes, scan_rsp: bytes|None)
    mp_int_t interval_ms = mp_obj_get_int(args[0]);

    mp_buffer_info_t adv_buf;
    mp_get_buffer_raise(args[1], &adv_buf, MP_BUFFER_READ);

    mp_buffer_info_t scan_buf = {0};
    bool have_scan = false;
    if (n_args >= 3 && args[2] != mp_const_none) {
        mp_get_buffer_raise(args[2], &scan_buf, MP_BUFFER_READ);
        have_scan = true;
    }

    ra_ble_adv_params_t p;
    p.interval_ms = (uint16_t)interval_ms;
    p.adv_data = (uint8_t *)adv_buf.buf;
    p.adv_data_len = (uint16_t)adv_buf.len;
    if (have_scan) {
        p.scan_rsp_data = (uint8_t *)scan_buf.buf;
        p.scan_rsp_len = (uint16_t)scan_buf.len;
    } else {
        p.scan_rsp_data = NULL;
        p.scan_rsp_len = 0;
    }

    ra_ble_status_t st = ra_ble_gap_start_advertising(&p);
    if (st != RA_BLE_SUCCESS) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("adv_start failed: %d"), (int)st);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_ra_ble_adv_start_obj, 2, 3, mp_ra_ble_adv_start);

static mp_obj_t mp_ra_ble_adv_stop(void) {
    ra_ble_status_t st = ra_ble_gap_stop_advertising();
    if (st != RA_BLE_SUCCESS) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("adv_stop failed: %d"), (int)st);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_ra_ble_adv_stop_obj, mp_ra_ble_adv_stop);

static mp_obj_t mp_ra_ble_get_event(void) {
    ble_event_t evt;
    if (!ra_ble_event_pop(&evt)) {
        return mp_const_none;
    }

    mp_obj_t data_obj = mp_obj_new_bytes(evt.data, evt.data_len);

    mp_obj_t tuple[4];
    tuple[0] = mp_obj_new_int((int)evt.type);
    tuple[1] = mp_obj_new_int((int)evt.conn_handle);
    tuple[2] = mp_obj_new_int((int)evt.attr_handle);
    tuple[3] = data_obj;
    return mp_obj_new_tuple(4, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_ra_ble_get_event_obj, mp_ra_ble_get_event);

static mp_obj_t mp_ra_ble_notify(size_t n_args, const mp_obj_t *args) {
    // notify(conn_handle, attr_handle, data)
    mp_int_t conn_handle = mp_obj_get_int(args[0]);
    mp_int_t attr_handle = mp_obj_get_int(args[1]);
    mp_buffer_info_t buf;
    mp_get_buffer_raise(args[2], &buf, MP_BUFFER_READ);

    ra_ble_status_t st = ra_ble_gatts_notify((uint16_t)conn_handle, (uint16_t)attr_handle,
                                             (uint8_t *)buf.buf, (uint16_t)buf.len);
    if (st != RA_BLE_SUCCESS) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("notify failed: %d"), (int)st);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_ra_ble_notify_obj, 3, 3, mp_ra_ble_notify);

static mp_obj_t mp_ra_ble_write_attr(mp_obj_t handle_in, mp_obj_t data_in) {
    // write_attr(attr_handle, data)
    mp_int_t attr_handle = mp_obj_get_int(handle_in);
    mp_buffer_info_t buf;
    mp_get_buffer_raise(data_in, &buf, MP_BUFFER_READ);

    ra_ble_status_t st = ra_ble_gatts_set_attr((uint16_t)attr_handle,
                                               (uint8_t *)buf.buf, (uint16_t)buf.len);
    if (st != RA_BLE_SUCCESS) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("write_attr failed: %d"), (int)st);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mp_ra_ble_write_attr_obj, mp_ra_ble_write_attr);

static mp_obj_t mp_ra_ble_read_attr(mp_obj_t handle_in) {
    // read_attr(attr_handle) -> bytes
    mp_int_t attr_handle = mp_obj_get_int(handle_in);
    uint8_t buf[256];
    uint16_t len = sizeof(buf);

    ra_ble_status_t st = ra_ble_gatts_get_attr((uint16_t)attr_handle, buf, &len);
    if (st != RA_BLE_SUCCESS) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("read_attr failed: %d"), (int)st);
    }
    return mp_obj_new_bytes(buf, len);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_ra_ble_read_attr_obj, mp_ra_ble_read_attr);

/* ---------- module globals ---------- */

static const mp_rom_map_elem_t ra_ble_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_ra_ble) },

    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&mp_ra_ble_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&mp_ra_ble_deinit_obj) },

    { MP_ROM_QSTR(MP_QSTR_poll), MP_ROM_PTR(&mp_ra_ble_poll_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_event), MP_ROM_PTR(&mp_ra_ble_get_event_obj) },

    { MP_ROM_QSTR(MP_QSTR_adv_start), MP_ROM_PTR(&mp_ra_ble_adv_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_adv_stop), MP_ROM_PTR(&mp_ra_ble_adv_stop_obj) },

    { MP_ROM_QSTR(MP_QSTR_get_state), MP_ROM_PTR(&mp_ra_ble_get_state_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_name), MP_ROM_PTR(&mp_ra_ble_set_name_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_addr), MP_ROM_PTR(&mp_ra_ble_get_addr_obj) },

    { MP_ROM_QSTR(MP_QSTR_notify), MP_ROM_PTR(&mp_ra_ble_notify_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_attr), MP_ROM_PTR(&mp_ra_ble_write_attr_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_attr), MP_ROM_PTR(&mp_ra_ble_read_attr_obj) },

    // States (match ra_ble_state_t)
    { MP_ROM_QSTR(MP_QSTR_STATE_OFF), MP_ROM_INT(RA_BLE_STATE_OFF) },
    { MP_ROM_QSTR(MP_QSTR_STATE_IDLE), MP_ROM_INT(RA_BLE_STATE_IDLE) },
    { MP_ROM_QSTR(MP_QSTR_STATE_ADVERTISING), MP_ROM_INT(RA_BLE_STATE_ADVERTISING) },
    { MP_ROM_QSTR(MP_QSTR_STATE_CONNECTED), MP_ROM_INT(RA_BLE_STATE_CONNECTED) },

    // Event types (match ble_event_type_t)
    { MP_ROM_QSTR(MP_QSTR_EVT_STACK_READY), MP_ROM_INT(BLE_EVT_STACK_READY) },
    { MP_ROM_QSTR(MP_QSTR_EVT_GAP_CONNECTED), MP_ROM_INT(BLE_EVT_GAP_CONNECTED) },
    { MP_ROM_QSTR(MP_QSTR_EVT_GAP_DISCONNECTED), MP_ROM_INT(BLE_EVT_GAP_DISCONNECTED) },
    { MP_ROM_QSTR(MP_QSTR_EVT_GAP_ADV_STARTED), MP_ROM_INT(BLE_EVT_GAP_ADV_STARTED) },
    { MP_ROM_QSTR(MP_QSTR_EVT_GAP_ADV_STOPPED), MP_ROM_INT(BLE_EVT_GAP_ADV_STOPPED) },
    { MP_ROM_QSTR(MP_QSTR_EVT_GATTS_WRITE), MP_ROM_INT(BLE_EVT_GATTS_WRITE) },
    { MP_ROM_QSTR(MP_QSTR_EVT_GATTS_READ), MP_ROM_INT(BLE_EVT_GATTS_READ) },
    { MP_ROM_QSTR(MP_QSTR_EVT_GATTS_NOTIFY_COMPLETE), MP_ROM_INT(BLE_EVT_GATTS_NOTIFY_COMPLETE) },
    { MP_ROM_QSTR(MP_QSTR_EVT_GATTS_INDICATE_COMPLETE), MP_ROM_INT(BLE_EVT_GATTS_INDICATE_COMPLETE) },
};

static MP_DEFINE_CONST_DICT(ra_ble_module_globals, ra_ble_module_globals_table);

const mp_obj_module_t ra_ble_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&ra_ble_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_ra_ble, ra_ble_user_cmodule);
