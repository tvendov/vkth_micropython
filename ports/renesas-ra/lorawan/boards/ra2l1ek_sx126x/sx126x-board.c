/*!
 * \file      sx126x-board.c
 *
 * \brief     Target board SX126x shield driver implementation
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


/*!
 * RF output power select
 */
uint8_t  devicePowerSelect = DEFAULT_POWER_SELECT;

/*!
 * RF clock oscillator select
 */
uint8_t  deviceClockSelect = DEFAULT_CLOCK_SELECT;

/*!
 * RF switch GPIO pin object
 */
#if defined(RP_CONTROL_ANTSW_BY_MCU)
Gpio_t AntPow;
#endif // #if defined(RP_CONTROL_ANTSW_BY_MCU)

#if defined(RP_DETECT_BOARD_CONFIG)
Gpio_t BoardPowerType; // 0:SX1262, 1:SX1261
Gpio_t BoardClockType; // 0:TCXO,   1:XTAL
#endif // RP_DETECT_BOARD_CONFIG

void SX126xBoardConfigInit( void )
{
#if defined(RP_DETECT_BOARD_CONFIG)
    // GPIO settings except PinName should be configured by RASC.
    GpioInit( &BoardClockType, RP_RADIO_XTAL_SEL, PIN_INPUT, PIN_PUSH_PULL, PIN_NO_PULL, 0 );
    GpioInit( &BoardPowerType, RP_RADIO_DEVICE_SEL, PIN_INPUT, PIN_PUSH_PULL, PIN_NO_PULL, 0 );

    if( 0 == GpioRead(&BoardClockType) ) {
        SX126xSetClockSelect( RADIO_CLOCK_TCXO_SEL );
    } else {
        SX126xSetClockSelect( RADIO_CLOCK_XTAL_SEL );
    }
    if ( 0 == GpioRead(&BoardPowerType) ) {
        SX126xSetPaSelect( RADIO_HIPOWER_SEL );
    } else
    {
        SX126xSetPaSelect( RADIO_LOPOWER_SEL );
    }
#else
    SX126xSetPaSelect( DEFAULT_POWER_SELECT );
    SX126xSetClockSelect( DEFAULT_CLOCK_SELECT );
#endif
}

void SX126xIoInit( void )
{
    // GPIO settings except PinName should be configured by RASC.
    GpioInit( &SX126x.Spi.Nss, RADIO_NSS, PIN_OUTPUT, PIN_PUSH_PULL, PIN_PULL_UP, 1 );
    GpioInit( &SX126x.BUSY, RADIO_BUSY, PIN_INPUT, PIN_PUSH_PULL, PIN_NO_PULL, 0 );
    GpioInit( &SX126x.DIO1, RADIO_DIO_1, PIN_INPUT, PIN_PUSH_PULL, PIN_NO_PULL, 0 );
    GpioInit( &SX126x.Reset, RADIO_RESET, PIN_OUTPUT, PIN_PUSH_PULL, PIN_NO_PULL, 0 );
#if defined(RP_CONTROL_ANTSW_BY_MCU)
    GpioInit( &AntPow, ANT_SWITCH_POWER, PIN_OUTPUT, PIN_PUSH_PULL, PIN_NO_PULL, 1 );
#endif
    SX126xAntSwOn();
}

void SX126xIoIrqInit( DioIrqHandler dioIrq )
{
    // Interrupt (Dio1) settings should be configured by RASC.
    R_ICU_ExternalIrqOpen(&g_external_radio_dio1_irq_ctrl, &g_external_radio_dio1_irq_cfg);
    R_ICU_ExternalIrqEnable(&g_external_radio_dio1_irq_ctrl);
}

void SX126xIoTcxoInit( void )
{
    if( RADIO_CLOCK_TCXO_SEL == SX126xGetClockSelect() )
    {
        CalibrationParams_t calibParam;

        SX126xSetDio3AsTcxoCtrl( RP_TCXO_CTRL_VOLTAGE, RP_TCXO_STAB_TIME );
        SX126xClearDeviceErrors();
        memset(&calibParam, 0, sizeof(calibParam));
        calibParam.Value = 0x7F;
        SX126xCalibrate(calibParam);
    }
}

void SX126xReset( void )
{
    DelayMs( 10 );
    GpioWrite( &SX126x.Reset, 0 );
    DelayMs( 20 );
    GpioWrite( &SX126x.Reset, 1 );
    DelayMs( 10 );
}

void SX126xWaitOnBusy( void )
{
    while( GpioRead( &SX126x.BUSY ) == 1 );
}

void SX126xWakeup( void )
{   
    CRITICAL_SECTION_BEGIN();

    GpioWrite( &SX126x.Spi.Nss, 0 );
    
    SpiInOut( &SX126x.Spi, RADIO_GET_STATUS );
    SpiInOut( &SX126x.Spi, 0x00 );

    GpioWrite( &SX126x.Spi.Nss, 1 );

    // Wait for chip to be ready.
    SX126xWaitOnBusy( );

    CRITICAL_SECTION_END();
}

void SX126xWriteCommand( RadioCommands_t command, uint8_t *buffer, uint16_t size )
{
#if defined(TRACE)
    uint16_t pos;
    print("#WC:");
    print_hex(command,2);
    for(pos=0; pos < size; pos++){
        print(",");
        print_hex(buffer[pos],2);
    }
    print("\r\n");
#endif
    
    uint16_t   i;

    SX126xCheckDeviceReady( );

    CRITICAL_SECTION_BEGIN();

    GpioWrite( &SX126x.Spi.Nss, 0 );

    SpiInOut( &SX126x.Spi, ( uint8_t )command );

    for( i = 0; i < size; i++ ) {
        SpiInOut( &SX126x.Spi, buffer[i] );
    }

    GpioWrite( &SX126x.Spi.Nss, 1 );

    CRITICAL_SECTION_END();

    if( command != RADIO_SET_SLEEP ) {
        SX126xWaitOnBusy( );
    }
}

uint8_t SX126xReadCommand( RadioCommands_t command, uint8_t *buffer, uint16_t size )
{
    uint16_t   i;
    uint8_t    status = 0;

    SX126xCheckDeviceReady( );
    
    CRITICAL_SECTION_BEGIN();

    GpioWrite( &SX126x.Spi.Nss, 0 );

    SpiInOut( &SX126x.Spi, ( uint8_t )command );
    status = SpiInOut( &SX126x.Spi, 0x00 );
    
    for( i = 0; i < size; i++ ) {
        buffer[i] = SpiInOut( &SX126x.Spi, 0 );
    }

    GpioWrite( &SX126x.Spi.Nss, 1 );
    
    CRITICAL_SECTION_END();

    SX126xWaitOnBusy( );

#if defined(TRACE)
    uint16_t pos;
    print("#RC:");
    print_hex(command,2);
    for(pos=0; pos < size; pos++){
        print(",");
        print_hex(buffer[pos],2);
    }
    print("\r\n");
#endif

    return status;
}

void SX126xWriteRegisters( uint16_t address, uint8_t *buffer, uint16_t size )
{
#if defined(TRACE)
    uint16_t pos;
    print("#WR:");
    print_hex(address,4);
    for(pos=0; pos < size; pos++){
        print(",");
        print_hex(buffer[pos],2);
    }
    print("\r\n");
#endif

    uint16_t   i;
    
    SX126xCheckDeviceReady( );
    
    CRITICAL_SECTION_BEGIN();

    GpioWrite( &SX126x.Spi.Nss, 0 );
    
    SpiInOut( &SX126x.Spi, RADIO_WRITE_REGISTER );
    SpiInOut( &SX126x.Spi, ( address & 0xFF00 ) >> 8 );
    SpiInOut( &SX126x.Spi, address & 0x00FF );
    
    for( i = 0; i < size; i++ ) {
        SpiInOut( &SX126x.Spi, buffer[i] );
    }

    GpioWrite( &SX126x.Spi.Nss, 1 );
    
    CRITICAL_SECTION_END();

    SX126xWaitOnBusy( );
}

void SX126xWriteRegister( uint16_t address, uint8_t value )
{
    SX126xWriteRegisters( address, &value, 1 );
}

void SX126xReadRegisters( uint16_t address, uint8_t *buffer, uint16_t size )
{
    uint16_t   i;
    
    SX126xCheckDeviceReady( );
    
    CRITICAL_SECTION_BEGIN();

    GpioWrite( &SX126x.Spi.Nss, 0 );
    
    SpiInOut( &SX126x.Spi, RADIO_READ_REGISTER );
    SpiInOut( &SX126x.Spi, ( address & 0xFF00 ) >> 8 );
    SpiInOut( &SX126x.Spi, address & 0x00FF );
    SpiInOut( &SX126x.Spi, 0 );
    
    for( i = 0; i < size; i++ ) {
        buffer[i] = SpiInOut( &SX126x.Spi, 0 );
    }
    
    GpioWrite( &SX126x.Spi.Nss, 1 );
    
    CRITICAL_SECTION_END();

    SX126xWaitOnBusy( );

#if defined(TRACE)
    uint16_t pos;
    print("#RR:");
    print_hex(address,4);
    for(pos=0; pos < size; pos++){
        print(",");
        print_hex(buffer[pos],2);
    }
    print("\r\n");
#endif
}

uint8_t SX126xReadRegister( uint16_t address )
{
    uint8_t data;
    SX126xReadRegisters( address, &data, 1 );
    return data;
}

void SX126xWriteBuffer( uint8_t offset, uint8_t *buffer, uint8_t size )
{
#if defined(TRACE)
    uint16_t pos;
    print("#WB:");
    print_hex(offset,2);
    for(pos=0; pos < size; pos++){
        print(",");
        print_hex(buffer[pos],2);
    }
    print("\r\n");
#endif

    uint16_t   i;
    
    SX126xCheckDeviceReady( );
    
    CRITICAL_SECTION_BEGIN();

    GpioWrite( &SX126x.Spi.Nss, 0 );
    
    SpiInOut( &SX126x.Spi, RADIO_WRITE_BUFFER );
    SpiInOut( &SX126x.Spi, offset );
    
    for( i = 0; i < size; i++ ) {
        SpiInOut( &SX126x.Spi, buffer[i] );
    }
    
    GpioWrite( &SX126x.Spi.Nss, 1 );
    
    CRITICAL_SECTION_END();

    SX126xWaitOnBusy( );
}

void SX126xReadBuffer( uint8_t offset, uint8_t *buffer, uint8_t size )
{
    uint16_t   i;
    
    SX126xCheckDeviceReady( );
    
    CRITICAL_SECTION_BEGIN();

    GpioWrite( &SX126x.Spi.Nss, 0 );

    SpiInOut( &SX126x.Spi, RADIO_READ_BUFFER );
    SpiInOut( &SX126x.Spi, offset );
    SpiInOut( &SX126x.Spi, 0 );
    
    for( i = 0; i < size; i++ ) {
        buffer[i] = SpiInOut( &SX126x.Spi, 0 );
    }
   
    GpioWrite( &SX126x.Spi.Nss, 1 );
    
    CRITICAL_SECTION_END();

    SX126xWaitOnBusy( );

#if defined(TRACE)
    uint16_t pos;
    print("#RB:");
    print_hex(offset,2);
    for(pos=0; pos < size; pos++){
        print(",");
        print_hex(buffer[pos],2);
    }
    print("\r\n");
#endif
}

void SX126xSetRfTxPower( int8_t power )
{
    SX126xSetTxParams( power, RADIO_RAMP_40_US );
}

uint8_t SX126xGetPaSelect( void )
{
    return devicePowerSelect;
}

void SX126xSetPaSelect( uint8_t paType )
{
    if( (paType == RADIO_LOPOWER_SEL) || (paType == RADIO_HIPOWER_SEL) )
    {
        devicePowerSelect = paType;
    }
}

uint8_t SX126xGetClockSelect( void )
{
    return deviceClockSelect;
}

void SX126xSetClockSelect( uint8_t clkType )
{
    if( (clkType == RADIO_CLOCK_XTAL_SEL) || (clkType == RADIO_CLOCK_TCXO_SEL) )
    {
        deviceClockSelect = clkType;
    }
}

void SX126xAntSwOn( void )
{
#if defined(RP_CONTROL_ANTSW_BY_MCU)
    GpioWrite( &AntPow, 1 );
#else //#if defined(RP_CONTROL_ANTSW_BY_MCU)
    /* none */
#endif //#if defined(RP_CONTROL_ANTSW_BY_MCU)
}

void SX126xAntSwOff( void )
{
#if defined(RP_CONTROL_ANTSW_BY_MCU)
    GpioWrite( &AntPow, 0 );
#else //#if defined(RP_CONTROL_ANTSW_BY_MCU)
    /* none */
#endif //#if defined(RP_CONTROL_ANTSW_BY_MCU)
}

bool SX126xCheckRfFrequency( uint32_t frequency )
{
    // Implement check. Currently all frequencies are supported
    return true;
}

uint32_t SX126xGetDio1PinState( void )
{
    return GpioRead( &SX126x.DIO1 );
}
