"""Reset VK_RA4M2 на COM18 и log-ва REPL output за DURATION_S секунди.

machine.reset() триггерира USB-CDC re-enumeration → COM18 изчезва временно.
Затова reconnectism с retry loop след reset-а.
"""
import serial, sys, time

DURATION_S = 60
PORT = "COM21"
BAUD = 115200


def open_port(retries=20, delay=0.5):
    for i in range(retries):
        try:
            return serial.Serial(PORT, BAUD, timeout=0.2)
        except (serial.SerialException, OSError) as e:
            if i == 0:
                print(f"[monitor] waiting for {PORT}...", flush=True)
            time.sleep(delay)
    raise SystemExit(f"[monitor] {PORT} not available after {retries*delay:.1f}s")


# 1. Connect, break running script, hard reset
s = open_port()
s.write(b"\x03\x03")
time.sleep(0.3)
s.read(s.in_waiting or 1)
s.write(b"\r\nimport machine\r\nmachine.reset()\r\n")
time.sleep(0.2)
try:
    s.close()
except Exception:
    pass

# 2. Wait for USB re-enumerate, reopen, log
print(f"[monitor] machine.reset() sent — reconnecting...", flush=True)
time.sleep(2.0)                                          # дай време на USB-CDC да изчезне
s = open_port()
print(f"[monitor] reconnected, listening {DURATION_S}s...", flush=True)

t_end = time.time() + DURATION_S
while time.time() < t_end:
    try:
        data = s.read(2048)
    except (serial.SerialException, OSError):
        # USB drop в средата — reopen
        try: s.close()
        except: pass
        s = open_port()
        continue
    if data:
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()
s.close()
print(f"\n[monitor] done.", flush=True)
