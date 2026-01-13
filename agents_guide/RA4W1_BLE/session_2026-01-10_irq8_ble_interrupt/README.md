# Session: 2026-01-10 - RA4W1 BLE ICU IRQ8 verification

## Goal
Verify whether RA4W1 BLE middleware requires ICU IRQ8 and whether the current EK_RA4W1 Smart Configurator output allocates it.

## Key Findings (confirmed by code + library inspection)
1. **RA4W1 reserves ICU IRQ8 for BLE middleware**
   - In FSP RA4W1 ELC event list:
     - `ELC_EVENT_ICU_IRQ8` comment: "Interrupt for BLE middleware use only"
     - File: `lib/fsp/ra/fsp/src/bsp/mcu/ra4w1/bsp_elc.h`

2. **EK_RA4W1 generated vectors do NOT allocate IRQ8**
   - `ports/renesas-ra/boards/EK_RA4W1/ra_gen/vector_data.c` includes:
     - vectors + event links for `EVENT_ICU_IRQ0..4` only
     - no `EVENT_ICU_IRQ8` entry

3. **EK_RA4W1 configuration.xml has external IRQ modules only for channels 0..4**
   - `ports/renesas-ra/boards/EK_RA4W1/configuration.xml` contains `module.driver.external_irq_on_icu` modules:
     - `g_external_irq0` (channel 0)
     - `g_external_irq1` (channel 1)
     - `g_external_irq2` (channel 2)
     - `g_external_irq3` (channel 3)
     - `g_external_irq4` (channel 4)
   - No external IRQ instance for channel 8.

4. **The prebuilt BLE stack library actually uses ICU + requires an external IRQ instance**
   - `libr_ble.a` contains object `rf_icu.o` with exported function:
     - `r_ble_icu_intout_interrupt`
   - `libr_ble.a` has undefined reference(s) to:
     - `g_ble_external_irq`
   - This indicates the middleware expects the platform to provide an FSP `external_irq_instance_t` (or compatible pointer) for the BLE RF interrupt path.

5. **Our current port provides only a stub for `g_ble_external_irq`**
   - `ports/renesas-ra/ble/ra_ble_config.c`:
     - `void *g_ble_external_irq = NULL;` (explicitly marked as stub)

## Conclusion
The statement is **correct in substance**:
- On RA4W1, **ICU IRQ8 is explicitly reserved for BLE middleware**, and our current EK_RA4W1 generated FSP config **does not allocate/route IRQ8**.
- Additionally, the middleware library requires `g_ble_external_irq`, but the port currently provides only a NULL stub, so the BLE interrupt path cannot function.

## Artifacts
- `nm_ble_symbols_isr_irq.txt` - symbol scan output
- `nm_ble_undefined_grep.txt` - undefined-symbol scan output

