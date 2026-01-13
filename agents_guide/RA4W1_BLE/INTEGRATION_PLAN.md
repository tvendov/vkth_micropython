# RA4W1 BLE Integration Plan for MicroPython

**Date:** 2026-01-07  
**Target:** Renesas RA4W1 MicroPython Port  
**Approach:** Native FSP BLE Stack (r_ble + rm_ble_abs)

---

## Architecture Decision

**Selected: Variant A - Native FSP BLE + MicroPython Wrapper**

- MicroPython communicates directly with R_BLE API
- GATT profiles/services generated via QE for BLE or manually defined
- **Advantages:**
  - Low latency
  - Full control over BLE stack
  - No HCI transport overhead
  - Optimal for RA4W1 with integrated BLE

**Rejected: Variant B - HCI Interface to NimBLE**
- Would require HCI transport layer
- Not typical for RA4W1 with native BLE stack
- Existing `mpbthciport.c` is for external BLE modules via UART

---

## Directory Structure

```
ports/renesas-ra/
├── ble/                          # NEW: BLE implementation
│   ├── ra_ble.c                  # FSP BLE wrapper (R_BLE API calls)
│   ├── ra_ble.h                  # Internal BLE interface
│   ├── ra_ble_gap.c              # GAP operations (advertising, scanning, connection)
│   ├── ra_ble_gatt.c             # GATT server/client operations
│   └── ra_ble_events.c           # Event queue and callback handling
├── modble_renesas.c              # NEW: MicroPython module "renesas_ble"
├── modble_renesas.h
├── fsp_cfg/
│   ├── r_ble_cfg.h               # NEW: BLE configuration
│   └── rm_ble_abs_cfg.h          # NEW: BLE abstraction config
└── boards/EK_RA4W1/
    ├── ra_gen/
    │   ├── ble_profile.c         # NEW: Generated GATT profile (from QE)
    │   └── ble_profile.h
    └── mpconfigboard.h           # Add MICROPY_HW_ENABLE_BLE
```

---

## Phase 1: Minimal C BLE Bring-up (Standalone Test)

**Goal:** Prove BLE stack works on hardware before MicroPython integration

### Steps:
1. Create FSP project for RA4W1 in e2 studio (or use CLI)
2. Add `r_ble_compact` module via FSP configurator
3. Generate skeleton code with QE for BLE
4. Implement "Hello BLE" test:
   - Start advertising with device name
   - Accept connection
   - Implement minimal GATT service (e.g., Battery Service or custom UUID)
   - Test notify/indicate characteristic

### Deliverable:
- Standalone C demo that advertises and accepts connections
- Verified on EK-RA4W1 board
- Code saved in `agents_guide/RA4W1_BLE/bringup_test/`

---

## Phase 2: FSP BLE Build System Integration

**Goal:** Compile BLE library as part of MicroPython firmware

### Makefile Changes (`ports/renesas-ra/Makefile`):

```makefile
# BLE support (RA4W1 only)
ifeq ($(CMSIS_MCU),RA4W1)
ifeq ($(MICROPY_HW_ENABLE_BLE),1)
CFLAGS += -DMICROPY_HW_ENABLE_BLE=1

# BLE source files
SRC_C += \
    ble/ra_ble.c \
    ble/ra_ble_gap.c \
    ble/ra_ble_gatt.c \
    ble/ra_ble_events.c \
    modble_renesas.c

# FSP BLE library
HAL_SRC_C += \
    $(HAL_DIR)/ra/fsp/src/r_ble/r_ble.c \
    $(HAL_DIR)/ra/fsp/src/rm_ble_abs/rm_ble_abs.c

# BLE library (prebuilt .a if provided by FSP)
# LIBS += $(HAL_DIR)/ra/fsp/lib/ble/libble_compact.a

# Include paths
INC += -I$(HAL_DIR)/ra/fsp/inc/api
INC += -I$(HAL_DIR)/ra/fsp/inc/instances
INC += -Ible
endif
endif
```

### Board Configuration (`boards/EK_RA4W1/mpconfigboard.h`):

```c
#define MICROPY_HW_ENABLE_BLE       (1)
#define MICROPY_BLE_EVENT_QUEUE_SIZE (32)  // Ring buffer size for BLE events
```

---

## Phase 3: BLE Event Queue System

**Critical:** BLE callbacks run in interrupt context → cannot execute Python code directly

### Design:

```c
// ble/ra_ble_events.c

typedef struct {
    uint16_t event_type;
    uint16_t conn_handle;
    uint16_t data_len;
    uint8_t data[BLE_EVENT_MAX_PAYLOAD];  // Or pointer to slab
} ble_event_t;

typedef struct {
    ble_event_t events[MICROPY_BLE_EVENT_QUEUE_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} ble_event_queue_t;

// Called from BLE callback (IRQ context)
void ra_ble_event_push(uint16_t event_type, uint16_t conn_hdl, void *data, uint16_t len);

// Called from main loop (safe Python context)
bool ra_ble_event_pop(ble_event_t *event);
```

---

## Phase 4: MicroPython API Design

### Option 1: `renesas_ble` Module (Fast Prototype)

```python
import renesas_ble as ble

ble.init()
ble.advertise(name="RA4W1-MP", interval_ms=100)

def on_connect(conn_handle):
    print(f"Connected: {conn_handle}")

ble.on("connect", on_connect)
ble.notify(conn_handle, char_handle, b"Hello")
```

### Option 2: Standard `bluetooth.BLE` (Long-term)

**Recommendation:** Start with `renesas_ble`, migrate to `bluetooth.BLE` later

---

## Next Steps

1. ✅ Research complete
2. ⏳ Create minimal C BLE test
3. ⏳ Integrate into build system
4. ⏳ Implement event queue
5. ⏳ Create `renesas_ble` module
6. ⏳ Test on hardware

