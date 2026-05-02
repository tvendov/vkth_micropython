// TTN Payload Formatter — VK_RA4M2 LoRaWAN end-node.
//
// Инсталиране:
//   TTN Console → Application → Payload formatters → Uplink/Downlink → Custom
//
// FPort=2 layout
//   Uplink   (3 bytes):  bytes 0-1 = temp int16 BE x100 (0.01°C),  byte 2 = button(0/1)
//   Downlink (1 byte):   byte 0    = relay (0=off, 1=on)
//
// FPort=1 продължава да работи с UTF-8 текст (compat).

function bytesToHex(b) {
    var s = "";
    for (var i = 0; i < b.length; i++) {
        var h = (b[i] & 0xFF).toString(16);
        if (h.length < 2) h = "0" + h;
        s += h;
    }
    return s;
}

function decodeUplink(input) {
    var bytes = input.bytes;
    var port  = input.fPort;

    var decoded = { bytes: bytes, hex: bytesToHex(bytes), fPort: port };

    if (port === 2 && bytes.length >= 3) {
        // temp = int16 BE / 100
        var t = (bytes[0] << 8) | bytes[1];
        if (t & 0x8000) t -= 0x10000;            // sign-extend
        decoded.temp        = t / 100.0;
        decoded.button      = bytes[2];
        decoded.button_text = bytes[2] ? "on" : "off";
    }

    if (port === 1) {
        var text = "";
        for (var i = 0; i < bytes.length; i++) text += String.fromCharCode(bytes[i]);
        decoded.text = text;
        var m = text.match(/^uptime=(\d+)s$/);
        if (m) decoded.uptime_s = parseInt(m[1], 10);
    }

    return { data: decoded, warnings: [], errors: [] };
}

// Downlink encode: {"relay": 1} → byte 0x01 на FPort=2
function encodeDownlink(input) {
    var d = input.data || {};
    var port = (input.fPort != null) ? input.fPort : 2;

    if (port === 2) {
        var relay = 0;
        if (typeof d.relay === "boolean")        relay = d.relay ? 1 : 0;
        else if (typeof d.relay === "number")    relay = d.relay & 0x01;
        else if (typeof d.relay === "string") {
            var s = d.relay.toLowerCase();
            relay = (s === "on" || s === "1" || s === "true") ? 1 : 0;
        }
        return { bytes: [relay], fPort: 2, warnings: [], errors: [] };
    }

    // Fallback — raw bytes/hex
    var bytes = [];
    if (Array.isArray(d.bytes)) bytes = d.bytes;
    else if (typeof d.hex === "string") {
        var hex = d.hex.replace(/\s+/g, "");
        for (var i = 0; i + 1 < hex.length; i += 2) bytes.push(parseInt(hex.substr(i, 2), 16));
    }
    return { bytes: bytes, fPort: port, warnings: [], errors: [] };
}

function decodeDownlink(input) {
    var b = input.bytes;
    var data = { bytes: b, hex: bytesToHex(b), fPort: input.fPort };
    if (input.fPort === 2 && b.length >= 1) {
        data.relay      = b[0];
        data.relay_text = b[0] ? "on" : "off";
    }
    return { data: data, warnings: [], errors: [] };
}
