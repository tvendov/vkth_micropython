#ifndef MICROPY_INCLUDED_RENESAS_RA_BLE_EVENTS_H
#define MICROPY_INCLUDED_RENESAS_RA_BLE_EVENTS_H

#include <stdint.h>
#include <stdbool.h>

#ifndef MICROPY_BLE_EVENT_QUEUE_SIZE
#define MICROPY_BLE_EVENT_QUEUE_SIZE (32)
#endif

#ifndef BLE_EVENT_MAX_PAYLOAD
#define BLE_EVENT_MAX_PAYLOAD (64)
#endif

typedef enum {
    BLE_EVT_NONE = 0,

    /* GAP */
    BLE_EVT_STACK_READY,
    BLE_EVT_GAP_CONNECTED,
    BLE_EVT_GAP_DISCONNECTED,
    BLE_EVT_GAP_ADV_STARTED,
    BLE_EVT_GAP_ADV_STOPPED,

    /* GATTS */
    BLE_EVT_GATTS_WRITE,
    BLE_EVT_GATTS_READ,
    BLE_EVT_GATTS_NOTIFY_COMPLETE,
    BLE_EVT_GATTS_INDICATE_COMPLETE,

    BLE_EVT_MAX
} ble_event_type_t;

typedef struct {
    ble_event_type_t type;
    uint16_t conn_handle;
    uint16_t attr_handle;
    uint16_t data_len;
    uint8_t  data[BLE_EVENT_MAX_PAYLOAD];
} ble_event_t;

typedef struct {
    uint32_t pushed;
    uint32_t popped;
    uint32_t dropped;
} ble_event_stats_t;

void ra_ble_event_queue_init(void);
void ra_ble_event_push(ble_event_type_t type, uint16_t conn_hdl,
                       uint16_t attr_hdl, const uint8_t *data, uint16_t len);
bool ra_ble_event_pop(ble_event_t *evt);
bool ra_ble_event_queue_empty(void);
void ra_ble_event_get_stats(ble_event_stats_t *stats);

#endif
