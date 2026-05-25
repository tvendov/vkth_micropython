#!/bin/bash
# adr_parser.sh — parse chirpstack journal for LinkADRReq acknowledged events
# and update demo.adr_state with current TX power / DR / nb_trans per device.
#
# EU868 TX power table:
#   index 0 = 16 dBm
#   index 1 = 14 dBm
#   index 2 = 12 dBm
#   index 3 = 10 dBm
#   index 4 =  8 dBm
#   index 5 =  6 dBm
#   index 6 =  4 dBm
#   index 7 =  2 dBm

declare -A POW=(
  [0]=16 [1]=14 [2]=12 [3]=10 [4]=8 [5]=6 [6]=4 [7]=2
)

TARGET_DEVICES=("70b3d57ed0070001" "70b3d57ed0070002" "70b3d57ed0070003")

for eui in "${TARGET_DEVICES[@]}"; do
    line=$(journalctl -u chirpstack --no-pager 2>/dev/null \
           | grep "LinkADRReq acknowledged" | grep "$eui" | tail -1)
    [ -z "$line" ] && continue

    tx=$(echo "$line" | grep -oE "tx_power_index=[0-9]+" | grep -oE "[0-9]+")
    dr=$(echo "$line" | grep -oE "dr=[0-9]+" | grep -oE "[0-9]+")
    nb=$(echo "$line" | grep -oE "nb_trans=[0-9]+" | grep -oE "[0-9]+")
    [ -z "$tx" ] && continue
    dbm=${POW[$tx]:-0}
    eui_upper=$(echo "$eui" | tr 'a-z' 'A-Z')

    sudo -u postgres psql -d chirpstack -v ON_ERROR_STOP=1 -tAc <<EOF >/dev/null 2>&1
INSERT INTO demo.adr_state (deveui, ts, tx_power_index, tx_power_dbm, dr, nb_trans)
VALUES ('$eui_upper', NOW(), $tx, $dbm, $dr, $nb)
ON CONFLICT (deveui) DO UPDATE SET
  ts=NOW(),
  tx_power_index=EXCLUDED.tx_power_index,
  tx_power_dbm=EXCLUDED.tx_power_dbm,
  dr=EXCLUDED.dr,
  nb_trans=EXCLUDED.nb_trans;
EOF
done
