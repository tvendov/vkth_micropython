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
from machine import Pin, WS2812
from sx1262 import SX1262
from LoRaWAN.AES_CMAC import AES_CMAC
import cryptolib

# === Relay output (WS2812 — 1 LED, green=off / red=on) ===
WS2812_POWER  = "P500"
WS2812_DATA   = "P112"
RELAY_COLOR_OFF = (0, 80, 0)                          # зелено
RELAY_COLOR_ON  = (80, 0, 0)                          # червено

def make_relay_strip():
    Pin(WS2812_POWER, Pin.OUT, value=1)
    time.sleep_ms(100)
    return WS2812(pixel_count=1, pin=Pin(WS2812_DATA), channels=3)

def set_relay(strip, on):
    strip[0] = RELAY_COLOR_ON if on else RELAY_COLOR_OFF
    strip.write()

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
# ВНИМАНИЕ: TTN Fair Use Policy = 30 s airtime / device / day. 10 s interval
# при SF7 (~46 ms airtime) дава ~8640 uplinks дневно — само за тест!
UPLINK_INTERVAL_S = 10
FREQ_MHZ = 868.1
SF = 7
BW = 125.0
# RX2 параметри (EU868 default — TTN ползва същите)
RX2_FREQ_MHZ = 869.525
RX2_SF = 12

# Confirmed uplinks: всеки N-ти пакет ще е Confirmed (за тест на ACK).
# 0 = никога confirmed, 1 = всеки uplink, 5 = всеки 5-ти, etc.
CONFIRMED_EVERY = 5

# DeviceTimeReq: пратя в FOpts на първия uplink (или при липса на pending MAC).
ASK_DEVICE_TIME_AT_START = True


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


# === MAC command names (LoRaWAN 1.0.x § 5) ===
MAC_CMD_NAMES = {
    0x02: "LinkCheckAns", 0x03: "LinkADRReq", 0x04: "DutyCycleReq",
    0x05: "RXParamSetupReq", 0x06: "DevStatusReq", 0x07: "NewChannelReq",
    0x08: "RXTimingSetupReq", 0x09: "TxParamSetupReq", 0x0A: "DLChannelReq",
    0x0D: "DeviceTimeAns",
}


# === MAC command response builders (LoRaWAN 1.0.x § 5) ===

def mac_devstatus_ans(battery, margin_db):
    """DevStatusAns: battery (0=ext, 1-254=charge, 255=unknown) + margin dB."""
    m = max(-32, min(31, int(margin_db))) & 0x3F
    return bytes([0x06, battery & 0xFF, m])


def mac_linkadr_ans(power_ack=1, dr_ack=1, channel_ack=1):
    """LinkADRAns: 3 bits acknowledging power/DR/channel-mask requested settings."""
    status = (power_ack << 2) | (dr_ack << 1) | channel_ack
    return bytes([0x03, status])


def mac_rxtiming_ans():
    """RXTimingSetupAns: empty (no payload)."""
    return bytes([0x08])


def mac_devtime_req():
    """DeviceTimeReq: устройство пита TTN за GPS време (uplink-initiated)."""
    return bytes([0x0D])


def process_mac_commands(parsed_cmds, last_snr):
    """Връща FOpts bytes за следващия uplink — отговор на server commands.

    Single-channel SF7-only gateway:
      - DevStatusReq -> DevStatusAns(battery=255 unknown, margin=last RX SNR)
      - LinkADRReq   -> LinkADRAns(0,0,0) — ОТХВЪРЛЯМЕ ADR промените, защото
        gateway-ът е статичен (SF7 single-channel). TTN ще запази текущи DR/Pwr.
      - RXTimingSetupReq -> RXTimingSetupAns (empty payload)
    """
    response = b""
    for name, hex_data in parsed_cmds:
        cmd = bytes.fromhex(hex_data)
        cid = cmd[0]
        if cid == 0x06:                          # DevStatusReq
            response += mac_devstatus_ans(255, last_snr)
        elif cid == 0x03:                        # LinkADRReq
            # ОТХВЪРЛЯМЕ — gateway-ът не позволява смяна на DR/channel
            response += mac_linkadr_ans(power_ack=0, dr_ack=0, channel_ack=0)
        elif cid == 0x08:                        # RXTimingSetupReq
            response += mac_rxtiming_ans()
        # Други команди не отговаряме (DutyCycleReq, NewChannelReq, etc.)
    return response


def parse_downlink(devaddr_le, nwkskey, appskey, frame):
    """Декодира downlink frame. Връща dict с информация или None ако MIC fail."""
    if len(frame) < 12:
        return None
    mhdr = frame[0]
    mtype = (mhdr >> 5) & 0x07
    if mtype not in (0x03, 0x05):           # 011 unconf data dn, 101 conf data dn
        return {"error": "not data downlink", "mtype": mtype}

    rx_devaddr = bytes(frame[1:5])
    if rx_devaddr != devaddr_le:
        return {"error": "DevAddr mismatch",
                "got": bytes(reversed(rx_devaddr)).hex()}

    fctrl = frame[5]
    fcnt_lo = int.from_bytes(frame[6:8], "little")
    fopts_len = fctrl & 0x0F
    fopts = bytes(frame[8:8 + fopts_len])

    # FPort + FRMPayload
    rest = frame[8 + fopts_len:-4]
    rx_mic = bytes(frame[-4:])

    fport = rest[0] if len(rest) > 0 else None
    frm_payload = bytes(rest[1:]) if len(rest) > 1 else b""

    # MIC validation
    msg = bytes(frame[:-4])
    expected_mic = compute_uplink_mic(nwkskey, devaddr_le, fcnt_lo, 1, msg)
    if expected_mic != rx_mic:
        return {"error": "MIC mismatch",
                "got": rx_mic.hex(), "expected": expected_mic.hex()}

    # Decrypt FRMPayload (AppSKey за FPort != 0, NwkSKey за FPort == 0)
    decrypted = b""
    if frm_payload:
        key = nwkskey if fport == 0 else appskey
        decrypted = encrypt_frm_payload(key, devaddr_le, fcnt_lo, 1, frm_payload)

    # MAC commands в FOpts (downlink piggyback) или във FRMPayload (FPort=0)
    mac_cmds = fopts if fport != 0 else decrypted
    parsed_cmds = []
    device_time = None                          # ще бъде set при DeviceTimeAns
    i = 0
    while i < len(mac_cmds):
        cid = mac_cmds[i]
        name = MAC_CMD_NAMES.get(cid, "0x%02X" % cid)
        # ad-hoc length lookup (downlink direction)
        clen = {0x02: 2,  # LinkCheckAns: margin(1) + GwCnt(1)
                0x03: 4,  # LinkADRReq: DR/Pwr(1) + ChMask(2) + Redund(1)
                0x05: 4,  # RXParamSetupReq: DLSettings(1) + Frequency(3)
                0x06: 0,  # DevStatusReq
                0x07: 5,  # NewChannelReq
                0x08: 1,  # RXTimingSetupReq
                0x0A: 4,  # DLChannelReq
                0x0D: 5}.get(cid, 0)  # DeviceTimeAns: epoch(4) + frac(1)
        cmd_bytes = mac_cmds[i:i + 1 + clen]
        parsed_cmds.append((name, cmd_bytes.hex()))
        if cid == 0x0D and len(cmd_bytes) == 6:
            # DeviceTimeAns: 4 bytes GPS epoch (LE) + 1 byte fractional second.
            # GPS epoch = seconds since 1980-01-06 00:00:00 UTC.
            gps_epoch = int.from_bytes(cmd_bytes[1:5], "little")
            frac256 = cmd_bytes[5]
            device_time = (gps_epoch, frac256)
        i += 1 + clen

    return {
        "mtype": mtype,
        "fcnt": fcnt_lo,
        "fctrl": fctrl,
        "ack": bool(fctrl & 0x20),               # downlink ACK bit (LoRaWAN § 4.3.1)
        "fport": fport,
        "frm_payload_decrypted": decrypted,
        "mac_commands": parsed_cmds,
        "device_time": device_time,
    }


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


def send_uplink(sx, devaddr, nwkskey, appskey, fcnt, port, payload,
                fopts=b"", confirmed=False):
    """Изпраща Data Up frame. confirmed=True задава MType=Confirmed → TTN ще ACK-ва."""
    if len(fopts) > 15:
        raise ValueError("FOpts > 15 bytes — use FPort=0 frame instead")
    enc = encrypt_frm_payload(appskey, devaddr, fcnt, 0, payload)
    fctrl = len(fopts) & 0x0F                # FOptsLen в bit 0-3
    mac_payload = (devaddr + bytes([fctrl]) +
                   fcnt.to_bytes(2, "little") + fopts +
                   bytes([port]) + enc)
    mhdr = 0x80 if confirmed else 0x40       # 100 = Confirmed, 010 = Unconfirmed
    mic_input = bytes([mhdr]) + mac_payload
    mic = compute_uplink_mic(nwkskey, devaddr, fcnt, 0, mic_input)
    sx.send(mic_input + mic)


def listen_rx1(sx, devaddr, nwkskey, appskey,
               rx1_delay_ms=4500, recv_timeout_ms=2500):
    """RX1: 5 s след TX, на TX freq + TX SF (TTN изисква DR equal/lower)."""
    time.sleep_ms(rx1_delay_ms)
    msg, err = sx.recv(0, True, recv_timeout_ms)
    if not msg or len(msg) == 0:
        return None
    info = parse_downlink(devaddr, nwkskey, appskey, bytes(msg))
    if info is None:
        return None
    info["window"] = "RX1"
    info["rssi"] = sx.getRSSI()
    info["snr"]  = sx.getSNR()
    info["raw"]  = bytes(msg).hex()
    return info


def listen_rx2(sx, devaddr, nwkskey, appskey, recv_timeout_ms=2500):
    """RX2 fallback: 6 s след TX, на 869.525 MHz SF12 (EU868 default).

    Ако RX1 пропусне, TTN reпеat-ва на RX2 1 s по-късно. Чипа трябва да се
    превключи на тази конфигурация ad-hoc, после да се върне за следващ uplink.
    """
    sx.setFrequency(RX2_FREQ_MHZ)
    sx.setSpreadingFactor(RX2_SF)
    msg, err = sx.recv(0, True, recv_timeout_ms)
    # Възстанови за следващ uplink
    sx.setFrequency(FREQ_MHZ)
    sx.setSpreadingFactor(SF)
    if not msg or len(msg) == 0:
        return None
    info = parse_downlink(devaddr, nwkskey, appskey, bytes(msg))
    if info is None:
        return None
    info["window"] = "RX2"
    info["rssi"] = sx.getRSSI()
    info["snr"]  = sx.getSNR()
    info["raw"]  = bytes(msg).hex()
    return info


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

    # Periodic uplinks (FPort=2, 3-byte payload):
    #   bytes 0-1 temp   int16 BE × 100  (0.01 °C resolution, sim sweep 20..25 °C)
    #   byte 2    button uint8 (simulated: toggle на всеки uplink → 10s on/10s off)
    #
    # Downlink на FPort=2 (1 byte): relay (0=off→зелено, 1=on→червено).
    relay_strip = make_relay_strip()
    relay_state = 0
    set_relay(relay_strip, relay_state)               # старт: off / зелено
    pending_mac = mac_devtime_req() if ASK_DEVICE_TIME_AT_START else b""
    last_snr = 0
    sim_button = 0
    temp_c100 = 2000                                  # 20.00 °C старт
    temp_step = 25                                    # +0.25 °C / uplink
    while True:
        # Симулирам сензори
        sim_button ^= 1                               # 10s on, 10s off
        temp_c100 += temp_step
        if temp_c100 >= 2500: temp_step = -25         # 25.00 → започни надолу
        if temp_c100 <= 2000: temp_step = +25         # 20.00 → започни нагоре
        # encode int16 BE (two's complement за отрицателни)
        t = temp_c100 if temp_c100 >= 0 else (temp_c100 + 0x10000)
        payload = bytes([(t >> 8) & 0xFF, t & 0xFF, sim_button])

        confirmed = (CONFIRMED_EVERY > 0) and (fcnt % CONFIRMED_EVERY == 0) and (fcnt > 0)
        kind = "Confirmed" if confirmed else "Unconfirmed"
        info = "temp=%.2f°C button=%d relay=%d" % (
            temp_c100 / 100.0, sim_button, relay_state)
        if pending_mac:
            print("[FCnt=%d %s] %s + FOpts=%s" %
                  (fcnt, kind, info, pending_mac.hex()))
        else:
            print("[FCnt=%d %s] %s" % (fcnt, kind, info))
        try:
            send_uplink(sx, devaddr, nwkskey, appskey, fcnt, 0x02, payload,
                        fopts=pending_mac, confirmed=confirmed)
            pending_mac = b""                    # consumed; ще се преинит-ва от downlink
            dl = listen_rx1(sx, devaddr, nwkskey, appskey)
            if dl is None:
                print("  RX1: nothing — listening RX2 (869.525MHz SF12)...")
                dl = listen_rx2(sx, devaddr, nwkskey, appskey)
                if dl is None:
                    print("  RX2: nothing")
            if dl is None:
                pass                             # вече логнато
            elif "error" in dl:
                print("  downlink rejected: %s" % dl["error"])
            else:
                print("  *** %s downlink RSSI=%d SNR=%.1f FCnt=%d FPort=%s" %
                      (dl["window"], dl["rssi"], dl["snr"],
                       dl["fcnt"], dl["fport"]))
                if confirmed:
                    print("    ACK bit:", "YES (uplink confirmed)" if dl["ack"]
                                          else "NO (server didn't ACK)")
                if dl["device_time"] is not None:
                    gps_epoch, frac256 = dl["device_time"]
                    # GPS epoch 1980-01-06 = Unix 315964800; LoRaWAN добавя
                    # текущия leap-second offset (18 към 2024+).
                    # MicroPython time.gmtime() използва epoch 2000-01-01
                    # (Unix 946684800), а НЕ Unix 1970 — затова трябва да
                    # извадим тази разлика преди gmtime.
                    unix_s = 315964800 + gps_epoch - 18
                    mpy_s = unix_s - 946684800
                    yy, mm, dd, hh, mi, ss, _, _ = time.gmtime(mpy_s)
                    print("    *** UTC time from TTN: %04d-%02d-%02d %02d:%02d:%02d.%03d" %
                          (yy, mm, dd, hh, mi, ss, (frac256 * 1000) // 256))
                if dl["frm_payload_decrypted"]:
                    raw = dl["frm_payload_decrypted"]
                    if dl["fport"] is not None and dl["fport"] > 0:
                        # Application downlink
                        if dl["fport"] == 2 and len(raw) >= 1:
                            # Relay command: byte 0 = 0/1
                            new_state = raw[0] & 0x01
                            relay_state = new_state
                            set_relay(relay_strip, relay_state)
                            print("    relay → %s (%s)" %
                                  ("ON" if relay_state else "OFF",
                                   "червено" if relay_state else "зелено"))
                        else:
                            try:
                                txt = raw.decode("utf-8")
                                print("    app payload (FPort=%d): %r" %
                                      (dl["fport"], txt))
                            except Exception:
                                print("    app payload (FPort=%d, hex): %s" %
                                      (dl["fport"], raw.hex()))
                    else:
                        print("    FPort=0 MAC payload (hex):", raw.hex())
                if dl["mac_commands"]:
                    print("    MAC cmds:", dl["mac_commands"])
                    last_snr = dl["snr"]
                    pending_mac = process_mac_commands(dl["mac_commands"], last_snr)
                    if pending_mac:
                        print("    queued MAC ans for next uplink:", pending_mac.hex())
        except Exception as e:
            print("  send/recv error:", e)
        fcnt += 1
        save_int(FCNTUP_FILE, fcnt)
        gc.collect()
        time.sleep(UPLINK_INTERVAL_S)


if __name__ == "__main__":
    main()
