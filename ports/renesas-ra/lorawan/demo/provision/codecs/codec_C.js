// Class C — fPort 12 uplink, 22 (control) / 23 (config) downlink
// Payload v2 (11 bytes): header(6) + dl_rssi(i8) + dl_snr(i8) + dl_count(u8) + last_dl_lat_ms(u16 LE)
function decodeUplink(input) {
    if (input.fPort !== 12 || input.bytes.length < 6)
        return { errors: ["bad fPort or short payload"] };
    var b = input.bytes;
    var data = {
        temperature:        ((b[0] | (b[1] << 8)) << 16 >> 16) / 100.0,
        humidity:           b[2] / 2.0,
        battery_mv:         (b[3] | (b[4] << 8)) >>> 0,
        flags: {
            class_c_active: (b[5] & 0x01) !== 0,
            adr:            (b[5] & 0x02) !== 0,
            sensor_ok:      (b[5] & 0x04) !== 0
        }
    };
    var off = 6;
    // v2: bytes 6-7 = end-side downlink quality
    if (b.length >= 11) {
        data.dl_rssi_dbm = (b[6] << 24) >> 24;
        data.dl_snr_db   = (b[7] << 24) >> 24;
        off = 8;
    }
    // Class C extras: dl_count + last_dl_lat_ms
    if (b.length >= off + 3) {
        data.dl_count           = b[off];
        data.last_dl_latency_ms = (b[off + 1] | (b[off + 2] << 8)) >>> 0;
    }
    return { data: data };
}

function encodeDownlink(input) {
    var d = input.data;
    if (d.command === "rgb_set")
        return { bytes: [0x01, d.r||0, d.g||0, d.b||0], fPort: 22 };
    if (d.command === "rgb_off")
        return { bytes: [0x02], fPort: 22 };
    if (d.command === "blink")
        return { bytes: [0x03, d.count||3, d.r||0, d.g||0, d.b||255], fPort: 22 };
    if (d.command === "status_now")
        return { bytes: [0x04], fPort: 22 };
    if (d.command === "set_interval")
        return { bytes: [0x01, d.minutes||1], fPort: 23 };
    if (d.command === "set_rx2_dr")
        return { bytes: [0x02, d.dr||5], fPort: 23 };
    if (d.command === "force_rejoin")
        return { bytes: [0x03], fPort: 23 };
    return { errors: ["unknown command"] };
}
