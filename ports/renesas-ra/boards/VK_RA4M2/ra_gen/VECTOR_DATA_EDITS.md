# VK_RA4M2 vector_data.{c,h} — manual edits

Tracker for non-RASC modifications applied to `vector_data.c` and
`vector_data.h`. If/when RASC regenerates these files, re-apply each
hunk below or routine LoRaWAN/AGT bring-up will silently regress.

## Why edits exist

The port owns the AGT IRQ vectors for `Timer(1..4)` (channels 0..3,
slots 46-49, 52-59). The LoRaWAN renesas stack reserves AGT4/AGT5
(channels 4/5, slots 50/51 cycle-end + slot 62 compare-A) for its
free-run + sub-second timer board. Both must coexist in the same
firmware image without symbol collision.

Resolution: split the AGT vector table along
`MICROPY_HW_LORA_STACK_RENESAS`.

## Hunks

### 1. ISR prototypes (after `ctsu_end_isr` decl)

```c
void ra_port_agt_int_isr(void);
#if defined(MICROPY_HW_LORA_STACK_RENESAS) && (MICROPY_HW_LORA_STACK_RENESAS == 1)
void agt_int_isr(void);
void agt_comp_int_isr(void);
#endif
```

Applied identically in `vector_data.c` (~L29) and `vector_data.h`
(~L36).

### 2. AGT slots 46-63 (in `vector_data.c` g_vector_table)

Replace the contiguous block:

```c
    [46] = agt_int_isr, /* AGT0 INT */
    ...
    [63] = agt_int_isr, /* AGT5 COMPAREB */
```

with the dual-arm `#if defined(MICROPY_HW_LORA_STACK_RENESAS) ...
#else ... #endif` block. See current file for canonical body.

Routing summary:

| Slot | Vector              | Non-LoRaWAN flavour     | LoRaWAN flavour          |
|------|---------------------|-------------------------|--------------------------|
| 46   | AGT0_INT            | ra_port_agt_int_isr     | ra_port_agt_int_isr      |
| 47   | AGT1_INT            | ra_port_agt_int_isr     | ra_port_agt_int_isr      |
| 48   | AGT2_INT            | ra_port_agt_int_isr     | ra_port_agt_int_isr      |
| 49   | AGT3_INT            | ra_port_agt_int_isr     | ra_port_agt_int_isr      |
| 50   | AGT4_INT            | ra_port_agt_int_isr     | agt_int_isr (FSP r_agt)  |
| 51   | AGT5_INT            | ra_port_agt_int_isr     | agt_int_isr (FSP r_agt)  |
| 52-59| AGT0..3 COMPARE_A/B | ra_port_agt_int_isr     | ra_port_agt_int_isr      |
| 60   | AGT4_COMPARE_A      | ra_port_agt_int_isr     | ra_port_agt_int_isr      |
| 61   | AGT4_COMPARE_B      | ra_port_agt_int_isr     | ra_port_agt_int_isr      |
| 62   | AGT5_COMPARE_A      | ra_port_agt_int_isr     | agt_comp_int_isr (vendor)|
| 63   | AGT5_COMPARE_B      | ra_port_agt_int_isr     | ra_port_agt_int_isr      |

## Cross-references

- Symbol rename rationale: `ports/renesas-ra/ra/ra_timer.c` —
  `ra_port_agt_int_isr` is the renamed port handler; the legacy
  `agt_int_isr` is a compat shim under `#if !MICROPY_HW_LORA_STACK_RENESAS`
  so the 7 other boards' generated vector tables still bind without
  per-board edits.
- LoRaWAN side: `lorawan/boards/vk_ra4m2_sx126x/lorawan_hal_data.{c,h}`
  defines `g_timer0_*` (AGT4) and `g_timer1_*` (AGT5); the pristine
  vendor `lorawan/boards/ra2l1ek_sx126x/timer-board.c` consumes them
  via `R_AGT_Open`. Compare-A IRQ (slot 62) is enabled at runtime by
  `RpMcuResourceTimerStart` and its body is the vendor's
  `agt_comp_int_isr`.
- BSP coupling: `BSP_CLOCK_CFG_SUBCLOCK_POPULATED` MUST stay `1` in
  `ra_cfg/fsp_cfg/bsp/bsp_cfg.h` — without SOSC, AGT4/AGT5 lose their
  clock and the LoRaWAN timer state machine never advances past
  `RP_MCU_TIMER_STATE_INIT`.
