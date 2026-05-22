// MCU config
#define MICROPY_HW_BOARD_NAME       "VK_RA4M2"
#define MICROPY_HW_MCU_NAME         "RA4M2"
#define MICROPY_HW_MCU_SYSCLK       100000000  // ICLK: XTAL 16MHz /2 *25 /2 = 100MHz (PLL=200MHz)
#define MICROPY_HW_MCU_PCLK         50000000   // PCLKB: PLL 200MHz /4 = 50MHz

// Stage G direct-C-path compile-time gates (r16 design В§5).
// Default OFF вЂ” preserves r14 scheduler-trampoline behaviour bit-for-bit.
// Override via CFLAGS_EXTRA=-DMICROPY_HW_LORA_DIRECT_C_PATH=1 for HIL bring-up.
// Sub-gates collapse to the master switch in G.1; G.2/G.3 may set them
// independently for bisection.
#ifndef MICROPY_HW_LORA_DIRECT_C_PATH
#define MICROPY_HW_LORA_DIRECT_C_PATH         (0)
#endif
#ifndef MICROPY_HW_LORA_DIRECT_C_PATH_RX1
#define MICROPY_HW_LORA_DIRECT_C_PATH_RX1     (MICROPY_HW_LORA_DIRECT_C_PATH)
#endif
#ifndef MICROPY_HW_LORA_DIRECT_C_PATH_RX2
#define MICROPY_HW_LORA_DIRECT_C_PATH_RX2     (MICROPY_HW_LORA_DIRECT_C_PATH)
#endif
#ifndef MICROPY_HW_LORA_DIRECT_C_PATH_DIO1
#define MICROPY_HW_LORA_DIRECT_C_PATH_DIO1    (MICROPY_HW_LORA_DIRECT_C_PATH)
#endif

// LoRaWAN receive timing uses the active AGT backend.

// module config
// #define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_BASIC_FEATURES)
// #define MICROPY_ENABLE_FINALISER    (1)

#define MICROPY_EMIT_THUMB          (1)
#define MICROPY_EMIT_INLINE_THUMB   (1)
#define MICROPY_PY_BUILTINS_COMPLEX (1)
// AES Р·Р° LoRaWAN MIC (CMAC) Рё payload encryption (CTR).
// MICROPY_PY_CRYPTOLIB / _CTR / MICROPY_SSL_AXTLS СЃРµ РїРѕРґР°РІР°С‚ РѕС‚ Makefile (-D)
// РїРѕРґ РµРґРёРЅРЅРёСЏ РїСЂРµРІРєР»СЋС‡РІР°С‚РµР» MICROPY_HW_ENABLE_LORA РІ mpconfigboard.mk.
#define MICROPY_PY_GENERATOR_PEND_THROW (1)
#define MICROPY_PY_MATH             (1)
#define MICROPY_PY_HEAPQ            (1)
#define MICROPY_PY_THREAD           (0) // disable ARM_THUMB_FP using vldr due to RA has single float only
#define MICROPY_HW_MAX_TIMER        (6)
#define MICROPY_HW_MACHINE_TIMER_HARDWARE (1)

// peripheral config
#define MICROPY_HW_ENABLE_RNG       (1)     // SCE9 hardware RNG + AES (LoRaWAN)
#define MICROPY_HW_ENABLE_RTC       (1)
#define MICROPY_HW_RTC_SOURCE       (0)     // 0: subclock (SOSC 32.768 kHz crystal), 1: LOCO (RC ~32 kHz)
// SOSC 32.768 kHz crystal physically populated on P214/P215. Gates run-time
// source='sosc' selection on machine.RTC() and machine.Timer() (see machine_rtc.c,
// timer.c). Must match BSP_CLOCK_CFG_SUBCLOCK_POPULATED in ra_cfg/fsp_cfg/bsp/bsp_cfg.h.
#define MICROPY_HW_SUBCLK_POPULATED (1)
#define MICROPY_HW_ENABLE_ADC       (1)
#ifndef MICROPY_HW_ENABLE_DAC
#define MICROPY_HW_ENABLE_DAC       (1)
#endif
#ifndef MICROPY_HW_ENABLE_TOUCHPAD
#define MICROPY_HW_ENABLE_TOUCHPAD  (1)
#endif
// MICROPY_HW_ENABLE_DSP СЃРµ РґРµС„РёРЅРёСЂР° РѕС‚ Makefile (-D) РЅР° Р±Р°Р·Р°
// MICROPY_HW_ENABLE_DSP РІ mpconfigboard.mk. MРїСЂРµ-РґРµС„РёРЅРёСЂР°Р№ С‚СѓРє вЂ” РµРґРёРЅРµРЅ РїСЂРµРІРєР»СЋС‡РІР°С‚РµР».
#define MICROPY_HW_HAS_FLASH        (1)
#define MICROPY_HW_ENABLE_USBDEV    (1)
#define MICROPY_HW_USB_CDC          (1)
#define MICROPY_HW_ENABLE_UART_REPL (0)
#define MICROPY_HW_ENABLE_INTERNAL_FLASH_STORAGE (1)

// UART
#define MICROPY_HW_UART2_TX         (pin_P302)
#define MICROPY_HW_UART2_RX         (pin_P301)

// UART
#define MICROPY_HW_UART0_TX         (pin_P411) // REPL
#define MICROPY_HW_UART0_RX         (pin_P410) // REPL
// #define MICROPY_HW_UART0_CTS      (pin_P413) // NC
// #define MICROPY_HW_UART1_TX       (pin_P709) // Disable (read not work properly)
// #define MICROPY_HW_UART1_RX       (pin_P708) // Disable (read not work properly)
// #define MICROPY_HW_UART1_CTS      (pin_P711) // Disable (read not work properly)
// #define MICROPY_HW_UART2_TX       (pin_P302) // Disable
// #define MICROPY_HW_UART2_RX       (pin_P301) // Disable
// #define MICROPY_HW_UART2_CTS      (pin_P203) // NC
// #define MICROPY_HW_UART3_TX       (pin_P310) // Disable
// #define MICROPY_HW_UART3_RX       (pin_P309) // Disable
// #define MICROPY_HW_UART3_CTS      (pin_P312) // Disable
#define MICROPY_HW_UART4_TX         (pin_P205) // BLE DA14531 (board-wired, not user-available)
#define MICROPY_HW_UART4_RX         (pin_P206) // BLE DA14531 (board-wired, not user-available)
#define MICROPY_HW_UART4_RTS        (pin_P401) // BLE DA14531 RTS / shares pin with I2C0 SDA and UART7 TX
#define MICROPY_HW_UART4_CTS        (pin_P402) // BLE DA14531 CTS / shares pin with UART7 RX
// #define MICROPY_HW_UART5_TX       (pin_P501) // Disable
// #define MICROPY_HW_UART5_RX       (pin_P502) // Disable
// #define MICROPY_HW_UART5_CTS      (pin_P504) // Disable
// #define MICROPY_HW_UART6_TX       (pin_P506) // Disable (read not work properly)
// #define MICROPY_HW_UART6_RX       (pin_P505) // Disable (read not work properly)
// #define MICROPY_HW_UART6_CTS      (pin_P503) // Disable (read not work properly)
#define MICROPY_HW_UART7_TX         (pin_P401) // PMOD B
#define MICROPY_HW_UART7_RX         (pin_P402) // PMOD B
#define MICROPY_HW_UART7_CTS        (pin_P403) // PMOD B
// #define MICROPY_HW_UART8_TX       (pin_P105) // Disable (conflict with USER SW1)
// #define MICROPY_HW_UART8_RX       (pin_P104)
#define MICROPY_HW_UART9_TX         (pin_P602)
#define MICROPY_HW_UART9_RX         (pin_P601)
#define MICROPY_HW_UART9_CTS        (pin_P603)
// #define MICROPY_HW_UART_REPL        HW_UART_0
// #define MICROPY_HW_UART_REPL_BAUD   115200

// I2C
#define MICROPY_HW_I2C0_SCL         (pin_P400)
#define MICROPY_HW_I2C0_SDA         (pin_P401)

// I2C1 вЂ” shares P205/P206 with UART4 and BLE DA14531 (cannot use both at once)
// RA4M2 datasheet Table 19.23: P205=SCL1_B, P206=SDA1_B, PSEL=00111b
#define MICROPY_HW_I2C1_SCL         (pin_P205)
#define MICROPY_HW_I2C1_SDA         (pin_P206)

// I2CTarget: IIC0 and IIC1 interrupt vectors are registered (IRQ 32-39).
#define MICROPY_PY_MACHINE_I2C_TARGET_MAX   (2)

// I2C0 alternate pins: P100/P101 = SCL0_A/SDA0_A per RA4M2 datasheet Table 19.24.
// Default I2C0 is P400/P401. User can select P100/P101 at runtime:
//   I2C(0, scl=Pin("P100"), sda=Pin("P101"))
// Note: P100/P101 are IIC0 (NOT IIC1)!

// SPI
#define MICROPY_HW_SPI0_SSL         (pin_P103)
#define MICROPY_HW_SPI0_RSPCK       (pin_P102)
#define MICROPY_HW_SPI0_MISO        (pin_P100)
#define MICROPY_HW_SPI0_MOSI        (pin_P101)

// SPI1
// Notes:
// - We intentionally use the SPI1 "B" pin set that exists in this board's pins.csv:
//     MOSI=P109, MISO=P110, SCK=P111
// - We avoid P205/P206 here because CTSU uses them as TS01/TS02 touch inputs.
// - CS/SSL must be one of the RA SSL-capable pins (see ports/renesas-ra/ra/ra_spi.c ssl_pins[]).
#define MICROPY_HW_SPI1_SSL         (pin_P108)
#define MICROPY_HW_SPI1_RSPCK       (pin_P111)
#define MICROPY_HW_SPI1_MISO        (pin_P110)
#define MICROPY_HW_SPI1_MOSI        (pin_P109)

// SPI2 via SCI2 simple SPI.
// Notes:
// - This is a separate backend from the dedicated SPI/RSPI peripheral.
// - Current port support is master-only, 8-bit, blocking.
// - SCI2 shares the hardware block with UART(2), so they cannot be used at the same time.
#define MICROPY_HW_SPI2_SCK         (pin_P111)
#define MICROPY_HW_SPI2_MOSI        (pin_P112)
#define MICROPY_HW_SPI2_MISO        (pin_P113)
#define MICROPY_HW_SPI2_SCI_CH      (2)
#define MICROPY_HW_SPI2_SCI_AF      AF_SCI1   // SCI2 PSEL=00100b on these pins

// SPI3 via SCI9 simple SPI (RA4M2 datasheet Table 19.18, PSEL=00101b).
// Notes:
// - Master-only, 8-bit, blocking вЂ” same backend as SPI2.
// - Shares the SCI9 hardware block with UART9 (mutual exclusion via SCI owner).
// - Pins overlap with SPI1 (RSPI1) on P109/P110/P111. Cannot use SPI1 and SPI3
//   simultaneously; deinit one before constructing the other.
#define MICROPY_HW_SPI3_SCK         (pin_P111)  // SCK9
#define MICROPY_HW_SPI3_MOSI        (pin_P109)  // TXD9/MOSI9
#define MICROPY_HW_SPI3_MISO        (pin_P110)  // RXD9/MISO9
#define MICROPY_HW_SPI3_SCI_CH      (9)
#define MICROPY_HW_SPI3_SCI_AF      AF_SCI2    // SCI9 PSEL=00101b

// WS2812 over SCI TX-only.
// Notes:
// - P112 is the default data output for WS2812(...), but any valid SCI TX/MOSI pin can be selected at runtime.
// - This intentionally does not use the external SCK/MISO pins.
// - Channel ownership is enforced, so WS2812 cannot share a given SCI block with UART/SPI at the same time.
#define MICROPY_HW_WS2812_SCI_CH    (2)
#define MICROPY_HW_WS2812_DATA      (pin_P112)

// DAC
// RA4M2 DAC output pins.
// Note: this port exposes DA0 and DA1 when enabled.
#define MICROPY_HW_DAC0             (pin_P014)
#define MICROPY_HW_DAC1             (pin_P015)

// Quadrature Encoder (GPT Phase Counting)
// Uses GPT4: P302=GTIOC4A (encoder A), P301=GTIOC4B (encoder B)
// Board-edge pins. Note: shares pins with UART2 вЂ” do not use both simultaneously.
#define MICROPY_HW_ENCODER_A        (pin_P302) // GTIOC4A
#define MICROPY_HW_ENCODER_B        (pin_P301) // GTIOC4B

// PWM (GPT)
// RA4M2 has GPT0-7 (GPT0-3: 32-bit, GPT4-7: 16-bit).
// Pin selection avoids conflicts with UART0/2/7/9, I2C0/1, SPI0/1, DAC, TouchPad, LED, SW.
// GPT3A and GPT6A have no conflict-free pins available.
#define MICROPY_HW_PWM_0A           (pin_P107) // GTIOC0A
#define MICROPY_HW_PWM_0B           (pin_P106) // GTIOC0B
#define MICROPY_HW_PWM_1A           (pin_P105) // GTIOC1A
#define MICROPY_HW_PWM_1B           (pin_P104) // GTIOC1B
#define MICROPY_HW_PWM_2A           (pin_P113) // GTIOC2A
#define MICROPY_HW_PWM_2B           (pin_P114) // GTIOC2B
// GPT3A: no conflict-free pin (P111=SPI1, P403=UART7)
#define MICROPY_HW_PWM_3B           (pin_P112) // GTIOC3B
#define MICROPY_HW_PWM_4A           (pin_P115) // GTIOC4A
#define MICROPY_HW_PWM_4B           (pin_P608) // GTIOC4B (P204=LED, P301=UART2)
#define MICROPY_HW_PWM_5A           (pin_P409) // GTIOC5A
#define MICROPY_HW_PWM_5B           (pin_P408) // GTIOC5B
// GPT6A: no conflict-free pin (P400=I2C0, P411=UART0, P601=UART9)
#define MICROPY_HW_PWM_6B           (pin_P600) // GTIOC6B (P401=I2C0, P410=UART0)
#define MICROPY_HW_PWM_7A           (pin_P304) // GTIOC7A (P603=UART9)
#define MICROPY_HW_PWM_7B           (pin_P303) // GTIOC7B (P602=UART9)

// Switch
#define MICROPY_HW_HAS_SWITCH       (1)
#define MICROPY_HW_USRSW_PIN        (pin_P400)
#define MICROPY_HW_USRSW_PULL       (MP_HAL_PIN_PULL_UP)
#define MICROPY_HW_USRSW_EXTI_MODE  (MP_HAL_PIN_TRIGGER_FALLING)
#define MICROPY_HW_USRSW_PRESSED    (0)

// LEDs
#define MICROPY_HW_LED1             (pin_P204)
#define MICROPY_HW_LED_ON(pin)      mp_hal_pin_low(pin)
#define MICROPY_HW_LED_OFF(pin)     mp_hal_pin_high(pin)
#define MICROPY_HW_LED_TOGGLE(pin)  mp_hal_pin_toggle(pin)
