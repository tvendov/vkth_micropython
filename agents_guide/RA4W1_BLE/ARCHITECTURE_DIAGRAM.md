# RA4W1 BLE Architecture Diagram

## Current Implementation Stack

```
┌─────────────────────────────────────────────────────────────┐
│                    MicroPython Application                   │
│  import renesas_ble as ble                                   │
│  ble.active(True)                                            │
│  ble.advertise("RA4W1", 100)                                 │
└────────────────────┬────────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────────┐
│              MicroPython Module Layer                        │
│  modble_renesas.c / modble_renesas.h                         │
│  ✅ active()  ✅ advertise()  ✅ notify()                    │
│  ❌ gatts_register_services()  ❌ gatts_read/write()         │
└────────────────────┬────────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────────┐
│              BLE Wrapper Layer                               │
│  ra_ble.c / ra_ble.h                                         │
│  ✅ ra_ble_init()                                            │
│  ✅ ra_ble_gap_start_advertising()                           │
│  ✅ ra_ble_gatts_notify()                                    │
│  ❌ GATT DB configuration (NOT CALLED)                       │
└────────────────────┬────────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────────┐
│              Event Queue System                              │
│  ra_ble_events.c / ra_ble_events.h                           │
│  Lock-free ring buffer for BLE events                        │
└────────────────────┬────────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────────┐
│              FSP BLE Stack (Compact)                         │
│  lib/fsp/ra/fsp/lib/r_ble/cm4_gcc/compact/libr_ble.a        │
│  ✅ R_BLE_Open()                                             │
│  ✅ R_BLE_GAP_Init()                                         │
│  ✅ R_BLE_GATTS_Init()                                       │
│  ❌ R_BLE_GATTS_SetDbInst() (NOT CALLED)                     │
└────────────────────┬────────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────────┐
│              RA4W1 Hardware (BLE Radio)                      │
│  Integrated BLE 5.0 transceiver                              │
└─────────────────────────────────────────────────────────────┘
```

## GATT Database Integration Point

```
Current Flow (INCOMPLETE):
┌──────────────────┐
│ R_BLE_GATTS_Init │
└────────┬─────────┘
         │
         ▼
    ❌ MISSING: R_BLE_GATTS_SetDbInst(&gatt_db_config)
         │
         ▼
    ❌ NO SERVICES REGISTERED
         │
         ▼
    ❌ BLE SCANNER SEES EMPTY DEVICE


Fixed Flow (REQUIRED):
┌──────────────────┐
│ R_BLE_GATTS_Init │
└────────┬─────────┘
         │
         ▼
    ✅ R_BLE_GATTS_SetDbInst(&gatt_db_config)
         │
         ▼
    ✅ SERVICES REGISTERED
         │
         ▼
    ✅ BLE SCANNER SEES SERVICES
```

## GATT Database Structure Relationships

```
st_ble_gatts_db_cfg_t
├── p_uuid_table[]
│   ├── [0x0A, 0x18]  ← 0x180A (Device Info Service)
│   ├── [0x29, 0x2A]  ← 0x2A29 (Manufacturer Name)
│   └── [0x23, 0x2A]  ← 0x2A23 (System ID)
│
├── p_attr_cfg[]
│   ├── [0] uuid_offset=0, next=0xFFFF, data_offset=0
│   ├── [1] uuid_offset=2, next=0xFFFF, data_offset=0
│   └── [2] uuid_offset=4, next=0xFFFF, data_offset=8
│
├── p_uuid_cfg[]
│   ├── [0] offset=0, first=0, last=0
│   ├── [1] offset=2, first=1, last=1
│   └── [2] offset=4, first=2, last=2
│
└── p_attr_val_table[]
    ├── [0-7]   "Renesas" (Manufacturer Name)
    └── [8-15]  0x01 0x02 0x03 0x04 0x05 0x06 0x07 0x08
```

## Three Integration Approaches

```
Approach 1: STATIC (Recommended for Production)
┌─────────────────────────────────────────┐
│ Renesas QE Tool                         │
│ (Design GATT services visually)         │
└────────────────┬────────────────────────┘
                 │
                 ▼
        ┌────────────────────┐
        │ Generate C Code    │
        │ qe_ble_profile.c   │
        └────────────┬───────┘
                     │
                     ▼
        ┌────────────────────────────────┐
        │ Include in ra_ble_config.c     │
        │ Call R_BLE_GATTS_SetDbInst()   │
        └────────────────────────────────┘


Approach 2: DYNAMIC (Recommended for Flexibility)
┌─────────────────────────────────────────┐
│ Python Code                             │
│ ble.gatts_register_services(services)   │
└────────────────┬────────────────────────┘
                 │
                 ▼
        ┌────────────────────┐
        │ modble_renesas.c   │
        │ Parse Python API   │
        └────────────┬───────┘
                     │
                     ▼
        ┌────────────────────────────────┐
        │ Build GATT DB structures       │
        │ Call R_BLE_GATTS_SetDbInst()   │
        └────────────────────────────────┘


Approach 3: HYBRID (Balanced)
┌──────────────────────────────────────────┐
│ Static Base (QE-generated)               │
│ + Dynamic Extensions (Python API)        │
└──────────────────────────────────────────┘
```

## Implementation Timeline

```
Week 1: Phase 1 (Minimal GATT DB)
├── Create Device Information Service
├── Add to ra_ble_config.c
├── Call R_BLE_GATTS_SetDbInst()
└── Test with BLE scanner

Week 2: Phase 2 (Dynamic Registration)
├── Implement gatts_register_services()
├── Build GATT DB at runtime
├── Handle memory management
└── Test with multiple services

Week 3+: Phase 3 (Advanced Features)
├── Implement gatts_read/write
├── Add descriptors
├── Add security/permissions
└── Performance optimization
```

## Success Criteria

```
Phase 1 ✅
├── BLE scanner discovers Device Information Service
├── Manufacturer Name characteristic is readable
└── System ID characteristic is readable

Phase 2 ✅
├── Python code can register custom services
├── Services appear in BLE scanner
└── Notifications work

Phase 3 ✅
├── Full feature parity with standard MicroPython BLE
├── All tests passing
└── Documentation complete
```

