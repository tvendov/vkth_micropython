/*!
* \file      board.c
*
* \brief     Target board general functions implementation
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
#include "gpio.h"
#include "spi.h"
#include "uart.h"
#include "timer.h"
#include "timer-board.h"
#include "uart-board.h"
#include "delay.h"
#include "sx126x-board.h"
#include "board.h"

/*!
 * Indicates what interrupt woke MCU up from low power mode
 */
volatile uint32_t BoardMcuWokeUpBy;
BoardIsLowPowerAllowedCallback_t  *pBoardIsLowPowerAllowedCallback;
volatile uint8_t g_critical_sectioin_count = 0;


/*
* MCU objects
*/
Uart_t Uart0;

fsp_err_t BoardLpmInit(void);

/*!
* Initializes the unused GPIO to a know status
*/
static void BoardUnusedIoInit( void );

/*!
* System Clock Initialization (one-time clock configuration at beginning)
*/
static void SystemClockInit( void );

void BoardCriticalSectionBegin( uint32_t *mask )
{
    *mask = __get_PRIMASK( );
    __disable_irq( );
}

void BoardCriticalSectionEnd( uint32_t *mask )
{
    __set_PRIMASK( *mask );
}

void BoardInitMcu( void )
{
    R_BSP_PinAccessEnable();

    SystemClockInit();
    BoardUnusedIoInit();

    SX126xBoardConfigInit();
    SX126xIoInit();
    
    SpiInit( &SX126x.Spi, SPI_1, NC, NC, NC, NC);
  
    setvbuf(stdout, NULL, _IONBF, 1024); // buffering off
    UartInit(&Uart0, 0, NC, NC);

    BoardLpmInit();

    BoardTimerInit();

#ifdef RM_TINYCRYPT
    RM_TINCYRYPT_PORT_Init();
    HW_SCE_PowerOff();
#endif

#if defined(RM_HS300X_H)
    app_hs300x_init();
#endif
}

void BoardUartConfigure(uint32_t baud)
{
    // UART parameters should be configured by RASC
}

void BoardUartDeconfigure(void)
{
    UartDeInit( &Uart0);
}

void BoardResetMcu( void )
{
    BoardDisableAllIrq();
  
    //Restart system
    NVIC_SystemReset( );
}

void BoardDeInitMcu( void )
{
    SpiDeInit( &SX126x.Spi );
    SX126xIoDeInit( );
}

void SystemClockInit( void )
{
    R_CGC_Open(&g_cgc0_ctrl, &g_cgc0_cfg);

    R_CGC_ClockStart(&g_cgc0_ctrl, CGC_CLOCK_SUBCLOCK, NULL);
    R_BSP_SoftwareDelay(BSP_CLOCK_CFG_SUBCLOCK_STABILIZATION_MS, BSP_DELAY_UNITS_MILLISECONDS);

    R_CGC_ClockStop(&g_cgc0_ctrl, CGC_CLOCK_MOCO);
    R_CGC_ClockStop(&g_cgc0_ctrl, CGC_CLOCK_LOCO);
}

void SysTick_Handler( void )
{ 
}

void BoardLowPowerHandler( void )
{
}

void BoardInitPeriph( void )
{
}

void BoardGetUniqueId( uint8_t *id )
{
    // Dummy
    id[7] = 0x07;
    id[6] = 0x06;
    id[5] = 0x05;
    id[4] = 0x04;
    id[3] = 0x03;
    id[2] = 0x02;
    id[1] = 0x01;
    id[0] = 0x00;
}

uint8_t BoardGetBatteryLevel( void )
{
    return 0; // Dummy
}

static void BoardUnusedIoInit( void )
{
    // Disable SWDIO/SWCLK internal pull-up to reduce power.
    R_PFS->PORT[1].PIN[8].PmnPFS = 0; // P108/SWDIO
    R_PFS->PORT[3].PIN[0].PmnPFS = 0; // P300/SWCLK
}

uint8_t GetBoardPowerSource( void )
{
    return USB_POWER; // Dummy
}

void BoardRadioIrqPreprocess( void )
{
    if(GetLowPowerFlag() & WAKEUP_TRIGGER_READY)
    {
        SetLowPowerFlag(WAKEUP_TRIGGER_RADIO_DIO1);
    }
}

void BoardRadioDio1Callback(external_irq_callback_args_t *p_args)
{
    RadioOnDioIrq();
}

void BoardRadioDio1DisableIrq()
{
    R_ICU_ExternalIrqDisable(&g_external_radio_dio1_irq_ctrl);
}

void BoardRadioDio1EnableIrq()
{
    R_ICU_ExternalIrqEnable(&g_external_radio_dio1_irq_ctrl);
}

void BoardTimerDisableIrq()
{
    R_BSP_IrqDisable(g_timer0_cfg.cycle_end_irq);
    R_BSP_IrqDisable(g_timer1_cfg.cycle_end_irq);
}
void BoardTimerEnableIrqNoClear()
{
    R_BSP_IrqEnableNoClear(g_timer0_cfg.cycle_end_irq);
    R_BSP_IrqEnableNoClear(g_timer1_cfg.cycle_end_irq);
}

//------------------------------------------------------------------------
// printf/scanf functions
//------------------------------------------------------------------------

/*
* Function to be used by stdout for printf etc
*/
int _write( int fd, const void *buf, size_t count )
{
  while( UartPutBuffer( &Uart0, ( uint8_t* )buf, ( uint16_t )count ) != 0 ){ };
  return count;
}

/*
* Function to be used by stdin for scanf etc
*/
int _read( int fd, const void *buf, size_t count )
{
  size_t bytesRead = 0;

  while( UartGetBuffer( &Uart0, ( uint8_t* )buf, count, ( uint16_t* )&bytesRead ) != 0 ){ };
  // Echo back the character
  while( UartPutBuffer( &Uart0, ( uint8_t* )buf, ( uint16_t )bytesRead ) != 0 ){ };
  return bytesRead;
}


//------------------------------------------------------------------------
// Low Power Management
//------------------------------------------------------------------------

fsp_err_t BoardLpmInit( void )
{
    fsp_err_t err = R_LPM_Open(&g_lpm0_ctrl, &g_lpm0_cfg);

    return err;
}

fsp_err_t BoardLpmEnter( void )
{
    fsp_err_t err = R_LPM_LowPowerModeEnter(&g_lpm0_ctrl);

    if (FSP_SUCCESS == err) {
        BoardLpmExit( );
    }

    return err;
}

fsp_err_t BoardLpmExit( void )
{
    fsp_err_t err = FSP_SUCCESS;

    return err;
}

int16_t SetLowPower( void )
{
    fsp_err_t err = FSP_SUCCESS;

#if defined(DEBUG_LORAMAC) || defined(DEBUG_PRVLORA)
    uint8_t loraMode;   // 0 = LoRaWAN, 1 = PrivateLoRa, (else) = none
    uint32_t debugMode;

  #if defined(DEBUG_LORAMAC) && defined(DEBUG_PRVLORA)
    extern uint8_t AppGetLoRaMode( void );  // function in main.c
    loraMode = AppGetLoRaMode();
  #elif defined(DEBUG_LORAMAC)
    loraMode = 0;  // LoRaWAN
  #else
    loraMode = 1;  // PrivateLoRa
  #endif

  #if defined(DEBUG_LORAMAC)
    if( loraMode == 0 )
    {
        debugMode = LoRaMacDebug.Mode & LORAMAC_DEBUG_MODE_PSEUDO_MCULOWPWR;
        if( debugMode != 0 )
        {
            return( LoRaMacDebugSetPseudoLowPower() );
        }
    }
  #endif
  #if defined(DEBUG_PRVLORA)
    if( loraMode == 1 )
    {
        debugMode  = AppPrvLoRaDebugGetMode();
        debugMode &= APP_PRVLORA_DEBUG_APP_PSEUDO_MCULOWPWR;
        if( debugMode != 0 )
        {
            return( AppPrvLoRaDebugSetPseudoLowPower() );
        }
    }
  #endif
#endif  // defined(DEBUG_LORAMAC) || defined(DEBUG_PRVLORA)

    BoardDisableAllIrq();

    if( RP_LOWPWR_COND_ENTRY ) {
        SetLowPowerFlag(WAKEUP_TRIGGER_READY);

        do {
            BoardLpmEnter( );
            BoardEnableAllIrq( );
            BoardDisableAllIrq( );
        } while ( RP_LOWPWR_COND_LOOP );
        
        ClearLowPowerFlag();
    }
    
    BoardEnableAllIrq();
    
    return err;
}

uint32_t GetLowPowerFlag( void )
{
    return BoardMcuWokeUpBy;
}

void ClearLowPowerFlag( void )
{
    BoardMcuWokeUpBy = WAKEUP_TRIGGER_NONE;
}

void SetLowPowerFlag( uint32_t flag )
{
    if(flag == WAKEUP_TRIGGER_READY)
    {
        BoardMcuWokeUpBy = flag;
    }
    
    if(BoardMcuWokeUpBy & WAKEUP_TRIGGER_READY)
    {
        BoardMcuWokeUpBy |= flag;
    }
}

bool BoardIsLowPowerAllowed( void )
{
    /* return true if users want MCU to low power. */
    bool retLowPwrAllowed = true;

    if( pBoardIsLowPowerAllowedCallback )
    {
       retLowPwrAllowed = (*pBoardIsLowPowerAllowedCallback)(); 
    }

    return( retLowPwrAllowed );
}

void BoardSetIsLowPowerAllowedCallback( BoardIsLowPowerAllowedCallback_t *p )
{
    pBoardIsLowPowerAllowedCallback = p;
}
