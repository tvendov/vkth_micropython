# full_join_uplink.py — пълен LoRaWAN flow за TTN single-channel SF7 gateway:
#   1. OTAA Join (TX JoinRequest, RX JoinAccept)
#   2. Decrypt JoinAccept (AES-128 ECB encrypt = decrypt при LoRaWAN)
#   3. Извличане на DevAddr/AppNonce/NetID
#   4. Derive NwkSKey + AppSKey от AppKey
#   5. Изпращане на Unconfirmed Data Up FPort=1 "hello" с FCnt=0

from machine import Pin
from sx1262 import SX1262
from LoRaWAN.AES_CMAC import AES_CMAC
import cryptolib
import time

RF_SW = Pin("P100", Pin.OUT, value=1)

# Credentials — попълни от TTN Console (placeholder-и!)
DevEUI  = bytes(8)
JoinEUI = bytes(8)
AppKey  = bytes(16)

# === Helpers ===

def aes_ecb_encrypt(key, block16):
    """AES-128 ECB encrypt one 16-byte block (used for LoRaWAN decrypt also)."""
    aes = cryptolib.aes(key, 1)
    return aes.encrypt(bytes(block16))


def decrypt_join_accept(appkey, encrypted):
    """Decrypt JoinAccept payload — LoRaWAN използва ECB-encrypt за decrypt."""
    out = b""
    for i in range(0, len(encrypted), 16):
        out += aes_ecb_encrypt(appkey, encrypted[i:i + 16])
    return out


def derive_session_key(appkey, prefix, app_nonce, net_id, dev_nonce):
    """NwkSKey (prefix=0x01) или AppSKey (prefix=0x02) per LoRaWAN 1.0.x § 6.2.5."""
    blk = bytes([prefix]) + app_nonce + net_id + dev_nonce + b"\x00" * 7
    return aes_ecb_encrypt(appkey, blk)


def encrypt_frm_payload(appskey, devaddr_le, fcnt32, direction, plaintext):
    """AES-CTR encrypt (или decrypt) на FRMPayload с AppSKey."""
    out = bytearray(len(plaintext))
    blocks = (len(plaintext) + 15) // 16
    fcnt_b = fcnt32.to_bytes(4, "little")
    for i in range(blocks):
        a = bytearray(16)
        a[0] = 0x01
        a[5] = direction              # 0 = uplink, 1 = downlink
        a[6:10] = devaddr_le
        a[10:14] = fcnt_b
        a[15] = i + 1
        s_i = aes_ecb_encrypt(appskey, bytes(a))
        for j in range(min(16, len(plaintext) - i * 16)):
            out[i * 16 + j] = plaintext[i * 16 + j] ^ s_i[j]
    return bytes(out)


def compute_uplink_mic(nwkskey, devaddr_le, fcnt32, direction, msg):
    """LoRaWAN MIC = AES-CMAC(NwkSKey, B0 | msg)[:4]."""
    b0 = bytearray(16)
    b0[0] = 0x49
    b0[5] = direction
    b0[6:10] = devaddr_le
    b0[10:14] = fcnt32.to_bytes(4, "little")
    b0[15] = len(msg)
    cmac_input = bytes(b0) + msg
    return bytes(AES_CMAC().encode(nwkskey, cmac_input))[:4]


# === 1. Init radio (tx_cw proven config + LoRaWAN sync + rxIq) ===
sx = SX1262(spi_bus=3, clk="P111", mosi="P109", miso="P110",
            cs="P206", irq="P015", rst="P001", gpio="P002")
sx.begin(
    freq=868.1, bw=125.0, sf=7, cr=5,
    syncWord=0x34, power=14, currentLimit=60.0,
    preambleLength=8, implicit=False, implicitLen=0xFF,
    crcOn=True, txIq=False, rxIq=True,
    tcxoVoltage=1.8, useRegulatorLDO=False, blocking=True,
)
print("Radio OK\n")

# === 2. JoinRequest ===
DevNonce = 3                                         # увеличаваме всеки join
DevNonce_LE = DevNonce.to_bytes(2, "little")

mhdr = bytes([0x00])
join_req = mhdr + bytes(reversed(JoinEUI)) + bytes(reversed(DevEUI)) + DevNonce_LE
mic = bytes(AES_CMAC().encode(AppKey, join_req))[:4]
join_req += mic
print("JoinRequest:", join_req.hex())

t0 = time.ticks_ms()
sx.send(join_req)
print("Sent (took %d ms)" % time.ticks_diff(time.ticks_ms(), t0))

# === 3. RX JoinAccept ===
print("Waiting for JoinAccept...")
msg, err = sx.recv(0, True, 8000)
if not msg or len(msg) == 0:
    print("FAIL — no JoinAccept (err=%d)" % err)
    raise SystemExit

print("Got %d bytes: %s" % (len(msg), bytes(msg).hex()))
print("RSSI=%d  SNR=%.2f" % (sx.getRSSI(), sx.getSNR()))

# === 4. Decrypt JoinAccept ===
ja_mhdr = bytes(msg[:1])                             # 0x20
ja_enc  = bytes(msg[1:])                             # encrypted payload + MIC
plain   = decrypt_join_accept(AppKey, ja_enc)

if len(plain) == 16:
    # Без CFList
    AppNonce  = plain[0:3]
    NetID     = plain[3:6]
    DevAddr   = plain[6:10]                          # LE in air, will display reversed
    DLSettings = plain[10]
    RxDelay   = plain[11]
    CFList    = b""
    ja_mic    = plain[12:16]
else:
    # 32 байта = с CFList
    AppNonce  = plain[0:3]
    NetID     = plain[3:6]
    DevAddr   = plain[6:10]
    DLSettings = plain[10]
    RxDelay   = plain[11]
    CFList    = plain[12:28]
    ja_mic    = plain[28:32]

print("\n=== Decrypted JoinAccept ===")
print("AppNonce/JoinNonce:", AppNonce.hex())
print("NetID:             ", NetID.hex())
print("DevAddr (LE):      ", DevAddr.hex(), " MSB:", bytes(reversed(DevAddr)).hex())
print("DLSettings:         0x%02X" % DLSettings)
print("RxDelay:            %d s" % RxDelay)
print("CFList:            ", CFList.hex() if CFList else "(none)")
print("MIC (in payload):  ", ja_mic.hex())

# === 5. Derive session keys ===
NwkSKey = derive_session_key(AppKey, 0x01, AppNonce, NetID, DevNonce_LE)
AppSKey = derive_session_key(AppKey, 0x02, AppNonce, NetID, DevNonce_LE)
print("\nNwkSKey:", NwkSKey.hex())
print("AppSKey:", AppSKey.hex())

# === 6. Първи uplink (Unconfirmed Data Up) ===
time.sleep(2)                                        # малка пауза преди uplink
print("\n=== Uplink #0 ===")

FCnt = 0
FPort = 0x01
plaintext = b"hello VK_RA4M2"
encrypted = encrypt_frm_payload(AppSKey, DevAddr, FCnt, 0, plaintext)

# MAC payload: DevAddr_LE (4) | FCtrl (1) | FCnt_LE (2) | FPort (1) | encrypted_payload
mac_payload = (
    DevAddr +
    bytes([0x00]) +                                  # FCtrl
    FCnt.to_bytes(2, "little") +
    bytes([FPort]) +
    encrypted
)
phy_mhdr = bytes([0x40])                             # Unconfirmed Data Up
mic_input = phy_mhdr + mac_payload
uplink_mic = compute_uplink_mic(NwkSKey, DevAddr, FCnt, 0, mic_input)
uplink_frame = mic_input + uplink_mic

print("Uplink frame:", uplink_frame.hex(), "(%d B)" % len(uplink_frame))
print("Plaintext:   ", plaintext)
print("Encrypted:   ", encrypted.hex())

print("\nSending uplink...")
sx.send(uplink_frame)
print("Done. Provери TTN Console → Device → Live data за uplink.")
