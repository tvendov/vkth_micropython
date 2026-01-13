# GATT Database Technical Analysis

## Structure Relationships

### 1. UUID Table (`p_uuid_table`)
**Purpose:** Packed byte array of all UUIDs used in GATT DB

**Format:**
```
Offset  Content
0-1     16-bit UUID 1 (little-endian)
2-3     16-bit UUID 2
4-19    128-bit UUID 3 (little-endian)
20-21   16-bit UUID 4
...
```

**Example:**
```c
uint8_t uuid_table[] = {
    0x0A, 0x18,  // offset 0: 0x180A (Device Info Service)
    0x29, 0x2A,  // offset 2: 0x2A29 (Manufacturer Name)
    0x23, 0x2A,  // offset 4: 0x2A23 (System ID)
};
```

### 2. Attribute Configuration (`p_attr_cfg`)
**Purpose:** Array of attribute definitions (one per handle)

**Key Fields:**
- `uuid_offset`: Byte offset in `p_uuid_table` where this attribute's UUID is stored
- `next`: Handle of next attribute with SAME UUID (linked list), or 0xFFFF if last
- `p_data_offset`: Byte offset in `p_attr_val_table` for this attribute's value
- `aux_prop`: Attribute properties (read/write/notify/indicate)

**Example:**
```c
st_ble_gatts_db_attr_cfg_t attr_cfg[] = {
    // Handle 0: Service declaration
    {.uuid_offset = 0, .next = 0xFFFF, .p_data_offset = 0, .aux_prop = 0x01},
    // Handle 1: Characteristic (Manufacturer Name)
    {.uuid_offset = 2, .next = 0xFFFF, .p_data_offset = 0, .aux_prop = 0x02},
    // Handle 2: Characteristic (System ID)
    {.uuid_offset = 4, .next = 0xFFFF, .p_data_offset = 8, .aux_prop = 0x02},
};
```

### 3. UUID Configuration (`p_uuid_cfg`)
**Purpose:** Index for fast UUID lookup

**Key Fields:**
- `offset`: Byte offset in `p_uuid_table` for this UUID
- `first`: Handle of FIRST attribute with this UUID
- `last`: Handle of LAST attribute with this UUID

**Example:**
```c
st_ble_gatts_db_uuid_cfg_t uuid_cfg[] = {
    {.offset = 0, .first = 0, .last = 0},  // 0x180A at handle 0
    {.offset = 2, .first = 1, .last = 1},  // 0x2A29 at handle 1
    {.offset = 4, .first = 2, .last = 2},  // 0x2A23 at handle 2
};
```

## Validation Rules

### Rule 1: UUID Offset Consistency
```
For each attr_cfg[i]:
  - attr_cfg[i].uuid_offset must point to valid UUID in uuid_table
  - uuid_cfg[j].offset must match some attr_cfg[i].uuid_offset
```

### Rule 2: Handle Continuity
```
- Handles are array indices: 0, 1, 2, ..., attr_num-1
- No gaps allowed
- first/last in uuid_cfg must be valid handles
```

### Rule 3: Linked List Integrity
```
For each UUID:
  - uuid_cfg[j].first points to first attr with this UUID
  - Follow attr_cfg[handle].next chain until 0xFFFF
  - Last handle in chain must equal uuid_cfg[j].last
```

### Rule 4: Data Offset Validity
```
- p_data_offset must be < total size of p_attr_val_table
- No overlapping data regions (unless intentional sharing)
- Const attributes use p_const_attr_val_table instead
```

## Memory Layout Example

### GATT DB for Device Info Service
```
UUID Table (6 bytes):
  [0x0A, 0x18, 0x29, 0x2A, 0x23, 0x2A]
   └─ offset 0    └─ offset 2    └─ offset 4

Attribute Config (3 entries):
  Handle 0: uuid_offset=0, next=0xFFFF, data_offset=0, prop=0x01
  Handle 1: uuid_offset=2, next=0xFFFF, data_offset=0, prop=0x02
  Handle 2: uuid_offset=4, next=0xFFFF, data_offset=8, prop=0x02

UUID Config (3 entries):
  UUID 0x180A: offset=0, first=0, last=0
  UUID 0x2A29: offset=2, first=1, last=1
  UUID 0x2A23: offset=4, first=2, last=2

Attribute Value Table (16 bytes):
  [0-7]:   "Renesas" (Manufacturer Name)
  [8-15]:  0x01 0x02 0x03 0x04 0x05 0x06 0x07 0x08 (System ID)
```

## Debugging Tips

1. **Verify UUID offsets:**
   ```c
   for (int i = 0; i < attr_num; i++) {
       uint16_t offset = attr_cfg[i].uuid_offset;
       assert(offset < uuid_table_size);
   }
   ```

2. **Check handle chains:**
   ```c
   for (int i = 0; i < uuid_num; i++) {
       uint16_t handle = uuid_cfg[i].first;
       while (handle != 0xFFFF) {
           handle = attr_cfg[handle].next;
       }
       assert(handle == uuid_cfg[i].last);
   }
   ```

3. **Validate data offsets:**
   ```c
   for (int i = 0; i < attr_num; i++) {
       uint16_t offset = attr_cfg[i].p_data_offset;
       assert(offset < attr_val_table_size);
   }
   ```

