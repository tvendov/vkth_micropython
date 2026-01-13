# Phase 1 Testing Guide - GATT DB Verification

## Status: 🔨 BUILD IN PROGRESS

## What to Do Now

### Step 1: Wait for Build to Complete
```bash
# Build is running in background
# Check progress with:
tail -f build_output.log
```

### Step 2: Check Build Result
```bash
# After build completes, check for errors:
grep -i "error" build_output.log
```

**Expected:**
- ✅ No errors
- ✅ Firmware file created: `build-EK_RA4W1/firmware.elf`

---

## Step 3: Flash to Board

### Option A: Using mpremote
```bash
mpremote connect /dev/ttyUSB0 erase
mpremote connect /dev/ttyUSB0 cp build-EK_RA4W1/firmware.elf :
```

### Option B: Using J-Link
```bash
JLinkExe -device RA4W1 -if SWD -speed 4000 -CommandFile flash.jlink
```

### Option C: Using Renesas Flash Programmer
1. Open Renesas Flash Programmer
2. Select RA4W1 device
3. Load `build-EK_RA4W1/firmware.elf`
4. Click "Program"

---

## Step 4: Run Test Script

### Create test_gatt_phase1.py:
```python
import renesas_ble as ble
import time

print("=" * 60)
print("Phase 1 GATT DB Test")
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
print("[3] Testing GATT DB...")
print("")
print("    INSTRUCTIONS:")
print("    1. Open BLE Scanner app on your phone")
print("    2. Look for device named 'RA4W1'")
print("    3. Tap on it to connect")
print("    4. Check 'Services' section")
print("")
print("    EXPECTED RESULT:")
print("    ✅ You should see:")
print("       - Service: 0x180A (Device Information)")
print("       - Characteristic: 0x2A29 (Manufacturer Name)")
print("       - Characteristic: 0x2A23 (System ID)")
print("")
print("    ❌ If you see NO services:")
print("       - GATT DB is NOT configured")
print("       - Check build errors")
print("")

# Keep running
try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    print("\nTest stopped")
    ble.active(False)
```

### Copy to board:
```bash
mpremote cp test_gatt_phase1.py :
```

### Run on board:
```bash
mpremote run test_gatt_phase1.py
```

---

## Step 5: Verify with BLE Scanner

### iOS (BLE Scanner by Bluepixel)
1. Open BLE Scanner app
2. Tap "Scan" button
3. Look for "RA4W1" device
4. Tap on device name
5. Scroll to "Services" section

### Android (BLE Scanner by Bluepixel)
1. Open BLE Scanner app
2. Grant location permission
3. Tap "START SCAN"
4. Find "RA4W1" in list
5. Tap on device
6. Scroll to "GATT Services"

---

## Expected Output

### ✅ SUCCESS (GATT DB Configured)
```
Device: RA4W1
├── RSSI: -45 dBm
├── Address: AA:BB:CC:DD:EE:FF
└── Services:
    └── 0x180A (Device Information Service)
        ├── 0x2A29 (Manufacturer Name String)
        │   └── Value: "Renesas"
        └── 0x2A23 (System ID)
            └── Value: 01 02 03 04 05 06 07 08
```

### ❌ FAILURE (GATT DB NOT Configured)
```
Device: RA4W1
├── RSSI: -45 dBm
├── Address: AA:BB:CC:DD:EE:FF
└── Services:
    (EMPTY - NO SERVICES!)
```

---

## Troubleshooting

### Problem: Build Failed
**Solution:**
1. Check `build_output.log` for errors
2. Look for undefined references
3. Verify ra_ble_config.c is included
4. Check FSP library headers

### Problem: Device Not Found
**Solution:**
1. Check if board is powered
2. Check if MicroPython is running
3. Try restarting board
4. Check Bluetooth on phone

### Problem: Device Found but No Services
**Solution:**
1. This means GATT DB is NOT configured
2. Check if R_BLE_GATTS_SetDbInst() was called
3. Verify GATT DB structure in ra_ble_config.c
4. Check for runtime errors in serial output

### Problem: Can't Read Characteristics
**Solution:**
1. Verify characteristic has READ property
2. Check if value is initialized
3. Try reconnecting
4. Check for permission issues

---

## Serial Debug Output

### To see debug messages:
```bash
mpremote connect /dev/ttyUSB0 repl
```

### Expected output:
```
[BLE] GATT DB configured successfully
[BLE] Attributes: 3
[BLE] UUIDs: 3
```

---

## Next Steps

### If Test Passes ✅
1. Congratulations! GATT DB is working
2. Move to Phase 2: Dynamic Service Registration
3. Read: ACTION_PLAN_2026-01-10.md

### If Test Fails ❌
1. Check build errors
2. Verify GATT DB structure
3. Check FSP library compatibility
4. Review GATT_DB_TECHNICAL_ANALYSIS.md

---

## Quick Checklist

- [ ] Build completed without errors
- [ ] Firmware flashed to board
- [ ] Test script running
- [ ] BLE Scanner app installed
- [ ] Device "RA4W1" found in BLE Scanner
- [ ] Services visible in BLE Scanner
- [ ] Characteristics readable
- [ ] Values correct

---

## Summary

**Phase 1 Testing:**
1. Build the project
2. Flash to board
3. Run test script
4. Verify with BLE Scanner
5. Check for services

**Expected Result:** Device Information Service visible in BLE Scanner

**Next:** Phase 2 - Dynamic Service Registration

