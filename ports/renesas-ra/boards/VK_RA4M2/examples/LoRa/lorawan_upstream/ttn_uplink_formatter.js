// TTN Uplink Payload Formatter — VK_RA4M2 LoRaWAN end-node.
//
// Инсталиране:
//   TTN Console → Application → Payload formatters → Uplink → Custom javascript
//   Pastenи ЦЕЛИЯ файл (с двете функции).
//
// FPort=2 (3 bytes): bytes 0-1 = temp int16 BE x100, byte 2 = relay state (0/1)
// FPort=1: UTF-8 текст (compat).

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
        var t = (bytes[0] << 8) | bytes[1];
        if (t & 0x8000) t -= 0x10000;
        decoded.temp       = t / 100.0;
        decoded.relay      = bytes[2];
        decoded.relay_text = bytes[2] ? "on" : "off";
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
