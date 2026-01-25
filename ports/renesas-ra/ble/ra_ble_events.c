#include <string.h>
#include "ra_ble_events.h"

#if defined(__ARMCC_VERSION) || defined(__GNUC__) || defined(__ICCARM__)
#include "cmsis_compiler.h"
#endif
#ifndef __DMB
#define __DMB() __asm volatile ("dmb" ::: "memory")
#endif

typedef struct {
    ble_event_t events[MICROPY_BLE_EVENT_QUEUE_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
    ble_event_stats_t stats;
} ble_event_queue_t;

static ble_event_queue_t g_ble_event_queue;

void ra_ble_event_queue_init(void) {
    memset(&g_ble_event_queue, 0, sizeof(g_ble_event_queue));
}

void ra_ble_event_push(ble_event_type_t type, uint16_t conn_hdl,
                       uint16_t attr_hdl, const uint8_t *data, uint16_t len) {
    uint16_t next_head = (uint16_t)((g_ble_event_queue.head + 1) % MICROPY_BLE_EVENT_QUEUE_SIZE);
    if (next_head == g_ble_event_queue.tail) {
        g_ble_event_queue.stats.dropped++;
        return;
    }

    ble_event_t *evt = &g_ble_event_queue.events[g_ble_event_queue.head];
    evt->type = type;
    evt->conn_handle = conn_hdl;
    evt->attr_handle = attr_hdl;
    evt->data_len = (len > BLE_EVENT_MAX_PAYLOAD) ? BLE_EVENT_MAX_PAYLOAD : len;
    if (data && evt->data_len) {
        memcpy(evt->data, data, evt->data_len);
    }

    __DMB();
    g_ble_event_queue.head = next_head;
    g_ble_event_queue.stats.pushed++;
}

bool ra_ble_event_pop(ble_event_t *evt) {
    __DMB();
    if (g_ble_event_queue.tail == g_ble_event_queue.head) {
        return false;
    }

    memcpy(evt, &g_ble_event_queue.events[g_ble_event_queue.tail], sizeof(*evt));
    g_ble_event_queue.tail = (uint16_t)((g_ble_event_queue.tail + 1) % MICROPY_BLE_EVENT_QUEUE_SIZE);
    g_ble_event_queue.stats.popped++;
    return true;
}

bool ra_ble_event_queue_empty(void) {
    return (g_ble_event_queue.tail == g_ble_event_queue.head);
}

void ra_ble_event_get_stats(ble_event_stats_t *stats) {
    if (stats) {
        memcpy(stats, &g_ble_event_queue.stats, sizeof(*stats));
    }
}
