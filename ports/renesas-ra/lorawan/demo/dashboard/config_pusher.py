#!/usr/bin/env python3
# config_pusher.py - VK_RA4M2 LoRaWAN config writeback service.
# Polls demo.device_config for unsent rows; enqueues ChirpStack downlinks
# on FPort=100, payload = key_id || value (little-endian per fPort 100 spec).

import time
import logging
import psycopg2
from psycopg2.extras import RealDictCursor

PG_DSN   = "host=localhost dbname=chirpstack user=chirpstack password=chirpstack"
POLL_SEC = 5
FPORT    = 100

logging.basicConfig(level=logging.INFO,
                    format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("cfgpush")


def push_pending(cur):
    cur.execute("""
        SELECT id, deveui, key_id, value
          FROM demo.device_config
         WHERE pushed_at IS NULL
         ORDER BY ts ASC
         LIMIT 50
    """)
    rows = cur.fetchall()
    for r in rows:
        cfg_id = r["id"]
        deveui = r["deveui"].lower()

        # 1. Verify dev_eui exists in ChirpStack
        cur.execute("SELECT 1 FROM device WHERE dev_eui = decode(%s,'hex')",
                    (deveui,))
        if not cur.fetchone():
            log.warning("cfg_id=%d dev_eui=%s NOT in ChirpStack - skip+mark",
                        cfg_id, deveui)
            cur.execute("""
                UPDATE demo.device_config
                   SET pushed_at  = NOW(),
                       applied_at = NOW(),
                       applied_ok = FALSE
                 WHERE id = %s
            """, (cfg_id,))
            continue

        # 2. Build payload key_id || value
        payload = bytes([r["key_id"]]) + bytes(r["value"])

        # 3. Insert into ChirpStack device_queue_item
        cur.execute("""
            INSERT INTO device_queue_item
              (id, dev_eui, f_port, confirmed, data, created_at, is_pending, is_encrypted)
            VALUES
              (gen_random_uuid(), decode(%s,'hex'), %s, FALSE, %s, NOW(), FALSE, FALSE)
            RETURNING id
        """, (deveui, FPORT, payload))
        queue_id = cur.fetchone()["id"]

        # 4. Stamp + remember mapping
        cur.execute("UPDATE demo.device_config SET pushed_at = NOW() WHERE id = %s",
                    (cfg_id,))
        cur.execute("""
            INSERT INTO demo.downlinks_inflight (queue_id, cfg_id) VALUES (%s, %s)
        """, (queue_id, cfg_id))

        log.info("PUSH cfg_id=%d dev=%s key_id=0x%02x len=%d queue_id=%s",
                 cfg_id, deveui, r["key_id"], len(payload), queue_id)


def main():
    log.info("config_pusher starting; poll=%ds fport=%d", POLL_SEC, FPORT)
    while True:
        try:
            with psycopg2.connect(PG_DSN) as pg, \
                 pg.cursor(cursor_factory=RealDictCursor) as cur:
                push_pending(cur)
                pg.commit()
        except Exception as e:
            log.exception("push cycle error: %r", e)
        time.sleep(POLL_SEC)


if __name__ == "__main__":
    main()
