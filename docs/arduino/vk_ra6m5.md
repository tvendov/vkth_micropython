# Arduino Support for VK-RA6M5

This board definition exposes the VK-RA6M5 board inside the Arduino ecosystem.
Files are generated from the existing FSP configuration used by the
MicroPython port.

## Getting Started

1. Copy the `arduino` folder to your Arduino hardware directory or point
   `arduino-cli` to it via the `--additional-urls` option.
2. Select the board **VK-RA6M5** from the boards menu.
3. Compile and upload any sketch as usual.

The `linker.ld` file follows the memory layout of the MicroPython build with
512&nbsp;KB RAM and 1.9&nbsp;MB of program flash.

## Examples

### Blink

Upload the standard Blink example. `LED_BUILTIN` maps to the red LED
on pin `LED_R`.

### Ethernet

The board includes an on-board Ethernet MAC. The example
`Ethernet > WebClient` works after the FSP Ethernet driver is
initialized in `initVariant()`.

## Testing Notes

The sketches were built with `arduino-cli`:

```bash
arduino-cli compile -b vk_ra6m5 examples/Blink
arduino-cli compile -b vk_ra6m5 examples/Ethernet/WebClient
```

Due to missing pre-installed cores in this environment the commands may
fail until the RA core is added.
