#!/bin/bash
# preflight.sh — pre-demo system health check
# Runs ON the ChirpStack server (192.168.1.188) OR remotely via SSH.
#
# Usage:
#   ./preflight.sh                       # local (when run on server)
#   ssh user@server "bash -s" < preflight.sh    # remote
#
# Exit codes: 0 = all OK, 1 = warnings, 2 = critical failure

set -u
GREEN='\033[32m'
RED='\033[31m'
YELLOW='\033[33m'
BLUE='\033[36m'
RST='\033[0m'

GATEWAY_IP="192.168.1.187"
EXPECTED_GW_EUI="2cf7f110801003cb"
EXPECTED_DEVICES=("70b3d57ed0070001" "70b3d57ed0070002" "70b3d57ed0070003")

WARN=0
FAIL=0

ok()    { echo -e "  ${GREEN}✓${RST} $*"; }
warn()  { echo -e "  ${YELLOW}!${RST} $*"; WARN=$((WARN+1)); }
fail()  { echo -e "  ${RED}✗${RST} $*"; FAIL=$((FAIL+1)); }
info()  { echo -e "  ${BLUE}·${RST} $*"; }
hdr()   { echo -e "\n${BLUE}── $* ──${RST}"; }

# Ensure psql + ss available
need() {
    command -v "$1" >/dev/null 2>&1 || { fail "missing command: $1"; return 1; }
}

#─────────────────────────────────────────────────────────
hdr "Tools"
need ss      || true
need ping    || true
command -v psql >/dev/null 2>&1 && ok "psql found" || warn "psql not in PATH (will use sudo)"

#─────────────────────────────────────────────────────────
hdr "Services (systemd)"
for svc in chirpstack chirpstack-gateway-bridge mosquitto postgresql redis-server; do
    state=$(systemctl is-active "$svc" 2>/dev/null || echo "unknown")
    if [[ "$state" == "active" ]]; then
        ok "$svc → $state"
    else
        fail "$svc → $state"
    fi
done

#─────────────────────────────────────────────────────────
hdr "Listening ports"
check_port() {
    local port=$1 desc=$2 protocol=${3:-tcp}
    local flag="-tlnp"
    [[ "$protocol" == "udp" ]] && flag="-ulnp"
    if sudo ss $flag 2>/dev/null | grep -q ":$port "; then
        ok "$port/$protocol — $desc"
    else
        fail "$port/$protocol — $desc not listening"
    fi
}
check_port 8080 "ChirpStack UI/gRPC"
check_port 3001 "BasicStation websocket (gateway-bridge)"
check_port 1883 "MQTT broker"
check_port 5432 "PostgreSQL"
check_port 6379 "Redis"

#─────────────────────────────────────────────────────────
hdr "Gateway connectivity"
gw_age=$(sudo -u postgres psql -d chirpstack -tAc \
    "SELECT EXTRACT(EPOCH FROM (now() - last_seen_at))::int FROM gateway LIMIT 1;" 2>/dev/null)

if [[ -z "$gw_age" ]]; then
    fail "no gateway in DB"
elif [[ "$gw_age" -lt 60 ]]; then
    ok "gateway last_seen ${gw_age}s ago (< 60s) ✓"
elif [[ "$gw_age" -lt 300 ]]; then
    warn "gateway last_seen ${gw_age}s ago (60–300s) — possibly disconnected"
else
    fail "gateway last_seen ${gw_age}s ago (> 5min) — DISCONNECTED"
fi

# TCP sessions on bridge port
sessions=$(sudo ss -tn 2>/dev/null | grep -c ":3001 ")
if [[ "$sessions" -gt 0 ]]; then
    ok "bridge has $sessions active TCP session(s) on :3001"
else
    warn "no active sessions on :3001 (gateway not connected)"
fi

# Ping gateway IP
if ping -c 1 -W 2 "$GATEWAY_IP" >/dev/null 2>&1; then
    ok "ping $GATEWAY_IP (gateway IP)"
else
    warn "cannot ping $GATEWAY_IP"
fi

#─────────────────────────────────────────────────────────
hdr "Demo devices"
for eui in "${EXPECTED_DEVICES[@]}"; do
    row=$(sudo -u postgres psql -d chirpstack -tAc "
        SELECT d.name || '|' ||
               COALESCE(d.enabled_class,'?') || '|' ||
               dp.mac_version || '|' ||
               dp.reg_params_revision || '|' ||
               COALESCE(EXTRACT(EPOCH FROM (now() - d.last_seen_at))::text, 'never') || '|' ||
               d.f_cnt_up
        FROM device d JOIN device_profile dp ON dp.id = d.device_profile_id
        WHERE encode(d.dev_eui,'hex') = '$eui';" 2>/dev/null)

    if [[ -z "$row" ]]; then
        fail "$eui — not in DB"
        continue
    fi
    IFS='|' read -r name cls mac rp age fcnt <<<"$row"

    # Validate critical fields
    issues=""
    [[ "$mac" == "1.0.4" ]] || issues="$issues mac=$mac"
    [[ "$rp"  == "A"     ]] || issues="$issues rp=$rp"
    [[ -n "$cls" && "$cls" != "?" ]] || issues="$issues no-class"

    if [[ "$age" == "never" ]]; then
        if [[ -n "$issues" ]]; then
            fail "$name — never joined; problems:$issues"
        else
            warn "$name [$cls $mac/$rp] — never joined (board ON?)"
        fi
    else
        age_int=${age%.*}
        if [[ "$age_int" -lt 90 ]]; then
            ok "$name [$cls] — last_seen ${age_int}s ago, fCnt=$fcnt"
        elif [[ "$age_int" -lt 600 ]]; then
            warn "$name [$cls] — last_seen ${age_int}s ago (idle)"
        else
            warn "$name [$cls] — last_seen ${age_int}s ago (stale)"
        fi
    fi
done

#─────────────────────────────────────────────────────────
hdr "Recent errors (last 10 min)"
err_count=$(sudo journalctl -u chirpstack --since "10 min ago" --no-pager 2>/dev/null | \
    grep -cE "ERROR" | head -1)
if [[ "$err_count" -eq 0 ]]; then
    ok "0 errors in chirpstack log"
else
    warn "$err_count error(s) in chirpstack log — investigate:"
    sudo journalctl -u chirpstack --since "10 min ago" --no-pager 2>/dev/null | \
        grep "ERROR" | grep -v "fine_timestamp" | tail -3 | sed 's/^/      /'
fi

br_err=$(sudo journalctl -u chirpstack-gateway-bridge --since "10 min ago" --no-pager 2>/dev/null | \
    grep -cE "level=error|level=fatal")
if [[ "$br_err" -eq 0 ]]; then
    ok "0 errors in bridge log"
else
    warn "$br_err error(s) in bridge log"
fi

#─────────────────────────────────────────────────────────
hdr "Disk space"
disk_pct=$(df -P / | awk 'NR==2 {print $5}' | tr -d '%')
if [[ "$disk_pct" -lt 80 ]]; then
    ok "disk usage / = ${disk_pct}%"
elif [[ "$disk_pct" -lt 95 ]]; then
    warn "disk usage / = ${disk_pct}% — consider cleanup"
else
    fail "disk usage / = ${disk_pct}% — CRITICAL"
fi

#─────────────────────────────────────────────────────────
hdr "Recent radio traffic (last 2 min)"
uplinks=$(sudo journalctl -u chirpstack-gateway-bridge --since "2 min ago" --no-pager 2>/dev/null | \
    grep -cE "uplink frame|join-request")
stats=$(sudo journalctl -u chirpstack-gateway-bridge --since "2 min ago" --no-pager 2>/dev/null | \
    grep -c "event=stats")
info "uplinks (incl. proprietary): $uplinks  |  stats events: $stats"

#─────────────────────────────────────────────────────────
hdr "Summary"
TOTAL=$((WARN + FAIL))
if [[ "$FAIL" -gt 0 ]]; then
    echo -e "${RED}✗ FAIL: $FAIL critical, $WARN warnings${RST}"
    exit 2
elif [[ "$WARN" -gt 0 ]]; then
    echo -e "${YELLOW}! READY with $WARN warning(s)${RST}"
    exit 1
else
    echo -e "${GREEN}✓ ALL SYSTEMS GO${RST}"
    exit 0
fi
