# VK_RA4M2 bootloader test status

This records the current known-good RA4M2 DFU bootloader path.

## Validated result

Tested on `R7FA4M2AD3CFP` through J-Link OB-RA4M2.

Bootloader artifact:

```text
build/arduino-renesas-bootloader-v7-dfuse-autoinc-pid0369.bin
SHA256 D89D0F5FF49E4268FAB834B84A1A238EBDBDBF990E24794A1EAA80BEE4D95185
```

Smoke app artifact:

```text
arduino_core_ra4m2/artifacts/vk_ra4m2_smoke_blink.bin
SHA256 252B7EF7FC259DFBF11C9005CC6A2D9F4380E5E4EDD6F062AF59A5638CF2FB38
```

Confirmed flow:

```text
J-Link flash bootloader at 0x00000000: verify successful
dfu-util -d 2341:0369 -a 0 -s 0x00010000:leave -D vk_ra4m2_smoke_blink.bin: success
J-Link readback at 0x00010000: 2001FE00 000102E9 ...
USB after leave: Runtime [1209:0001], interface "DFU-RT Port"
```

The important fix is DfuSe address handling: `dfu-util -s` sends 5-byte DfuSe
commands in block 0 and data with `wValue = 2`. The bootloader must write data
at the current DfuSe address and auto-increment it after each payload.

## 1. Flash only the bootloader

Erase code flash at least from `0x00000000` through `0x0006FFFF`, then flash:

```text
build/arduino-renesas-bootloader-v7-dfuse-autoinc-pid0369.bin -> 0x00000000
```

Use the `.bin` for this test, not the `.hex`, because the `.hex` also carries
option-setting sections.

## 2. DFU test with empty app area

With the app area erased, reset the board and check USB enumeration:

```sh
dfu-util -l
```

Expected result: a DFU device with VID/PID `2341:0369` and board string
`VK_RA4M2`.

If no USB device appears, the failure is before app loading: clock, USB pins,
USB LDO, reset/startup, or TinyUSB init.

## 3. Upload the real smoke app

Do not use the earlier minimal fake app for final validation. Validate with a
real Arduino-linked image for `0x00010000`.

```sh
dfu-util -d 2341:0369 -a 0 -s 0x00010000:leave -D arduino_core_ra4m2/artifacts/vk_ra4m2_smoke_blink.bin
```

Expected after `:leave`: the board stops enumerating as bootloader DFU and
appears as runtime USB `VID_1209&PID_0001`, with `dfu-util -l` reporting
`DFU-RT Port`.

## 4. Double-reset recovery test

Press reset twice within the bootloader window. Expected result:

```sh
dfu-util -l
```

again shows the VK_RA4M2 DFU device.

If step 2 works but step 4 fails, the problem is the double-tap magic path
through `R_SYSTEM->VBTBKR[0]`.

## 5. Vector readback

Confirm app flash with J-Link:

```text
mem32 0x00010000 8
```

Expected first words:

```text
00010000 = 2001FE00 000102E9 ...
```
