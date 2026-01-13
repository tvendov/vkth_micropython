# Session 2026-01-09: TODO Stubs Implementation in ra_ble.c

## Objective
Implement the previously empty TODO stubs in `ports/renesas-ra/ble/ra_ble.c`:
- `ra_ble_gap_disconnect()`
- `ra_ble_gatts_notify()`
- `ra_ble_gatts_indicate()`
- `ra_ble_gap_set_device_name()`

Also add missing GAP callback events for connect/disconnect.

---

## Changes Made

### 1. GAP Callback Events (ra_ble_gap_cb)
**Location:** `ports/renesas-ra/ble/ra_ble.c` lines 107-124

Added handling for:
- `BLE_GAP_EVENT_CONN_IND` → Pushes `BLE_EVT_GAP_CONNECTED` with conn_hdl
- `BLE_GAP_EVENT_DISCONN_IND` → Pushes `BLE_EVT_GAP_DISCONNECTED` with conn_hdl and reason

```c
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
    break;
}
```

### 2. ra_ble_gap_disconnect()
**Location:** lines 334-343

```c
ra_ble_status_t ra_ble_gap_disconnect(uint16_t conn_handle) {
    if (g_ble_state == RA_BLE_STATE_OFF || !g_ble_stack_on) {
        return RA_BLE_ERR_INVALID_STATE;
    }
    // Reason 0x13 = "Remote User Terminated Connection"
    ble_status_t st = R_BLE_GAP_Disconnect(conn_handle, 0x13);
    return ra_ble_status_from_fsp(st);
}
```

### 3. ra_ble_gatts_notify()
**Location:** lines 345-363

```c
ra_ble_status_t ra_ble_gatts_notify(uint16_t conn_handle, uint16_t attr_handle,
                                    const uint8_t *data, uint16_t len) {
    if (!g_ble_stack_on) return RA_BLE_ERR_INVALID_STATE;
    if (data == NULL || len == 0) return RA_BLE_ERR_INVALID_ARG;
    
    st_ble_gatt_hdl_value_pair_t ntf;
    ntf.attr_hdl = attr_handle;
    ntf.value.value_len = len;
    ntf.value.p_value = (uint8_t *)data;
    
    ble_status_t st = R_BLE_GATTS_Notification(conn_handle, &ntf);
    return ra_ble_status_from_fsp(st);
}
```

### 4. ra_ble_gatts_indicate()
**Location:** lines 365-385

Same structure as notify, but uses `R_BLE_GATTS_Indication()`.
Note: Indication confirmation received via `BLE_GATTS_EVENT_HDL_VAL_CNF` (requires GATTS callback registration).

### 5. ra_ble_gap_set_device_name()
**Location:** lines 392-418

FSP Compact BLE library does NOT have `R_BLE_GAP_SetDevName`.
Solution: Store name locally for use in advertising payload.

```c
#define RA_BLE_DEVICE_NAME_MAX_LEN 29  // Max for adv payload
static char g_ble_device_name[RA_BLE_DEVICE_NAME_MAX_LEN + 1];
static uint8_t g_ble_device_name_len;

ra_ble_status_t ra_ble_gap_set_device_name(const char *name, uint8_t len) {
    if (name == NULL) return RA_BLE_ERR_INVALID_ARG;
    if (len > RA_BLE_DEVICE_NAME_MAX_LEN) len = RA_BLE_DEVICE_NAME_MAX_LEN;
    memcpy(g_ble_device_name, name, len);
    g_ble_device_name[len] = '\0';
    g_ble_device_name_len = len;
    return RA_BLE_SUCCESS;
}

// New getter for adv payload builder
const char *ra_ble_gap_get_device_name(uint8_t *out_len);
```

---

## Header Changes
**File:** `ports/renesas-ra/ble/ra_ble.h`

Added declaration:
```c
const char *ra_ble_gap_get_device_name(uint8_t *out_len);
```

---

## Status
| Function | Status | FSP API Used |
|----------|--------|--------------|
| GAP CONN_IND callback | ✅ Done | Event parsing |
| GAP DISCONN_IND callback | ✅ Done | Event parsing |
| ra_ble_gap_disconnect | ✅ Done | R_BLE_GAP_Disconnect |
| ra_ble_gatts_notify | ✅ Done | R_BLE_GATTS_Notification |
| ra_ble_gatts_indicate | ✅ Done | R_BLE_GATTS_Indication |
| ra_ble_gap_set_device_name | ✅ Done | Local storage (no FSP API) |

---

## Additional Changes (same session)

### 6. Advertising Payload Builder
**Location:** lines 236-272

New function `ra_ble_build_default_adv_payload()` builds standard advertising data:
- AD Type 0x01 (Flags): LE General Discoverable + BR/EDR Not Supported
- AD Type 0x09 (Complete Local Name): Uses stored device name from `ra_ble_gap_set_device_name()`
- Falls back to AD Type 0x08 (Shortened Local Name) if full name doesn't fit

```c
static uint8_t *ra_ble_build_default_adv_payload(uint8_t *out_len) {
    // Builds: [02 01 06] [len 09 <name>]
    // Returns pointer to static buffer
}
```

### 7. Integration in ra_ble_gap_start_advertising
**Location:** lines 375-400

Modified to use default payload when caller doesn't provide custom adv_data:
- If `params->adv_data` is NULL, calls `ra_ble_build_default_adv_payload()`
- Device name appears in BLE scanner apps

### 8. GATTS Callback Registration
**Location:** lines 139-194 (callback), lines 254-267 (registration)

Added `ra_ble_gatts_cb()` callback and registered it in `ra_ble_init()`:
- `R_BLE_GATTS_Init(1)` - Initialize with 1 callback slot
- `R_BLE_GATTS_RegisterCb(ra_ble_gatts_cb, 1)` - Register with priority 1

Handles events:
| Event | Action |
|-------|--------|
| `BLE_GATTS_EVENT_WRITE_REQ` | Push `BLE_EVT_GATTS_WRITE` with conn_hdl + attr_hdl |
| `BLE_GATTS_EVENT_READ_REQ` | Push `BLE_EVT_GATTS_READ` |
| `BLE_GATTS_EVENT_HDL_VAL_CNF` | Push `BLE_EVT_GATTS_INDICATE_COMPLETE` |

---

## Updated Status
| Function | Status |
|----------|--------|
| Adv payload with device name | ✅ Done |
| GATTS callback (write/read/indicate) | ✅ Done |

---

## Next Steps
1. **Build test** - Compile with MSYS2 to verify no link errors
2. **Hardware test** - Flash to EK-RA4W1 and test with BLE scanner app

