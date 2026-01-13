# Action Plan: GATT DB Integration for RA4W1 (2026-01-10)

## Current State
✅ BLE stack initialized and working
✅ GAP advertising/connection working
✅ GATT notifications/indications working
❌ **GATT Database NOT configured** - No services registered

## Problem
Without GATT DB configuration, BLE peripheral has NO services to advertise.
FSP requires `R_BLE_GATTS_SetDbInst()` call with proper `st_ble_gatts_db_cfg_t` structure.

## Solution Path

### Phase 1: Minimal GATT DB (Week 1)
**Goal:** Get ANY GATT service working

**Tasks:**
1. [ ] Create minimal GATT DB with Device Information Service
   - Service UUID: 0x180A
   - Characteristic: Manufacturer Name (0x2A29)
   - Characteristic: System ID (0x2A23)

2. [ ] Add to `ra_ble_config.c`:
   ```c
   static const uint8_t g_ble_uuid_table[] = { ... };
   static const st_ble_gatts_db_attr_cfg_t g_ble_attr_cfg[] = { ... };
   static const st_ble_gatts_db_uuid_cfg_t g_ble_uuid_cfg[] = { ... };
   static uint8_t g_ble_attr_val_table[] = { ... };
   
   st_ble_gatts_db_cfg_t g_ble_gatts_db_cfg = { ... };
   ```

3. [ ] Modify `ra_ble_init()` in `ra_ble.c`:
   ```c
   extern st_ble_gatts_db_cfg_t g_ble_gatts_db_cfg;
   R_BLE_GATTS_SetDbInst(&g_ble_gatts_db_cfg);
   ```

4. [ ] Test with BLE scanner app
   - Verify services appear
   - Verify characteristics readable

### Phase 2: Dynamic Service Registration (Week 2)
**Goal:** Implement MicroPython `gatts_register_services()`

**Tasks:**
1. [ ] Design Python API:
   ```python
   services = (
       (UUID(0x180A), (
           (UUID(0x2A29), FLAG_READ),
       )),
   )
   handles = ble.gatts_register_services(services)
   ```

2. [ ] Implement C wrapper in `modble_renesas.c`:
   - Parse Python tuples
   - Build GATT DB structures
   - Call `R_BLE_GATTS_SetDbInst()`

3. [ ] Handle memory management:
   - Allocate UUID table
   - Allocate attribute configs
   - Allocate value storage

4. [ ] Test with multiple services

### Phase 3: Advanced Features (Week 3+)
**Goal:** Full feature parity with standard MicroPython BLE

**Tasks:**
1. [ ] Implement `gatts_read()` / `gatts_write()`
2. [ ] Implement `gatts_notify()` / `gatts_indicate()`
3. [ ] Implement descriptors (CCCD, etc.)
4. [ ] Add security/permissions
5. [ ] Performance optimization

## Implementation Order

1. **Start with Phase 1** - Get minimal GATT DB working
2. **Validate** - Test with BLE scanner
3. **Document** - Add examples and API docs
4. **Then Phase 2** - Dynamic registration
5. **Then Phase 3** - Advanced features

## Testing Strategy

### Unit Tests
- [ ] GATT DB structure validation
- [ ] UUID offset verification
- [ ] Handle chain integrity

### Integration Tests
- [ ] BLE scanner can discover services
- [ ] Characteristics are readable
- [ ] Notifications work
- [ ] Multiple connections

### Hardware Tests
- [ ] EK_RA4W1 board
- [ ] BLE scanner app (iOS/Android)
- [ ] Stress testing (many notifications)

## Success Criteria

✅ Phase 1 Complete:
- BLE scanner discovers Device Information Service
- Manufacturer Name characteristic is readable
- System ID characteristic is readable

✅ Phase 2 Complete:
- Python code can register custom services
- Services appear in BLE scanner
- Notifications work

✅ Phase 3 Complete:
- Full feature parity with standard MicroPython BLE
- All tests passing
- Documentation complete

## Resources Needed

1. **Renesas QE** - For GATT DB generation (optional)
2. **BLE Scanner App** - For testing (iOS/Android)
3. **EK_RA4W1 Board** - For hardware testing
4. **FSP Documentation** - For API reference

## Timeline

- **Week 1:** Phase 1 (Minimal GATT DB)
- **Week 2:** Phase 2 (Dynamic Registration)
- **Week 3+:** Phase 3 (Advanced Features)

**Total Effort:** ~3-4 weeks for full implementation

