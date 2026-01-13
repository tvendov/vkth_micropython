# GATT Database Integration Guide for RA4W1

## Problem
Currently, RA4W1 BLE stack is initialized but **NO GATT services are registered**. 
FSP requires explicit GATT DB configuration before advertising starts.

## Solution: Three Approaches

### Approach 1: Static GATT DB (Recommended for Production)
**Use Renesas QE to generate GATT DB configuration**

1. **Generate with QE:**
   - Open Renesas QE
   - Design BLE services/characteristics
   - Export as C code → `qe_ble_profile.c`

2. **Integration:**
   ```c
   // In ra_ble_init() after R_BLE_GATTS_Init():
   extern st_ble_gatts_db_cfg_t g_ble_gatts_db_cfg;  // From QE
   R_BLE_GATTS_SetDbInst(&g_ble_gatts_db_cfg);
   ```

3. **Advantages:**
   - ✅ Optimized for flash/RAM
   - ✅ Full FSP feature support
   - ✅ Production-ready

### Approach 2: Dynamic GATT DB (MicroPython Standard)
**Implement `gatts_register_services()` wrapper**

```python
# Python API
services = (
    (UUID(0x180A), (  # Device Information Service
        (UUID(0x2A29), FLAG_READ),  # Manufacturer Name
    )),
)
handles = ble.gatts_register_services(services)
```

**Implementation:**
- Parse Python service tuples
- Build `st_ble_gatts_db_cfg_t` at runtime
- Call `R_BLE_GATTS_SetDbInst()`

### Approach 3: Hybrid (Recommended for Development)
**Static base + dynamic extensions**

1. QE generates core services (GAP, GATT)
2. Python code adds custom services dynamically
3. Requires careful handle management

## GATT DB Structure Deep Dive

### st_ble_gatts_db_cfg_t
```c
typedef struct {
    uint8_t *p_uuid_table;           // UUID byte array (packed)
    st_ble_gatts_db_attr_cfg_t *p_attr_cfg;  // Attribute configs
    st_ble_gatts_db_uuid_cfg_t *p_uuid_cfg;  // UUID index
    uint8_t *p_attr_val_table;       // Attribute values
    uint8_t *p_const_attr_val_table; // Const attribute values
    uint16_t attr_num;               // Total attributes
    uint16_t uuid_num;               // Total UUIDs
} st_ble_gatts_db_cfg_t;
```

### Key Fields Explained

**p_uuid_table:** Packed UUID bytes
```
[16-bit UUID 1] [16-bit UUID 2] [128-bit UUID 3] ...
```

**p_attr_cfg[i]:** Each attribute entry
```c
typedef struct {
    uint16_t uuid_offset;    // Offset in p_uuid_table
    uint16_t next;           // Next attr with same UUID (linked list)
    uint16_t p_data_offset;  // Offset in p_attr_val_table
    uint8_t aux_prop;        // Properties (read/write/notify)
} st_ble_gatts_db_attr_cfg_t;
```

**p_uuid_cfg[i]:** UUID index
```c
typedef struct {
    uint16_t offset;         // Offset in p_uuid_table
    uint16_t first;          // First attr handle with this UUID
    uint16_t last;           // Last attr handle with this UUID
} st_ble_gatts_db_uuid_cfg_t;
```

## Implementation Checklist

- [ ] Obtain FSP BLE QE tool
- [ ] Design GATT services in QE
- [ ] Generate C code from QE
- [ ] Extract `st_ble_gatts_db_cfg_t` structure
- [ ] Add to `ra_ble_config.c`
- [ ] Call `R_BLE_GATTS_SetDbInst()` in `ra_ble_init()`
- [ ] Test with BLE scanner app
- [ ] Document generated structure

