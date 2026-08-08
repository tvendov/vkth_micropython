/*!
* \file      uart-board.c
*
* \brief     Target board UART driver implementation
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
#include "uart-board.h"
#if !defined(RP_USE_DEFAULT_UART_RX_HANDLER)
#include "at-command.h"
#endif

static volatile bool g_uart_tx_completed = false;
static volatile bool g_uart_rx_completed = false;
static volatile bool g_uart_trx_error = false;

void user_uart_callback(uart_callback_args_t *p_args);

void UartMcuInit( Uart_t *obj, UartId_t uartId, PinNames tx, PinNames rx )
{
    R_SAU_UART_Open(&g_uart0_ctrl, &g_uart0_cfg);
}

void UartMcuConfig( Uart_t *obj, UartMode_t mode, uint32_t baudrate, WordLength_t wordLength, StopBits_t stopBits, Parity_t parity, FlowCtrl_t flowCtrl )
{
    // UART parameters should be configured by RASC
}

void UartMcuDeInit( Uart_t *obj )
{
    R_SAU_UART_Close(&g_uart0_ctrl);
}

uint8_t UartMcuPutChar( Uart_t *obj, uint8_t data )
{
    return UartMcuPutBuffer( obj, &data, 1 );
}

uint8_t UartMcuGetChar( Uart_t *obj, uint8_t *data )
{
    uint16_t nbReadBytes;
    return UartMcuGetBuffer( obj, data, 1, &nbReadBytes );
}

uint8_t UartMcuPutBuffer( Uart_t *obj, uint8_t *buffer, uint16_t size )
{
    uint32_t timeout = 0x20000; // tentative

    g_uart_tx_completed = false;
    g_uart_trx_error = false;

    if (FSP_SUCCESS != R_SAU_UART_Write(&g_uart0_ctrl, buffer, size)) {
        return 1; // NG
    }

    while ( !g_uart_tx_completed ) {
        if ( --timeout == 0U ) {
            return 1; // NG
        }
    }

    return 0; // OK
}

uint8_t UartMcuGetBuffer( Uart_t *obj, uint8_t *buffer, uint16_t size, uint16_t *nbReadBytes )
{
    uint32_t timeout = 0x20000; // tentative

    g_uart_rx_completed = false;
    g_uart_trx_error = false;

    if (FSP_SUCCESS != R_SAU_UART_Read(&g_uart0_ctrl, buffer, size)) {
        return 1; // NG
    }

    while ( !g_uart_rx_completed ) {
        if ( g_uart_trx_error || (--timeout == 0U) ) {
            return 1; // NG
        }
    }

    *nbReadBytes = size;

    return 0; // OK
}

void user_uart_callback(uart_callback_args_t *p_args)
{
    BoardDisableAllIrq();
    switch( p_args->event ) {
        case UART_EVENT_RX_CHAR:
            #if !defined(RP_USE_DEFAULT_UART_RX_HANDLER)
            AtGetCharByte((uint8_t)p_args->data);
            #endif
            break;
        case UART_EVENT_TX_COMPLETE:
            g_uart_tx_completed = true;
            break;
        case UART_EVENT_RX_COMPLETE:
            g_uart_rx_completed = true;
            break;
        case UART_EVENT_BREAK_DETECT:
        case UART_EVENT_ERR_OVERFLOW:
        case UART_EVENT_ERR_FRAMING:
        case UART_EVENT_ERR_PARITY:
            g_uart_trx_error = true;
            break;
        default:
            break;
    }
    BoardEnableAllIrq();
}


/***********************************************************************
 *	function Name  : RpMcuUartPutChar
 *	parameters     : ch : Character to be sent.
 *	return value   : none
 *	description    : Put character to UART.
 **********************************************************************/
void RpMcuUartPutChar( char ch )
{
    (void)UartMcuPutBuffer( NULL/*not used*/, (uint8_t *)&ch, 1 );
}

/******************************************************************************
Function Name:       RpMcuPrint
Parameters:          str
                       Pointer to string.
Return value:        none.
Description:         Put strings to UART.
******************************************************************************/
void RpMcuPrint( char *str )
{
    /* Loop till end of strings */
    while (*str != '\0')
    {
        /* Put character to UART */
        RpMcuUartPutChar( *str );
        str++;
    }
}

/******************************************************************************
Function Name:       RpMcuPrintHex
Parameters:          Num
                       Number(HEX).
                     Len
                       Length of strings.
Return value:        none.
Description:         Put numerical strings to UART.
******************************************************************************/
void RpMcuPrintHex( uint32_t Num, uint8_t Len )
{
    uint8_t tmp;

    /* Loop till end of strings */
    while( Len > 0 )
    {
        /* Get digit */
        tmp = (uint8_t)( (Num >> ((Len - 1) * 4)) & 0x0F );

        /* numerical -> HEX character and put to UART */
        if( tmp <= 0x09 )
        {
            RpMcuUartPutChar( (char)('0' + tmp) );
        }
        else
        {
            RpMcuUartPutChar( (char)('A' + (tmp - 0x0A)) );
        }

        /* Decrement length */
        Len--;
    }
}


/******************************************************************************
Function Name:       RpMcuPrintDec
Parameters:          DecNum
                       Number(DEC).
                     Len
                       Length of strings.
Return value:        none.
Description:         Put numerical strings to UART.
******************************************************************************/
void RpMcuPrintDec( int32_t DecNum, uint8_t Len, char SupCh )
{
    int32_t     digit = 1;
    uint8_t     SupressFlg = 1;
    uint8_t     Num;
    uint8_t     i;

    /* minus number? */
    if( DecNum < 0 )
    {
        /* Put "-" character */
        RpMcuUartPutChar( '-' );
        DecNum = -DecNum;
    }

    /* Count digit */
    for( i = 1; i < Len; i++ )
    {
        digit *= 10;
    }

    while( digit > 0 )
    {
        if( digit == 1 )
        {
            /* Suppression off (Lowest digit) */
            SupressFlg = 0;
        }

        /* Get top digit number */
        Num = (unsigned char)((DecNum / digit) % 10);

        if( SupressFlg == 1 )
        {
            if( Num == 0 )
            {
                if( SupCh != '\0' )
                {
                    RpMcuUartPutChar( SupCh );
                }
            }
            else
            {
                /* Put a character(top digit number) */
                RpMcuUartPutChar( (char)('0' + Num) );

                /* Supression off */
                SupressFlg = 0;
            }
        }
        else
        {
            /* Put a character(top digit number) */
            RpMcuUartPutChar( (char)('0' + Num) );
        }

        /* remove top digit number and move a figure one place to the right */
        DecNum = DecNum - (Num * digit);
        digit = digit / 10;
    }
}
