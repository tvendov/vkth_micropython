// MCU config
#define MICROPY_HW_BOARD_NAME       "RA4M1 CLICKER"
#define MICROPY_HW_MCU_NAME         "RA4M1"
#define MICROPY_HW_MCU_SYSCLK       48000000
#define MICROPY_HW_MCU_PCLK         48000000

// module config
#define MICROPY_EMIT_THUMB          (0)
#define MICROPY_EMIT_INLINE_THUMB   (0)
#define MICROPY_PY_BUILTINS_COMPLEX (0)
#define MICROPY_PY_GENERATOR_PEND_THROW (0)
#define MICROPY_PY_MATH             (0)
#define MICROPY_PY_HEAPQ            (0)
#define MICROPY_PY_THREAD           (0)

// peripheral config
#define MICROPY_HW_ENABLE_RTC       (1)
#define MICROPY_HW_RTC_SOURCE       (0)         // 0: subclock, 1:LOCO
#define MICROPY_HW_ENABLE_ADC       (1)
#define MICROPY_HW_ENABLE_DAC       (1)
#define MICROPY_HW_ENABLE_OPAMP     (1)
#define MICROPY_HW_ENABLE_COMPARATOR (1)
#define MICROPY_HW_HAS_FLASH        (1)
#define MICROPY_HW_ENABLE_INTERNAL_FLASH_STORAGE (1)

// board config
// DAC
#define MICROPY_HW_DAC0             (pin_P014) // A4

// UART
#define MICROPY_HW_UART0_TX         (pin_P411) // MBTX0 - REPL
#define MICROPY_HW_UART0_RX         (pin_P410) // MBRX0 - REPL
// #define MICROPY_HW_UART0_CTS     (pin_P103) // Disable (Conflict with SSLA0)
//#define MICROPY_HW_UART1_TX         (pin_P401) // mikroBUS
//#define MICROPY_HW_UART1_RX         (pin_P402) // mikroBUS
#define MICROPY_HW_UART2_TX         (pin_P302) // mikroBUS
#define MICROPY_HW_UART2_RX         (pin_P301) // mikroBUS
#define MICROPY_HW_UART_REPL        HW_UART_0
#define MICROPY_HW_UART_REPL_BAUD   115200

// I2C
#define MICROPY_HW_I2C0_SCL         (pin_P400) // mikroBUS
#define MICROPY_HW_I2C0_SDA         (pin_P401) // mikroBUS
#define MICROPY_HW_I2C1_SCL         (pin_P100) // Available pins
#define MICROPY_HW_I2C1_SDA         (pin_P101) // Available pins

// SPI
// SPI0 (P100/P101/P102/P103) conflicts with I2C1, so use SPI1 instead
#define MICROPY_HW_SPI1_SSL         (pin_P112) // mikroBUS
#define MICROPY_HW_SPI1_RSPCK       (pin_P111) // mikroBUS
#define MICROPY_HW_SPI1_MISO        (pin_P110) // mikroBUS
#define MICROPY_HW_SPI1_MOSI        (pin_P109) // mikroBUS

// PWM (GPT)
// GPT0
#define MICROPY_HW_PWM_0A           (pin_P107) // GPT0_A (MBPWM)
#define MICROPY_HW_PWM_0B           (pin_P106) // GPT0_B

// GPT1
#define MICROPY_HW_PWM_1A           (pin_P105) // GPT1_A
#define MICROPY_HW_PWM_1B           (pin_P104) // GPT1_B

// GPT2
#define MICROPY_HW_PWM_2A           (pin_P103) // GPT2_A (MBSSL)
#define MICROPY_HW_PWM_2B           (pin_P102) // GPT2_B (MBSCK)

// GPT3
#define MICROPY_HW_PWM_3A           (pin_P111) // GPT3_A (SPI1_SCK)
#define MICROPY_HW_PWM_3B           (pin_P112) // GPT3_B (SPI1_SSL)

// GPT4
#define MICROPY_HW_PWM_4A           (pin_P205) // GPT4_A (MBSCLI / I2C alt)
#define MICROPY_HW_PWM_4B           (pin_P301) // GPT4_B (UART2_RX / SW2)

// GPT5
#define MICROPY_HW_PWM_5A           (pin_P409) // GPT5_A (LED1)
#define MICROPY_HW_PWM_5B           (pin_P408) // GPT5_B (LED2)

// GPT6
#define MICROPY_HW_PWM_6A           (pin_P400) // GPT6_A (I2C0_SCL)
#define MICROPY_HW_PWM_6B           (pin_P401) // GPT6_B (I2C0_SDA)

// GPT7
#define MICROPY_HW_PWM_7A           (pin_P304) // GPT7_A (USR button / IRQ9)
#define MICROPY_HW_PWM_7B           (pin_P303) // GPT7_B

// Switch
#define MICROPY_HW_HAS_SWITCH       (1)
#define MICROPY_HW_USRSW_PIN        (pin_P304)
#define MICROPY_HW_USRSW_PULL       (MP_HAL_PIN_PULL_NONE)
#define MICROPY_HW_USRSW_EXTI_MODE  (MP_HAL_PIN_TRIGGER_FALLING)
#define MICROPY_HW_USRSW_PRESSED    (0)

// LEDs
#define MICROPY_HW_LED1             (pin_P409)
#define MICROPY_HW_LED2             (pin_P408)
#define MICROPY_HW_LED_ON(pin)      mp_hal_pin_high(pin)
#define MICROPY_HW_LED_OFF(pin)     mp_hal_pin_low(pin)
#define MICROPY_HW_LED_TOGGLE(pin)  mp_hal_pin_toggle(pin)
