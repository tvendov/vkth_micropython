# RA4M1 CLICKER - USB CDC Build Log
**Date:** 2024-12-30

## Build Configuration

### Hardware
- **Board:** RA4M1 CLICKER
- **MCU:** Renesas RA4M1 (Cortex-M4)
- **Flash:** 256 KB total
- **RAM:** 32 KB

### Memory Layout (Linker Script)

| Region | Before | After | Change |
|--------|--------|-------|--------|
| FLASH (code) | 220 KB (0x37000) | **222 KB (0x37800)** | +2 KB |
| FLASH_FS | 36 KB (0x9000) | **34 KB (0x8800)** | -2 KB |
| RAM | 32 KB | 32 KB | - |
| DATA_FLASH | 8 KB | 8 KB | - |

### Files Modified

**1. ports/renesas-ra/boards/RA4M1_CLICKER/manifest.py**
```python
# Before:
require("sdcard")

# After:
include("$(MPY_DIR)/extmod/asyncio")
```

**2. ports/renesas-ra/boards/RA4M1_CLICKER/ra4m1_clicker.ld**
```c
// Lines 8-9 changed:
FLASH (rx)   : ORIGIN = 0x00000000, LENGTH = 0x00037800  /* 222KB */
FLASH_FS (r) : ORIGIN = 0x00037800, LENGTH = 0x00008800  /* 34KB */
```

## Build Results

```
text    data     bss     dec     hex filename
225616       0   30784  256400   3e990 build-RA4M1_CLICKER/firmware.elf
```

### Memory Usage
- **Code (text):** 225,616 bytes = 220.3 KB
- **RAM (bss):** 30,784 bytes = 30.0 KB
- **Free Flash:** ~1.7 KB (222KB - 220.3KB)
- **Free RAM:** ~1.2 KB (32KB - 30KB)

## Features Included

| Feature | Status | Size |
|---------|--------|------|
| USB CDC REPL | ✅ YES | ~11.5 KB |
| asyncio (frozen) | ✅ YES | ~6.9 KB |
| VFS_FAT | ✅ YES | ~16.9 KB |
| DAC | ✅ YES | ~0.9 KB |
| OPAMP | ✅ YES | ~0.3 KB |
| Comparator | ✅ YES | ~0.7 KB |
| I2C Target | ✅ YES | - |
| sdcard (frozen) | ❌ NO | removed |
| UART REPL | ❌ NO | USB CDC instead |
| VFS_LFS2 | ❌ NO | not compiled |

## Component Breakdown

### USB CDC Stack (~11.5 KB)
```
cdc_device.o:         1,466 bytes
usbd.o:               4,032 bytes
usbd_control.o:         522 bytes
dcd_rusb2.o:          3,023 bytes
tusb.o:               1,326 bytes
mp_usbd_cdc.o:          520 bytes
mp_usbd.o:              105 bytes
mp_usbd_descriptor.o:   269 bytes
```

### VFS FAT (~16.9 KB)
```
ff.o:                10,887 bytes
ffunicode.o:          1,190 bytes
vfs.o:                1,830 bytes
vfs_fat.o:            1,457 bytes
vfs_fat_file.o:         675 bytes
vfs_blockdev.o:         424 bytes
vfs_fat_diskio.o:       242 bytes
vfs_reader.o:           228 bytes
```

### Frozen Modules (asyncio ~6.9 KB)
```
asyncio/__init__.mpy:    377 bytes
asyncio/core.mpy:      2,556 bytes
asyncio/event.mpy:       539 bytes
asyncio/funcs.mpy:       978 bytes
asyncio/lock.mpy:        498 bytes
asyncio/stream.mpy:    1,954 bytes
uasyncio.mpy:             67 bytes
```

## Build Command
```bash
cd ports/renesas-ra
make BOARD=RA4M1_CLICKER -j16
```

## Build Output
```
Use make V=1 or set BUILD_VERBOSE in your environment to increase build verbosity.
LINK build-RA4M1_CLICKER/firmware.elf
   text    data     bss     dec     hex filename
 225616       0   30784  256400   3e990 build-RA4M1_CLICKER/firmware.elf
GEN build-RA4M1_CLICKER/firmware.hex
GEN build-RA4M1_CLICKER/firmware.bin
```

## Success Status
✅ **Build completed successfully**
✅ **No linker errors**
✅ **Firmware ready for flashing**

## Summary
- Reduced FLASH_FS by 2KB to free space for code
- Added USB CDC support (+11.5KB)
- Added asyncio frozen module (+6.9KB)
- Removed sdcard frozen module (-2.3KB)
- Net increase: ~16KB, accommodated by 2KB FS reduction + optimization
- Final firmware fits with ~1.7KB margin

