# lorawan_app.py — продуктивен LoRaWAN end-node за VK_RA4M2 + Wio + TTN.
#
# Поведение:
#   1. При boot: зарежда session (DevAddr/NwkSKey/AppSKey) и FCntUp от /flash.
#   2. Ако няма session → OTAA join + запазва session.
#   3. Loop: всяка минута уплинк "VK_RA4M2 uptime=Ns" с инкрементиран FCnt.
#   4. FCntUp + DevNonce се записват в /flash след всяка операция (replay safety).
#
# Configuration: попълни DevEUI/JoinEUI/AppKey в LoRaConfig_TTN.py.

import time
import gc
from machine import Pin
from sx1262 import SX1262
from LoRaWAN.AES_CMAC import AES_CMAC
import cryptolib

# === Credentials ===
from LoRaConfig_TTN import LoRaConfig
DevEUI  = bytes(LoRaConfig.DevEUI)
JoinEUI = bytes(LoRaConfig.JoinEUI)
AppKey  = bytes(LoRaConfig.AppKey)

# === Persistent state files ===
DEVNONCE_FILE = "/flash/lw_devnonce.dat"
SESSION_FILE  = "/flash/lw_session.dat"
FCNTUP_FILE   = "/flash/lw_fcntup.dat"

# === Radio config (T3-proven) ===
RX1_DELAY_MS = 5000
RX1_TIMEOUT_MS = 1500
UPLINK_INTERVAL_S = 60
FREQ_MHZ = 868.1
SF = 7
BW = 125.0


# === Crypto helpers ===

def aes_ecb(key, block16):
    return cryptolib.aes(key, 1).encrypt(bytes(block16))


def decrypt_join_accept(appkey, encrypted):
    out = b""
    for i in range(0, len(encrypted), 16):
        out += aes_ecb(appkey, encrypted[i:i + 16])
    return out


def derive_session_key(appkey, prefix, app_nonce, net_id, dev_nonce):
    blk = bytes([prefix]) + app_nonce + net_id + dev_nonce + b"\x00" * 7
    return aes_ecb(appkey, blk)


def encrypt_frm_payload(appskey, devaddr_le, fcnt32, direction, plaintext):
    out = bytearray(len(plaintext))
    for i in range((len(plaintext) + 15) // 16):
        a = bytearray(16)
        a[0] = 0x01
        a[5] = direction
        a[6:10] = devaddr_le
        a[10:14] = fcnt32.to_bytes(4, "little")
        a[15] = i + 1
        s_i = aes_ecb(appskey, bytes(a))
        for j in range(min(16, len(plaintext) - i * 16)):
            out[i * 16 + j] = plaintext[i * 16 + j] ^ s_i[j]
    return bytes(out)


def compute_uplink_mic(nwkskey, devaddr_le, fcnt32, direction, msg):
    b0 = bytearray(16)
    b0[0] = 0x49
    b0[5] = direction
    b0[6:10] = devaddr_le
    b0[10:14] = fcnt32.to_bytes(4, "little")
    b0[15] = len(msg)
    return bytes(AES_CMAC().encode(nwkskey, bytes(b0) + msg))[:4]


# === Persistence ===

def load_int(path, default=0):
    try:
        with open(path) as f:
            return int(f.read().strip())
    except Exception:
        return default


def save_int(path, value):
    with open(path, "w") as f:
        f.write(str(value))


def load_session():
    """Връща (devaddr, nwkskey, appskey) или None ако няма запазена сесия."""
    try:
        with open(SESSION_FILE, "rb") as f:
            data = f.read()
        if len(data) != 4 + 16 + 16:
            return None
        return data[:4], data[4:20], data[20:36]
    except Exception:
        return None


def save_session(devaddr, nwkskey, appskey):
    with open(SESSION_FILE, "wb") as f:
        f.write(devaddr + nwkskey + appskey)


# === LoRaWAN flow ===

def init_radio():
    Pin("P100", Pin.OUT, value=1)            # RF_SW1 enable (Wio-SX1262)
    sx = SX1262(spi_bus=1, clk="P111", mosi="P109", miso="P110",
                cs="P206", irq="P015", rst="P001", gpio="P002")
    sx.begin(
        freq=FREQ_MHZ, bw=BW, sf=SF, cr=5,
        syncWord=0x34, power=14, currentLimit=60.0,
        preambleLength=8, implicit=False, implicitLen=0xFF,
        crcOn=True, txIq=False, rxIq=True,
        tcxoVoltage=1.8, useRegulatorLDO=False, blocking=True,
    )
    return sx


def otaa_join(sx, dev_nonce):
    """Връща (devaddr_le, nwkskey, appskey) при успех, или None при провал."""
    dn_le = dev_nonce.to_bytes(2, "little")
    join_req = bytes([0x00]) + bytes(reversed(JoinEUI)) + bytes(reversed(DevEUI)) + dn_le
    join_req += bytes(AES_CMAC().encode(AppKey, join_req))[:4]

    print("OTAA join (DevNonce=%d)..." % dev_nonce)
    sx.send(join_req)
    msg, err = sx.recv(0, True, 8000)
    if not msg or len(msg) == 0:
        print("  join failed (no JoinAccept, err=%d)" % err)
        return None

    plain = decrypt_join_accept(AppKey, bytes(msg[1:]))
    app_nonce = plain[0:3]
    net_id    = plain[3:6]
    devaddr   = plain[6:10]
    nwkskey = derive_session_key(AppKey, 0x01, app_nonce, net_id, dn_le)
    appskey = derive_session_key(AppKey, 0x02, app_nonce, net_id, dn_le)

    print("  joined: DevAddr=%s" % bytes(reversed(devaddr)).hex())
    return devaddr, nwkskey, appskey


def send_uplink(sx, devaddr, nwkskey, appskey, fcnt, port, payload):
    enc = encrypt_frm_payload(appskey, devaddr, fcnt, 0, payload)
    mac_payload = (devaddr + bytes([0x00]) +
                   fcnt.to_bytes(2, "little") + bytes([port]) + enc)
    mic_input = bytes([0x40]) + mac_payload
    mic = compute_uplink_mic(nwkskey, devaddr, fcnt, 0, mic_input)
    sx.send(mic_input + mic)


# === Main ===

def main():
    print("=" * 50)
    print("VK_RA4M2 LoRaWAN end-node (TTN, EU868 SF7)")
    print("=" * 50)

    sx = init_radio()
    print("Radio OK")

    # Load or create session
    session = load_session()
    if session is None:
        # Initial DevNonce 100 за избягване на replay при ново устройство
        # (TTN не е виждал стойности 1-99, така започваме чисто).
        dev_nonce = max(100, load_int(DEVNONCE_FILE, 0) + 1)
        joined = None
        for attempt in range(5):
            save_int(DEVNONCE_FILE, dev_nonce)
            joined = otaa_join(sx, dev_nonce)
            if joined is not None:
                break
            dev_nonce += 1                   # retry с по-голям nonce
            time.sleep(3)
        if joined is None:
            print("Cannot join after 5 retries. Check credentials/gateway/antenna.")
            return
        devaddr, nwkskey, appskey = joined
        save_session(devaddr, nwkskey, appskey)
        save_int(FCNTUP_FILE, 0)
        fcnt = 0
    else:
        devaddr, nwkskey, appskey = session
        fcnt = load_int(FCNTUP_FILE, 0)
        print("Loaded session: DevAddr=%s FCnt=%d" %
              (bytes(reversed(devaddr)).hex(), fcnt))

    # Periodic uplinks
    uptime_ref_s = time.ticks_ms() // 1000
    while True:
        now_s = time.ticks_ms() // 1000
        uptime = now_s - uptime_ref_s
        payload = ("uptime=%ds" % uptime).encode()
        print("[FCnt=%d] uplink: %r" % (fcnt, payload))
        try:
            send_uplink(sx, devaddr, nwkskey, appskey, fcnt, 0x01, payload)
        except Exception as e:
            print("  send error:", e)
        fcnt += 1
        save_int(FCNTUP_FILE, fcnt)
        gc.collect()
        time.sleep(UPLINK_INTERVAL_S)


if __name__ == "__main__":
    main()
