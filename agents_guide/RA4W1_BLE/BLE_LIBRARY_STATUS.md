# FSP BLE Library Status - 2026-01-07

## Investigation Results

### ✅ BLE API Headers - AVAILABLE
Located in: `lib/fsp/ra/fsp/inc/api/`

**Core BLE API:**
- `r_ble_api.h` - Main BLE API (12,982 lines)

**BLE Abstraction Layer:**
- `rm_ble_abs_api.h` - BLE Abstraction API

**BLE Mesh APIs:**
- `rm_ble_mesh_api.h`
- `rm_ble_mesh_access_api.h`
- `rm_ble_mesh_bearer_api.h`
- `rm_ble_mesh_config_client_api.h`
- `rm_ble_mesh_health_server_api.h`
- `rm_ble_mesh_lower_trans_api.h`
- `rm_ble_mesh_model_client_api.h`
- `rm_ble_mesh_model_server_api.h`
- `rm_ble_mesh_network_api.h`
- `rm_ble_mesh_provision_api.h`
- `rm_ble_mesh_scene_server_api.h`
- `rm_ble_mesh_upper_trans_api.h`

### ✅ BLE Source Files - PARTIALLY AVAILABLE
Located in: `lib/fsp/ra/fsp/src/`

**Available sources:**
1. **r_ble/** - Platform control (RTOS integration)
   - `r_ble.c` (159 lines) - RTOS wake-up functions only

2. **rm_ble_abs/** - BLE Abstraction layer
   - `rm_ble_abs.c`

3. **rm_ble_abs_gtl/** - GTL (Generic Transport Layer)
   - `rm_ble_abs_gtl.c`
   - `r_ble_gtl_api.c`

4. **rm_ble_abs_spp/** - SPP (Serial Port Profile)
   - `rm_ble_abs_spp.c`
   - `r_ble_spp_api.c`

### ❌ BLE Protocol Stack - MISSING (Precompiled Binary)

**Expected location:** `lib/fsp/ra/fsp/lib/`

**Current status:**
- Only `rm_zmod4xxx/` library present
- **NO BLE binary library found**

**What's missing:**
- BLE Protocol Stack binary (`.a` or `.lib`)
- Core BLE functionality (GAP, GATT, L2CAP, etc.)

---

## Analysis

### FSP BLE Architecture

```
┌─────────────────────────────────────────┐
│   MicroPython Application (Python)     │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│   renesas_ble Module (modble_renesas.c) │  ← We created this
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│   BLE Wrapper (ra_ble.c)                │  ← We created this
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│   rm_ble_abs (Abstraction Layer)        │  ← Source available
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│   r_ble API (r_ble_api.h)               │  ← Header available
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│   BLE Protocol Stack (BINARY)           │  ← ❌ MISSING!
│   - GAP, GATT, L2CAP, SMP               │
│   - Link Layer, HCI                     │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│   RA4W1 BLE Hardware                    │
└─────────────────────────────────────────┘
```

### Why is the BLE Stack Binary?

According to FSP documentation and release notes:
- BLE Protocol Stack is **proprietary Renesas IP**
- Distributed as **precompiled library** (not source)
- Different variants: Extended, Balance, Compact
- Requires **QE for BLE** tool to configure profiles

---

## Options to Obtain BLE Library

### Option 1: Download FSP Full Release ✅ RECOMMENDED
**Source:** https://github.com/renesas/fsp/releases

**Latest:** FSP v6.3.0 (18 Dec 2024)

**Download:**
- `FSP_Packs_v6.3.0.zip` (166 MB)
- OR `FSP_Packs_v6.3.0.exe` (196 MB - Windows installer)

**Extract:**
```bash
# Download FSP_Packs_v6.3.0.zip
unzip FSP_Packs_v6.3.0.zip
# Look for BLE library in:
# - packs/Renesas/RA_BLE/
# - OR lib/ble/
```

**Pros:**
- Official release
- Complete BLE stack
- Includes all variants (Extended/Balance/Compact)
- Includes QE for BLE configuration files

**Cons:**
- Large download (166 MB)
- Need to extract only BLE parts

---

### Option 2: Use e² studio / RASC
**Tool:** Renesas Advanced Smart Configurator

**Process:**
1. Install e² studio or standalone RASC
2. Create new RA4W1 project
3. Add BLE stack via configurator
4. Extract generated library files

**Pros:**
- Generates proper configuration
- Includes QE for BLE integration

**Cons:**
- Requires Windows/Linux GUI tool
- Heavyweight installation

---

### Option 3: Extract from Example Project
**Source:** https://github.com/renesas/ra-fsp-examples

**Process:**
1. Find RA4W1 BLE example
2. Extract library from example project
3. Copy to MicroPython project

**Pros:**
- Working example as reference
- Minimal download

**Cons:**
- May not have latest version
- Need to find correct example

---

## Recommended Action Plan

### Step 1: Download FSP Packs
```bash
cd /tmp
wget https://github.com/renesas/fsp/releases/download/v6.3.0/FSP_Packs_v6.3.0.zip
unzip FSP_Packs_v6.3.0.zip
```

### Step 2: Locate BLE Library
Look for:
- `lib/ble/*.a` or `lib/ble/*.lib`
- `packs/Renesas/RA_BLE/lib/*.a`
- Binary files matching: `libble_*.a` or `r_ble_*.a`

### Step 3: Copy to MicroPython Project
```bash
# Create lib directory
mkdir -p lib/fsp/ra/fsp/lib/ble

# Copy BLE library
cp <extracted_path>/lib/ble/*.a lib/fsp/ra/fsp/lib/ble/
```

### Step 4: Update Makefile
Add to `ports/renesas-ra/Makefile`:
```makefile
# BLE library path
LIBS += -L$(HAL_DIR)/ra/fsp/lib/ble
LIBS += -lble_compact  # or -lble_balance, -lble_extended
```

---

## ✅ RESOLUTION - Library Found!

### Source
**e² studio installation:** `C:\Renesas\RA\e2studio_v2024-10_fsp_v5.7.0`

**FSP Pack:** `Renesas.RA.5.7.0.pack`

**Location:** `/c/Renesas/RA/e2studio_v2024-10_fsp_v5.7.0/internal/projectgen/ra/packs/`

### Extracted Libraries

**BLE Stack variants (GCC):**
```
ra/fsp/lib/r_ble/cm4_gcc/
├── all/              libr_ble.a (1.1 MB)  - Extended (all features)
├── balance/          libr_ble.a (952 KB)  - Balance (medium)
└── compact/          libr_ble.a (872 KB)  - Compact (minimal) ✅ SELECTED
```

**BLE Mesh (optional):**
```
ra/fsp/lib/rm_ble_mesh/cm4_gcc/
├── core/             librm_ble_mesh_core.a
├── osal_baremetal/   librm_ble_mesh_os.a
└── platform_ra4w1/   librm_ble_mesh_platform.a
```

### Copied to Project

```bash
lib/fsp/ra/fsp/lib/r_ble/cm4_gcc/
├── compact/          libr_ble.a (872 KB)  ✅ For baremetal
├── compact_freertos/ libr_ble.a (873 KB)
└── compact_threadx/  libr_ble.a (873 KB)
```

### Makefile Integration

Added to `ports/renesas-ra/Makefile`:
```makefile
# FSP BLE library (precompiled)
BLE_LIB_DIR = $(HAL_DIR)/ra/fsp/lib/r_ble/cm4_gcc/compact
LIBS += -L$(BLE_LIB_DIR) -lr_ble

# FSP BLE platform sources
HAL_SRC_C += \
	$(HAL_DIR)/ra/fsp/src/r_ble/r_ble.c \
	$(HAL_DIR)/ra/fsp/src/rm_ble_abs/rm_ble_abs.c
```

---

## Next Steps

1. ✅ **BLE library obtained** - COMPLETE
2. ✅ **Copied to project** - COMPLETE
3. ✅ **Makefile updated** - COMPLETE
4. ✅ **Test compilation** - COMPLETE ✅
5. ✅ **Created BLE config files** - COMPLETE
6. ⏳ **Flash to board** - NEXT
7. ⏳ **Test BLE on hardware**
8. ⏳ **Implement FSP callbacks in ra_ble.c**

---

## ✅ BUILD SUCCESS

**Date:** 2026-01-08
**Firmware Size:** 252 KB
**Memory Usage:** 337.7 KB total (251.4 KB flash + 86.3 KB RAM)

See `BUILD_SUCCESS.md` for full details.

---

## References

- FSP Releases: https://github.com/renesas/fsp/releases
- FSP Documentation: https://renesas.github.io/fsp
- QE for BLE: https://www.renesas.com/en/software-tool/qe-ble-development-assistance-tool-bluetooth-low-energy
- RA4W1 Datasheet: https://www.renesas.com/us/en/products/microcontrollers-microprocessors/ra-cortex-m-mcus/ra4w1-group

