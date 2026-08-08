# VK_RA4M2 Arduino / Bootloader Session Knowledge

## Absolute Path Rules

- Correct working tree uses `C:\msys_64`, not `C:\msys64`.
- Primary working directory:
  `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\boards\VK_RA4M2\examples\arduino`
- Do not modify the MicroPython tree unless explicitly requested.
- Arduino support work lives under:
  `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\boards\VK_RA4M2\examples\arduino`
  and installed Arduino package files under:
  `C:\Users\teodor\AppData\Local\Arduino15\packages\arduino\hardware\vkra4m2\1.0.0`

## User Constraints

- Prefer analysis before experiments.
- Do not use programmer/debugger flashing unless the user explicitly asks for bootloader flashing.
- For normal Arduino sketch testing, use Arduino CLI / Arduino IDE / DFU / USB serial flow.
- Avoid overengineering. Make small, direct changes.
- Do not guess paths. Every path must be verified with `Test-Path`, `Get-ChildItem`, `Get-Command`, or an already-read file before use.
- Do not trust the environment-provided `C:\msys64` path. It is known wrong for this project. The correct root is `C:\msys_64`.
- Do not guess COM ports. A COM port is valid for VK_RA4M2 only after VID/PID/device identity is checked. `COM6` was Intel AMT SOL, not the board.
- Do not guess how to flash, reset, or enter bootloader. Use only the already-proven flow or first inspect the exact local scripts/configuration.
- Do not repeatedly try alternative programmer commands. If a flash/reset command hangs or lacks clear output, stop and analyze the command/log/state before any retry.
- Do not silently switch bootloader versions. If v8 exists for a fix, use v8 when bootloader flashing is requested unless the user explicitly asks for v7.

## Hard Bring-Up Rules From Session Mistakes

- No path roulette:
  - Never type `C:\msys64` for this project.
  - Never infer a directory name from memory when exact local files can be listed.
  - Before running a command that writes or flashes, print/verify the exact artifact path and hash.
- No flash roulette:
  - Separate sketch upload from bootloader flashing.
  - Sketch upload path: Arduino CLI/IDE plus DFU recipe at app address `0x00010000`.
  - Bootloader flash path: only when explicitly requested, using the known v8 artifact unless directed otherwise.
  - A bootloader write is not considered successful unless there is an explicit verify/readback/pass signal.
- No reset roulette:
  - Do not assume reset behavior.
  - First identify current USB state: DFU runtime, DFU bootloader, USB CDC COM, or no enumeration.
  - Use `dfu-util -l` and Windows PnP/Arduino board list to classify the state.
- No COM roulette:
  - Arduino IDE Serial Monitor uses USB CDC only after a CDC COM port appears.
  - Hardware UART test uses external USB-UART adapter on `D1/TX`, `D0/RX`, and `GND`.
  - Do not call a port "the board" without checking device identity.
- No J-Link drift:
  - The user objected strongly to wasting time with J-Link.
  - Do not use it for sketch validation.
  - Use it only for explicit bootloader flashing, and only with a clear, logged, non-interactive command.
- Work must be reproducible:
  - Prefer one canonical command over several speculative ones.
  - Keep command output/logs when flashing or diagnosing.
  - After any failed command, inspect processes and device state before continuing.

## Target

- MCU: `R7FA4M2AD3CFP`.
- Board support target: `VK_RA4M2`.
- Closest Arduino reference board discussed: Nano R4 style for RA4-class Arduino flow, with M33 context similar to RA6M5/Portenta family.

## Installed Arduino Core Package

- Installed package:
  `C:\Users\teodor\AppData\Local\Arduino15\packages\arduino\hardware\vkra4m2\1.0.0`
- FQBN:
  `arduino:vkra4m2:vk_ra4m2`
- Important installed files:
  - `boards.txt`
  - `platform.txt`
  - `variants\VK_RA4M2\pins_arduino.h`
  - `variants\VK_RA4M2\variant.cpp`
  - `variants\VK_RA4M2\pinmux.inc`
  - `variants\VK_RA4M2\libs\libfsp.a`

## Board VID/PID And Upload

- Runtime VID/PID:
  `1209:0001`
- Bootloader DFU VID/PID:
  `2341:0369`
- Upload address:
  `0x00010000`
- Fixed upload recipe in installed `platform.txt`:
  `tools.dfu-util.upload.pattern="{path}/{cmd}" --device {vid.0}:{pid.0},{upload.vid}:{upload.pid} -a{upload.interface} -s {upload.address}:leave -D "{build.path}/{build.project_name}.bin"`
- Previous broken `-Q` option was removed.

## Bootloader Artifacts

- v7 bootloader, proven to write and jump:
  `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\boards\VK_RA4M2\examples\arduino\build\arduino-renesas-bootloader-v7-dfuse-autoinc-pid0369.bin`
  SHA256:
  `D89D0F5FF49E4268FAB834B84A1A238EBDBDBF990E24794A1EAA80BEE4D95185`
- v8 bootloader, intended current bootloader because it fixes DFU upload/readback callback handling:
  `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\boards\VK_RA4M2\examples\arduino\build\arduino-renesas-bootloader-v8-dfuse-uploadfix-pid0369.bin`
  SHA256:
  `8185CADC0D503721D71D632C948306773C8C953F513C74BEA5E3806BE95A6950`
- v8 source fix was outside the MicroPython tree:
  `C:\msys_64\home\teodor\arduino_ra4m2\bootloader\arduino-renesas-bootloader\src\main.c`
- v8 fix summary:
  `tud_dfu_upload_cb()` handles DfuSe block numbering with `block_num >= 2 ? block_num - 2 : block_num` and `_offset`.
- Last attempted bootloader flash was not confirmed successful. A hanging programmer process was stopped.

## J-Link Bootloader Flash Recipe

- Use this only when the user explicitly asks to write the bootloader.
- Do not use J-Link for Arduino sketch validation.
- Current intended bootloader artifact is v8:
  `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\boards\VK_RA4M2\examples\arduino\build\arduino-renesas-bootloader-v8-dfuse-uploadfix-pid0369.bin`
- Verify this file before flashing:
  `SHA256 8185CADC0D503721D71D632C948306773C8C953F513C74BEA5E3806BE95A6950`
- Existing J-Link commander script:
  `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\boards\VK_RA4M2\examples\arduino\flash_bootloader_vk_ra4m2.jlink`
- Script content currently expected:
  ```text
  r
  h
  loadbin C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\boards\VK_RA4M2\examples\arduino\build\arduino-renesas-bootloader-v8-dfuse-uploadfix-pid0369.bin 0x00000000
  verifybin C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\boards\VK_RA4M2\examples\arduino\build\arduino-renesas-bootloader-v8-dfuse-uploadfix-pid0369.bin 0x00000000
  mem32 0x00000000 8
  mem32 0x00010000 8
  r
  g
  q
  ```
- Canonical command shape:
  ```powershell
  & 'C:\Program Files\SEGGER\JLink\JLink.exe' -device R7FA4M2AD -if SWD -speed 4000 -autoconnect 1 -CommanderScript 'C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\boards\VK_RA4M2\examples\arduino\flash_bootloader_vk_ra4m2.jlink'
  ```
- Required success criteria:
  - J-Link command exits normally.
  - `verifybin` reports success.
  - No hanging `JLink`, `JFlash`, `JRun`, or `JMem` process remains.
  - After reset, USB state is checked with `dfu-util -l`.
- Important warning:
  - A previous attempt with this flow hung and was stopped.
  - Therefore, before retrying, run a short connection-only diagnostic and capture separate stdout/stderr logs.
  - Do not repeatedly retry speculative J-Link commands.

## FSP / RAM / Stack

- FSP was generated in the MicroPython RA4M2 port and used as a source/reference for Arduino support.
- Main stack was reduced to avoid overlap:
  `BSP_CFG_STACK_MAIN_BYTES` changed from `0x2000` to `0x1000`.
- Rebuilt `libfsp.a` hash:
  `C640434A200FA5489400E9BD1274BD8CF14323347F40A300A3B8447A2C79F3DC`

## Known Working Arduino Upload Flow

- Explicit FQBN upload worked before:
  `arduino-cli upload -v -b arduino:vkra4m2:vk_ra4m2 -p COM43 -i <bin>`
- Full compile plus upload worked before:
  `arduino-cli compile -v -u -p COM43 -b arduino:vkra4m2:vk_ra4m2 --build-path <build_path> <sketch_path>`
- Smoke blink size previously:
  Flash about `39348 bytes`
  RAM about `5516 bytes`
- Current observed `COM6` is not the board. It is Intel AMT SOL.
- DFU runtime has been observed as:
  `[1209:0001] DFU-RT Port`

## LED And UART Variant Facts

- `LED_BUILTIN` is `PIN_LED`, pin index `27`.
- Pin index `27` maps to `BSP_IO_PORT_02_PIN_04` / `P204`.
- LED is active-low on VK_RA4M2:
  - `LOW` = on
  - `HIGH` = off
- `Serial` in normal USB build maps to `SerialUSB`.
- `Serial1` maps to `_UART1_`, but the object selects the actual SCI channel from TX/RX pin capabilities at `begin()`.
- D0/D1:
  - `D0` / pin index `0` = `P301`
  - `D1` / pin index `1` = `P302`
- Pinmux says:
  - `P302` = SCI channel 2 TX
  - `P301` = SCI channel 2 RX
- Therefore `Serial1.begin()` on D1/D0 should configure real hardware SCI2 UART.

## LED + UART Test Sketch

- Test sketch:
  `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\boards\VK_RA4M2\examples\arduino\arduino_core_ra4m2\led_uart_test\led_uart_test.ino`
- Purpose:
  - Blink active-low LED.
  - Print ticks to USB CDC through `Serial`.
  - Print ticks to hardware UART through `Serial1`.
  - Bridge USB CDC input to hardware UART and UART input back to USB CDC.
- Important distinction:
  - Arduino IDE Serial Monitor talks to `Serial` over USB CDC.
  - Real MCU UART is `Serial1` on `D1/TX` and `D0/RX`.
  - For direct UART-only testing, use an external USB-UART adapter connected to `D1`, `D0`, and `GND`.
- Compile result:
  Flash `39976 bytes`
  RAM `5528 bytes`

## Current Status Snapshot

- Board has been seen as DFU runtime `[1209:0001]`.
- No valid VK_RA4M2 USB CDC COM port was visible at the last check.
- `COM6` must not be treated as the board.
- v8 bootloader exists and should be the current candidate if bootloader flashing is requested.
- Sketch-level testing should proceed through Arduino IDE/CLI upload over DFU, then observe whether USB CDC COM enumerates.

## Next Practical Steps

1. Confirm whether the board is in DFU runtime with `dfu-util -l`.
2. Upload `led_uart_test` through Arduino IDE or Arduino CLI using FQBN `arduino:vkra4m2:vk_ra4m2`.
3. After upload, check for a new USB CDC COM port.
4. If USB CDC appears, use Arduino IDE Serial Monitor at `115200`.
5. For direct UART validation, connect USB-UART adapter:
   - adapter RX to `D1/TX`
   - adapter TX to `D0/RX`
   - adapter GND to board GND
   - terminal baud `115200`
