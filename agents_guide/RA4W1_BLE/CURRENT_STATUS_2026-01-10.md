# RA4W1 BLE Integration - Current Status (2026-01-10)

## Overview
Renesas RA4W1 has **native FSP BLE stack integration** in MicroPython. The implementation uses:
- **FSP BLE Compact library** (precompiled, ~872 KB)
- **Custom wrapper layer** (`ra_ble.c/h`) - FSP API abstraction
- **MicroPython module** (`modble_renesas.c/h`) - Python API
- **Event queue system** (lock-free ring buffer)

## Architecture

### 1. FSP BLE Stack (Hardware Layer)
```
lib/fsp/ra/fsp/
├── lib/r_ble/cm4_gcc/compact/libr_ble.a    ✅ Precompiled
├── src/r_ble/r_ble.c                       ✅ Platform layer
└── src/rm_ble_abs/rm_ble_abs.c             ✅ Abstraction layer
```

**Key FSP APIs:**
- `R_BLE_Open()` / `R_BLE_Close()` - Stack lifecycle
- `R_BLE_GAP_Init()` / `R_BLE_GATTS_Init()` - GAP/GATT initialization
- `R_BLE_Execute()` - Event pump (must call regularly)
- `R_BLE_GATTS_SetDbInst()` - **GATT DB configuration** (NOT YET USED)

### 2. Wrapper Layer (`ports/renesas-ra/ble/`)
```
ra_ble.c/h              - Main BLE wrapper (FSP API calls)
ra_ble_events.c/h       - Event queue (lock-free ring buffer)
ra_ble_config.c         - Configuration constants
```

**Current Implementation:**
- ✅ GAP: advertising, connection, disconnection
- ✅ GATT: notifications, indications, write/read callbacks
- ✅ Event handling: GAP events, GATT events
- ❌ **GATT DB configuration** - NOT IMPLEMENTED
- ❌ Dynamic service registration - NOT IMPLEMENTED

### 3. MicroPython Module (`modble_renesas.c/h`)
```python
import renesas_ble as ble

ble.active(True)                    # Initialize
ble.advertise("RA4W1", 100)         # Start advertising
ble.on("connect", callback)         # Register callbacks
ble.notify(conn_hdl, attr_hdl, data) # Send notification
```

## GATT Database Status

### Current Limitation
**The GATT database is NOT configured.** FSP requires:
```c
st_ble_gatts_db_cfg_t db_config = {
    .p_uuid_table = ...,           // UUID byte array
    .p_attr_cfg = ...,             // Attribute configuration
    .p_uuid_cfg = ...,             // UUID index
    .p_attr_val_table = ...,       // Attribute values
    // ... more fields
};
R_BLE_GATTS_SetDbInst(&db_config);  // Must call BEFORE advertising
```

### How to Generate GATT DB
1. **Use Renesas QE (Quick Emulator)** - GUI tool to design GATT services
2. **Output:** Generated C code with `st_ble_gatts_db_cfg_t` structure
3. **Integration:** Include generated code, call `R_BLE_GATTS_SetDbInst()`

### Alternative: Dynamic Registration
MicroPython standard BLE module supports dynamic service registration:
```python
ble.gatts_register_services(services_definition)
```
**Status:** NOT INTEGRATED with Renesas FSP yet

## Build Configuration

**File:** `ports/renesas-ra/Makefile`
```makefile
ifeq ($(CMSIS_MCU),RA4W1)
ifeq ($(MICROPY_HW_ENABLE_BLE),1)
SRC_C += ble/ra_ble.c ble/ra_ble_events.c ble/ra_ble_config.c modble_renesas.c
LIBS += -L$(BLE_LIB_DIR) -lr_ble
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_ble/r_ble.c $(HAL_DIR)/ra/fsp/src/rm_ble_abs/rm_ble_abs.c
```

**Build Command:**
```bash
cd ports/renesas-ra
make BOARD=EK_RA4W1 MICROPY_HW_ENABLE_BLE=1
```

## Next Steps

### Priority 1: GATT Database Configuration
- [ ] Generate GATT DB using Renesas QE
- [ ] Integrate generated code into build
- [ ] Call `R_BLE_GATTS_SetDbInst()` during initialization
- [ ] Test with actual services

### Priority 2: Dynamic Service Registration
- [ ] Implement MicroPython `gatts_register_services()` wrapper
- [ ] Map Python service definitions to FSP GATT DB
- [ ] Support runtime service registration

### Priority 3: Testing & Documentation
- [ ] Create example BLE peripheral application
- [ ] Document GATT DB structure
- [ ] Add unit tests

