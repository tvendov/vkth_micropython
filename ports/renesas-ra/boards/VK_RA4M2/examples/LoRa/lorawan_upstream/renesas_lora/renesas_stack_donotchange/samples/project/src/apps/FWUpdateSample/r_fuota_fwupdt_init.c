/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#include <stdio.h>
#include "board.h"

#include "timer-board.h"

SX126x_t SX126x;

void SX126xBoardConfigInit( void )
{
}

void SX126xIoInit( void )
{
}

void SpiInit( Spi_t *obj, SpiId_t spiId, PinNames mosi, PinNames miso, PinNames sclk, PinNames nss )
{
    FSP_PARAMETER_NOT_USED( obj );
    FSP_PARAMETER_NOT_USED( spiId );
    FSP_PARAMETER_NOT_USED( mosi );
    FSP_PARAMETER_NOT_USED( miso );
    FSP_PARAMETER_NOT_USED( sclk );
    FSP_PARAMETER_NOT_USED( nss );
}

#ifndef DEBUG_FUOTAUPDT_DBGPRINT
int setvbuf( FILE *__restrict fp, char *__restrict ch, int a, size_t size )
{
    FSP_PARAMETER_NOT_USED( fp );
    FSP_PARAMETER_NOT_USED( ch );
    FSP_PARAMETER_NOT_USED( a );
    FSP_PARAMETER_NOT_USED( size );
    return 0;
}
#endif

void UartInit( Uart_t *obj, UartId_t uartId, PinNames tx, PinNames rx )
{
#ifdef DEBUG_FUOTAUPDT_DBGPRINT
    UartMcuInit( obj, uartId, tx, rx );
#else
    FSP_PARAMETER_NOT_USED( obj );
    FSP_PARAMETER_NOT_USED( uartId );
    FSP_PARAMETER_NOT_USED( tx );
    FSP_PARAMETER_NOT_USED( rx );
#endif
}

void BoardTimerInit( void )
{
}

uint8_t UartPutBuffer( Uart_t *obj, uint8_t *buffer, uint16_t size )
{
#ifdef DEBUG_FUOTAUPDT_DBGPRINT
    return UartMcuPutBuffer( obj, buffer, size );
#else
    FSP_PARAMETER_NOT_USED( obj );
    FSP_PARAMETER_NOT_USED( buffer );
    FSP_PARAMETER_NOT_USED( size );
    return 0;
#endif
}

uint8_t UartGetBuffer( Uart_t *obj, uint8_t *buffer, uint16_t size, uint16_t *nbReadBytes )
{
#ifdef DEBUG_FUOTAUPDT_DBGPRINT
    return UartMcuGetBuffer( obj, buffer, size, nbReadBytes );
#else
    FSP_PARAMETER_NOT_USED( obj );
    FSP_PARAMETER_NOT_USED( buffer );
    FSP_PARAMETER_NOT_USED( size );
    FSP_PARAMETER_NOT_USED( nbReadBytes );
    return 0;
#endif
}

void agt_comp_int_isr (void)
{
}

