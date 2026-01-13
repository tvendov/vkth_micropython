# FSP Development Guide for RA4W1 BLE Integration

**Source:** https://renesas.github.io/fsp/_s_t_a_r_t__d_e_v.html  
**Date:** 2026-01-07  
**FSP Version:** 6.3.0

---

## Key Concepts

### What is FSP?
**Flexible Software Package (FSP)** - Renesas software framework providing:
- HAL drivers for all peripherals
- Middleware (RTOS, networking, USB, BLE, etc.)
- Configuration tools (e² studio, RASC)
- Code generation from GUI configuration

### What is e² studio?
Eclipse-based IDE with:
- **FSP Configuration Perspective** - GUI for hardware/software config
- **C/C++ Perspective** - Code editing
- **Debug Perspective** - Debugging
- **Code accelerators:**
  - Developer Assistance (drag-drop API calls)
  - Autocomplete (context-aware)
  - Information icons (links to docs/examples)

---

## FSP Project Structure

```
project/
├── configuration.xml          # Main FSP config (open with RA Config Editor)
├── ra_cfg/
│   ├── fsp_cfg/              # FSP module configurations
│   │   ├── bsp/              # BSP config (bsp_cfg.h)
│   │   ├── r_ble_cfg.h       # BLE config (if BLE enabled)
│   │   └── r_*_cfg.h         # Other module configs
│   └── ra_gen/               # Generated code (DO NOT EDIT)
│       ├── common_data.c/h   # Module instances
│       ├── hal_data.c/h      # HAL data structures
│       ├── pin_data.c        # Pin configuration
│       └── vector_data.c     # Interrupt vectors
├── src/
│   ├── hal_entry.c           # User application entry point
│   └── main.c                # System initialization
└── script/
    └── fsp.ld                # Linker script
```

---

## Configuration Tabs in RA Config Editor

### 1. **Summary Tab**
- Overview of board, device, toolchain, FSP version
- List of all modules/components

### 2. **BSP Tab**
- Board Support Package settings
- Stack sizes, heap size
- Parameter checking enable/disable
- Properties view shows configurable options

### 3. **Clocks Tab**
- Clock tree configuration
- Source selection (HOCO, MOSC, PLL)
- Dividers for ICLK, PCLK, FCLK, etc.
- **Critical for BLE:** Ensure 32 MHz clock for RF

### 4. **Pins Tab**
- Pin function assignment
- Electrical characteristics (drive strength, pull-up/down)
- **For BLE:** RF pins are auto-configured

### 5. **Interrupts Tab**
- Add/configure interrupts
- Set priorities
- Assign callback functions

### 6. **Event Links Tab**
- Configure Event Link Controller (ELC)
- Link peripheral events to other peripherals

### 7. **Stacks Tab** ⭐ **MOST IMPORTANT**
- Add HAL drivers and middleware
- Configure module properties
- **For BLE:**
  - Add `r_ble` or `rm_ble_abs` module
  - Configure GAP/GATT settings
  - Set callbacks

### 8. **Components Tab**
- Overview of selected modules
- Add sample code/drivers

---

## Adding BLE to FSP Project

### Step 1: Add BLE Stack Module

1. Open `configuration.xml` in RA Config Editor
2. Go to **Stacks** tab
3. Click **New Stack** → **Connectivity** → **BLE**
4. Select:
   - `r_ble_compact` (for RA4W1)
   - OR `rm_ble_abs` (abstraction layer)

### Step 2: Configure BLE Module

In **Properties** view:
- **Name:** `g_ble0` (instance name)
- **GAP Callback:** `ble_gap_callback`
- **GATT Server Callback:** `ble_gatts_callback`
- **GATT Client Callback:** `ble_gattc_callback` (if needed)
- **Vendor Specific Callback:** `ble_vs_callback`

### Step 3: Configure BLE Settings

Create `r_ble_cfg.h` in `ra_cfg/fsp_cfg/`:
```c
#define BLE_CFG_LIBRARY_TYPE        (BLE_LIB_COMPACT)
#define BLE_CFG_RF_DEEP_SLEEP_EN    (1)
#define BLE_CFG_SYNCHRONIZATION_TYPE (0)  // 0=semaphore, 1=event
```

### Step 4: Generate Project Content

Click **Generate Project Content** button (or Ctrl+S)

This generates:
- `ra_gen/common_data.c` - BLE instance `g_ble0_ctrl`, `g_ble0_cfg`
- `ra_gen/hal_data.c` - BLE initialization data
- Callback function prototypes in `hal_data.h`

---

## BLE Callback Implementation

### GAP Callback (Connection/Advertising)
```c
void ble_gap_callback(uint16_t event_type, ble_status_t result, 
                      st_ble_evt_data_t *p_data) {
    switch (event_type) {
        case BLE_GAP_EVENT_CONN_IND:
            // Connection established
            uint16_t conn_hdl = p_data->conn_ind.conn_hdl;
            break;
            
        case BLE_GAP_EVENT_DISCONN_IND:
            // Disconnected
            break;
            
        case BLE_GAP_EVENT_ADV_REPT_IND:
            // Advertising report (scan result)
            break;
    }
}
```

### GATT Server Callback (Write/Read)
```c
void ble_gatts_callback(uint16_t event_type, ble_status_t result,
                        st_ble_gatts_evt_data_t *p_data) {
    switch (event_type) {
        case BLE_GATTS_EVENT_WRITE_REQ:
            // Handle write request
            uint16_t attr_hdl = p_data->write_req.attr_hdl;
            uint8_t *value = p_data->write_req.value;
            uint16_t len = p_data->write_req.value_len;
            break;
            
        case BLE_GATTS_EVENT_READ_REQ:
            // Handle read request
            break;
    }
}
```

---

## Critical Notes for MicroPython Integration

### 1. **Callbacks Run in IRQ Context**
- **DO NOT** call Python code directly from callbacks
- Use event queue (ring buffer) to defer to main loop

### 2. **Memory Allocation**
- Avoid `malloc()` in callbacks
- Use static buffers or memory pools

### 3. **RTOS vs Bare Metal**
- FSP BLE can work with or without RTOS
- For MicroPython: use **bare metal** mode (no RTOS)
- Set `BSP_CFG_RTOS = 0` in `bsp_cfg.h`

### 4. **Clock Requirements**
- BLE requires stable 32 MHz clock
- Check clock configuration in **Clocks** tab

### 5. **Stack Size**
- BLE stack needs ~16 KB stack
- Set in **BSP** tab: `BSP_CFG_STACK_MAIN_BYTES = 0x4000`

---

## Next Steps for MicroPython

1. ✅ Created event queue system (`ra_ble_events.c`)
2. ✅ Created BLE wrapper (`ra_ble.c`)
3. ✅ Created MicroPython module (`modble_renesas.c`)
4. ⏳ **TODO:** Add to Makefile and test build
5. ⏳ **TODO:** Integrate FSP BLE library (when available)
6. ⏳ **TODO:** Test on hardware

---

## References

- FSP Documentation: https://renesas.github.io/fsp
- FSP GitHub: https://github.com/renesas/fsp
- RA4W1 User Manual: https://www.renesas.com/ra4w1
- BLE Examples: https://github.com/renesas/ra-fsp-examples

