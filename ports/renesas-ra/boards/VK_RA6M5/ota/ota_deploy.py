"""
ota_deploy.py — единен deploy скрипт за VK_RA6M5 OTA.

Подава му се ЕДИН аргумент — пътят към един от:

  bootloader.bin / .hex   → JLink провизия (writes at 0x00000000)
                            пише и Slot A firmware ако е до script-а
  firmware.bin / .elf     → опаковка чрез ota_pack.py (auto sign-key
                            ако е наличен), после remote deploy
  firmware*.ota           → remote deploy през WebREPL
                            (upload → flash_to_other_slot → mark PENDING → reset)

Auto-detect по разширение и магия в първите 64 байта.

Допълнителни флагове:
  --ip <ip>            — WebREPL IP (default: открива чрез serial)
  --pass <password>    — WebREPL password (default: vk6m5)
  --com <port>         — Serial COM port (default: COM32)
  --version <X.Y.Z>    — версия за пакетиране (default: 0.0.1 + git short)
  --sign-key <path>    — ECDSA private key за подписване
  --no-mark-pending    — само upload, не пуска auto-flash + reset
  --jlink <path>       — път до JLink.exe (default: standard install)

Примери:
  python ota_deploy.py boot/build/bootloader.hex
  python ota_deploy.py firmware_v1.0.0_slotB.ota
  python ota_deploy.py ../../../build-VK_RA6M5/firmware.bin --version 1.0.1
"""
import argparse
import hashlib
import os
import re
import socket
import struct
import subprocess
import sys
import tempfile
import time
import base64

THIS_DIR = os.path.dirname(os.path.abspath(__file__))
PACK_PY  = os.path.join(THIS_DIR, "ota_pack.py")
JLINK_DEFAULT_WIN = r"C:\Program Files\SEGGER\JLink\JLink.exe"
DEFAULT_PASS = b"vk6m5"


# ---------------- type detection ----------------

def detect_type(path):
    """Return one of: 'bootloader', 'firmware-bin', 'firmware-ota', 'unknown'."""
    base = os.path.basename(path).lower()
    if base.startswith("bootloader"):
        return "bootloader"
    if base.endswith(".ota"):
        return "firmware-ota"
    if base.endswith(".bin") or base.endswith(".elf") or base.endswith(".hex"):
        # Distinguish bootloader-sized binary from full firmware:
        # bootloader < 32 KB, firmware ~ 500 KB
        try:
            sz = os.path.getsize(path)
        except OSError:
            return "unknown"
        if sz < 32 * 1024:
            return "bootloader"
        if base.endswith(".hex"):
            return "firmware-hex"
        return "firmware-bin"
    return "unknown"


# ---------------- helpers ----------------

def find_device_ip(com="COM32", timeout=10):
    """Wake device on serial, soft-reboot, parse IP from boot banner."""
    try:
        import serial
    except ImportError:
        return None
    try:
        ser = serial.Serial(com, 115200, timeout=0.5)
    except Exception as e:
        print("  serial open failed:", e)
        return None
    ser.write(b"\r\n"); time.sleep(0.3); ser.read(8192)
    ser.write(b"\x03\x03"); time.sleep(0.3); ser.read(8192)
    ser.write(b"\x04")  # ctrl-D = soft reset
    deadline = time.time() + timeout
    buf = b""
    while time.time() < deadline:
        d = ser.read(2048)
        if d: buf += d
        m = re.search(rb"(\d+\.\d+\.\d+\.\d+)", buf)
        if m and b"WebREPL" in buf:
            ser.close()
            return m.group(1).decode()
    ser.close()
    return None


def jlink_flash(jlink_exe, hex_or_bin_path, address, do_erase=False):
    """Flash a single binary at the given address via JLink."""
    if not os.path.exists(jlink_exe):
        print("  JLink not found at", jlink_exe)
        return False
    script_path = tempfile.mktemp(suffix=".jlink")
    erase_line = "erase\n" if do_erase else ""
    if hex_or_bin_path.lower().endswith(".bin"):
        load_line = "loadbin %s 0x%08X\n" % (hex_or_bin_path, address)
    else:
        load_line = "loadfile %s\n" % hex_or_bin_path
    with open(script_path, "w") as f:
        f.write("si SWD\n"
                "speed 4000\n"
                "device R7FA6M5BH\n"
                "connect\n"
                "halt\n" +
                erase_line +
                load_line +
                "r\ng\nexit\n")
    try:
        r = subprocess.run([jlink_exe, "-CommanderScript", script_path,
                            "-ExitOnError", "1", "-NoGui", "1"],
                           capture_output=True, text=True, timeout=120)
        ok = r.returncode == 0 and ("Verify successful" in r.stdout
                                    or "Programming flash" in r.stdout
                                    or "Done" in r.stdout)
        if not ok:
            print("  JLink output:")
            print(r.stdout[-500:])
        return ok
    finally:
        try: os.remove(script_path)
        except OSError: pass


# ---------------- WebREPL ----------------

def _frame(p, op=0x81):
    pl = len(p)
    if pl < 126:    h = bytes([op, 0x80|pl])
    elif pl < 65536:h = bytes([op, 0x80|126]) + struct.pack(">H", pl)
    else:           h = bytes([op, 0x80|127]) + struct.pack(">Q", pl)
    m = os.urandom(4)
    return h + m + bytes(b ^ m[i & 3] for i, b in enumerate(p))

def _hs(s):
    k = base64.b64encode(os.urandom(16)).decode()
    s.send((f"GET / HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {k}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
    b = b""
    while b"\r\n\r\n" not in b: b += s.recv(4096)
    return b"101 Switching" in b

def webrepl_connect(ip, password, timeout=15):
    s = socket.socket(); s.settimeout(timeout); s.connect((ip, 8266))
    if not _hs(s): s.close(); return None
    s.settimeout(0.8); time.sleep(0.5)
    try: s.recv(8192)
    except: pass
    s.send(_frame(password + b"\r\n")); time.sleep(0.8)
    try: s.recv(8192)
    except: pass
    s.settimeout(timeout)
    return s

def _read_n(s, n, deadline):
    b = b""
    while len(b) < n:
        if time.time() > deadline: raise IOError("timeout")
        d = s.recv(n - len(b))
        if not d: raise IOError("EOF")
        b += d
    return b

def _read_frame(s, t=8.0):
    s.settimeout(t); deadline = time.time() + t
    h = _read_n(s, 2, deadline); pl = h[1] & 0x7f
    if pl == 126: pl = struct.unpack(">H", _read_n(s, 2, deadline))[0]
    elif pl == 127: pl = struct.unpack(">Q", _read_n(s, 8, deadline))[0]
    return h[0] & 0x0f, _read_n(s, pl, deadline)

def webrepl_put(s, dest_path, payload):
    """Upload `payload` to `dest_path` on device via WebREPL."""
    fname = dest_path.encode()
    sz = len(payload)
    rec = b"WA" + bytes([1, 0]) + b"\x00"*8 + struct.pack("<L", sz) + \
          struct.pack("<H", len(fname)) + fname + b"\x00"*(64 - len(fname))
    s.send(_frame(rec, 0x82))
    op, p = _read_frame(s, 8)
    if p[:2] != b"WB" or (p[2] | (p[3] << 8)) != 0:
        return False
    CHUNK = 2048
    progress_step = max(1, sz // 20)
    next_progress = progress_step
    for i in range(0, sz, CHUNK):
        s.send(_frame(payload[i:i+CHUNK], 0x82))
        if (i // CHUNK) % 32 == 31:
            time.sleep(0.04)
        if i >= next_progress:
            print("    %d / %d B (%d%%)" % (i, sz, i * 100 // sz))
            next_progress += progress_step
    op, p = _read_frame(s, 120)
    return p[:2] == b"WB" and (p[2] | (p[3] << 8)) == 0

def _safe_recv(s, t=2.0, mx=131072):
    s.settimeout(t); b = b""
    try:
        while True:
            d = s.recv(8192)
            if not d: break
            b += d
            if len(b) >= mx: break
            s.settimeout(0.3)
    except: pass
    return b

def _decode(raw):
    o = b""; i = 0
    while i + 2 <= len(raw):
        h0 = raw[i]; pl = raw[i+1] & 0x7f; i += 2
        if pl == 126:
            if i+2 > len(raw): break
            pl = (raw[i] << 8) | raw[i+1]; i += 2
        elif pl == 127:
            if i+8 > len(raw): break
            pl = struct.unpack(">Q", raw[i:i+8])[0]; i += 8
        if i+pl > len(raw): break
        o += raw[i:i+pl]; i += pl
    return o.decode(errors="replace")

def webrepl_repl(s, expr, settle=1.0, t=4.0):
    s.send(_frame(expr.encode() + b"\r"))
    time.sleep(settle)
    return _decode(_safe_recv(s, t, 262144))


# ---------------- deploy modes ----------------

def deploy_bootloader(args, path):
    print("[mode] bootloader provisioning via JLink")
    print("  artifact:", path)
    # Look for a Slot A firmware next to the bootloader so we provision both.
    boot_dir = os.path.dirname(os.path.abspath(path))
    candidates = []
    # build-VK_RA6M5_slotA/firmware.hex (parallel to ota/boot)
    for rel in [
        "../../../../../build-VK_RA6M5_slotA/firmware.hex",
        "../../../../build-VK_RA6M5_slotA/firmware.hex",
        "../../../build-VK_RA6M5_slotA/firmware.hex",
    ]:
        c = os.path.normpath(os.path.join(boot_dir, rel))
        if os.path.exists(c):
            candidates.append(c)
            break
    print("  + Slot A image:", candidates[0] if candidates else "(none found)")

    print("\n  step 1/2 — flash bootloader at 0x00000000")
    if not jlink_flash(args.jlink, path, 0x00000000, do_erase=True):
        sys.exit("    JLink flash failed")
    print("    OK")

    if candidates:
        print("\n  step 2/2 — flash Slot A image at 0x00010000")
        if not jlink_flash(args.jlink, candidates[0], 0x00010000):
            sys.exit("    JLink flash failed")
        print("    OK")

    print("\n  Provisioning complete.  Allow ~10 s for boot, then check:")
    print("    python -c \"import socket; s=socket.socket(); s.settimeout(3); "
          "s.connect(('<ip>', 8266)); print('UP')\"")


def deploy_ota_file(args, ota_path):
    print("[mode] remote deploy of .ota via WebREPL")
    print("  artifact:", ota_path)

    ip = args.ip or find_device_ip(args.com)
    if not ip:
        sys.exit("  could not auto-detect device IP. Pass --ip explicitly.")
    print("  device IP:", ip)

    with open(ota_path, "rb") as f:
        blob = f.read()
    print("  size:", len(blob), "B")

    print("\n  step 1/4 — connect WebREPL")
    s = webrepl_connect(ip, args.password)
    if not s:
        sys.exit("    WebREPL connect failed")
    print("    OK")

    print("\n  step 2/4 — push ota.py + .ota to /flash")
    ota_py = open(os.path.join(THIS_DIR, "ota.py"), "rb").read()
    if not webrepl_put(s, "/flash/ota.py", ota_py):
        sys.exit("    push ota.py failed")
    print("    ota.py uploaded")
    s.close(); time.sleep(1)

    s = webrepl_connect(ip, args.password)
    if not webrepl_put(s, "/flash/_ota_pending.ota", blob):
        sys.exit("    push .ota failed")
    print("    .ota uploaded")
    s.close(); time.sleep(1)

    print("\n  step 3/4 — flash_to_other_slot + verify")
    s = webrepl_connect(ip, args.password)
    out = webrepl_repl(s,
        "import sys; sys.modules.pop('ota', None); import ota, _ota; "
        "info = ota.flash_to_other_slot(); "
        "print('TARGET:', info['target_slot_name'], 'V', info['version'])",
        settle=15.0, t=20.0)
    print("    " + out.replace("\n", "\n    "))
    if "programmed OK" not in out:
        if "erase failed" in out or "OSError" in out:
            print("    HINT: this often means the FSP flash module is in a")
            print("          sticky state from a previous run.  Try:")
            print("            JLink.exe -CommanderScript boot/jlink_provision.txt")
            print("          to reset the device cleanly, then re-run this script.")
        sys.exit("    flash_to_other_slot failed")

    if args.no_mark_pending:
        print("\n  --no-mark-pending: leaving slot UNPENDING; reset manually")
        s.close()
        return

    print("\n  step 4/4 — set state PENDING + reset")
    out = webrepl_repl(s,
        "_ota.set_state(info['target_slot_base'], 0x01); "
        "print('PENDING set, resetting'); "
        "import machine; machine.reset()",
        settle=2.0)
    print("    " + out.replace("\n", "\n    "))
    s.close()
    print("\n  Reset triggered.  Bootloader will pick the new slot.")
    print("  After boot, on REPL:  ota.boot_check(); ota.mark_good()")


def deploy_bin(args, bin_path):
    print("[mode] pack firmware.bin → .ota → remote deploy")
    print("  artifact:", bin_path)

    # Determine version
    version = args.version or "0.0.1"
    build_hash = "0"
    try:
        r = subprocess.run(["git", "rev-parse", "--short=8", "HEAD"],
                           capture_output=True, text=True, timeout=5)
        if r.returncode == 0: build_hash = r.stdout.strip()
    except Exception:
        pass

    # Pack
    out_ota = os.path.join(tempfile.gettempdir(),
                           "deploy_v%s.ota" % version)
    cmd = [sys.executable, PACK_PY, bin_path, out_ota,
           "--version", version, "--build-hash", build_hash]
    if args.sign_key:
        cmd.extend(["--sign-key", args.sign_key])
    print("  packing:  " + " ".join(cmd[2:]))
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout); print(r.stderr, file=sys.stderr)
        sys.exit("  packer failed")
    print(r.stdout.strip())

    # Deploy as .ota
    deploy_ota_file(args, out_ota)


# ---------------- main ----------------

def main():
    ap = argparse.ArgumentParser(description="Унифициран VK_RA6M5 OTA deploy")
    ap.add_argument("artifact", help="bootloader.{bin,hex} / firmware.{bin,ota,hex}")
    ap.add_argument("--ip", default=None, help="WebREPL IP (auto-detect if omitted)")
    ap.add_argument("--password", default=DEFAULT_PASS,
                    help="WebREPL password (default: vk6m5)")
    ap.add_argument("--com", default="COM32",
                    help="Serial port for IP discovery / JLink (default: COM32)")
    ap.add_argument("--version", default=None,
                    help="Version when packing a .bin (default: 0.0.1)")
    ap.add_argument("--sign-key", default=None,
                    help="ECDSA-P256 private key for signing (only when packing)")
    ap.add_argument("--no-mark-pending", action="store_true",
                    help="Just upload to OTHER slot; don't mark PENDING/reset")
    ap.add_argument("--jlink", default=JLINK_DEFAULT_WIN,
                    help="Path to JLink.exe (default: standard install)")
    args = ap.parse_args()

    # Coerce password to bytes
    if isinstance(args.password, str):
        args.password = args.password.encode()

    if not os.path.exists(args.artifact):
        sys.exit("error: artifact not found: " + args.artifact)

    kind = detect_type(args.artifact)
    print("=" * 60)
    print("artifact: " + args.artifact)
    print("type    : " + kind)
    print("size    : %d B" % os.path.getsize(args.artifact))
    print("=" * 60)

    if kind == "bootloader":
        deploy_bootloader(args, args.artifact)
    elif kind == "firmware-ota":
        deploy_ota_file(args, args.artifact)
    elif kind in ("firmware-bin", "firmware-hex"):
        if kind == "firmware-hex":
            sys.exit("hex requires JLink path; use .bin or .ota for remote deploy")
        deploy_bin(args, args.artifact)
    else:
        sys.exit("unknown artifact type — pass a .bin/.hex/.ota file")


if __name__ == "__main__":
    main()
