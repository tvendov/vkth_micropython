# BLE Event Queue Design for MicroPython RA4W1

**Critical Design Constraint:** BLE callbacks execute in interrupt context and **CANNOT** call Python code directly.

---

## Problem Statement

FSP BLE stack uses callback-driven architecture:
```c
void ble_gap_app_cb(uint16_t event_type, ble_status_t result, st_ble_evt_data_t *p_data) {
    // ⚠️ RUNS IN INTERRUPT CONTEXT
    // ❌ CANNOT call mp_call_function_*()
    // ❌ CANNOT allocate Python objects
    // ❌ CANNOT acquire GIL
}
```

**Solution:** Event queue with deferred processing in main loop.

---

## Architecture

```
┌─────────────────┐
│  BLE Callback   │ (IRQ context)
│  (FSP stack)    │
└────────┬────────┘
         │ Push event
         ▼
┌─────────────────┐
│  Ring Buffer    │ (lock-free)
│  Event Queue    │
└────────┬────────┘
         │ Pop event
         ▼
┌─────────────────┐
│  Main Loop      │ (Python context)
│  Event Handler  │
└────────┬────────┘
         │ Call Python callback
         ▼
┌─────────────────┐
│  User Python    │
│  Code           │
└─────────────────┘
```

---

## Event Structure

```c
#define BLE_EVENT_MAX_PAYLOAD 64

typedef enum {
    BLE_EVT_NONE = 0,
    BLE_EVT_GAP_CONNECTED,
    BLE_EVT_GAP_DISCONNECTED,
    BLE_EVT_GAP_ADV_COMPLETE,
    BLE_EVT_GATT_WRITE,
    BLE_EVT_GATT_READ,
    BLE_EVT_GATT_NOTIFY_COMPLETE,
    // ... more events
} ble_event_type_t;

typedef struct {
    ble_event_type_t type;
    uint16_t conn_handle;
    uint16_t attr_handle;
    uint16_t data_len;
    uint8_t data[BLE_EVENT_MAX_PAYLOAD];
} ble_event_t;
```

---

## Ring Buffer Implementation

```c
typedef struct {
    ble_event_t events[MICROPY_BLE_EVENT_QUEUE_SIZE];
    volatile uint16_t head;  // Write index (IRQ)
    volatile uint16_t tail;  // Read index (main loop)
    volatile uint16_t dropped; // Overflow counter
} ble_event_queue_t;

static ble_event_queue_t g_ble_event_queue;

// Called from BLE callback (IRQ context)
void ra_ble_event_push(ble_event_type_t type, uint16_t conn_hdl, 
                       uint16_t attr_hdl, const uint8_t *data, uint16_t len) {
    uint16_t next_head = (g_ble_event_queue.head + 1) % MICROPY_BLE_EVENT_QUEUE_SIZE;
    
    // Check overflow
    if (next_head == g_ble_event_queue.tail) {
        g_ble_event_queue.dropped++;
        return; // Queue full, drop event
    }
    
    ble_event_t *evt = &g_ble_event_queue.events[g_ble_event_queue.head];
    evt->type = type;
    evt->conn_handle = conn_hdl;
    evt->attr_handle = attr_hdl;
    evt->data_len = (len > BLE_EVENT_MAX_PAYLOAD) ? BLE_EVENT_MAX_PAYLOAD : len;
    
    if (data && evt->data_len > 0) {
        memcpy(evt->data, data, evt->data_len);
    }
    
    // Commit write
    g_ble_event_queue.head = next_head;
}

// Called from main loop (Python context)
bool ra_ble_event_pop(ble_event_t *evt) {
    if (g_ble_event_queue.tail == g_ble_event_queue.head) {
        return false; // Queue empty
    }
    
    memcpy(evt, &g_ble_event_queue.events[g_ble_event_queue.tail], sizeof(ble_event_t));
    g_ble_event_queue.tail = (g_ble_event_queue.tail + 1) % MICROPY_BLE_EVENT_QUEUE_SIZE;
    
    return true;
}
```

---

## FSP Callback Integration

```c
// GAP callback (registered with R_BLE_GAP_Init)
void ble_gap_callback(uint16_t event_type, ble_status_t result, st_ble_evt_data_t *p_data) {
    switch (event_type) {
        case BLE_GAP_EVENT_CONN_IND:
            ra_ble_event_push(BLE_EVT_GAP_CONNECTED, 
                             p_data->conn_ind.conn_hdl, 0, NULL, 0);
            break;
            
        case BLE_GAP_EVENT_DISCONN_IND:
            ra_ble_event_push(BLE_EVT_GAP_DISCONNECTED,
                             p_data->disconn_ind.conn_hdl, 0, NULL, 0);
            break;
            
        // ... handle other events
    }
}

// GATT Server callback
void ble_gatts_callback(uint16_t event_type, ble_status_t result, st_ble_gatts_evt_data_t *p_data) {
    switch (event_type) {
        case BLE_GATTS_EVENT_WRITE_REQ:
            ra_ble_event_push(BLE_EVT_GATT_WRITE,
                             p_data->write_req.conn_hdl,
                             p_data->write_req.attr_hdl,
                             p_data->write_req.value,
                             p_data->write_req.value_len);
            break;
            
        // ... handle other events
    }
}
```

---

## Main Loop Integration

```c
// In main.c REPL loop
void mp_ble_process_events(void) {
    ble_event_t evt;
    
    while (ra_ble_event_pop(&evt)) {
        // Now safe to call Python code
        mp_obj_t callback = mp_ble_get_callback(evt.type);
        
        if (callback != mp_const_none) {
            mp_obj_t args[3];
            args[0] = mp_obj_new_int(evt.conn_handle);
            args[1] = mp_obj_new_int(evt.attr_handle);
            args[2] = mp_obj_new_bytes(evt.data, evt.data_len);
            
            mp_call_function_n_kw(callback, 3, 0, args);
        }
    }
}

// Add to main REPL loop:
for (;;) {
    mp_ble_process_events();  // Process BLE events
    
    if (pyexec_mode_kind == PYEXEC_MODE_RAW_REPL) {
        pyexec_raw_repl();
    } else {
        pyexec_friendly_repl();
    }
}
```

---

## Memory Considerations

**Queue Size:** `MICROPY_BLE_EVENT_QUEUE_SIZE = 32`
- Each event: ~70 bytes
- Total: ~2.2 KB static RAM

**Payload Limit:** `BLE_EVENT_MAX_PAYLOAD = 64`
- For larger data (e.g., long writes), use memory pool or dynamic allocation

**Overflow Policy:** Drop oldest events (current implementation drops newest)

---

## Next Steps

1. Implement `ra_ble_events.c` with ring buffer
2. Register FSP callbacks
3. Integrate into main loop
4. Test event throughput and latency

