// Class B — fPort 11 uplink, 21 downlink
// Payload v2 (10 bytes): header(6) + dl_rssi(i8) + dl_snr(i8) + b_status(u8) + ping_rx_count(u8)
function decodeUplink(input) {
    if (input.fPort !== 11 || input.bytes.length < 6)
        return { errors: ["bad fPort or short payload"] };
    var b = input.bytes;
    var statusMap = ["ClassA", "ClassA-sim", "ClassB-active"];
    var data = {
        temperature:    ((b[0] | (b[1] << 8)) << 16 >> 16) / 100.0,
        humidity:       b[2] / 2.0,
        battery_mv:     (b[3] | (b[4] << 8)) >>> 0,
        flags: {
            confirmed:  (b[5] & 0x01) !== 0,
            adr:        (b[5] & 0x02) !== 0,
            sensor_ok:  (b[5] & 0x04) !== 0
        }
    };
    var off = 6;
    // v2: bytes 6-7 = end-side downlink quality
    if (b.length >= 10) {
        data.dl_rssi_dbm = (b[6] << 24) >> 24;
        data.dl_snr_db   = (b[7] << 24) >> 24;
        off = 8;
    }
    if (b.length >= off + 2) {
        data.class_b_status = statusMap[b[off]] || "unknown";
        data.ping_rx_count  = b[off + 1];
    }
    return { data: data };
}

function encodeDownlink(input) {
    var d = input.data;
    var cmds = {
        "relay_on":        [0x01, d.duration || 0],
        "relay_off":       [0x02],
        "set_ping_period": [0x03, (d.period || 2) & 0x07],
        "class_a":         [0x04],
        "class_b":         [0x05]
    };
    if (cmds[d.command]) return { bytes: cmds[d.command], fPort: 21 };
    return { errors: ["unknown command"] };
}
