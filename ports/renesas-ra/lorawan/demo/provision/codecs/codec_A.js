// Class A — fPort 10 uplink, 20 downlink
// Payload v2 (8 bytes): header(6) + dl_rssi(i8) + dl_snr(i8)
function decodeUplink(input) {
    if (input.fPort !== 10 || input.bytes.length < 6)
        return { errors: ["bad fPort or short payload"] };
    var b = input.bytes;
    var data = {
        temperature:  ((b[0] | (b[1] << 8)) << 16 >> 16) / 100.0,
        humidity:     b[2] / 2.0,
        battery_mv:   (b[3] | (b[4] << 8)) >>> 0,
        flags: {
            confirmed:  (b[5] & 0x01) !== 0,
            adr:        (b[5] & 0x02) !== 0,
            sensor_ok:  (b[5] & 0x04) !== 0,
            p109:       (b[5] & 0x08) !== 0
        }
    };
    // v2: bytes 6-7 = end-side downlink quality (last RX)
    if (b.length >= 8) {
        data.dl_rssi_dbm = (b[6] << 24) >> 24;  // sign-extend int8
        data.dl_snr_db   = (b[7] << 24) >> 24;
    }
    return { data: data };
}

function encodeDownlink(input) {
    var d = input.data;
    if (d.command === "set_interval")
        return { bytes: [0x01, Math.floor(d.seconds / 10)], fPort: 20 };
    if (d.command === "force_rejoin")
        return { bytes: [0x02], fPort: 20 };
    if (d.command === "led_test")
        return { bytes: [0x03, d.count || 3], fPort: 20 };
    if (d.command === "set_p109")
        return { bytes: [0x04, d.value ? 1 : 0], fPort: 20 };
    return { errors: ["unknown command"] };
}
