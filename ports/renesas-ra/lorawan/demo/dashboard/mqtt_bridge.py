#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
mqtt_bridge.py - subscribes to ChirpStack MQTT, decodes LoRaWAN demo payloads,
inserts rows into PostgreSQL demo schema for Grafana dashboard.

Payload format (common header, 6 bytes):
  temp_c100   int16  LE  bytes 0-1
  hum_p2      uint8      byte  2
  battery_mv  uint16 LE  bytes 3-4
  flags       uint8      byte  5

Class B appends (2 bytes):
  b_status        uint8  byte 6
  ping_rx_count   uint8  byte 7

Class C appends (3 bytes):
  dl_count        uint8  byte  6
  last_dl_lat_ms  uint16 LE bytes 7-8
"""

import json
import struct
import base64
import logging
import signal
import sys
import time
import paho.mqtt.client as mqtt
import psycopg2

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("mqtt_bridge")

MQTT_HOST = "localhost"
MQTT_PORT = 1883
APP_ID    = "afa9488a-a5e5-4ab7-b20d-185e807ad5ac"
TOPIC_SUB = f"application/{APP_ID}/device/+/event/+"
PG_DSN    = "host=localhost dbname=chirpstack user=chirpstack password=chirpstack"

DEVEUI_CLASS = {
    "70b3d57ed0070001": "A",
    "70b3d57ed0070002": "B",
    "70b3d57ed0070003": "C",
}


def decode_payload(raw_b64, deveui):
    """Decode base64 payload bytes into field dict. Returns {} on error.

    Layout (with end-side link quality):
      bytes 0-5  : header (temp_c100 i16, hum_p2 u8, battery_mv u16, flags u8)
      bytes 6-7  : end-side downlink quality (dl_rssi i8, dl_snr i8) [v2]
      bytes 8+   : per-class extras (B: b_status,ping_rx_count; C: dl_count,lat_ms)

    For backward compat: if payload is exactly 6/8/9 bytes (old format
    without end-side quality), skip the new fields.
    """
    try:
        data = base64.b64decode(raw_b64)
    except Exception:
        return {}
    if len(data) < 6:
        return {}

    temp_c100, hum_p2, battery_mv, flags = struct.unpack_from("<hBHB", data, 0)
    fields = {
        "temp_c100":  temp_c100,
        "hum_p2":     hum_p2,
        "battery_mv": battery_mv,
        "flags":      flags,
    }

    # NEW v2 format: bytes 6-7 are dl_rssi (int8), dl_snr (int8)
    # Detect by length: v1 layouts were 6 (A), 8 (B), 9 (C); v2 is 8 (A), 10 (B), 11 (C)
    cls = DEVEUI_CLASS.get(deveui.lower(), "?")
    has_dl_quality = False
    if cls == "A" and len(data) >= 8:
        has_dl_quality = True
    elif cls == "B" and len(data) >= 10:
        has_dl_quality = True
    elif cls == "C" and len(data) >= 11:
        has_dl_quality = True

    if has_dl_quality:
        dl_rssi, dl_snr = struct.unpack_from("bb", data, 6)
        fields["dl_rssi"] = dl_rssi
        fields["dl_snr"]  = dl_snr
        extras_offset = 8
    else:
        extras_offset = 6

    if cls == "B" and len(data) >= extras_offset + 2:
        fields["b_status"], fields["ping_rx_count"] = struct.unpack_from(
            "BB", data, extras_offset)
    elif cls == "C" and len(data) >= extras_offset + 3:
        fields["dl_count"] = data[extras_offset]
        fields["last_dl_lat_ms"] = struct.unpack_from(
            "<H", data, extras_offset + 1)[0]
    return fields


def insert_uplink(cur, deveui, evt, payload_fields):
    cls = DEVEUI_CLASS.get(deveui.lower(), "?")
    rx_list = evt.get("rxInfo") or [{}]
    rx = rx_list[0] if rx_list else {}
    tx = evt.get("txInfo") or {}
    mod = (tx.get("modulation") or {}).get("lora") or {}
    cur.execute("""
        INSERT INTO demo.uplinks
            (deveui, class, fcnt, fport, rssi, snr, dr, channel_freq,
             temp_c100, hum_p2, battery_mv, flags,
             ping_rx_count, b_status, dl_count, last_dl_lat_ms,
             dl_rssi, dl_snr)
        VALUES (%s,%s,%s,%s,%s,%s,%s,%s, %s,%s,%s,%s, %s,%s,%s,%s, %s,%s)
    """, (
        deveui.upper(), cls,
        evt.get("fCnt"), evt.get("fPort"),
        rx.get("rssi"), rx.get("snr"),
        evt.get("dr"), tx.get("frequency"),
        payload_fields.get("temp_c100"), payload_fields.get("hum_p2"),
        payload_fields.get("battery_mv"), payload_fields.get("flags"),
        payload_fields.get("ping_rx_count"), payload_fields.get("b_status"),
        payload_fields.get("dl_count"), payload_fields.get("last_dl_lat_ms"),
        payload_fields.get("dl_rssi"), payload_fields.get("dl_snr"),
    ))


def insert_event(cur, deveui, etype, summary):
    cur.execute(
        "INSERT INTO demo.events (deveui, etype, summary) VALUES (%s,%s,%s)",
        (deveui.upper() if deveui else None, etype, summary[:200]),
    )


def on_connect(client, userdata, flags, rc, props=None):
    log.info("MQTT connected rc=%s", rc)
    client.subscribe(TOPIC_SUB, qos=1)
    log.info("Subscribed: %s", TOPIC_SUB)


def on_disconnect(client, userdata, flags, rc, props=None):
    log.warning("MQTT disconnected rc=%s, will auto-reconnect", rc)


def on_message(client, userdata, msg):
    db = userdata["db"]
    parts = msg.topic.split("/")
    if len(parts) < 6:
        return
    deveui = parts[3]
    etype  = parts[5]
    try:
        evt = json.loads(msg.payload)
    except Exception as e:
        log.warning("JSON decode failed: %s", e)
        return

    try:
        with db.cursor() as cur:
            if etype == "up":
                raw = evt.get("data", "")
                fields = decode_payload(raw, deveui) if raw else {}
                insert_uplink(cur, deveui, evt, fields)
                rx0 = (evt.get("rxInfo") or [{}])[0]
                summary = "fCnt=%s fPort=%s RSSI=%sdBm SNR=%s" % (
                    evt.get("fCnt"), evt.get("fPort"),
                    rx0.get("rssi"), rx0.get("snr"))
                insert_event(cur, deveui, "up", summary)
            elif etype == "join":
                insert_event(cur, deveui, "join",
                             "Join DevAddr=" + str(evt.get("devAddr", "?")))
            elif etype == "ack":
                insert_event(cur, deveui, "ack", "fCnt=%s" % evt.get("fCnt"))
                cur.execute(
                    "INSERT INTO demo.downlinks (deveui, class, fcnt) VALUES (%s,%s,%s)",
                    (deveui.upper(),
                     DEVEUI_CLASS.get(deveui.lower(), "?"),
                     evt.get("fCnt")))
            elif etype == "txack":
                qid = evt.get("queueItemId")
                if qid:
                    cur.execute("""
                        UPDATE demo.device_config
                           SET applied_at = NOW(),
                               applied_ok = TRUE
                         WHERE id = (SELECT cfg_id FROM demo.downlinks_inflight
                                      WHERE queue_id = %s::uuid)
                    """, (qid,))
                    cur.execute("DELETE FROM demo.downlinks_inflight WHERE queue_id = %s::uuid", (qid,))
                insert_event(cur, deveui, "txack", "queueItemId=%s" % qid)
            elif etype in ("log", "status"):
                insert_event(cur, deveui, etype, json.dumps(evt)[:200])
        db.commit()
    except Exception as e:
        log.error("DB error: %s", e)
        try:
            db.rollback()
        except Exception:
            pass


def connect_db_with_retry():
    for attempt in range(10):
        try:
            db = psycopg2.connect(PG_DSN)
            db.autocommit = False
            log.info("PostgreSQL connected")
            return db
        except Exception as e:
            log.warning("DB connect attempt %d failed: %s", attempt + 1, e)
            time.sleep(2)
    raise SystemExit("ERROR: PostgreSQL not reachable after 10 attempts")


def main():
    db = connect_db_with_retry()

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="demo_bridge")
    client.user_data_set({"db": db})
    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message

    def _stop(sig, frame):
        log.info("Stopping bridge")
        try:
            client.disconnect()
            db.close()
        except Exception:
            pass
        sys.exit(0)

    signal.signal(signal.SIGTERM, _stop)
    signal.signal(signal.SIGINT,  _stop)

    client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
    log.info("Bridge started")
    client.loop_forever()


if __name__ == "__main__":
    main()
