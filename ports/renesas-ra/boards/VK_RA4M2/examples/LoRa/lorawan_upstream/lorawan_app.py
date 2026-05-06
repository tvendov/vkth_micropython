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
import asyncio
import framebuf
from machine import Pin, WS2812, SoftI2C
from sx1262 import SX1262
from LoRaWAN.AES_CMAC import AES_CMAC
import cryptolib

# === Relay output (WS2812 — 1 LED, green=off / red=on / yellow=cycle flash) ===
WS2812_POWER  = "P500"
WS2812_DATA   = "P112"
RELAY_COLOR_OFF   = (0, 80, 0)                        # зелено
RELAY_COLOR_ON    = (80, 0, 0)                        # червено
CYCLE_FLASH_COLOR = (60, 60, 0)                       # жълт flash на long-press

# === Button input (active-low с pull-up на P014) ===
BUTTON_PIN = "P014"                                    # D0 на VK_RA4M2 (active-low)
button = None                                          # ButtonLatch, init в app()

# === OLED display (SoftI2C на P301/P302, SSD1306 128x32 на 0x3C) ===
OLED_SCL  = "P301"
OLED_SDA  = "P302"
OLED_FREQ = 400_000
OLED_W    = 128
OLED_H    = 32
OLED_ADDR = 0x3C
CREDENTIALS_BOOT_WINDOW_MS = 20_000                    # 20s след boot
CRED_PAGE_MS = 5_000                                   # 5s на страница (4 страници)

def make_relay_strip():
    Pin(WS2812_POWER, Pin.OUT, value=1)
    time.sleep_ms(100)
    return WS2812(pixel_count=1, pin=Pin(WS2812_DATA), channels=3)

def set_relay(strip, on):
    strip[0] = RELAY_COLOR_ON if on else RELAY_COLOR_OFF
    strip.write()

# === Credentials ===
# Първо опитваме Data Flash (provision-нати веднъж per board чрез
# provision_credentials.py). Ако няма валиден record → fallback към
# LoRaConfig_TTN.py файла. Това позволява firmware update без re-identify.
#
# Data Flash record (40 bytes в блок 0):
#   0..3   "LWCR" magic
#   4      version (0x01)
#   5      reserved
#   6..13  DevEUI (8 bytes MSB)
#   14..21 JoinEUI (8 bytes MSB)
#   22..37 AppKey (16 bytes MSB)
#   38..39 CRC16-CCITT BE над bytes 0..37

def _crc16_ccitt(data, crc=0xFFFF):
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc

def _load_credentials():
    try:
        import dataflash
        blob = bytes(dataflash.read(0, 40))
        if blob[0:4] == b"LWCR" and blob[4] == 0x01:
            stored_crc = (blob[38] << 8) | blob[39]
            if _crc16_ccitt(blob[:38]) == stored_crc:
                print("Credentials: Data Flash (provision-нати)")
                return blob[6:14], blob[14:22], blob[22:38]
            else:
                print("Credentials: Data Flash CRC fail → fallback към .py")
        # else: блок е празен (0xFF) или magic не съвпада → fallback
    except Exception as e:
        print("Credentials: Data Flash read error (%s) → fallback към .py" % e)
    from LoRaConfig_TTN import LoRaConfig
    print("Credentials: LoRaConfig_TTN.py (fallback)")
    return (bytes(LoRaConfig.DevEUI), bytes(LoRaConfig.JoinEUI),
            bytes(LoRaConfig.AppKey))

DevEUI, JoinEUI, AppKey = _load_credentials()


# === SSD1306 OLED 128x32 driver (минимален, framebuf-based) ===

class SSD1306:
    def __init__(self, i2c, addr=OLED_ADDR):
        self.i2c = i2c
        self.addr = addr
        self.pages = OLED_H // 8
        self._tx_buf = bytearray(1 + OLED_W * self.pages)
        self._tx_buf[0] = 0x40
        self._fb_view = memoryview(self._tx_buf)[1:]
        self.fb = framebuf.FrameBuffer(self._fb_view, OLED_W, OLED_H,
                                       framebuf.MONO_VLSB)
        self._cmd1 = bytearray(2)
        self._cmd1[0] = 0x00
        # 128x32: mux=0x1F, com_pin=0x02
        for c in (0xAE, 0xD5, 0x80, 0xA8, 0x1F, 0xD3, 0x00, 0x40,
                  0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x02,
                  0x81, 0x7F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF):
            self._cmd1[1] = c
            self.i2c.writeto(self.addr, self._cmd1)
        self._show_cmd = bytearray(
            b"\x00\x21\x00\x7F\x22\x00" + bytes([self.pages - 1]))
        self.fb.fill(0)
        self.show()

    def fill(self, c):  self.fb.fill(c)
    def text(self, s, x, y, color=1):  self.fb.text(s, x, y, color)
    def show(self):
        self.i2c.writeto(self.addr, self._show_cmd)
        self.i2c.writeto(self.addr, self._tx_buf)


def make_oled():
    """Опитва SoftI2C + SSD1306. Връща инстанция или None ако failed."""
    try:
        i2c = SoftI2C(scl=Pin(OLED_SCL, Pin.OPEN_DRAIN),
                      sda=Pin(OLED_SDA, Pin.OPEN_DRAIN), freq=OLED_FREQ)
        if OLED_ADDR not in i2c.scan():
            print("OLED: 0x%02X не е на bus-а; headless mode" % OLED_ADDR)
            return None
        oled = SSD1306(i2c, OLED_ADDR)
        print("OLED: SSD1306 на 0x%02X (%dx%d)" % (OLED_ADDR, OLED_W, OLED_H))
        return oled
    except Exception as e:
        print("OLED init error: %s; headless mode" % e)
        return None


# === Persistent state files ===
DEVNONCE_FILE = "/flash/lw_devnonce.dat"
SESSION_FILE  = "/flash/lw_session.dat"
FCNTUP_FILE   = "/flash/lw_fcntup.dat"

# === Radio config (EU868 multi-channel, ChirpStack/SenseCAP M2-ready) ===
# Default 3 mandatory ch (RP002-1.0.3 § 2.5.2). CFList от JoinAccept или
# NewChannelReq могат да добавят още до 5 канала (типично 867.1..867.9).
# За SCG fallback (TTN single-channel gateway) → намали списъка до [868_100_000].
EU868_DEFAULT_CHANNELS_HZ = [868_100_000, 868_300_000, 868_500_000]
BW = 125.0

# RX2 параметри (EU868 default — TTN/ChirpStack ползват същите).
# RXParamSetupReq би ги override-нал — не е имплементирано тук.
RX2_FREQ_MHZ = 869.525
RX2_SF = 12

# DR → SF mapping (EU868 LoRa-only @ BW=125 kHz; DR6=SF7BW250, DR7=FSK
# не ги поддържаме — gateway-ите и ChirpStack profile-ите ги ползват рядко).
DR_TO_SF = {0: 12, 1: 11, 2: 10, 3: 9, 4: 8, 5: 7}
# LinkADRReq TX power index → dBm (RP002-1.0.3 EU868 § 2.5.3).
PWR_IDX_TO_DBM = {0: 14, 1: 12, 2: 10, 3: 8, 4: 6, 5: 4, 6: 2, 7: 0}

# Initial DR / power при ново join (преди ADR convergence).
# DR5/SF7 + idx0/+14dBm = max throughput, valid за 3-те mandatory канала.
DEFAULT_DR        = 5
DEFAULT_PWR_IDX   = 0

# RX1 delay: ChirpStack default = 1 s; TTN ползва 5 s. Override-ва се от
# JoinAccept RXDelay byte и от RXTimingSetupReq (persist в RX1_DELAY_FILE).
DEFAULT_RX1_DELAY_MS = 1000

# Confirmed uplinks: всеки N-ти пакет ще е Confirmed (за тест на ACK).
CONFIRMED_EVERY = 5
ASK_DEVICE_TIME_AT_START = True

# === Cycle-able runtime params (кратко натискане на бутона) ===
# TX power: 6 нива (EU868 legal max = +14 dBm). Бутонът = manual override
# над ADR; следващ LinkADRReq ще пренапише (last-writer-wins).
TX_POWER_LEVELS     = (-3, 0, 5, 8, 11, 14)
DEFAULT_TXPOWER_IDX = 5                              # +14 dBm

# Uplink interval (6 стойности 2..60 s). Multi-channel duty cycle per канал:
# 2 s × 3 канала × 46 ms airtime = 0.77%/канал < 1% ETSI. SCG (1 канал) →
# 2 s = 2.3% duty cycle violation (виж SCG fallback по-горе).
UPLINK_INTERVALS    = (2, 5, 10, 20, 40, 60)
DEFAULT_INTERVAL_IDX = 2                             # 10 s

# Persistent state files (manual config + ADR runtime от LinkADRReq)
TXPOWER_FILE        = "/flash/lw_txpower.dat"
INTERVAL_FILE       = "/flash/lw_interval.dat"
DR_FILE             = "/flash/lw_dr.dat"             # current DR (LinkADRReq)
PWR_IDX_FILE        = "/flash/lw_pwridx.dat"         # current LoRaWAN pwr idx
CHANNELS_FILE       = "/flash/lw_channels.dat"       # extra Hz channels (CFList/NewChannelReq), CSV
CHMASK_FILE         = "/flash/lw_chmask.dat"         # LinkADRReq ChMask runtime
RX1_DELAY_FILE      = "/flash/lw_rx1delay.dat"       # RX1 delay (ms)
NBTRANS_FILE        = "/flash/lw_nbtrans.dat"        # LinkADRReq NbTrans
# A1: FCntDown replay protection
FCNTDN_FILE         = "/flash/lw_fcntdn.dat"         # last received FCntDown (replay guard)
# A2: RXParamSetupReq runtime (override-ва default RX2 + RX1 DR offset)
RX2_FREQ_FILE       = "/flash/lw_rx2freq.dat"        # RX2 freq в Hz
RX2_SF_FILE         = "/flash/lw_rx2sf.dat"          # RX2 SF
RX1_DR_OFFSET_FILE  = "/flash/lw_rx1off.dat"         # RX1 DR offset (0..5)
# A3: DLChannelReq per-channel RX1 freq override
RX1_FREQ_OVR_FILE   = "/flash/lw_rx1ovr.dat"         # CSV: "idx=freq_hz,idx=freq_hz,..."

# Бутон: long-press threshold + boot config window (interval mode)
LONG_PRESS_MS = 1500
INTERVAL_CONFIG_WINDOW_MS = 30_000                   # 30s след boot


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


# === Multi-channel + ADR runtime state ===
#
# Module-level dict, четим от main() и от MAC command handlers. Persistirано
# в /flash/lw_*.dat файлове. CFList от JoinAccept или NewChannelReq добавят
# канали в active_channels; LinkADRReq филтрира кои са TX-enabled (chmask)
# и сменя DR/Pwr; RXTimingSetupReq + JoinAccept RXDelay сменят rx1_delay_ms.
ADR = {
    "active_channels":     list(EU868_DEFAULT_CHANNELS_HZ),  # Hz
    "chmask":              (1 << len(EU868_DEFAULT_CHANNELS_HZ)) - 1,
    "dr":                  DEFAULT_DR,
    "pwr_idx":             DEFAULT_PWR_IDX,
    "tx_power_dbm":        PWR_IDX_TO_DBM[DEFAULT_PWR_IDX],
    "rx1_delay_ms":        DEFAULT_RX1_DELAY_MS,
    "nb_trans":            1,
    "last_tx_freq_hz":     EU868_DEFAULT_CHANNELS_HZ[0],     # за RX1/RX2 restore
    "last_tx_sf":          DR_TO_SF[DEFAULT_DR],
    "last_tx_channel_idx": 0,                                # A3: за RX1 freq override lookup
    # A1: FCntDown replay protection — -1 = no previous downlink yet
    "fcnt_dn":             -1,
    # A2: RXParamSetupReq runtime (default = EU868 spec)
    "rx2_freq_hz":         int(RX2_FREQ_MHZ * 1_000_000),
    "rx2_sf":              RX2_SF,
    "rx1_dr_offset":       0,                                # 0 = RX1 DR == TX DR
    # A3: DLChannelReq per-channel RX1 freq override (ch_idx → freq_hz)
    "rx1_freq_override":   {},
}


def load_channels():
    """Чете extra channels (CSV freq в Hz) от CHANNELS_FILE; връща list[int]."""
    try:
        with open(CHANNELS_FILE) as f:
            data = f.read().strip()
        if not data:
            return []
        return [int(x) for x in data.split(",") if x]
    except Exception:
        return []


def save_channels(extra_channels):
    """Записва само extras (default 3 са в кода). CSV формат за minimal parser."""
    with open(CHANNELS_FILE, "w") as f:
        f.write(",".join(str(c) for c in extra_channels))


def pick_tx_channel(fcnt):
    """Round-robin TX channel selection върху enabled channels.

    Round-robin (а не random) защото е debuggable: при FCnt=N винаги знаеш
    кой канал е ползван, без зависимост от entropy source.
    Връща (channel_index, freq_hz) — idx нужен за A3 RX1 freq override lookup.
    """
    available = [
        (i, ch) for i, ch in enumerate(ADR["active_channels"])
        if ADR["chmask"] & (1 << i)
    ]
    if not available:
        return 0, ADR["active_channels"][0]    # ChMask disable-нал всичко → fallback
    return available[fcnt % len(available)]


def load_rx1_overrides():
    """A3: чете per-channel RX1 freq override от RX1_FREQ_OVR_FILE.

    Format: "0=868000000,1=867500000". Връща dict {ch_idx: freq_hz}.
    """
    try:
        with open(RX1_FREQ_OVR_FILE) as f:
            data = f.read().strip()
        if not data:
            return {}
        out = {}
        for kv in data.split(","):
            if "=" in kv:
                k, v = kv.split("=", 1)
                out[int(k)] = int(v)
        return out
    except Exception:
        return {}


def save_rx1_overrides(overrides):
    """A3: записва RX1 freq override dict в CSV формат."""
    with open(RX1_FREQ_OVR_FILE, "w") as f:
        f.write(",".join("%d=%d" % (k, v) for k, v in overrides.items()))


def parse_cflist(plain):
    """Parse CFList tail (16 байта) от decrypted JoinAccept.

    JoinAccept без CFList = 12 байта (AppNonce 3 + NetID 3 + DevAddr 4 +
    DLSettings 1 + RXDelay 1). С CFList → 28 байта.

    CFList format (CFListType=0, EU868 § 6.2.5):
      bytes 0..14 : 5 × 24-bit LE freq в units of 100 Hz
      byte  15    : CFListType — 0 за freq list, 1 за ChMask (US915 only)

    Връща list от freq в Hz (само ненулевите).
    """
    if len(plain) < 28:
        return []
    cflist = plain[12:28]
    if cflist[15] != 0x00:
        return []                           # тип 1 (ChMask) — не за EU868
    extras = []
    for i in range(5):
        f100 = (cflist[i * 3] |
                (cflist[i * 3 + 1] << 8) |
                (cflist[i * 3 + 2] << 16))
        if f100:
            extras.append(f100 * 100)
    return extras


def add_channel(freq_hz):
    """Добавя нов канал ако не е дублиран и масивът има място (max 8 за EU868).

    Persist-ва extras + ChMask (auto-enable новия канал).
    """
    if freq_hz in ADR["active_channels"]:
        return False
    if len(ADR["active_channels"]) >= 8:
        return False
    ADR["active_channels"].append(freq_hz)
    bit = 1 << (len(ADR["active_channels"]) - 1)
    ADR["chmask"] |= bit
    extras = ADR["active_channels"][len(EU868_DEFAULT_CHANNELS_HZ):]
    try:
        save_channels(extras)
        save_int(CHMASK_FILE, ADR["chmask"])
    except Exception as e:
        print("    add_channel persist err:", e)
    return True


def apply_linkadr_req(sx, cmd):
    """Прилага LinkADRReq към радиото и runtime state-а.

    cmd[0] = 0x03 (CID), cmd[1] = DR/Pwr, cmd[2:4] = ChMask LE, cmd[4] = Redund.
    Връща (dr_ack, pwr_ack, chmask_ack). Прилага физически само ако трите
    ack-а са 1 (atomic — RP002-1.0.3 § 2.6.2).
    """
    dr_pwr      = cmd[1]
    chmask_lo   = cmd[2] | (cmd[3] << 8)
    redund      = cmd[4]
    req_dr      = (dr_pwr >> 4) & 0x0F
    req_pwr     = dr_pwr & 0x0F
    chmask_cntl = (redund >> 4) & 0x07
    nb_trans    = redund & 0x0F
    if nb_trans == 0:
        nb_trans = ADR["nb_trans"]          # 0 = invalid → keep last

    # DR validation: 0..5 или 0xF (no change)
    if req_dr == 0x0F:
        new_dr = ADR["dr"]; dr_ack = 1
    elif req_dr in DR_TO_SF:
        new_dr = req_dr;    dr_ack = 1
    else:
        new_dr = ADR["dr"]; dr_ack = 0

    # Power validation: 0..7 или 0xF (no change)
    if req_pwr == 0x0F:
        new_pwr_idx = ADR["pwr_idx"]; pwr_ack = 1
    elif req_pwr in PWR_IDX_TO_DBM:
        new_pwr_idx = req_pwr;        pwr_ack = 1
    else:
        new_pwr_idx = ADR["pwr_idx"]; pwr_ack = 0

    # ChMask: ChMaskCntl=0 (mask за ch 0..15) и =6 (all on) поддържаме.
    n_ch = len(ADR["active_channels"])
    full_mask = (1 << n_ch) - 1
    if chmask_cntl == 0:
        new_chmask = chmask_lo & full_mask
        if new_chmask == 0:                 # disabling всички = invalid
            new_chmask = ADR["chmask"]; chmask_ack = 0
        else:
            chmask_ack = 1
    elif chmask_cntl == 6:
        new_chmask = full_mask;            chmask_ack = 1
    else:
        new_chmask = ADR["chmask"];        chmask_ack = 0

    if dr_ack and pwr_ack and chmask_ack:
        new_sf  = DR_TO_SF[new_dr]
        new_dbm = PWR_IDX_TO_DBM[new_pwr_idx]
        try:
            sx.setSpreadingFactor(new_sf)
            sx.setOutputPower(new_dbm)
        except Exception as e:
            print("    apply_linkadr radio err:", e)
        ADR["dr"]           = new_dr
        ADR["pwr_idx"]      = new_pwr_idx
        ADR["tx_power_dbm"] = new_dbm
        ADR["chmask"]       = new_chmask
        ADR["nb_trans"]     = nb_trans
        try:
            save_int(DR_FILE,      new_dr)
            save_int(PWR_IDX_FILE, new_pwr_idx)
            save_int(CHMASK_FILE,  new_chmask)
            save_int(NBTRANS_FILE, nb_trans)
        except Exception as e:
            print("    apply_linkadr persist err:", e)
        print("    LinkADR applied: DR=%d (SF%d) Pwr=%d (%+ddBm) ChMask=0x%02X NbTrans=%d" %
              (new_dr, new_sf, new_pwr_idx, new_dbm, new_chmask, nb_trans))
    else:
        print("    LinkADR rejected: dr_ack=%d pwr_ack=%d chmask_ack=%d" %
              (dr_ack, pwr_ack, chmask_ack))

    return dr_ack, pwr_ack, chmask_ack


def apply_rxtiming_req(cmd):
    """RXTimingSetupReq → нов RX1 delay в seconds. Del=0 → 1 s; иначе N s."""
    del_field = cmd[1] & 0x0F
    delay_s = max(1, del_field)
    ADR["rx1_delay_ms"] = delay_s * 1000
    try:
        save_int(RX1_DELAY_FILE, ADR["rx1_delay_ms"])
    except Exception as e:
        print("    apply_rxtiming persist err:", e)
    print("    RXTimingSetup applied: RX1 delay = %d s" % delay_s)


def apply_rxparamsetup_req(cmd):
    """A2: RXParamSetupReq → нов RX2 freq/SF + RX1 DR offset (LoRaWAN 1.0.3 § 5.4).

    cmd[1] = DLSettings: bits 6..4 = RX1DROffset (0..5), bits 3..0 = RX2 DR.
    cmd[2..4] = RX2 Frequency (24-bit LE × 100 Hz). 0 = no change.

    Връща ack bytes (CID + status). Status bits: 0=ChannelACK, 1=RX2DR_ACK, 2=RX1DROffset_ACK.
    Сървърът чака **трите** ack=1 → иначе ще повтори командата.
    """
    dl_settings   = cmd[1]
    rx1_dr_offset = (dl_settings >> 4) & 0x07
    rx2_dr        = dl_settings & 0x0F
    freq_hz       = (cmd[2] | (cmd[3] << 8) | (cmd[4] << 16)) * 100

    rx1_off_ok = 1 if 0 <= rx1_dr_offset <= 5 else 0
    rx2_dr_ok  = 1 if rx2_dr in DR_TO_SF else 0
    ch_ok      = 1 if (freq_hz == 0 or 863_000_000 <= freq_hz <= 870_000_000) else 0

    if rx1_off_ok and rx2_dr_ok and ch_ok:
        ADR["rx1_dr_offset"] = rx1_dr_offset
        ADR["rx2_sf"]        = DR_TO_SF[rx2_dr]
        if freq_hz > 0:
            ADR["rx2_freq_hz"] = freq_hz
        try:
            save_int(RX1_DR_OFFSET_FILE, rx1_dr_offset)
            save_int(RX2_SF_FILE, ADR["rx2_sf"])
            save_int(RX2_FREQ_FILE, ADR["rx2_freq_hz"])
        except Exception as e:
            print("    apply_rxparamsetup persist err:", e)
        print("    RXParamSetup applied: RX2=%.3f MHz SF%d, RX1DROffset=%d" %
              (ADR["rx2_freq_hz"] / 1e6, ADR["rx2_sf"], rx1_dr_offset))
    else:
        print("    RXParamSetup rejected: rx1_off_ok=%d rx2_dr_ok=%d ch_ok=%d" %
              (rx1_off_ok, rx2_dr_ok, ch_ok))

    status = (rx1_off_ok << 2) | (rx2_dr_ok << 1) | ch_ok
    return bytes([0x05, status])


def apply_dlchannel_req(cmd):
    """A3: DLChannelReq → per-channel RX1 freq override (LoRaWAN 1.0.3 § 5.7).

    cmd[1] = ChIndex; cmd[2..4] = Frequency (24-bit LE × 100 Hz).
    Asymmetric link: за TX на канал N, RX1 е на различна freq. Рядко в EU868
    но spec-defined. ChIndex трябва да съществува в active_channels.

    Връща ack bytes. Status bits: 0=ChannelFreqOK, 1=UplinkFreqExists.
    """
    ch_idx  = cmd[1]
    freq_hz = (cmd[2] | (cmd[3] << 8) | (cmd[4] << 16)) * 100

    uplink_freq_exists = 1 if ch_idx < len(ADR["active_channels"]) else 0
    ch_freq_ok         = 1 if (863_000_000 <= freq_hz <= 870_000_000) else 0

    if uplink_freq_exists and ch_freq_ok:
        ADR["rx1_freq_override"][ch_idx] = freq_hz
        try:
            save_rx1_overrides(ADR["rx1_freq_override"])
        except Exception as e:
            print("    apply_dlchannel persist err:", e)
        print("    DLChannel ch=%d → RX1 freq=%.3f MHz" % (ch_idx, freq_hz / 1e6))
    else:
        print("    DLChannel rejected: uplink_freq_exists=%d ch_freq_ok=%d" %
              (uplink_freq_exists, ch_freq_ok))

    status = (uplink_freq_exists << 1) | ch_freq_ok
    return bytes([0x0A, status])


def process_mac_commands(sx, parsed_cmds, last_snr):
    """Връща FOpts bytes за следващия uplink — отговор на server commands.

    EU868 multi-channel + ADR full path (Phase A spec-compliance):
      - DevStatusReq      → DevStatusAns(battery=255, margin=last RX SNR)
      - LinkADRReq        → apply DR/Pwr/ChMask + LinkADRAns
      - RXTimingSetupReq  → apply нов RX1 delay + RXTimingSetupAns
      - NewChannelReq     → добавя канал + NewChannelAns
      - RXParamSetupReq   → apply RX2 freq/SF + RX1 DR offset + ack [A2]
      - DLChannelReq      → per-channel RX1 freq override + ack [A3]
    Не отговаряме (за Phase B): DutyCycleReq, TxParamSetupReq.
    """
    response = b""
    for name, hex_data in parsed_cmds:
        cmd = bytes.fromhex(hex_data)
        cid = cmd[0]
        if cid == 0x06:                          # DevStatusReq
            response += mac_devstatus_ans(255, last_snr)
        elif cid == 0x03 and len(cmd) >= 5:      # LinkADRReq
            dr_ack, pwr_ack, chmask_ack = apply_linkadr_req(sx, cmd)
            response += mac_linkadr_ans(pwr_ack, dr_ack, chmask_ack)
        elif cid == 0x08 and len(cmd) >= 2:      # RXTimingSetupReq
            apply_rxtiming_req(cmd)
            response += mac_rxtiming_ans()
        elif cid == 0x05 and len(cmd) >= 5:      # RXParamSetupReq [A2]
            response += apply_rxparamsetup_req(cmd)
        elif cid == 0x0A and len(cmd) >= 5:      # DLChannelReq [A3]
            response += apply_dlchannel_req(cmd)
        elif cid == 0x07 and len(cmd) >= 6:      # NewChannelReq
            ch_idx = cmd[1]
            freq_hz = (cmd[2] | (cmd[3] << 8) | (cmd[4] << 16)) * 100
            dr_range = cmd[5]
            min_dr = dr_range & 0x0F
            max_dr = (dr_range >> 4) & 0x0F
            ch_freq_ok  = 1 if (freq_hz == 0 or 863_000_000 <= freq_hz <= 870_000_000) else 0
            dr_range_ok = 1 if (min_dr in DR_TO_SF and max_dr in DR_TO_SF and min_dr <= max_dr) else 0
            if ch_freq_ok and dr_range_ok and freq_hz != 0:
                add_channel(freq_hz)
                print("    NewChannel idx=%d freq=%.3f MHz DR%d..%d added" %
                      (ch_idx, freq_hz / 1e6, min_dr, max_dr))
            response += bytes([0x07, (dr_range_ok << 1) | ch_freq_ok])
        # Останалите MAC команди не отговаряме засега (DutyCycleReq за Phase B3).
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

    # A1: FCntDown replay protection (LoRaWAN 1.0.3 § 4.3.1.5).
    # Сравняваме fcnt_lo с последния видян; разрешаваме 16-bit rollover (last
    # в горната половина и new в долната). MIC е валиден на replay frame
    # (нищо в frame не зависи от текущото време), затова **тази проверка** е
    # реалната защита от replay attack — без нея атакуващ може да replay-не
    # "relay=1" command в безкрайност.
    last_fcnt_dn = ADR.get("fcnt_dn", -1)
    if last_fcnt_dn >= 0:
        is_rollover = last_fcnt_dn > 0xF000 and fcnt_lo < 0x1000
        if not is_rollover and fcnt_lo <= last_fcnt_dn:
            return {"error": "fcnt replay",
                    "got": fcnt_lo, "last": last_fcnt_dn}

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
    """Init на SX1262 с initial freq/SF/power от ADR runtime state.

    main() трябва да е заредил ADR от /flash/lw_*.dat преди извикване,
    иначе init-ва с DEFAULT_DR/DEFAULT_PWR_IDX/ch 0.
    """
    Pin("P100", Pin.OUT, value=1)            # RF_SW1 enable (Wio-SX1262)
    # SPI(3) = SCI9 simple-SPI + FIFO + dual DTC: P109/P110/P111, no polling.
    sx = SX1262(spi_bus=3, clk="P111", mosi="P109", miso="P110",
                cs="P206", irq="P015", rst="P001", gpio="P002")
    initial_freq_mhz = ADR["active_channels"][0] / 1e6
    initial_sf       = DR_TO_SF[ADR["dr"]]
    initial_dbm      = PWR_IDX_TO_DBM[ADR["pwr_idx"]]
    sx.begin(
        freq=initial_freq_mhz, bw=BW, sf=initial_sf, cr=5,
        syncWord=0x34, power=initial_dbm, currentLimit=60.0,
        preambleLength=8, implicit=False, implicitLen=0xFF,
        crcOn=True, txIq=False, rxIq=True,
        tcxoVoltage=1.8, useRegulatorLDO=False, blocking=True,
    )
    ADR["last_tx_freq_hz"] = ADR["active_channels"][0]
    ADR["last_tx_sf"]      = initial_sf
    ADR["tx_power_dbm"]    = initial_dbm
    return sx


def otaa_join(sx, dev_nonce):
    """Връща (devaddr_le, nwkskey, appskey) при успех, или None при провал.

    Join req → ch 0 (mandatory) при default DR (DR5/SF7).
    След JoinAccept:
      - RXDelay byte (plain[11]) → ADR['rx1_delay_ms']
      - CFList tail → add_channel() за всяка extra freq
    """
    join_freq_mhz = ADR["active_channels"][0] / 1e6
    join_sf       = DR_TO_SF[ADR["dr"]]
    try:
        sx.setFrequency(join_freq_mhz)
        sx.setSpreadingFactor(join_sf)
    except Exception as e:
        print("  set join freq/sf err:", e)
    ADR["last_tx_freq_hz"] = ADR["active_channels"][0]
    ADR["last_tx_sf"]      = join_sf

    dn_le = dev_nonce.to_bytes(2, "little")
    join_req = bytes([0x00]) + bytes(reversed(JoinEUI)) + bytes(reversed(DevEUI)) + dn_le
    join_req += bytes(AES_CMAC().encode(AppKey, join_req))[:4]

    print("OTAA join (DevNonce=%d, freq=%.3f MHz SF%d)..." %
          (dev_nonce, join_freq_mhz, join_sf))
    sx.send(join_req)
    msg, err = sx.recv(0, True, 8000)
    if not msg or len(msg) == 0:
        print("  join failed (no JoinAccept, err=%d)" % err)
        return None

    plain = decrypt_join_accept(AppKey, bytes(msg[1:]))
    if len(plain) < 12:
        print("  join failed (plain too short: %d)" % len(plain))
        return None
    app_nonce = plain[0:3]
    net_id    = plain[3:6]
    devaddr   = plain[6:10]
    # plain[10] = DLSettings (RX1DRoffset/RX2DR); plain[11] = RXDelay (s, 0=1)
    rx_delay_s = plain[11] & 0x0F
    if rx_delay_s == 0:
        rx_delay_s = 1
    ADR["rx1_delay_ms"] = rx_delay_s * 1000
    try:
        save_int(RX1_DELAY_FILE, ADR["rx1_delay_ms"])
    except Exception as e:
        print("  RX1 delay persist err:", e)

    # CFList: bytes 12..27 (16 байта) ако сървърът ги е приложил.
    cflist_extras = parse_cflist(plain)
    for f in cflist_extras:
        if add_channel(f):
            print("  CFList +%.3f MHz" % (f / 1e6))

    nwkskey = derive_session_key(AppKey, 0x01, app_nonce, net_id, dn_le)
    appskey = derive_session_key(AppKey, 0x02, app_nonce, net_id, dn_le)

    print("  joined: DevAddr=%s RX1delay=%ds channels=%d" %
          (bytes(reversed(devaddr)).hex(), rx_delay_s, len(ADR["active_channels"])))
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


async def _recv_yielding(sx, total_timeout_ms, slice_ms=100):
    """Sliced recv: множество кратки recv() с yield между тях.

    Чипа стои в RX mode за `slice_ms` (с timeout), recv връща ако има
    пакет или slice timeout-ва. След всеки slice yield-ваме на scheduler-а.
    Heartbeat и други корутини продължават да работят на всеки ~slice_ms.

    За TTN single-channel SF7 gateway, downlink-ът идва с predictable
    timing (+5s от TX), така че първите slice-ове го улавят. За multi-
    gateway/multi-channel use cases може да се пропуснат пакети в gap-а
    между slice-ове — за това нужно е true non-blocking IRQ-driven RX.
    """
    elapsed = 0
    while elapsed < total_timeout_ms:
        slice_t = slice_ms if (total_timeout_ms - elapsed) >= slice_ms \
                           else (total_timeout_ms - elapsed)
        msg, err = sx.recv(0, True, slice_t)
        if msg and len(msg) > 0:
            return msg, err
        elapsed += slice_t
        await asyncio.sleep_ms(0)               # yield to scheduler
    return b"", err


async def listen_rx1(sx, devaddr, nwkskey, appskey, recv_timeout_ms=2500):
    """RX1: ADR['rx1_delay_ms'] след TX, на TX freq (или DLChannel override) +
    RX1 DR (== TX DR - rx1_dr_offset, clamped към DR0).

    A2: rx1_dr_offset (от RXParamSetupReq) измества RX1 DR надолу. Default 0
    значи RX1 = TX DR (legacy behavior). При offset > 0 → setSpreadingFactor.

    A3: rx1_freq_override (от DLChannelReq) override-ва RX1 freq за дадения
    TX channel idx. Default = TX freq (legacy).
    """
    # A3: Apply RX1 freq override ако сървърът го е поискал за тоя канал
    override_hz = ADR["rx1_freq_override"].get(ADR["last_tx_channel_idx"])
    if override_hz:
        try:
            sx.setFrequency(override_hz / 1e6)
        except Exception as e:
            print("  RX1 override freq err:", e)

    # A2: Apply RX1 DR offset (downshift от TX DR)
    rx1_dr = max(0, ADR["dr"] - ADR["rx1_dr_offset"])
    rx1_sf = DR_TO_SF[rx1_dr]
    if rx1_sf != ADR["last_tx_sf"]:
        try:
            sx.setSpreadingFactor(rx1_sf)
        except Exception as e:
            print("  RX1 DR offset SF err:", e)

    await asyncio.sleep_ms(ADR["rx1_delay_ms"])
    msg, err = await _recv_yielding(sx, recv_timeout_ms)
    if not msg or len(msg) == 0:
        return None
    rssi = sx.getRSSI()
    snr  = sx.getSNR()
    info = parse_downlink(devaddr, nwkskey, appskey, bytes(msg))
    if info is None:
        info = {"error": "frame too short / not parseable"}
    info["window"] = "RX1"
    info["rssi"]   = rssi
    info["snr"]    = snr
    info["raw"]    = bytes(msg).hex()
    return info


async def listen_rx2(sx, devaddr, nwkskey, appskey, recv_timeout_ms=2500):
    """RX2 fallback: 1 s след RX1 close, на ADR['rx2_freq_hz']/ADR['rx2_sf'].

    A2: RXParamSetupReq може да override-не default 869.525/SF12 → ADR-а
    държи runtime стойностите. След recv възстановяваме на last_tx_* за
    коректност при exception/path-and (next uplink ги пренапише).
    """
    sx.setFrequency(ADR["rx2_freq_hz"] / 1e6)
    sx.setSpreadingFactor(ADR["rx2_sf"])
    msg, err = await _recv_yielding(sx, recv_timeout_ms)
    rssi = sx.getRSSI() if (msg and len(msg) > 0) else None
    snr  = sx.getSNR()  if (msg and len(msg) > 0) else None
    # Restore последния TX freq/SF (next uplink ще ги пренапише)
    sx.setFrequency(ADR["last_tx_freq_hz"] / 1e6)
    sx.setSpreadingFactor(ADR["last_tx_sf"])
    if not msg or len(msg) == 0:
        return None
    info = parse_downlink(devaddr, nwkskey, appskey, bytes(msg))
    if info is None:
        info = {"error": "frame too short / not parseable"}
    info["window"] = "RX2"
    info["rssi"]   = rssi
    info["snr"]    = snr
    info["raw"]    = bytes(msg).hex()
    return info


# === Button latch (polls P014 на ~50ms, latch-ва натискане до consume) ===

class ButtonLatch:
    """Async polling button с short/long press detection.

    Hardware: P014 → бутон → GND (active-low с PULL_UP).

    Callbacks:
      on_press      — short press (<LONG_PRESS_MS), вика се на release edge
      on_long_press — long press (≥LONG_PRESS_MS), вика се при достигане на
                      threshold-а (докато бутонът още е натиснат)

    Логика:
      released → pressed:    запомням press_t
      pressed (held):        ако е минал LONG_PRESS_MS → long_fired + on_long_press
      pressed → released:    ако !long_fired → short press → on_press
                             ако long_fired → нищо (вече сме обработили)
    """
    def __init__(self, pin_name, on_press=None, on_long_press=None):
        self.pin = Pin(pin_name, Pin.IN, Pin.PULL_UP)
        self.was_pressed = False
        self.press_t = 0
        self.long_fired = False
        self.on_press = on_press
        self.on_long_press = on_long_press

    async def poll_task(self, interval_ms=50):
        while True:
            cur_pressed = (self.pin.value() == 0)
            now = time.ticks_ms()
            if cur_pressed and not self.was_pressed:
                # rising edge — започва натискане
                self.press_t = now
                self.long_fired = False
            elif cur_pressed and self.was_pressed:
                # held — провери за long press threshold
                if (not self.long_fired and
                        time.ticks_diff(now, self.press_t) >= LONG_PRESS_MS):
                    self.long_fired = True
                    if self.on_long_press is not None:
                        try:
                            self.on_long_press()
                        except Exception as e:
                            print("button on_long_press error:", e)
            elif not cur_pressed and self.was_pressed:
                # falling edge — released
                if not self.long_fired:
                    if self.on_press is not None:
                        try:
                            self.on_press()
                        except Exception as e:
                            print("button on_press error:", e)
            self.was_pressed = cur_pressed
            await asyncio.sleep_ms(interval_ms)


# === Concurrent demo task ===

async def heartbeat_task():
    """Демо на async паралелизъм — печата heartbeat всеки 2 s.

    Ще го виждаш да тиктака през ~4.5s sleep преди RX1 и през 10s idle
    sleep между uplinks (~5 пъти на 10s cycle). Замразява се само по
    време на blocking sx.send() и sx.recv() (виж B-стъпка от плана).
    """
    n = 0
    t0_ms = time.ticks_ms()
    while True:
        uptime_s = time.ticks_diff(time.ticks_ms(), t0_ms) // 1000
        print("    [hb #%d uptime=%ds]" % (n, uptime_s))
        n += 1
        await asyncio.sleep(2)


# === Display state (shared между main() и display_task()) ===

DISP = {
    "boot_t0":      0,                        # time.ticks_ms() при boot — set в app()
    "tx_power":     PWR_IDX_TO_DBM[DEFAULT_PWR_IDX],
    "sf":           DR_TO_SF[DEFAULT_DR],
    "interval_s":   UPLINK_INTERVALS[DEFAULT_INTERVAL_IDX],
    "fcnt":         0,
    "last_rssi":    None,
    "last_snr":     None,
    "last_window":  None,                     # "RX1" / "RX2" / None
}


async def display_task(oled):
    """OLED render task — tick на 500ms.

    Първите CREDENTIALS_BOOT_WINDOW_MS (20s) — top 2 = radio, bot 2 = credentials
    rotation (4 страници × 5s). После — 4 реда radio params (forever).
    """
    if oled is None:
        return

    cred_pages = (
        ("DevEUI",     DevEUI.hex().upper()),
        ("JoinEUI",    JoinEUI.hex().upper()),
        ("AppKey 1/2", AppKey.hex()[0:16].upper()),
        ("AppKey 2/2", AppKey.hex()[16:32].upper()),
    )

    while True:
        try:
            now = time.ticks_ms()
            uptime_ms = time.ticks_diff(now, DISP["boot_t0"])
            tx_pow = DISP["tx_power"]
            interval = DISP["interval_s"]
            fcnt = DISP["fcnt"]
            rssi = DISP["last_rssi"]
            snr  = DISP["last_snr"]
            win  = DISP["last_window"]
            sf   = DISP["sf"]

            # Top row: TX power + interval (винаги, и в Phase 1, и в Phase 2)
            top_line = "TX%+ddBm Int%ds" % (tx_pow, interval)

            oled.fill(0)
            oled.text(top_line[:16], 0, 0)

            if uptime_ms < CREDENTIALS_BOOT_WINDOW_MS:
                # Phase 1: y=8 = SF + RSSI/SNR (или just SF ако no RX)
                if rssi is None:
                    oled.text("SF%d %s" % (sf, "RX:--"), 0, 8)
                else:
                    oled.text("SF%d R%+d %s" % (sf, rssi, win or "--"), 0, 8)
                # y=16, y=24: credentials rotation
                page_idx = (uptime_ms // CRED_PAGE_MS) % len(cred_pages)
                label, value = cred_pages[page_idx]
                oled.text(label[:16], 0, 16)
                oled.text(value[:16], 0, 24)
            else:
                # Phase 2: 4 реда radio params (top row вече зает)
                oled.text("SF%d FCnt %d" % (sf, fcnt), 0, 8)
                if rssi is None:
                    oled.text("RSSI ---", 0, 16)
                    oled.text("SNR ---", 0, 24)
                else:
                    oled.text("RSSI %d %s" % (rssi, win or "--"), 0, 16)
                    oled.text("SNR %+.1f dB" % snr, 0, 24)

            oled.show()
        except Exception as e:
            print("display_task error:", e)
        await asyncio.sleep_ms(500)


# === Main ===

async def main():
    print("=" * 50)
    print("VK_RA4M2 LoRaWAN end-node (EU868 multi-channel)")
    print("=" * 50)

    # === Load ADR runtime state от /flash/ преди init_radio ===
    # init_radio чете ADR['active_channels'][0] / ADR['dr'] / ADR['pwr_idx']
    # за initial freq/SF/power, така че трябва да е заредено преди това.
    ADR["dr"]           = load_int(DR_FILE,        DEFAULT_DR)
    ADR["pwr_idx"]      = load_int(PWR_IDX_FILE,   DEFAULT_PWR_IDX)
    ADR["chmask"]       = load_int(CHMASK_FILE,    ADR["chmask"])
    ADR["rx1_delay_ms"] = load_int(RX1_DELAY_FILE, DEFAULT_RX1_DELAY_MS)
    ADR["nb_trans"]     = load_int(NBTRANS_FILE,   1)
    # A1: FCntDown replay state
    ADR["fcnt_dn"]      = load_int(FCNTDN_FILE,    -1)
    # A2: RXParamSetupReq runtime (default = EU868 spec, override-ва се от server)
    ADR["rx2_freq_hz"]  = load_int(RX2_FREQ_FILE,  int(RX2_FREQ_MHZ * 1_000_000))
    ADR["rx2_sf"]       = load_int(RX2_SF_FILE,    RX2_SF)
    ADR["rx1_dr_offset"] = load_int(RX1_DR_OFFSET_FILE, 0)
    # A3: DLChannelReq per-channel RX1 freq override
    ADR["rx1_freq_override"] = load_rx1_overrides()

    for f in load_channels():
        if f not in ADR["active_channels"] and len(ADR["active_channels"]) < 8:
            ADR["active_channels"].append(f)
    if ADR["dr"] not in DR_TO_SF:
        ADR["dr"] = DEFAULT_DR
    if ADR["pwr_idx"] not in PWR_IDX_TO_DBM:
        ADR["pwr_idx"] = DEFAULT_PWR_IDX
    n_ch = len(ADR["active_channels"])
    ADR["chmask"] &= (1 << n_ch) - 1
    if ADR["chmask"] == 0:
        ADR["chmask"] = (1 << n_ch) - 1
    if ADR["rx2_sf"] not in DR_TO_SF.values():
        ADR["rx2_sf"] = RX2_SF
    if not (0 <= ADR["rx1_dr_offset"] <= 5):
        ADR["rx1_dr_offset"] = 0
    ADR["tx_power_dbm"] = PWR_IDX_TO_DBM[ADR["pwr_idx"]]
    print("ADR: DR%d (SF%d) Pwr=idx%d (%+ddBm) ChMask=0x%02X (%d ch) RX1=%dms NbTrans=%d" %
          (ADR["dr"], DR_TO_SF[ADR["dr"]], ADR["pwr_idx"],
           PWR_IDX_TO_DBM[ADR["pwr_idx"]], ADR["chmask"], n_ch,
           ADR["rx1_delay_ms"], ADR["nb_trans"]))
    print("Channels:", ", ".join("%.3f" % (c / 1e6) for c in ADR["active_channels"]))
    print("RX2: %.3f MHz SF%d, RX1DROffset=%d, FCntDn=%d, RX1ovr=%d" %
          (ADR["rx2_freq_hz"] / 1e6, ADR["rx2_sf"], ADR["rx1_dr_offset"],
           ADR["fcnt_dn"], len(ADR["rx1_freq_override"])))

    sx = init_radio()
    print("Radio OK")

    # Load or create session
    session = load_session()
    if session is None:
        # Fresh start → reset ADR runtime state към defaults.
        # CFList от JoinAccept ще добави обратно extra канали; LinkADRReq
        # от сървъра ще adjust-не DR/Pwr/ChMask след първите uplink-ове.
        ADR["active_channels"]   = list(EU868_DEFAULT_CHANNELS_HZ)
        ADR["chmask"]            = (1 << len(EU868_DEFAULT_CHANNELS_HZ)) - 1
        ADR["dr"]                = DEFAULT_DR
        ADR["pwr_idx"]           = DEFAULT_PWR_IDX
        ADR["tx_power_dbm"]      = PWR_IDX_TO_DBM[DEFAULT_PWR_IDX]
        ADR["nb_trans"]          = 1
        # A1: reset replay counter — нова сесия = нов FCntDown space
        ADR["fcnt_dn"]           = -1
        # A2: reset RX2 + RX1 DR offset към EU868 defaults
        ADR["rx2_freq_hz"]       = int(RX2_FREQ_MHZ * 1_000_000)
        ADR["rx2_sf"]            = RX2_SF
        ADR["rx1_dr_offset"]     = 0
        # A3: clear DLChannel overrides
        ADR["rx1_freq_override"] = {}
        try:
            save_int(DR_FILE,           DEFAULT_DR)
            save_int(PWR_IDX_FILE,      DEFAULT_PWR_IDX)
            save_int(CHMASK_FILE,       ADR["chmask"])
            save_int(NBTRANS_FILE,      1)
            save_int(FCNTDN_FILE,       -1)
            save_int(RX2_FREQ_FILE,     ADR["rx2_freq_hz"])
            save_int(RX2_SF_FILE,       ADR["rx2_sf"])
            save_int(RX1_DR_OFFSET_FILE, 0)
            save_channels([])
            save_rx1_overrides({})
        except Exception as e:
            print("ADR reset persist err:", e)
        try:
            sx.setSpreadingFactor(DR_TO_SF[ADR["dr"]])
            sx.setOutputPower(PWR_IDX_TO_DBM[ADR["pwr_idx"]])
        except Exception as e:
            print("ADR reset radio err:", e)

        # Initial DevNonce 100 за избягване на replay при ново устройство.
        dev_nonce = max(100, load_int(DEVNONCE_FILE, 0) + 1)
        joined = None
        for attempt in range(5):
            save_int(DEVNONCE_FILE, dev_nonce)
            joined = otaa_join(sx, dev_nonce)
            if joined is not None:
                break
            dev_nonce += 1                   # retry с по-голям nonce
            await asyncio.sleep(3)
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
        # Resume — ADR state вече е заредено по-горе. init_radio ползва
        # ADR['dr']/ADR['pwr_idx'] за initial config, но init_radio() може
        # да е stick-нал на defaults ако е извикан преди load. Re-apply:
        try:
            sx.setSpreadingFactor(DR_TO_SF[ADR["dr"]])
            sx.setOutputPower(PWR_IDX_TO_DBM[ADR["pwr_idx"]])
        except Exception as e:
            print("apply saved ADR err:", e)
        print("Loaded session: DevAddr=%s FCnt=%d" %
              (bytes(reversed(devaddr)).hex(), fcnt))

    # Periodic uplinks (FPort=2, 3-byte payload):
    #   bytes 0-1 temp  int16 BE × 100  (sim sweep 20..25 °C, 0.01 °C res)
    #   byte 2    relay uint8 (актуален state на актуатора, 0=off / 1=on)
    #
    # Downlink на FPort=2 (1 byte): relay (0=off→зелено, 1=on→червено).
    # Така uplink/downlink имат симетричен smysъл: и двете носят relay state.
    relay_strip = make_relay_strip()
    relay_state = 0
    set_relay(relay_strip, relay_state)               # старт: off / зелено

    # === Load manual config (TX power button, uplink interval button) ===
    # Бутонът cycle-ва тези стойности; ADR LinkADRReq override-ва TX power
    # (last-writer-wins). Interval е manual-only — сървърът не го контролира.
    txpower_idx  = load_int(TXPOWER_FILE,  DEFAULT_TXPOWER_IDX)
    interval_idx = load_int(INTERVAL_FILE, DEFAULT_INTERVAL_IDX)
    if not (0 <= txpower_idx < len(TX_POWER_LEVELS)):
        txpower_idx = DEFAULT_TXPOWER_IDX
    if not (0 <= interval_idx < len(UPLINK_INTERVALS)):
        interval_idx = DEFAULT_INTERVAL_IDX
    current_tx_power   = TX_POWER_LEVELS[txpower_idx]
    current_interval_s = UPLINK_INTERVALS[interval_idx]
    try:
        sx.setOutputPower(current_tx_power)
        ADR["tx_power_dbm"] = current_tx_power
    except Exception as e:
        print("setOutputPower init error:", e)
    DISP["tx_power"]   = current_tx_power
    DISP["sf"]         = DR_TO_SF[ADR["dr"]]
    DISP["interval_s"] = current_interval_s
    DISP["fcnt"]       = fcnt
    print("Manual config: TX=%+ddBm (idx=%d) Interval=%ds (idx=%d) SF=%d" %
          (current_tx_power, txpower_idx, current_interval_s, interval_idx,
           DR_TO_SF[ADR["dr"]]))

    boot_t0_main = DISP["boot_t0"]                    # за in_config_window()

    # === Button callback ===
    # Едно натискане:
    #   1. Toggle relay (color сменя green↔red)
    #   2. Cycle активния параметър:
    #        първите 30s след boot → cycle interval (6 стойности)
    #        след 30s              → cycle TX power (6 стойности)
    def in_config_window():
        return time.ticks_diff(time.ticks_ms(), boot_t0_main) < INTERVAL_CONFIG_WINDOW_MS

    def on_btn_press():
        nonlocal relay_state
        nonlocal txpower_idx, interval_idx, current_tx_power, current_interval_s

        # 1. Toggle relay
        relay_state = 1 - relay_state
        set_relay(relay_strip, relay_state)

        # 2. Cycle активен параметър
        if in_config_window():
            interval_idx = (interval_idx + 1) % len(UPLINK_INTERVALS)
            current_interval_s = UPLINK_INTERVALS[interval_idx]
            DISP["interval_s"] = current_interval_s
            try: save_int(INTERVAL_FILE, interval_idx)
            except Exception as e: print("save interval err:", e)
            print("    [btn → relay=%s, interval=%ds (idx=%d)]" %
                  ("ON" if relay_state else "OFF",
                   current_interval_s, interval_idx))
        else:
            txpower_idx = (txpower_idx + 1) % len(TX_POWER_LEVELS)
            current_tx_power = TX_POWER_LEVELS[txpower_idx]
            DISP["tx_power"] = current_tx_power
            try:
                sx.setOutputPower(current_tx_power)
            except Exception as e:
                print("setOutputPower err:", e)
            try: save_int(TXPOWER_FILE, txpower_idx)
            except Exception as e: print("save txpower err:", e)
            print("    [btn → relay=%s, TX=%+ddBm (idx=%d)]" %
                  ("ON" if relay_state else "OFF",
                   current_tx_power, txpower_idx))

    button.on_press = on_btn_press

    pending_mac = mac_devtime_req() if ASK_DEVICE_TIME_AT_START else b""
    last_snr = 0
    temp_c100 = 2000                                  # 20.00 °C старт
    temp_step = 25                                    # +0.25 °C / uplink
    while True:
        # Pращаме authoritative relay state в byte 2 (не button event-а).
        # Бутонът остава само като local toggle source чрез on_btn_press callback.
        temp_c100 += temp_step
        if temp_c100 >= 2500: temp_step = -25         # 25.00 → започни надолу
        if temp_c100 <= 2000: temp_step = +25         # 20.00 → започни нагоре
        # encode int16 BE (two's complement за отрицателни)
        t = temp_c100 if temp_c100 >= 0 else (temp_c100 + 0x10000)
        payload = bytes([(t >> 8) & 0xFF, t & 0xFF, relay_state])

        # === Pick TX channel + apply SF (round-robin през enabled канали) ===
        # Връща (channel_idx, freq_hz). idx нужен за A3 RX1 freq override lookup.
        tx_ch_idx, tx_freq_hz = pick_tx_channel(fcnt)
        cur_sf     = DR_TO_SF[ADR["dr"]]
        ADR["last_tx_channel_idx"] = tx_ch_idx
        ADR["last_tx_freq_hz"]     = tx_freq_hz
        ADR["last_tx_sf"]          = cur_sf
        try:
            sx.setFrequency(tx_freq_hz / 1e6)
            sx.setSpreadingFactor(cur_sf)
        except Exception as e:
            print("  set freq/sf err:", e)
        DISP["sf"] = cur_sf

        confirmed = (CONFIRMED_EVERY > 0) and (fcnt % CONFIRMED_EVERY == 0) and (fcnt > 0)
        kind = "Confirmed" if confirmed else "Unconfirmed"
        info = "temp=%.2f°C relay=%d" % (temp_c100 / 100.0, relay_state)
        ch_info = "ch=%.3f MHz DR%d" % (tx_freq_hz / 1e6, ADR["dr"])
        if pending_mac:
            print("[FCnt=%d %s %s] %s + FOpts=%s" %
                  (fcnt, kind, ch_info, info, pending_mac.hex()))
        else:
            print("[FCnt=%d %s %s] %s" % (fcnt, kind, ch_info, info))
        try:
            send_uplink(sx, devaddr, nwkskey, appskey, fcnt, 0x02, payload,
                        fopts=pending_mac, confirmed=confirmed)
            pending_mac = b""                    # consumed; ще се преинит-ва от downlink
            dl = await listen_rx1(sx, devaddr, nwkskey, appskey)
            if dl is None:
                print("  RX1: nothing — listening RX2 (869.525MHz SF12)...")
                dl = await listen_rx2(sx, devaddr, nwkskey, appskey)
                if dl is None:
                    print("  RX2: nothing")
            if dl is None:
                pass                             # вече логнато
            elif "error" in dl:
                print("  downlink rejected: %s" % dl["error"])
                # RSSI/SNR са физическа стойност, валидни и при parse error
                if "rssi" in dl:
                    DISP["last_rssi"]   = dl["rssi"]
                    DISP["last_snr"]    = dl["snr"]
                    DISP["last_window"] = dl["window"]
            else:
                print("  *** %s downlink RSSI=%d SNR=%.1f FCnt=%d FPort=%s" %
                      (dl["window"], dl["rssi"], dl["snr"],
                       dl["fcnt"], dl["fport"]))
                DISP["last_rssi"]   = dl["rssi"]
                DISP["last_snr"]    = dl["snr"]
                DISP["last_window"] = dl["window"]
                # A1: persist FCntDown срещу replay (само на успешен parse,
                # не на MIC fail / replay-rejected — иначе атакуващ може да
                # натиска counter-а ни напред с произволни стойности)
                ADR["fcnt_dn"] = dl["fcnt"]
                try:
                    save_int(FCNTDN_FILE, dl["fcnt"])
                except Exception as e:
                    print("    fcnt_dn persist err:", e)
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
                    pending_mac = process_mac_commands(sx, dl["mac_commands"], last_snr)
                    if pending_mac:
                        print("    queued MAC ans for next uplink:", pending_mac.hex())
                    # ADR/MAC може да е сменил DR/Pwr → sync DISP за OLED
                    DISP["sf"]       = DR_TO_SF[ADR["dr"]]
                    DISP["tx_power"] = ADR["tx_power_dbm"]
                    current_tx_power = ADR["tx_power_dbm"]
        except Exception as e:
            print("  send/recv error:", e)
        fcnt += 1
        save_int(FCNTUP_FILE, fcnt)
        DISP["fcnt"] = fcnt
        gc.collect()
        await asyncio.sleep(current_interval_s)


async def app():
    """Entry point — стартира LoRaWAN main + heartbeat + button + display паралелно."""
    global button
    DISP["boot_t0"] = time.ticks_ms()
    button = ButtonLatch(BUTTON_PIN)
    oled = make_oled()
    await asyncio.gather(
        main(),
        heartbeat_task(),
        button.poll_task(),
        display_task(oled),
    )


if __name__ == "__main__":
    asyncio.run(app())
