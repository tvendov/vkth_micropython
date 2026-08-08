# Device Control Interface — Grafana contextual control + unified uplink-interval

Last assignment (2026-05-28). Per-device control of the LoRaWAN demo devices from
the Grafana dashboard: relay (Device A) + uplink interval (all devices), with a
**contextual tabbed UI** (select A/B/C → only that device's controls show).

Pipeline: Grafana panel → HTTP endpoint (`p109-ctl` :8081) → ChirpStack
`device_queue_item` → LoRaWAN downlink → device applies → confirming uplink →
`demo.uplinks` → Grafana.

Server: `192.168.2.130` (ChirpStack 4.17 + Grafana 13.0.1 + Postgres). SSH
`vkrz` / `vkrzg2lc`, host-key SHA256:BMG0U+ohV23QgKDjh6WCIn9gbUvNyLeic44prvT9w2U.

Devices (Application UUID afa9488a-a5e5-4ab7-b20d-185e807ad5ac):
- A `70B3D57ED0070001` Class A — AppKey `9A7F263557E26259B7061BD6FC8EBA27`
- B `70B3D57ED0070002` Class B — no demo running / never joined
- C `70B3D57ED0070003` Class C — continuous RX
JoinEUI `0000000000000000`.

---

## 1. Relay control (Device A, P103)

- DL: fPort **20**, cmd **0x04**, 1 byte: `[0x04, 0|1]` → `_relay_state` + `Pin("P103")`.
- UL feedback: `flags` bit 3 (0x08) = relay state.
- Fast-confirm: relay DL sets `_force_uplink` → device sends a confirming uplink
  early (>=3 s spacing for EU868 1% duty cycle) instead of waiting the full
  interval, so Grafana reflects the new state in ~3-4 s instead of ~30 s.

## 2. Unified uplink-interval protocol (A + C; B when it has a demo)

Replaces the divergent per-device schemes (A: cmd0x01 / 10 s steps; C: cmd0x01 /
minutes on fPort 23).

- **Set:** fPort **20**, cmd **0x05**, 1 byte = `seconds / 5`.
  Range 10–1275 s, **step 5 s**. Device: `interval = data[1] * 5`.
- **Feedback:** append **1 byte** `interval_s / 5` at the END of the uplink
  payload (A and C) so Grafana shows the real active interval.
  - A payload: `<hBHBbb` (8 B) → `<hBHBbbB` (9 B, +interval).
  - C payload: 11 B → 12 B (+interval byte at end).
- **Cancel scope:** the endpoint cancels only same-command pending queue items
  (`get_byte(data,0)` matches), so relay (0x04) and interval (0x05) on the shared
  fPort 20 do not clobber each other.
- Validation (server): `seconds % 5 == 0`, `10 <= seconds <= 1275` (DC floor).

## 3. HTTP endpoint — `/opt/demo/p109_ctl.py` (systemd `p109-ctl`, :8081)

- `GET /p109?value=0|1` — Device A relay; cancels pending 0x04, queues new.
- `GET /p109/state` — `{device,pending,target}` for slider init.
- `GET /interval?dev=A|B|C&seconds=N` — validates, queues `[0x05, N/5]` fPort 20.
- `GET /interval/state?dev=A|B|C` — `{interval_s,pending,target}` for input init.
CORS `*`. DevEUI map A/B/C inside the file. Relay is Device A only.

## 4. Bridge / DB / codecs (master)

- `demo.uplinks` has column `interval_s integer` (nullable; NULL on old firmware).
- `/opt/demo/mqtt_bridge.py` decodes the trailing interval byte
  (A len>=9, C len>=12, B len>=11) → `interval_s = byte * 5`, backward-compatible.
- Codec files (`provision/codecs/codec_*.js`) are reference only — the endpoint
  inserts raw bytes and the bridge decodes for the DB, so codecs are not on the
  live data path (update for ChirpStack-UI parity only).

## 5. Grafana — contextual control panel

Dashboard `lorawan-demo` (file-provisioned `/var/lib/grafana/dashboards/lorawan-demo.json`,
reload ~10 s). Built/redeployed by `grafana_addpanel.py` (panels id 90 row + 91 text).

- Tabs **Device A / B / C** — clicking shows only that device's pane (contextual).
- Pane A: P103 relay slider (left OFF / right ON; "Will set: X" appears on flip,
  hides once queued) + uplink-interval row.
- Panes B, C: device note + uplink-interval row (offered for all devices).
- One-shot init (`<img onerror>`, `window.__dcinit` guard — NOT polling): slider
  ← `/p109/state`; each interval input ← `/interval/state?dev=X`.
- `disable_sanitize_html = true` in grafana.ini lets inline `onclick`/`onerror`
  run (Grafana does not execute injected `<script>` blocks).

## Status

DONE (master, live):
- relay control + fast-confirm uplink
- `/interval` + `/interval/state` endpoints, cmd-scoped cancel (collision fix)
- `interval_s` column + bridge decode (backward-compatible)
- contextual tabbed Grafana panel (A full; B/C interval offered, device-specific
  controls stubbed)

PENDING (slave / board firmware — `class_a_demo.py`, `class_c_demo.py`):
- handle cmd 0x05; append `interval_s/5` to uplink (A→9 B, C→12 B)
- OLED shows wake interval clearly (`30s`)
Until deployed: interval changes via 0x05 are ignored by current firmware and
`active` shows "—". Device B awaits any Class B demo.

Constraint: demo Python only — vendor LoRaMac stack is certified, never modified.
