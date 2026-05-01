// TTN Payload Formatter за VK_RA4M2 LoRaWAN end-node.
//
// Инсталиране:
//   TTN Console → Application → Payload formatters → Uplink → Formatter type
//   → "Custom JavaScript formatter" → постави този код → Save changes
//
// Получаваме байтове от lorawan_app.py uplink (FPort=1):
//   "uptime=0s", "uptime=60s", "uptime=120s", ...
//
// След инсталиране, в Live data → Forward uplink data message ще видиш:
//   "decoded_payload": { "text": "uptime=120s", "uptime_s": 120 }

function decodeUplink(input) {
    var bytes = input.bytes;
    var port = input.fPort;

    // Текстов uplink на FPort=1
    if (port === 1) {
        var text = "";
        for (var i = 0; i < bytes.length; i++) {
            text += String.fromCharCode(bytes[i]);
        }

        var decoded = { text: text };

        // Ако следва формат "uptime=Ns", извлечи числото
        var m = text.match(/^uptime=(\d+)s$/);
        if (m) {
            decoded.uptime_s = parseInt(m[1], 10);
        }

        return {
            data: decoded,
            warnings: [],
            errors: [],
        };
    }

    // Други портове — върни raw bytes
    return {
        data: { raw: Array.from(bytes), fPort: port },
        warnings: ["Unknown FPort " + port],
        errors: [],
    };
}

// Опционално: encoder за downlinks (още не ни трябва за този end-node).
function encodeDownlink(input) {
    return {
        bytes: [],
        fPort: 1,
        warnings: [],
        errors: [],
    };
}

// Опционално: decoder за downlinks (за бъдеща Class-A разширение).
function decodeDownlink(input) {
    return {
        data: { bytes: Array.from(input.bytes) },
        warnings: [],
        errors: [],
    };
}
