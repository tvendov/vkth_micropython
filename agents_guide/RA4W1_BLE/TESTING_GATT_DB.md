# Testing GATT Database Configuration

## Problem
How to verify if GATT DB is configured or not?

## Solution: Three Testing Methods

### Method 1: BLE Scanner App (EASIEST) ✅

**What You Need:**
- EK_RA4W1 board
- Smartphone (iOS or Android)
- BLE Scanner app

**Steps:**

1. **Build and Flash MicroPython:**
   ```bash
   cd renesas_micropython
   make -C ports/renesas-ra clean
   make -C ports/renesas-ra
   # Flash to board
   ```

2. **Run Python Script on Board:**
   ```python
   import renesas_ble as ble
   
   ble.active(True)
   ble.advertise("RA4W1", 100)
   print("Advertising...")
   ```

3. **Open BLE Scanner App:**
   - iOS: "BLE Scanner" by Bluepixel
   - Android: "BLE Scanner" by Bluepixel
   - Or any standard BLE scanner

4. **Look for "RA4W1" device**

5. **Check Services:**
   - ✅ **GATT DB Configured:** You see services (Device Info, etc.)
   - ❌ **GATT DB NOT Configured:** You see NO services (empty)

**Expected Output (GATT DB Configured):**
```
Device: RA4W1
├── Service: Device Information (0x180A)
│   ├── Characteristic: Manufacturer Name (0x2A29)
│   └── Characteristic: System ID (0x2A23)
└── Service: Generic Access (0x1800)
    ├── Characteristic: Device Name (0x2A00)
    └── Characteristic: Appearance (0x2A01)
```

**Expected Output (GATT DB NOT Configured):**
```
Device: RA4W1
(No services listed)
```

---

### Method 2: Serial Debug Output (MEDIUM)

**What You Need:**
- Serial terminal (PuTTY, Tera Term, etc.)
- USB cable to board

**Steps:**

1. **Add Debug Code to `ra_ble.c`:**
   ```c
   // In ra_ble_init() after R_BLE_GATTS_Init():
   
   extern st_ble_gatts_db_cfg_t g_ble_gatts_db_cfg;
   
   st = R_BLE_GATTS_SetDbInst(&g_ble_gatts_db_cfg);
   if (st == BLE_SUCCESS) {
       printf("[BLE] GATT DB configured successfully\n");
       printf("[BLE] Attributes: %d\n", g_ble_gatts_db_cfg.attr_num);
       printf("[BLE] UUIDs: %d\n", g_ble_gatts_db_cfg.uuid_num);
   } else {
       printf("[BLE] GATT DB configuration FAILED: %d\n", st);
   }
   ```

2. **Build and Flash**

3. **Open Serial Terminal:**
   ```
   COM Port: COMx (check Device Manager)
   Baud Rate: 115200
   Data Bits: 8
   Stop Bits: 1
   Parity: None
   ```

4. **Look for Output:**
   - ✅ **Success:** `[BLE] GATT DB configured successfully`
   - ❌ **Failure:** `[BLE] GATT DB configuration FAILED`

---

### Method 3: Code Inspection (HARDEST)

**What You Need:**
- Source code access
- Understanding of FSP BLE API

**Steps:**

1. **Check `ra_ble.c`:**
   ```bash
   grep -n "R_BLE_GATTS_SetDbInst" ports/renesas-ra/ble/ra_ble.c
   ```
   - ✅ **Found:** GATT DB is being configured
   - ❌ **Not Found:** GATT DB is NOT being configured

2. **Check `ra_ble_config.c`:**
   ```bash
   grep -n "st_ble_gatts_db_cfg_t" ports/renesas-ra/ble/ra_ble_config.c
   ```
   - ✅ **Found:** GATT DB structure is defined
   - ❌ **Not Found:** GATT DB structure is NOT defined

3. **Check for GATT DB structures:**
   ```bash
   grep -n "g_ble_gatts_db_cfg\|g_ble_uuid_table\|g_ble_attr_cfg" \
        ports/renesas-ra/ble/ra_ble_config.c
   ```

---

## Quick Test Script

**Save as `test_gatt_db.py` on board:**

```python
import renesas_ble as ble
import time

print("=" * 50)
print("GATT DB Configuration Test")
print("=" * 50)

# Initialize BLE
ble.active(True)
print("[1] BLE activated")

# Start advertising
ble.advertise("RA4W1", 100)
print("[2] Advertising started")

# Wait for connections
print("[3] Waiting for BLE scanner...")
print("    Open BLE Scanner app and look for 'RA4W1'")
print("    Check if services are visible")
print("")
print("    ✅ Services visible = GATT DB is configured")
print("    ❌ No services = GATT DB is NOT configured")
print("")

# Keep running
while True:
    time.sleep(1)
```

---

## Expected Results

### Current State (GATT DB NOT Configured)
```
BLE Scanner Output:
├── Device: RA4W1
│   ├── RSSI: -50 dBm
│   ├── Address: XX:XX:XX:XX:XX:XX
│   └── Services: (NONE - EMPTY!)
```

### After Fix (GATT DB Configured)
```
BLE Scanner Output:
├── Device: RA4W1
│   ├── RSSI: -50 dBm
│   ├── Address: XX:XX:XX:XX:XX:XX
│   └── Services:
│       ├── 0x180A (Device Information)
│       ├── 0x1800 (Generic Access)
│       └── 0x1801 (Generic Attribute)
```

---

## Troubleshooting

### Problem: Device not found in BLE Scanner
- [ ] Check if board is powered
- [ ] Check if MicroPython is running
- [ ] Check if `ble.active(True)` was called
- [ ] Check if `ble.advertise()` was called
- [ ] Try restarting board

### Problem: Services not visible
- [ ] This is the GATT DB problem!
- [ ] Means `R_BLE_GATTS_SetDbInst()` is not being called
- [ ] Implement Phase 1 from ACTION_PLAN_2026-01-10.md

### Problem: Serial output not showing
- [ ] Check COM port in Device Manager
- [ ] Check baud rate (should be 115200)
- [ ] Try different terminal software
- [ ] Check USB cable connection

---

## Next Steps

1. **Run Method 1 (BLE Scanner)** - Easiest way to verify
2. **If no services visible** - GATT DB is NOT configured
3. **Read ACTION_PLAN_2026-01-10.md** - How to fix it
4. **Implement Phase 1** - Add minimal GATT DB
5. **Test again** - Verify services appear

