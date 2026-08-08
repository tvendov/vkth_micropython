// TTN Downlink Payload Formatter — VK_RA4M2 LoRaWAN end-node.
//
// Инсталиране:
//   TTN Console → Application → Payload formatters → Downlink → Custom javascript
//   Pastenи ЦЕЛИЯ файл (с трите функции — bytesToHex, encodeDownlink, decodeDownlink).
//
// Тук задължително трябва да са И двете: encodeDownlink (за изпращане)
// И decodeDownlink (за visualizing на изпратените downlink-ове в Live data).
// Без decodeDownlink → "Decode downlink data message failure" грешка в console.
//
// FPort=2 (1 byte): byte 0 = relay (0=off → зелено, 1=on → червено)
// JSON формат за изпращане:  {"relay": 1}  или  {"relay": "on"}  или  {"relay": true}

function bytesToHex(b) {
    var s = "";
    for (var i = 0; i < b.length; i++) {
        var h = (b[i] & 0xFF).toString(16);
        if (h.length < 2) h = "0" + h;
        s += h;
    }
    return s;
}

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
