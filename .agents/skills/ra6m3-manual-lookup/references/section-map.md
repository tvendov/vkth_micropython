# RA6M3 Manual Section Map

Use this map to route searches in `ports/renesas-ra/boards/VK_RA6M3/r01uh0886ej0120-ra6m3.md`. The source is the RA6M3 Group User's Manual: Hardware, document R01UH0886EJ0120, Rev. 1.20.

## Contents

- Search Method
- System And Core
- Timers And Network
- Communications And Interfaces
- Security, Analog, Memory, And Graphics
- Appendices And Project Anchors

## Search Method

- Find a body chapter title with `rg -n '^(#{1,6} )?<chapter>\. <title>$' <manual>`.
- Find an exact register, field, pin, or signal with `rg -n -F -C 8 -- "<identifier>" <manual>`.
- Search both the full register name and abbreviation, such as `Interrupt Request Register` and `IR`.
- Check `Overview`, `Register Descriptions`, `Operation`, `Interrupt Sources`, and `Usage Notes` within the selected chapter.
- Treat early matches containing dot leaders and page numbers as table-of-contents entries.

## System And Core

| Ch. | Subject |
| ---: | --- |
| 1 | Overview |
| 2 | CPU |
| 3 | Operating Modes |
| 4 | Address Space |
| 5 | Memory Mirror Function (MMF) |
| 6 | Resets |
| 7 | Option-Setting Memory |
| 8 | Low Voltage Detection (LVD) |
| 9 | Clock Generation Circuit |
| 10 | Clock Frequency Accuracy Measurement Circuit (CAC) |
| 11 | Low Power Modes |
| 12 | Battery Backup Function |
| 13 | Register Write Protection |
| 14 | Interrupt Controller Unit (ICU) |
| 15 | Buses |
| 16 | Memory Protection Unit (MPU) |
| 17 | DMA Controller (DMAC) |
| 18 | Data Transfer Controller (DTC) |
| 19 | Event Link Controller (ELC) |
| 20 | I/O Ports |

## Timers And Network

| Ch. | Subject |
| ---: | --- |
| 21 | Key Interrupt Function (KINT) |
| 22 | Port Output Enable for GPT (POEG) |
| 23 | General PWM Timer (GPT) |
| 24 | PWM Delay Generation Circuit |
| 25 | Low Power Asynchronous General-Purpose Timer (AGT) |
| 26 | Realtime Clock (RTC) |
| 27 | Watchdog Timer (WDT) |
| 28 | Independent Watchdog Timer (IWDT) |
| 29 | Ethernet MAC Controller (ETHERC) |
| 30 | Ethernet PTP Controller (EPTPC) |
| 31 | Ethernet DMA Controller (EDMAC) |

## Communications And Interfaces

| Ch. | Subject |
| ---: | --- |
| 32 | USB 2.0 Full-Speed Module (USBFS) |
| 33 | USB 2.0 High-Speed Module (USBHS) |
| 34 | Serial Communications Interface (SCI) |
| 35 | IrDA Interface |
| 36 | I2C Bus Interface (IIC) |
| 37 | Controller Area Network (CAN) Module |
| 38 | Serial Peripheral Interface (SPI) |
| 39 | Quad Serial Peripheral Interface (QSPI) |
| 40 | Cyclic Redundancy Check (CRC) Calculator |
| 41 | Serial Sound Interface Enhanced (SSIE) |
| 42 | Sampling Rate Converter (SRC) |
| 43 | SD/MMC Host Interface (SDHI) |
| 44 | Parallel Data Capture Unit (PDC) |
| 45 | Boundary Scan |

## Security, Analog, Memory, And Graphics

| Ch. | Subject |
| ---: | --- |
| 46 | Secure Cryptographic Engine (SCE7) |
| 47 | 12-Bit A/D Converter (ADC12) |
| 48 | 12-Bit D/A Converter (DAC12) |
| 49 | Temperature Sensor (TSN) |
| 50 | High-Speed Analog Comparator (ACMPHS) |
| 51 | Capacitive Touch Sensing Unit (CTSU) |
| 52 | Data Operation Circuit (DOC) |
| 53 | SRAM |
| 54 | Standby SRAM |
| 55 | Flash Memory |
| 56 | 2D Drawing Engine (DRW) |
| 57 | JPEG Codec (JPEG) |
| 58 | Graphics LCD Controller (GLCDC) |
| 59 | Internal Voltage Regulator |
| 60 | Electrical Characteristics |

## Appendices And Project Anchors

- Appendix 1: Port States in Each Processing Mode
- Appendix 2: Package Dimensions
- Appendix 3: I/O Registers
- Pin mux: `ra6m3_af.csv`, `pins.csv`, `ra_gen/pin_data.c`, `mpconfigboard.h`
- Clocks: `ra_gen/bsp_clock_cfg.h`, `ra_config.h`
- Interrupt/event routing: `ra_gen/vector_data.*`, chapter 14, chapter 19
- Enabled FSP peripherals: `ra_gen/hal_data.*`, `ra_cfg/`, `RA6M3_hal.h`
- Current MicroPython behavior: `ports/renesas-ra/ra/` and the upper-layer files in `ports/renesas-ra/`
