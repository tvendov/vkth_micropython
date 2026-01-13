# BLE Scanner Guide - How to Test GATT DB

## Quick Start (5 minutes)

### Step 1: Get BLE Scanner App
**iOS:**
- App Store → Search "BLE Scanner"
- Download "BLE Scanner" by Bluepixel (blue icon)

**Android:**
- Google Play → Search "BLE Scanner"
- Download "BLE Scanner" by Bluepixel

### Step 2: Prepare Board
```python
# Run this on RA4W1 board:
import renesas_ble as ble

ble.active(True)
ble.advertise("RA4W1", 100)
print("Advertising as RA4W1...")
```

### Step 3: Open BLE Scanner
1. Open app
2. Tap "Scan" button
3. Look for "RA4W1" in the list

### Step 4: Check Services
1. Tap on "RA4W1" device
2. Look at "Services" section

**Result:**
- ✅ **Services visible** = GATT DB is configured
- ❌ **No services** = GATT DB is NOT configured

---

## Detailed Instructions

### iOS (BLE Scanner by Bluepixel)

**Opening the App:**
1. Launch BLE Scanner
2. You'll see "Scanning..." at top
3. Wait 2-3 seconds for devices to appear

**Finding RA4W1:**
1. Look for device named "RA4W1"
2. Check RSSI (signal strength) - should be -30 to -70 dBm
3. Tap on it to connect

**Viewing Services:**
1. After tapping device, you'll see details
2. Scroll down to "Services" section
3. Each service shows:
   - Service UUID (e.g., 0x180A)
   - Service name (e.g., Device Information)
   - Characteristics under it

**Expected Services (if GATT DB configured):**
```
Services:
├── 0x180A - Device Information Service
│   ├── 0x2A29 - Manufacturer Name String
│   └── 0x2A23 - System ID
├── 0x1800 - Generic Access
│   ├── 0x2A00 - Device Name
│   └── 0x2A01 - Appearance
└── 0x1801 - Generic Attribute
    └── 0x2A05 - Service Changed
```

**If GATT DB NOT configured:**
```
Services:
(Empty - no services listed)
```

---

### Android (BLE Scanner by Bluepixel)

**Opening the App:**
1. Launch BLE Scanner
2. Grant location permission (required for BLE)
3. Tap "START SCAN" button

**Finding RA4W1:**
1. Scroll through device list
2. Look for "RA4W1"
3. Check RSSI value
4. Tap on device name

**Viewing Services:**
1. Device details will open
2. Scroll down to "GATT Services"
3. Each service shows UUID and characteristics

**Expected Output:**
Same as iOS (see above)

---

## What Each Service Means

### 0x180A - Device Information Service
**Purpose:** Provides device information
**Characteristics:**
- 0x2A29 - Manufacturer Name (e.g., "Renesas")
- 0x2A23 - System ID (e.g., MAC address)
- 0x2A24 - Model Number
- 0x2A25 - Serial Number

### 0x1800 - Generic Access Service
**Purpose:** Basic device info
**Characteristics:**
- 0x2A00 - Device Name (e.g., "RA4W1")
- 0x2A01 - Appearance (device type)

### 0x1801 - Generic Attribute Service
**Purpose:** GATT server info
**Characteristics:**
- 0x2A05 - Service Changed (notifications)

---

## Troubleshooting

### Problem: Can't find RA4W1 device

**Solution 1: Check Board**
- [ ] Is board powered on?
- [ ] Is MicroPython running?
- [ ] Did you call `ble.active(True)`?
- [ ] Did you call `ble.advertise()`?

**Solution 2: Check Phone**
- [ ] Is Bluetooth enabled?
- [ ] Is location enabled? (Android)
- [ ] Is app permission granted?
- [ ] Try restarting app

**Solution 3: Check Distance**
- [ ] Move phone closer to board
- [ ] Remove obstacles
- [ ] Try different location

### Problem: Device found but no services

**This is the GATT DB problem!**
- [ ] GATT DB is NOT configured
- [ ] Read: ACTION_PLAN_2026-01-10.md
- [ ] Implement Phase 1 to add GATT DB

### Problem: Services visible but can't read

**Possible causes:**
- [ ] Characteristic doesn't have READ permission
- [ ] Characteristic value is not set
- [ ] Connection dropped

**Solution:**
- [ ] Check characteristic properties
- [ ] Verify value is initialized
- [ ] Try reconnecting

---

## Advanced: Reading Characteristic Values

### iOS
1. Tap on characteristic
2. Tap "Read" button
3. Value appears in "Value" field

### Android
1. Tap on characteristic
2. Tap "Read" button
3. Value appears below

**Example:**
```
Characteristic: 0x2A29 (Manufacturer Name)
Value: "Renesas" (hex: 52 65 6E 65 73 61 73)
```

---

## Advanced: Sending Notifications

### If Characteristic has NOTIFY property:

**iOS:**
1. Tap characteristic
2. Tap "Notify" button
3. Watch for value updates

**Android:**
1. Tap characteristic
2. Enable "Notify"
3. Watch for updates

---

## Screenshots Interpretation

### ✅ GATT DB Configured
```
Device: RA4W1
├── RSSI: -45 dBm
├── Address: AA:BB:CC:DD:EE:FF
├── Flags: 0x06
└── Services:
    ├── 0x180A (Device Information)
    ├── 0x1800 (Generic Access)
    └── 0x1801 (Generic Attribute)
```

### ❌ GATT DB NOT Configured
```
Device: RA4W1
├── RSSI: -45 dBm
├── Address: AA:BB:CC:DD:EE:FF
├── Flags: 0x06
└── Services:
    (Empty)
```

---

## Next Steps

1. **Download BLE Scanner app**
2. **Run test script on board**
3. **Open BLE Scanner**
4. **Look for RA4W1**
5. **Check if services are visible**

**If services visible:** ✅ GATT DB is configured
**If no services:** ❌ GATT DB is NOT configured → Read ACTION_PLAN_2026-01-10.md

