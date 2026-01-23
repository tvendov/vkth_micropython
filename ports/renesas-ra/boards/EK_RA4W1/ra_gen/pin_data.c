/* generated pin source file - do not edit */
#include "bsp_api.h"
#include "r_ioport_api.h"

const ioport_pin_cfg_t g_bsp_pin_cfg_data[] =
{
    /* Port 0 - ADC/Analog pins */
    { .pin = BSP_IO_PORT_00_PIN_04, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P004: ADC AN04, IRQ03 */
    { .pin = BSP_IO_PORT_00_PIN_10, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P010: ADC AN05, TS30 */
    { .pin = BSP_IO_PORT_00_PIN_11, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P011: ADC AN06, TS31 */
    { .pin = BSP_IO_PORT_00_PIN_14, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P014: ADC AN09, DAC */
    { .pin = BSP_IO_PORT_00_PIN_15, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P015: ADC AN10, TS28, IRQ07 */

    /* Port 1 - SPI0, SCI9, GPIO */
    { .pin = BSP_IO_PORT_01_PIN_00, .pin_cfg = ((uint32_t)IOPORT_CFG_PERIPHERAL_PIN
        | (uint32_t)IOPORT_PERIPHERAL_SPI) },   /* P100: SPI0 MISO */
    { .pin = BSP_IO_PORT_01_PIN_01, .pin_cfg = ((uint32_t)IOPORT_CFG_PERIPHERAL_PIN
        | (uint32_t)IOPORT_PERIPHERAL_SPI) },   /* P101: SPI0 MOSI */
    { .pin = BSP_IO_PORT_01_PIN_02, .pin_cfg = ((uint32_t)IOPORT_CFG_PERIPHERAL_PIN
        | (uint32_t)IOPORT_PERIPHERAL_SPI) },   /* P102: SPI0 RSPCK */
    { .pin = BSP_IO_PORT_01_PIN_03, .pin_cfg = ((uint32_t)IOPORT_CFG_PERIPHERAL_PIN
        | (uint32_t)IOPORT_PERIPHERAL_SPI) },   /* P103: SPI0 SSL0 */
    { .pin = BSP_IO_PORT_01_PIN_04, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P104: TS13, IRQ01 */
    { .pin = BSP_IO_PORT_01_PIN_05, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P105: TS34, IRQ00 */
    { .pin = BSP_IO_PORT_01_PIN_06, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_OUTPUT
        | (uint32_t)IOPORT_CFG_PORT_OUTPUT_HIGH) },  /* P106: GPIO (LED) */
    { .pin = BSP_IO_PORT_01_PIN_07, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P107: GPIO */
    { .pin = BSP_IO_PORT_01_PIN_08, .pin_cfg = ((uint32_t)IOPORT_CFG_PERIPHERAL_PIN
        | (uint32_t)IOPORT_PERIPHERAL_DEBUG) }, /* P108: SWDIO/TMS */
    { .pin = BSP_IO_PORT_01_PIN_09, .pin_cfg = ((uint32_t)IOPORT_CFG_PERIPHERAL_PIN
        | (uint32_t)IOPORT_PERIPHERAL_SCI1_3_5_7_9) },  /* P109: SCI9 TXD */
    { .pin = BSP_IO_PORT_01_PIN_10, .pin_cfg = ((uint32_t)IOPORT_CFG_PERIPHERAL_PIN
        | (uint32_t)IOPORT_PERIPHERAL_SCI1_3_5_7_9) },  /* P110: SCI9 RXD */
    { .pin = BSP_IO_PORT_01_PIN_11, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P111: TS12, IRQ04 */

    /* Port 2 - CTSU, SCI4 */
    { .pin = BSP_IO_PORT_02_PIN_00, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P200: NMI */
    { .pin = BSP_IO_PORT_02_PIN_01, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P201: MD */
    { .pin = BSP_IO_PORT_02_PIN_04, .pin_cfg = ((uint32_t)IOPORT_CFG_PERIPHERAL_PIN
        | (uint32_t)IOPORT_PERIPHERAL_IIC) },   /* P204: IIC0 SCL, TS00 */
    { .pin = BSP_IO_PORT_02_PIN_05, .pin_cfg = ((uint32_t)IOPORT_CFG_PERIPHERAL_PIN
        | (uint32_t)IOPORT_PERIPHERAL_CTSU) },  /* P205: TSCAP - CTSU capacitor */
    { .pin = BSP_IO_PORT_02_PIN_06, .pin_cfg = ((uint32_t)IOPORT_CFG_PERIPHERAL_PIN
        | (uint32_t)IOPORT_PERIPHERAL_SCI0_2_4_6_8) },  /* P206: SCI4 RXD, TS01 */
    { .pin = BSP_IO_PORT_02_PIN_12, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P212: EXTAL, SCI1 RXD */
    { .pin = BSP_IO_PORT_02_PIN_13, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P213: XTAL, SCI1 TXD */
    { .pin = BSP_IO_PORT_02_PIN_14, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P214: XCOUT */
    { .pin = BSP_IO_PORT_02_PIN_15, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P215: XCIN */

    /* Port 3 - Debug */
    { .pin = BSP_IO_PORT_03_PIN_00, .pin_cfg = ((uint32_t)IOPORT_CFG_PERIPHERAL_PIN
        | (uint32_t)IOPORT_PERIPHERAL_DEBUG) }, /* P300: SWCLK/TCK */

    /* Port 4 - CTSU, GPIO */
    { .pin = BSP_IO_PORT_04_PIN_02, .pin_cfg = ((uint32_t)IOPORT_CFG_IRQ_ENABLE
        | (uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P402: IRQ04, TS18, SW1 */
    { .pin = BSP_IO_PORT_04_PIN_04, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_OUTPUT
        | (uint32_t)IOPORT_CFG_PORT_OUTPUT_HIGH) },  /* P404: GPIO (LED) */
    { .pin = BSP_IO_PORT_04_PIN_07, .pin_cfg = ((uint32_t)IOPORT_CFG_PERIPHERAL_PIN
        | (uint32_t)IOPORT_PERIPHERAL_IIC) },   /* P407: IIC0 SDA, TS03 */
    { .pin = BSP_IO_PORT_04_PIN_09, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P409: GPT5, IRQ06 */
    { .pin = BSP_IO_PORT_04_PIN_14, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P414: GPT0, IRQ09 */

    /* Port 5 - ADC/QSPI */
    { .pin = BSP_IO_PORT_05_PIN_01, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P501: ADC AN17, IRQ11 */

    /* USB pins */
    { .pin = BSP_IO_PORT_09_PIN_14, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P914: USB DP */
    { .pin = BSP_IO_PORT_09_PIN_15, .pin_cfg = ((uint32_t)IOPORT_CFG_PORT_DIRECTION_INPUT) },  /* P915: USB DM */
};

const ioport_cfg_t g_bsp_pin_cfg =
{ .number_of_pins = sizeof(g_bsp_pin_cfg_data) / sizeof(ioport_pin_cfg_t), .p_pin_cfg_data = &g_bsp_pin_cfg_data[0], };

#if BSP_TZ_SECURE_BUILD

void R_BSP_PinCfgSecurityInit(void);

/* Initialize SAR registers for secure pins. */
void R_BSP_PinCfgSecurityInit(void) {
    #if (2U == BSP_FEATURE_IOPORT_VERSION)
    uint32_t pmsar[BSP_FEATURE_BSP_NUM_PMSAR];
    #else
    uint16_t pmsar[BSP_FEATURE_BSP_NUM_PMSAR];
    #endif
    memset(pmsar, 0xFF, BSP_FEATURE_BSP_NUM_PMSAR * sizeof(R_PMISC->PMSAR[0]));


    for (uint32_t i = 0; i < g_bsp_pin_cfg.number_of_pins; i++)
    {
        uint32_t port_pin = g_bsp_pin_cfg.p_pin_cfg_data[i].pin;
        uint32_t port = port_pin >> 8U;
        uint32_t pin = port_pin & 0xFFU;
        pmsar[port] &= (uint16_t) ~(1U << pin);
    }

    for (uint32_t i = 0; i < BSP_FEATURE_BSP_NUM_PMSAR; i++)
    {
        #if (2U == BSP_FEATURE_IOPORT_VERSION)
        R_PMISC->PMSAR[i].PMSAR = (uint16_t)pmsar[i];
        #else
        R_PMISC->PMSAR[i].PMSAR = pmsar[i];
        #endif
    }

}
#endif
