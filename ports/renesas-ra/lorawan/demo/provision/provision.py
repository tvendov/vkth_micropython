#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
provision.py — LoRaWAN Demo Device Provisioning Tool

Reads devices.yaml and applies device_profile + device + device_keys
INSERT statements to ChirpStack PostgreSQL via SSH/plink.

Avoids the manual SQL traps:
  - reg_params_revision: 'A' (NOT 'RP002_1_0_3')
  - class_b_params/class_c_params: field name 'timeout' (NOT 'class_x_timeout')
  - mac_version: '1.0.4' (NOT 'LORAWAN_1_0_4')
  - All NOT NULL fields auto-populated with safe defaults.

Usage:
  python3 provision.py            # apply all devices from devices.yaml
  python3 provision.py --dry-run  # print SQL only, no exec
  python3 provision.py --verify   # query DB and print current state
  python3 provision.py --delete   # remove all devices in yaml from DB

Requirements:
  - Python 3.8+
  - pip install pyyaml
  - plink.exe in PATH (Windows) OR ssh+sshpass (Linux)
"""

import argparse
import os
import secrets
import shutil
import subprocess
import sys
import uuid
from pathlib import Path

try:
    import yaml
except ImportError:
    print("ERROR: pip install pyyaml")
    sys.exit(1)


# ---------------- SSH/plink wrapper ----------------

def find_ssh_tool():
    """Find available SSH-with-password tool."""
    for tool in ("plink.exe", "plink", "sshpass"):
        path = shutil.which(tool) or shutil.which(
            f"C:/Program Files/PuTTY/{tool}"
        )
        if path:
            return tool, path
    return None, None


def is_local(server: dict) -> bool:
    """Detect if we're running ON the chirpstack server itself."""
    if os.environ.get("PROVISION_LOCAL") == "1":
        return True
    # Check if psql is available + we can sudo to postgres
    if shutil.which("psql"):
        try:
            r = subprocess.run(
                ["sudo", "-n", "-u", "postgres", "psql", "-d", "chirpstack",
                 "-tAc", "SELECT 1"],
                capture_output=True, text=True, timeout=5
            )
            if r.returncode == 0 and r.stdout.strip() == "1":
                return True
        except Exception:
            pass
    return False


def ssh_run(server: dict, sql: str) -> tuple[int, str, str]:
    """Run SQL via psql on remote server. Returns (rc, stdout, stderr)."""
    # Local fast-path: skip SSH if we're already on the server
    if is_local(server):
        proc = subprocess.run(
            ["sudo", "-u", "postgres", "psql", "-d", "chirpstack", "-X", "-q", "-t"],
            input=sql, capture_output=True, text=True, timeout=30
        )
        return proc.returncode, proc.stdout, proc.stderr

    tool, path = find_ssh_tool()
    if not tool:
        sys.exit("ERROR: plink.exe (Windows) or sshpass (Linux) not found")

    # Use stdin for SQL to avoid quote escaping hell
    if tool.startswith("plink"):
        cmd = [
            path, "-ssh", "-batch",
            "-hostkey", server["hostkey"],
            "-pw", server["pass"],
            f"{server['user']}@{server['host']}",
            "sudo -u postgres psql -d chirpstack -X -q -t",
        ]
    else:
        cmd = [
            "sshpass", "-p", server["pass"],
            "ssh", "-o", "StrictHostKeyChecking=accept-new",
            f"{server['user']}@{server['host']}",
            "sudo -u postgres psql -d chirpstack -X -q -t",
        ]

    proc = subprocess.run(
        cmd, input=sql, capture_output=True, text=True, timeout=30
    )
    return proc.returncode, proc.stdout, proc.stderr


# ---------------- SQL builders ----------------

def quote_str(s: str) -> str:
    """Single-quote PostgreSQL string with escape."""
    return "'" + s.replace("'", "''") + "'"


def build_profile_sql(d: dict, defaults: dict, tenant_id: str, codec_text: str) -> tuple[str, str]:
    """Build INSERT SQL for device_profile. Returns (profile_id, sql)."""
    profile_id = str(uuid.uuid4())

    # Merge defaults with device-specific
    region              = defaults["region"]
    mac_version         = defaults["mac_version"]
    reg_params_revision = defaults["reg_params_revision"]
    adr_algorithm_id    = defaults["adr_algorithm_id"]
    rx1_delay           = defaults["rx1_delay"]
    dev_status_interval = defaults["device_status_req_interval"]
    flush_queue         = "true" if defaults["flush_queue_on_activate"] else "false"
    allow_roaming       = "true" if defaults["allow_roaming"] else "false"
    dr_array            = "{" + ",".join(str(v) for v in defaults["supported_uplink_data_rates"]) + "}"

    supports_class_b = "true" if d.get("supports_class_b", False) else "false"
    supports_class_c = "true" if d.get("supports_class_c", False) else "false"

    # JSONB params — keep empty {} for unused class
    import json as _json
    class_b_params = _json.dumps(d.get("class_b_params", {}))
    class_c_params = _json.dumps(d.get("class_c_params", {}))

    uplink_interval = int(d.get("uplink_interval", 60))

    # Use dollar-quoting for codec to avoid escape issues
    codec_quoted = f"$CODEC$ {codec_text} $CODEC$"

    sql = f"""
INSERT INTO device_profile (
    id, tenant_id, created_at, updated_at,
    name, description, region, mac_version, reg_params_revision,
    adr_algorithm_id,
    payload_codec_runtime, payload_codec_script,
    uplink_interval, device_status_req_interval,
    supports_otaa, supports_class_b, supports_class_c,
    class_b_params, class_c_params,
    tags, measurements, auto_detect_measurements,
    flush_queue_on_activate, allow_roaming,
    rx1_delay, app_layer_params,
    firmware_version, vendor_profile_id,
    supported_uplink_data_rates
) VALUES (
    {quote_str(profile_id)}, {quote_str(tenant_id)}, NOW(), NOW(),
    {quote_str(d['profile_name'])},
    {quote_str(d.get('profile_description', ''))},
    {quote_str(region)}, {quote_str(mac_version)}, {quote_str(reg_params_revision)},
    {quote_str(adr_algorithm_id)},
    'JS', {codec_quoted},
    {uplink_interval}, {dev_status_interval},
    true, {supports_class_b}, {supports_class_c},
    {quote_str(class_b_params)}::jsonb, {quote_str(class_c_params)}::jsonb,
    '{{}}'::jsonb, '{{}}'::jsonb, true,
    {flush_queue}, {allow_roaming},
    {rx1_delay}, '{{}}'::jsonb,
    '', 0,
    {quote_str(dr_array)}::smallint[]
);
"""
    return profile_id, sql


def build_device_sql(d: dict, defaults: dict, app_id: str, profile_id: str) -> str:
    join_eui = d.get("join_eui", defaults["join_eui"])
    enabled_class = d.get("enabled_class", "A")
    skip_fcnt = "true" if d.get("skip_fcnt_check", defaults.get("skip_fcnt_check", False)) else "false"
    ext_power = "true" if d.get("external_power_source", defaults.get("external_power_source", False)) else "false"

    return f"""
INSERT INTO device (
    dev_eui, application_id, device_profile_id, created_at, updated_at,
    name, description, external_power_source,
    enabled_class, skip_fcnt_check, is_disabled,
    tags, variables, join_eui, app_layer_params, f_cnt_up
) VALUES (
    decode({quote_str(d['dev_eui'])}, 'hex'),
    {quote_str(app_id)},
    {quote_str(profile_id)},
    NOW(), NOW(),
    {quote_str(d['name'])},
    {quote_str(d.get('description', ''))},
    {ext_power},
    {quote_str(enabled_class)}, {skip_fcnt}, false,
    '{{}}'::jsonb, '{{}}'::jsonb,
    decode({quote_str(join_eui)}, 'hex'),
    '{{}}'::jsonb, 0
);
"""


def build_keys_sql(d: dict) -> str:
    return f"""
INSERT INTO device_keys (
    dev_eui, created_at, updated_at, nwk_key, app_key,
    dev_nonces, join_nonce, gen_app_key
) VALUES (
    decode({quote_str(d['dev_eui'])}, 'hex'), NOW(), NOW(),
    decode({quote_str(d['app_key'])}, 'hex'),
    decode({quote_str(d['app_key'])}, 'hex'),
    '{{}}'::jsonb, 0,
    decode('00000000000000000000000000000000', 'hex')
);
"""


def build_delete_sql(d: dict) -> str:
    return f"""
DELETE FROM device WHERE dev_eui = decode({quote_str(d['dev_eui'])}, 'hex');
DELETE FROM device_profile WHERE name = {quote_str(d['profile_name'])};
"""


# ---------------- Main flow ----------------

def load_codec(yaml_dir: Path, codec_file: str) -> str:
    path = yaml_dir / codec_file
    if not path.exists():
        sys.exit(f"ERROR: codec file not found: {path}")
    return path.read_text(encoding="utf-8")


def cmd_apply(cfg: dict, yaml_dir: Path, dry_run: bool) -> int:
    server = cfg["server"]
    tenant_id = cfg["tenant_id"]
    app_id = cfg["application_id"]
    defaults = cfg["defaults"]

    print("=" * 60)
    print(f"PROVISIONING {len(cfg['devices'])} devices to {server['host']}")
    print(f"Tenant:      {tenant_id}")
    print(f"Application: {app_id}")
    print(f"Mode:        {'DRY-RUN' if dry_run else 'APPLY'}")
    print("=" * 60)

    errors = 0
    for d in cfg["devices"]:
        # Generate AppKey if missing
        if not d.get("app_key"):
            d["app_key"] = secrets.token_hex(16).upper()
            print(f"\n[{d['name']}] Generated AppKey: {d['app_key']}")

        codec_text = load_codec(yaml_dir, d["codec_file"])

        profile_id, sql_p = build_profile_sql(d, defaults, tenant_id, codec_text)
        sql_d = build_device_sql(d, defaults, app_id, profile_id)
        sql_k = build_keys_sql(d)

        # Wrap in transaction so all-or-nothing
        full_sql = f"BEGIN;\n{sql_p}\n{sql_d}\n{sql_k}\nCOMMIT;\n"

        print(f"\n[{d['name']}]")
        print(f"  DevEUI:     {d['dev_eui']}")
        print(f"  AppKey:     {d['app_key']}")
        print(f"  Profile:    {d['profile_name']} (id={profile_id})")
        print(f"  Class:      {d.get('enabled_class', 'A')} (B={d.get('supports_class_b', False)}, C={d.get('supports_class_c', False)})")
        print(f"  Codec:      {d['codec_file']} ({len(codec_text)} chars)")

        if dry_run:
            print(f"  SQL preview ({len(full_sql)} chars):")
            print("  " + full_sql.strip()[:300].replace("\n", "\n  ") + "...")
            continue

        rc, out, err = ssh_run(server, full_sql)
        if rc != 0 or "ERROR" in (out + err):
            print(f"  ✗ FAILED rc={rc}")
            print(f"    stdout: {out.strip()[:300]}")
            print(f"    stderr: {err.strip()[:300]}")
            errors += 1
        else:
            print(f"  ✓ OK")

    print()
    print("=" * 60)
    if errors:
        print(f"COMPLETED WITH {errors} ERROR(S)")
        return 1
    print("ALL DEVICES PROVISIONED ✓")
    return 0


def cmd_verify(cfg: dict) -> int:
    server = cfg["server"]
    eui_list = ",".join(
        f"decode('{d['dev_eui']}', 'hex')"
        for d in cfg["devices"]
    )
    sql = f"""
SELECT d.name, encode(d.dev_eui,'hex') AS eui,
       d.enabled_class AS cls,
       dp.mac_version, dp.reg_params_revision AS rp,
       dp.supports_class_b AS B, dp.supports_class_c AS C,
       d.last_seen_at, d.f_cnt_up
FROM device d JOIN device_profile dp ON dp.id = d.device_profile_id
WHERE d.dev_eui IN ({eui_list})
ORDER BY d.name;
"""
    print("Querying current state...\n")
    rc, out, err = ssh_run(server, sql)
    print(out)
    if err.strip():
        print(f"stderr: {err.strip()}")
    return rc


def cmd_delete(cfg: dict, dry_run: bool) -> int:
    server = cfg["server"]
    print(f"DELETING {len(cfg['devices'])} devices...")
    for d in cfg["devices"]:
        sql = build_delete_sql(d)
        print(f"\n[{d['name']}] dev_eui={d['dev_eui']}")
        if dry_run:
            print(f"  SQL: {sql.strip()}")
            continue
        rc, out, err = ssh_run(server, sql)
        if rc == 0:
            print(f"  ✓ deleted")
        else:
            print(f"  ✗ failed: {err.strip()[:200]}")
    return 0


def main():
    ap = argparse.ArgumentParser(description="Provision LoRaWAN demo devices")
    ap.add_argument("--config", default="devices.yaml", help="YAML config (default: devices.yaml)")
    ap.add_argument("--dry-run", action="store_true", help="Print SQL only, do not execute")
    ap.add_argument("--verify", action="store_true", help="Query and print current DB state")
    ap.add_argument("--delete", action="store_true", help="Delete all devices in config from DB")
    args = ap.parse_args()

    yaml_path = Path(args.config).resolve()
    if not yaml_path.exists():
        sys.exit(f"ERROR: config file not found: {yaml_path}")

    cfg = yaml.safe_load(yaml_path.read_text(encoding="utf-8"))
    yaml_dir = yaml_path.parent

    if args.verify:
        return cmd_verify(cfg)
    if args.delete:
        return cmd_delete(cfg, args.dry_run)
    return cmd_apply(cfg, yaml_dir, args.dry_run)


if __name__ == "__main__":
    sys.exit(main())
