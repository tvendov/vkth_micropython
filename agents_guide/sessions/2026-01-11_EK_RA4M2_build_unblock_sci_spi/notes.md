## EK_RA4M2 build unblock: RA4M2 missing conditionals (SCI + SPI)

### Errors
- `ra/ra_sci.c`: `#error "CMSIS MCU Series is not specified."` (multiple pin tables)
- `ra/ra_spi.c`: `#error "CMSIS MCU Series is not specified."` (MOSI/MISO/SCK/SSL pin tables)

### Root cause
RA4M2 was not included in RA4-family `#if/#elif` blocks in SCI and SPI pin mapping tables.

### Fixes applied
- `ports/renesas-ra/ra/ra_sci.c`
  - Treat `RA4M2` like `RA4M1` for:
    - `ra_sci_tx_pins[]`
    - `ra_sci_rx_pins[]`
    - `ra_sci_cts_pins[]`
- `ports/renesas-ra/ra/ra_spi.c`
  - Treat `RA4M2` like `RA4M1` for:
    - `mosi_pins[]`
    - `miso_pins[]`
    - `sck_pins[]`
    - `ssl_pins[]`

### Next step
Re-run:
- `make BOARD=EK_RA4M2 -j16`

If a new `#error "CMSIS MCU Series is not specified."` appears, paste it and we patch the next missing RA4M2 conditional.
