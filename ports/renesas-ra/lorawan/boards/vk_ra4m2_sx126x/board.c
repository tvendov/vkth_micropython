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
#include "timer.h"
#include "timer-board.h"
#include "delay.h"
#include "sx126x-board.h"
#include "board.h"

/*!
 * Indicates what interrupt woke MCU up from low power mode
 */
volatile uint32_t BoardMcuWokeUpBy;
BoardIsLowPowerAllowedCallback_t  *pBoardIsLowPowerAllowedCallback;
volatile uint8_t g_critical_sectioin_count = 0;

fsp_err_t BoardLpmInit(void);

void BoardCriticalSectionBegin( uint32_t *mask )
{
    *mask = __get_PRIMASK( );
    __disable_irq( );
}

void BoardCriticalSectionEnd( uint32_t *mask )
{
    __set_PRIMASK( *mask );
}





void BoardResetMcu( void )
{
    BoardDisableAllIrq();
  
    //Restart system
    NVIC_SystemReset( );
}

void BoardDeInitMcu( void )
{
    SX126xIoDeInit( );
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

void BoardRadioDio1DisableIrq()
{
    /* DIO1 is owned by the Python-created Pin.irq/extint registration in
       sx126x-board.c. Keep this upstream compatibility hook inert so there is
       no second FSP ExternalIrq owner. */
}

void BoardRadioDio1EnableIrq()
{
    /* See BoardRadioDio1DisableIrq(). */
}

void BoardTimerDisableIrq()
{
    /* Timer ownership is delegated to the Python object passed as timer=.
       The LoRaWAN board layer must not manipulate a hardware timer IRQ here. */

}
void BoardTimerEnableIrqNoClear()
{
    /* See BoardTimerDisableIrq(): Python owns timer init/deinit/IRQ routing. */
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
