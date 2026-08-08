# VK_RA4M2 Arduino bootloader overlay

This directory contains the VK_RA4M2-specific overlay for building an Arduino-style
Renesas bootloader using:

- the local MicroPython VK_RA4M2 board configuration,
- Arduino's `ArduinoCore-renesas` layout conventions,
- Arduino's TinyUSB-based `arduino-renesas-bootloader` project.

The bootloader itself should stay upstream-based. The files here define the RA4M2
memory layout, board build flags, Arduino core board entry, and the MicroPython
hook needed for `machine.bootloader()`.

## Flash layout

VK_RA4M2 has 512 KiB code flash and 128 KiB RAM. This overlay reserves the first
64 KiB for the bootloader, matching the Arduino Renesas core convention used by
DFU boards that upload sketches at `0x00010000`.

```
0x00000000..0x0000FFFF  bootloader, 64 KiB
0x00010000..0x0006FFFF  Arduino sketch / MicroPython app image, 384 KiB
0x00070000..0x0007FFFF  MicroPython internal flash filesystem, 64 KiB
0x20000000..0x2001FFFF  RAM, 128 KiB
0x08000000..0x08001FFF  data flash, 8 KiB
```

If the Arduino sketch needs the full space, remove or move the MicroPython
filesystem area and update both the linker file and `boards.local.txt`.

## Build recipe

1. Clone Arduino's bootloader project outside this tree:

   ```sh
   git clone https://github.com/arduino/arduino-renesas-bootloader
   ```

2. Clone TinyUSB and prepare the Renesas RA dependencies:

   ```sh
   git clone https://github.com/hathach/tinyusb.git
   cd tinyusb
   git checkout 0.17.0
   python ./tools/get_deps.py ra
   export TINYUSB_ROOT=$PWD
   patch -p1 < ../arduino-renesas-bootloader/0001-fix-arduino-bootloaders.patch
   ```

3. Copy `Makefile.vk_ra4m2` into the root of `arduino-renesas-bootloader`.

4. Copy `tinyusb_hw_bsp_ra_boards_vk_ra4m2` to:

   ```sh
   cp -r tinyusb_hw_bsp_ra_boards_vk_ra4m2 "$TINYUSB_ROOT/hw/bsp/ra/boards/vk_ra4m2"
   ```

5. Build from the bootloader project:

   ```sh
   make -f Makefile.vk_ra4m2
   ```

The produced image must be flashed once at `0x00000000` using SWD/J-Link or
Renesas Flash Programmer. After that, Arduino uploads can target
`0x00010000` through DFU.

## Arduino core integration

Copy `boards.local.txt` entries into an ArduinoCore-renesas development checkout
or merge them into its `boards.txt`. The important fields are:

- `upload.tool=dfu-util`
- `upload.address=0x00010000`
- `upload.maximum_size=393216`
- `build.variant=VK_RA4M2`

The matching Arduino variant still has to provide the normal Renesas core files:
`fsp.ld`, `memory_regions.ld`, `variant.cpp`, `pins_arduino.h`, `includes.txt`,
`defines.txt`, `ldflags.txt`, and `libs/libfsp.a`.

## MicroPython integration

`vk_ra4m2_micropython_bootloader_hook.c` mirrors the Portenta C33 pattern used
in the MicroPython Renesas port. Add it to the VK_RA4M2 board build, then add
these declarations to `mpconfigboard.h`:

```c
#define MICROPY_BOARD_ENTER_BOOTLOADER(nargs, args) VK_RA4M2_board_enter_bootloader()
void VK_RA4M2_board_enter_bootloader(void);
```

That makes `machine.bootloader()` set the same double-tap magic word used by the
Arduino bootloader and then reset the MCU.

## Notes

- RA4M2 in this MicroPython tree currently uses `r_flash_hp`, so this overlay
  selects `r_flash_hp.c`, unlike UNO R4 Minima's RA4M1 profile which uses
  `r_flash_lp.c`.
- The bootloader assumes USB FS is available on P914/P915 and VBUS on P407,
  matching the local `pins.csv`.
- The MD pin is P201. A blank board can still enter Renesas ROM boot mode by
  holding MD low on reset, independent of this Arduino bootloader.
