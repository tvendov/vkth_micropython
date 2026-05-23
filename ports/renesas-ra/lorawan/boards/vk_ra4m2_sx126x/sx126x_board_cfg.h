/*
 * VK_RA4M2 SX126x board-init contract — single source of truth for the
 * struct that mod_lorawan.c (binding layer) hands to sx126x-board.c (board
 * layer).
 */
#ifndef LORAWAN_BOARDS_VK_RA4M2_SX126X_SX126X_BOARD_CFG_H
#define LORAWAN_BOARDS_VK_RA4M2_SX126X_SX126X_BOARD_CFG_H

#include "py/obj.h"

typedef struct {
    uint8_t  spi_bus;
    uint32_t spi_baud_hz;
    mp_obj_t spi_obj;
    void    *cs_pin;
    void    *rst_pin;
    void    *gpio_busy_pin;
    void    *irq_pin;
    void    *rf_sw_pin;
} sx126x_board_cfg_t;

void sx126x_board_init(const sx126x_board_cfg_t *cfg);
void sx126x_board_deinit(void);

#endif /* LORAWAN_BOARDS_VK_RA4M2_SX126X_SX126X_BOARD_CFG_H */
