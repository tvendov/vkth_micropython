# ArduinoCore-renesas VK_RA4M2 section

This section is separate from the bootloader. It tracks the ArduinoCore-renesas
board/variant work for VK_RA4M2 / R7FA4M2AD3CFP.

## Current local core

The active development checkout is:

```text
C:\msys_64\home\teodor\arduino_ra4m2\ArduinoCore-renesas-full
```

It already has:

```text
variants\VK_RA4M2
boards.txt entry: vk_ra4m2
```

## Bootloader-compatible layout

This variant is aligned with `arduino-renesas-bootloader-v2.bin`:

```text
0x00000000..0x0000FFFF  bootloader
0x00010000..0x0006FFFF  Arduino sketch, 384 KiB
0x00070000..0x0007FFFF  reserved / MicroPython filesystem compatibility area
```

Important values:

```text
upload.address=0x00010000
upload.maximum_size=393216
VID/PID runtime=1209:0001
VID/PID DFU=2341:0369
F_CPU=100000000
MCU=R7FA4M2AD3CFP
core=cortex-m33, fpv5-sp-d16, hard-float
```

## Files in this section

- `boards.vk_ra4m2.txt`: board entry for ArduinoCore-renesas `boards.txt`.
- `variant_overlay`: the key editable variant files copied from the local core.
- `smoke_blink`: first Arduino sketch for compile/upload testing.

The full local core variant also needs generated FSP headers and `libs/libfsp.a`;
those remain in the ArduinoCore-renesas checkout and are not duplicated here.

## Test target

First goal is not full Arduino API coverage. First goal is:

1. Compile `smoke_blink` for `vk_ra4m2`.
2. Upload at `0x00010000` through the v2 DFU bootloader.
3. Confirm `LED_BUILTIN` toggles on P204.

## Smoke build status

This has been compiled successfully with the installed Arduino platform:

```text
arduino:vkra4m2 1.0.0
C:\Users\teodor\AppData\Local\Arduino15\packages\arduino\hardware\vkra4m2\1.0.0
```

Build result:

```text
Sketch uses 39348 bytes (10%) of program storage space. Maximum is 393216 bytes.
Global variables use 5516 bytes (4%) of dynamic memory, leaving 125556 bytes for local variables. Maximum is 131072 bytes.
```

Output artifacts copied here:

```text
artifacts\vk_ra4m2_smoke_blink.bin
artifacts\vk_ra4m2_smoke_blink.hex
artifacts\vk_ra4m2_smoke_blink.elf
```

Recommended Arduino CLI compile+upload:

```powershell
arduino-cli compile -u -p COM43 -b arduino:vkra4m2:vk_ra4m2 smoke_blink
```

The installed `platform.txt` DFU recipe must use the runtime VID/PID first and
the bootloader VID/PID second:

```text
dfu-util --device 0x1209:0x0001,0x2341:0x0369 -a0 -s 0x00010000:leave -D sketch.bin
```

The installed VK_RA4M2 Arduino core needed these fixes before this linked and
uploaded:

```text
variants\VK_RA4M2\libs\libfsp.a rebuilt with RA4M2 FSP drivers
variants\VK_RA4M2\includes\ra_cfg\fsp_cfg\bsp\bsp_cfg.h stack reduced to 0x1000
variants\VK_RA4M2\memory_regions.ld stack symbol reduced to 0x1000
platform.txt dfu-util upload pattern changed from -Q to -s {upload.address}:leave
boards.txt VK_RA4M2 runtime VID/PID changed to 1209:0001
```
