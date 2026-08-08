/*!
* \file      gpio-board.c
*
* \brief     Target board GPIO driver implementation
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
#include "board.h"
#include "utilities.h"
#include "timer-board.h"
#include "gpio-board.h"


void GpioMcuInit( Gpio_t *obj, PinNames pin, PinModes mode, PinConfigs config, PinTypes type, uint32_t value )
{
    // Assign a pin name only. Pin attributes should be configured by RASC.
    if ( (NULL != obj) && (NC != obj->pin) ) {
        obj->pin = pin;
    }
}

void GpioMcuSetInterrupt( Gpio_t *obj, IrqModes irqMode, IrqPriorities irqPriority, GpioIrqHandler *irqHandler )
{
    // Interrupt settings should be configured by RASC.
}

void GpioMcuRemoveInterrupt( Gpio_t *obj )
{
    // Interrupt settings should be configured by RASC.
}

#define RP_GET_PORT_PIN(x) (bsp_io_port_pin_t)((((x)->pin & 0xf0) << 4) | ((x)->pin & 0xf));

void GpioMcuWrite( Gpio_t *obj, uint32_t value )
{
    bsp_io_level_t level = (0U == value) ? BSP_IO_LEVEL_LOW : BSP_IO_LEVEL_HIGH;
    bsp_io_port_pin_t pin = RP_GET_PORT_PIN(obj);

    if ( (NULL != obj) && (NC != obj->pin) ) {
        R_IOPORT_PinWrite( &g_ioport_ctrl, pin, level );
    }
}

void GpioMcuToggle( Gpio_t *obj )
{
    bsp_io_level_t level;
    bsp_io_port_pin_t pin = RP_GET_PORT_PIN(obj);

    if ( (NULL != obj) && (NC != obj->pin) ) {
        
        R_IOPORT_PinRead( &g_ioport_ctrl, pin, &level );
        level =  ( BSP_IO_LEVEL_LOW == level ) ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW;
        R_IOPORT_PinWrite( &g_ioport_ctrl, pin, level );
    }
}

uint32_t GpioMcuRead( Gpio_t *obj )
{
    bsp_io_level_t level;
    bsp_io_port_pin_t pin = RP_GET_PORT_PIN(obj);

    if ( (NULL != obj) && (NC != obj->pin) ) {
        R_IOPORT_PinRead( &g_ioport_ctrl, pin, &level );
        return( level );
    } else {
        return 0;
    }
}
