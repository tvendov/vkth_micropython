/*!
* \file      spi-board.c
*
* \brief     Target board SPI driver implementation
*
* \copyright Revised BSD License, see section \ref LICENSE.
*
* \code
*                ______                              _
*               / _____)             _              | |
*              ( (____  _____ ____ _| |_ _____  ____| |__
*               \____ \| ___ |    (_   _) ___ |/ ___)  _ \
*               _____) ) ____| | | || |_| ____( (___| | | |
*              (______/|_____)_|_|_| \__)_____)\____)_| |_|
*              (C)2013-2017 Semtech
*
* \endcode
*
* \author    Miguel Luis ( Semtech )
*
* \author    Gregory Cristian ( Semtech )
*/
/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/
#include "utilities.h"
#include "board.h"
#include "gpio.h"
#include "spi-board.h"

static volatile bool g_spi_transfer_complete = false;

void SpiInit( Spi_t *obj, SpiId_t spiId, PinNames mosi, PinNames miso, PinNames sclk, PinNames nss )
{
    R_SCI_SPI_Open(&g_spi0_ctrl, &g_spi0_cfg);
}

void SpiDeInit( Spi_t *obj )
{
    R_SCI_SPI_Close(&g_spi0_ctrl);
}

void SpiFormat( Spi_t *obj, int8_t bits, int8_t cpol, int8_t cpha, int8_t slave )
{
}

void SpiFrequency( Spi_t *obj, uint32_t hz )
{
}

uint16_t SpiInOut( Spi_t *obj, uint16_t outData )
{
    uint8_t rxBuf = 0x00;
    uint8_t txBuf = (uint8_t)outData;
    uint32_t timeout = 0x20000; // tentative

    g_spi_transfer_complete = false;
    R_SCI_SPI_WriteRead(&g_spi0_ctrl, &txBuf, &rxBuf, sizeof(txBuf), SPI_BIT_WIDTH_8_BITS);
    while ((false == g_spi_transfer_complete) && (--timeout != 0U)) {
    }

    return ( (uint16_t)rxBuf );
}

void sci_spi_callback(spi_callback_args_t *p_args)
{
    if (SPI_EVENT_TRANSFER_COMPLETE == p_args->event) {
        g_spi_transfer_complete = true;
    }
}
