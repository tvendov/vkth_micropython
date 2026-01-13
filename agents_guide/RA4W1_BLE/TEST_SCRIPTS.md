# Test Scripts for GATT DB Verification

## Script 1: Basic GATT DB Test

**File:** `test_gatt_basic.py`

```python
import renesas_ble as ble
import time

print("=" * 60)
print("GATT DB Configuration Test - Basic")
print("=" * 60)

# Initialize BLE
print("[1] Initializing BLE...")
ble.active(True)
print("    ✓ BLE activated")

# Start advertising
print("[2] Starting advertisement...")
ble.advertise("RA4W1", 100)
print("    ✓ Advertising as 'RA4W1'")

# Instructions
print("[3] Testing...")
print("")
print("    INSTRUCTIONS:")
print("    1. Open BLE Scanner app on your phone")
print("    2. Look for device named 'RA4W1'")
print("    3. Tap on it to see services")
print("")
print("    EXPECTED RESULT:")
print("    ✅ If services are visible → GATT DB is CONFIGURED")
print("    ❌ If NO services → GATT DB is NOT configured")
print("")

# Keep running
try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    print("\nTest stopped")
    ble.active(False)
```

**How to run:**
```bash
# Copy to board via mpremote
mpremote cp test_gatt_basic.py :

# Run on board
mpremote run test_gatt_basic.py
```

---

## Script 2: Detailed GATT DB Test

**File:** `test_gatt_detailed.py`

```python
import renesas_ble as ble
import time

print("=" * 60)
print("GATT DB Configuration Test - Detailed")
print("=" * 60)

# Initialize BLE
print("[1] Initializing BLE...")
try:
    ble.active(True)
    print("    ✓ BLE activated successfully")
except Exception as e:
    print(f"    ✗ BLE activation failed: {e}")
    exit(1)

# Start advertising
print("[2] Starting advertisement...")
try:
    ble.advertise("RA4W1", 100)
    print("    ✓ Advertising started")
except Exception as e:
    print(f"    ✗ Advertising failed: {e}")
    exit(1)

# Connection handler
def on_connect(conn_hdl):
    print(f"[3] Device connected (handle: {conn_hdl})")

def on_disconnect(conn_hdl):
    print(f"[4] Device disconnected (handle: {conn_hdl})")

# Register handlers
ble.on("connect", on_connect)
ble.on("disconnect", on_disconnect)

# Instructions
print("[3] Waiting for connections...")
print("")
print("    OPEN BLE SCANNER AND:")
print("    1. Find 'RA4W1' device")
print("    2. Tap to connect")
print("    3. Check services section")
print("")
print("    WHAT TO LOOK FOR:")
print("    ✅ Services visible = GATT DB configured")
print("    ❌ No services = GATT DB NOT configured")
print("")

# Keep running
try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    print("\nTest stopped")
    ble.active(False)
```

---

## Script 3: GATT DB Status Check

**File:** `test_gatt_status.py`

```python
import renesas_ble as ble

print("=" * 60)
print("GATT DB Status Check")
print("=" * 60)

# Check if BLE module has GATT functions
print("[1] Checking BLE module capabilities...")

functions = [
    'active',
    'advertise',
    'notify',
    'gatts_register_services',
    'gatts_read',
    'gatts_write',
]

for func in functions:
    if hasattr(ble, func):
        print(f"    ✓ {func}")
    else:
        print(f"    ✗ {func} (NOT IMPLEMENTED)")

# Try to activate BLE
print("[2] Activating BLE...")
try:
    ble.active(True)
    print("    ✓ BLE activated")
except Exception as e:
    print(f"    ✗ Error: {e}")
    exit(1)

# Try to advertise
print("[3] Starting advertisement...")
try:
    ble.advertise("RA4W1", 100)
    print("    ✓ Advertisement started")
except Exception as e:
    print(f"    ✗ Error: {e}")
    exit(1)

# Summary
print("")
print("=" * 60)
print("SUMMARY:")
print("=" * 60)
print("✓ BLE module is working")
print("✓ Device is advertising")
print("")
print("NEXT STEP:")
print("Open BLE Scanner app and check if services are visible")
print("")
print("If services are visible:")
print("  → GATT DB is CONFIGURED ✅")
print("")
print("If NO services are visible:")
print("  → GATT DB is NOT configured ❌")
print("  → Read: ACTION_PLAN_2026-01-10.md")
print("")

# Keep running
import time
try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    print("Stopped")
    ble.active(False)
```

---

## Script 4: Connection Test

**File:** `test_gatt_connection.py`

```python
import renesas_ble as ble
import time

print("=" * 60)
print("GATT DB Connection Test")
print("=" * 60)

# Initialize
ble.active(True)
ble.advertise("RA4W1", 100)

# Connection tracking
connections = {}

def on_connect(conn_hdl):
    connections[conn_hdl] = time.time()
    print(f"[CONNECT] Handle: {conn_hdl}, Time: {time.time()}")

def on_disconnect(conn_hdl):
    if conn_hdl in connections:
        duration = time.time() - connections[conn_hdl]
        del connections[conn_hdl]
        print(f"[DISCONNECT] Handle: {conn_hdl}, Duration: {duration:.1f}s")

ble.on("connect", on_connect)
ble.on("disconnect", on_disconnect)

print("[1] Advertising as 'RA4W1'")
print("[2] Waiting for connections...")
print("")
print("INSTRUCTIONS:")
print("1. Open BLE Scanner")
print("2. Find 'RA4W1'")
print("3. Tap to connect")
print("4. Check services")
print("5. Disconnect")
print("")
print("EXPECTED OUTPUT:")
print("[CONNECT] Handle: 0, Time: ...")
print("[DISCONNECT] Handle: 0, Duration: ...")
print("")

try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    print("\nTest stopped")
    ble.active(False)
```

---

## How to Use These Scripts

### Step 1: Copy to Board
```bash
mpremote cp test_gatt_basic.py :
```

### Step 2: Run on Board
```bash
mpremote run test_gatt_basic.py
```

### Step 3: Test with BLE Scanner
1. Open BLE Scanner app
2. Look for "RA4W1"
3. Check services

### Step 4: Interpret Results
- **Services visible** → GATT DB configured ✅
- **No services** → GATT DB NOT configured ❌

---

## Expected Output

### If GATT DB is Configured ✅
```
============================================================
GATT DB Configuration Test - Basic
============================================================
[1] Initializing BLE...
    ✓ BLE activated
[2] Starting advertisement...
    ✓ Advertising as 'RA4W1'
[3] Testing...

    INSTRUCTIONS:
    1. Open BLE Scanner app on your phone
    2. Look for device named 'RA4W1'
    3. Tap on it to see services

    EXPECTED RESULT:
    ✅ If services are visible → GATT DB is CONFIGURED
    ❌ If NO services → GATT DB is NOT configured
```

### If GATT DB is NOT Configured ❌
Same output, but BLE Scanner shows NO services.

---

## Troubleshooting

### Script won't run
- [ ] Check if MicroPython is installed
- [ ] Check if renesas_ble module is available
- [ ] Try `mpremote ls` to verify connection

### BLE Scanner can't find device
- [ ] Check if board is powered
- [ ] Check if script is running
- [ ] Try restarting board

### Device found but no services
- [ ] This is the GATT DB problem!
- [ ] Implement Phase 1 from ACTION_PLAN_2026-01-10.md

