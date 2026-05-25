CREATE TABLE demo.device_config (
  id          BIGSERIAL    PRIMARY KEY,
  deveui      TEXT         NOT NULL,
  key         TEXT         NOT NULL,
  key_id      SMALLINT     NOT NULL,
  value       BYTEA        NOT NULL,
  ts          TIMESTAMPTZ  NOT NULL DEFAULT NOW(),
  pushed_at   TIMESTAMPTZ,
  applied_at  TIMESTAMPTZ,         -- set when gateway tx_ack received (iter 1 semantic only)
  applied_ok  BOOLEAN               -- TRUE iff gateway TX'd the DL; does NOT mean device decoded it
);
CREATE INDEX dc_deveui_idx   ON demo.device_config (deveui);
CREATE INDEX dc_pending_idx  ON demo.device_config (deveui, key) WHERE pushed_at IS NULL;
CREATE INDEX dc_inflight_idx ON demo.device_config (id) WHERE pushed_at IS NOT NULL AND applied_at IS NULL;

CREATE TABLE demo.downlinks_inflight (
  queue_id  UUID         PRIMARY KEY,
  cfg_id    BIGINT       NOT NULL REFERENCES demo.device_config(id),
  created   TIMESTAMPTZ  NOT NULL DEFAULT NOW()
);

CREATE OR REPLACE FUNCTION demo.encode_u32_le(v BIGINT) RETURNS BYTEA
LANGUAGE SQL IMMUTABLE AS $$
  SELECT decode(
    lpad(to_hex((v) & 255)::text, 2, '0') ||
    lpad(to_hex((v >> 8) & 255)::text, 2, '0') ||
    lpad(to_hex((v >> 16) & 255)::text, 2, '0') ||
    lpad(to_hex((v >> 24) & 255)::text, 2, '0'),
    'hex')
$$;
