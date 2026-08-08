/*
 * TinyUSB board definition for VK_RA4M2.
 *
 * Copy this directory to:
 *   tinyusb/hw/bsp/ra/boards/vk_ra4m2
 */

#ifndef _BOARD_H_
#define _BOARD_H_

#ifdef __cplusplus
extern "C" {
#endif

#define LED1                BSP_IO_PORT_02_PIN_04
#define LED_STATE_ON        1

#define SW1                 BSP_IO_PORT_04_PIN_00
#define BUTTON_STATE_ACTIVE 0

static const ioport_pin_cfg_t board_pin_cfg[] = {
    { .pin = LED1, .pin_cfg = IOPORT_CFG_PORT_DIRECTION_OUTPUT },
    { .pin = SW1,  .pin_cfg = IOPORT_CFG_PORT_DIRECTION_INPUT },

    /* USB FS: VBUS=P407, D+=P914, D-=P915. */
    { .pin = BSP_IO_PORT_04_PIN_07, .pin_cfg = IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_USB_FS },
    { .pin = BSP_IO_PORT_09_PIN_14, .pin_cfg = IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_USB_FS },
    { .pin = BSP_IO_PORT_09_PIN_15, .pin_cfg = IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_USB_FS },
};

#ifdef __cplusplus
}
#endif

#endif
