
/*
    (C) 2019 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    LoRaMacDebug.c
  * @author  Renesas Electronics Corporation
  * @brief   Debug utilities
**/
/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
#if defined(DEBUG_LORAMAC)

#include "board.h"
#include "LoRaMac.h"
#include "LoRaMacClassB.h"
//
#include "at_proc.h"

#if !defined(LPWASTUDIO_VERSION) || (LPWASTUDIO_VERSION >= 2100)
#define LPWASTUDIO_SNIFFER_FORMAT   (2100)   // v2.1.0.0 compatible
#else
#define LPWASTUDIO_SNIFFER_FORMAT   (1000)   // v1.0.0.0 compatible
#endif
static uint16_t LoRaMacDebugGetBandwidth( int8_t bandWidthIndex );

LoRaMacDebug_t  LoRaMacDebug;

#define LORAMAC_DEBUG_RFPWRMODE_STANDBY     0
#define LORAMAC_DEBUG_RFPWRMODE_WAKEUP      1
#define LORAMAC_DEBUG_RFPWRMODE_SLEEPWARM   2
#define LORAMAC_DEBUG_RFPWRMODE_SLEEPCOLD   3
#define LORAMAC_DEBUG_RFPWRMODE_ACTIVETX    4
#define LORAMAC_DEBUG_RFPWRMODE_ACTIVERX    5

void LoRaMacDebugInit(void)
{
    memset1((uint8_t *)&LoRaMacDebug, 0, sizeof(LoRaMacDebug));

#if defined(DEBUG_LORAMAC_DEFAULT_MODE)
    LoRaMacDebugSetMode( DEBUG_LORAMAC_DEFAULT_MODE );
#endif
}

void LoRaMacDebugSetMode(uint32_t mode)
{
    LoRaMacDebug.Mode = mode;
}

uint32_t LoRaMacDebugGetMode(void)
{
    return(LoRaMacDebug.Mode);
}

void RadioDebugRadioIsChannelFree( uint8_t paketType, uint32_t freq, 
         uint32_t actualBandWidthVal, int16_t rssi, int16_t rssiThresh, uint32_t maxCarrierSenseTime, bool isChannelFree )
{
#if defined( LORACOMBO_ENABLED )
    if( LoRaMacIsStarted() == false )
    {
        return;  // currently LoRaWAN is stopped
    }
#endif

    if (LoRaMacDebug.Mode & LORAMAC_DEBUG_MODE_CCA)
    {
        //modem,bandwidth,frequency,rssi,cca_result
        //  modem: 0:GFSK, 1:LoRa
        //  bandwidth: in Hz
        //  freq: in Hz
        //  cca_result: true: channel free(idle), false: channel busy  
        print( "*CCA:" );
        print_dec( paketType, 3, '\0' );
        print( "," );
        print_dec( actualBandWidthVal, 10, '\0' );
        print( "," );
        print_dec( freq, 10, '\0' );
        print( "," );
        print_dec( rssi, 5, '\0' );
        print( "," );
        print_dec( isChannelFree, 3, '\0' );    // CCA result
        print_newline();
    }
}
void RadioDebugRadioSetRxConfig( RadioModems_t modem, uint32_t bandwidth,
                             uint32_t datarate, uint8_t coderate,
                             uint32_t bandwidthAfc, uint16_t preambleLen,
                             uint16_t symbTimeout, bool fixLen,
                             uint8_t payloadLen,
                             bool crcOn, bool freqHopOn, uint8_t hopPeriod,
                             bool iqInverted, bool rxContinuous )
{
#if defined( LORACOMBO_ENABLED )
    if( LoRaMacIsStarted() == false )
    {
        return;  // currently LoRaWAN is stopped
    }
#endif

    if (LoRaMacDebug.Mode & (LORAMAC_DEBUG_MODE_RX | LORAMAC_DEBUG_MODE_OUTPUT_SNIFFER_FORMAT))
    {
        LoRaMacDebug.RxConfig.Bandwidth = bandwidth;
        LoRaMacDebug.RxConfig.Datarate = datarate;
        LoRaMacDebug.RxConfig.SymbTimeout= symbTimeout;
    }
}

void RadioDebugRadioSetTxConfig( RadioModems_t modem, int8_t power, uint32_t fdev,
                        uint32_t bandwidth, uint32_t datarate,
                        uint8_t coderate, uint16_t preambleLen,
                        bool fixLen, bool crcOn, bool freqHopOn,
                        uint8_t hopPeriod, bool iqInverted, uint32_t timeout )
{
#if defined( LORACOMBO_ENABLED )
    if( LoRaMacIsStarted() == false )
    {
        return;  // currently LoRaWAN is stopped
    }
#endif

    if (LoRaMacDebug.Mode & (LORAMAC_DEBUG_MODE_TX | LORAMAC_DEBUG_MODE_OUTPUT_SNIFFER_FORMAT))
    {
        LoRaMacDebug.TxConfig.Bandwidth = bandwidth;
        LoRaMacDebug.TxConfig.Datarate = datarate;
        LoRaMacDebug.TxConfig.Timeout= timeout;
        LoRaMacDebug.TxConfig.Power = power;
    }
}

void RadioDebugRadioSend( uint8_t packetType, uint32_t freq, uint8_t *buffer, uint8_t size )
{
#if defined( LORACOMBO_ENABLED )
    if( LoRaMacIsStarted() == false )
    {
        return;  // currently LoRaWAN is stopped
    }
#endif

    if (LoRaMacDebug.Mode & LORAMAC_DEBUG_MODE_TX)
    {          
        //modem,bandwidth,datarate(sf),timeout,freq
        //  Modem: 0:GFSK, 1:LoRa
        //  Bandwidth: 4:128kHz,5:250kHz, 6:500kHz
        //  Datarate: SF(LoRa), Bitrate(GFSK)
        //  Timeout: Tx timeout (msec)
        //  Frequency: Frequency [Hz]
        //  Power: Tx power [dBm] 
        print( "*TX:" );
        print_dec( packetType, 3, '\0' );
        print( "," );
        print_dec( LoRaMacDebug.TxConfig.Bandwidth, 3, '\0' );
        print( "," );
        print_dec( LoRaMacDebug.TxConfig.Datarate, 10, '\0' );
        print( "," );
        print_dec( LoRaMacDebug.TxConfig.Timeout, 10, '\0' );
        print( "," );
        print_dec( freq, 10, '\0' );
        print( "," );
        print_dec( LoRaMacDebug.TxConfig.Power, 3, '\0' );
        print_newline();
    }

    if (LoRaMacDebug.Mode & LORAMAC_DEBUG_MODE_OUTPUT_SNIFFER_FORMAT)
    {
        // save Tx info
        LoRaMacDebug.Tx.pPayload = buffer;      // just save pointer, payload data is held in MacCtx.PktBuffer 
        LoRaMacDebug.Tx.PayloadLen = size;
        LoRaMacDebug.TxConfig.Frequency = freq;
        LoRaMacDebug.TxConfig.PacketType = packetType;
    }

    if (LoRaMacDebug.Mode & LORAMAC_DEBUG_MODE_LOWPWR_RF)
    {
        // set RF power mode
        LoRaMacDebug.LowPwrRf.RfPwrMode = LORAMAC_DEBUG_RFPWRMODE_ACTIVETX;
    }
}

void RadioDebugRadioRx( uint8_t packetType, uint32_t freq, uint32_t timeout, bool rxContinuous )
{
#if defined( LORACOMBO_ENABLED )
    if( LoRaMacIsStarted() == false )
    {
        return;  // currently LoRaWAN is stopped
    }
#endif

    if (LoRaMacDebug.Mode & LORAMAC_DEBUG_MODE_RX)
    {
        //modem,bandwidth,datarate(sf),symbTimeout,timeout
        //  Modem: 0:GFSK, 1:LoRa
        //  Bandwidth: 4:128kHz,5:250kHz, 6:500kHz
        //  Datarate: SF(LoRa), Bitrate(GFSK)
        //  SymbTimeout: Timeout in symbol(LoRa), in byte(GFSK)  
        print( "*RX:" );
        print_dec( packetType, 3, '\0' );
        print( "," );
        print_dec( LoRaMacDebug.RxConfig.Bandwidth, 3, '\0' );
        print( "," );
        print_dec( LoRaMacDebug.RxConfig.Datarate, 10, '\0' );
        print( "," );
        print_dec( LoRaMacDebug.RxConfig.SymbTimeout, 5, '\0' );
        print( "," );
        print_dec( (rxContinuous? 0:timeout), 10, '\0' );
        print( "," );
        print_dec( freq, 10, '\0' );
        print_newline();
    }

    if (LoRaMacDebug.Mode & LORAMAC_DEBUG_MODE_OUTPUT_SNIFFER_FORMAT)
    {
        // save Rx info
        LoRaMacDebug.RxConfig.Frequency = freq;
        LoRaMacDebug.RxConfig.PacketType = packetType;
    }

    if (LoRaMacDebug.Mode & LORAMAC_DEBUG_MODE_LOWPWR_RF)
    {
        // set RF power mode
        LoRaMacDebug.LowPwrRf.RfPwrMode = LORAMAC_DEBUG_RFPWRMODE_ACTIVERX;
    }
}

void RadioDebugRadioSleep( void )
{
#if defined( LORACOMBO_ENABLED )
    if( LoRaMacIsStarted() == false )
    {
        return;  // currently LoRaWAN is stopped
    }
#endif

    if ((LoRaMacDebug.Mode & LORAMAC_DEBUG_MODE_LOWPWR_RF) &&
        (LoRaMacDebug.LowPwrRf.RfPwrMode != LORAMAC_DEBUG_RFPWRMODE_SLEEPWARM))
    {
        print( "*RFPWR:WarmSleep" );
        print_newline();
        LoRaMacDebug.LowPwrRf.RfPwrMode = LORAMAC_DEBUG_RFPWRMODE_SLEEPWARM;
    }
}

void RadioDebugRadioSleepCold( void )
{
#if defined( LORACOMBO_ENABLED )
    if( LoRaMacIsStarted() == false )
    {
        return;  // currently LoRaWAN is stopped
    }
#endif

    if ((LoRaMacDebug.Mode & LORAMAC_DEBUG_MODE_LOWPWR_RF) &&
        (LoRaMacDebug.LowPwrRf.RfPwrMode != LORAMAC_DEBUG_RFPWRMODE_SLEEPCOLD))
    {
        print( "*RFPWR:ColdSleep" );
        print_newline();
        LoRaMacDebug.LowPwrRf.RfPwrMode = LORAMAC_DEBUG_RFPWRMODE_SLEEPCOLD;
    }
}

void RadioDebugRadioStandby( void )
{
#if defined( LORACOMBO_ENABLED )
    if( LoRaMacIsStarted() == false )
    {
        return;  // currently LoRaWAN is stopped
    }
#endif

    if ((LoRaMacDebug.Mode & LORAMAC_DEBUG_MODE_LOWPWR_RF) &&
        (LoRaMacDebug.LowPwrRf.RfPwrMode != LORAMAC_DEBUG_RFPWRMODE_STANDBY))
    {
        print( "*RFPWR:Standby" );
        print_newline();
        LoRaMacDebug.LowPwrRf.RfPwrMode = LORAMAC_DEBUG_RFPWRMODE_STANDBY;
    }
}

void RadioDebugRadioWakeUp( uint8_t preOpeMode )
{
#if defined( LORACOMBO_ENABLED )
    if( LoRaMacIsStarted() == false )
    {
        return;  // currently LoRaWAN is stopped
    }
#endif

    if (LoRaMacDebug.Mode & LORAMAC_DEBUG_MODE_LOWPWR_RF)
    {
        if (preOpeMode == MODE_COLD_SLEEP)
        {
            print( "*RFPWR:Wakeup from Cold" );
            print_newline();
            LoRaMacDebug.LowPwrRf.RfPwrMode = LORAMAC_DEBUG_RFPWRMODE_WAKEUP;
        }
        else if( preOpeMode == MODE_SLEEP)  // Warm Sleep
        {
            print( "*RFPWR:Wakeup from Warm" );
            print_newline();
            LoRaMacDebug.LowPwrRf.RfPwrMode = LORAMAC_DEBUG_RFPWRMODE_WAKEUP;
        }
    }
}

void LoRaMacDebugOnRadioTxDone( TimerTime_t txDoneTime )
{
    uint8_t     i;

#if defined( LORACOMBO_ENABLED )
    if( LoRaMacIsStarted() == false )
    {
        return;  // currently LoRaWAN is stopped
    }
#endif

    if (LoRaMacDebug.Mode & LORAMAC_DEBUG_MODE_OUTPUT_SNIFFER_FORMAT)
    {
#if (LPWASTUDIO_SNIFFER_FORMAT >= (2100))
        AtPrintCmdHeader("+RX");
#endif
        // sniffer formet: payload,rssi,snr,crc_error,freq,LoRa|SFxx(or GFSK|xx)
        for (i = 0; i < LoRaMacDebug.Tx.PayloadLen; i++)
        {
            print_hex( *(LoRaMacDebug.Tx.pPayload + i), 2 );
        }

        print( "," );
        print_dec( 0, 1, '\0' );
        print( "," );
        print_dec( 0, 1, '\0' );
        print( "," );
        print_dec( 0, 1, '\0' );
        print( "," );
        print_dec( LoRaMacDebug.TxConfig.Frequency, 10, '\0' );
        print( "," );
        if( LoRaMacDebug.TxConfig.PacketType == PACKET_TYPE_LORA )
        {
            print( "LoRa|SF" );
            print_dec( (uint16_t)LoRaMacDebug.TxConfig.Datarate, 5, '\0' );
            print( "BW" );
            print_dec( LoRaMacDebugGetBandwidth(LoRaMacDebug.TxConfig.Bandwidth), 5, '\0' );
        }
        else
        {
            print( "GFSK|" );
            print_dec( ((LoRaMacDebug.TxConfig.Datarate == 50000)? 50:0), 2, '\0' );
        }
#if (LPWASTUDIO_SNIFFER_FORMAT >= (2100))
        AtPrintTrailer();
#else
        print_newline();
#endif
    }
}

void LoRaMacDebugOnRadioRxDone( uint8_t *payload, uint16_t size, 
            int16_t rssi, int8_t snr, uint8_t rxSlot, TimerTime_t rxDoneTime )
{
    uint8_t     i;

#if defined( LORACOMBO_ENABLED )
    if( LoRaMacIsStarted() == false )
    {
        return;  // currently LoRaWAN is stopped
    }
#endif

    if (LoRaMacDebug.Mode & LORAMAC_DEBUG_MODE_OUTPUT_SNIFFER_FORMAT)
    {
#if (LPWASTUDIO_SNIFFER_FORMAT >= (2100))
        AtPrintCmdHeader("+RX");
#endif
        // sniffer formet: payload,rssi,snr,crc_error,freq,LoRa|SFxx(or GFSK|xx)
        for (i = 0; i < size; i++)
        {
            print_hex( payload[i], 2 );
        }

        print( "," );
        print_dec( rssi, 5, '\0' );
        print( "," );
        print_dec( snr, 3, '\0' );
        print( "," );
        print_dec( ((Radio.GetErrorFlag() & RADIO_PAYLOAD_CRC_ERROR)? 1: 0), 1, '\0' );
        print( "," );
        print_dec( LoRaMacDebug.RxConfig.Frequency, 10, '\0' );
        print( "," );
        if( LoRaMacDebug.RxConfig.PacketType == PACKET_TYPE_LORA )
        {
            print( "LoRa|SF" );
            print_dec( (uint16_t)LoRaMacDebug.RxConfig.Datarate, 5, '\0' );
            print( "BW" );
            print_dec( LoRaMacDebugGetBandwidth(LoRaMacDebug.RxConfig.Bandwidth), 5, '\0' );
        }
        else
        {
            print( "GFSK|" );
            print_dec( ((LoRaMacDebug.RxConfig.Datarate == 50000)? 50:0), 2, '\0' );
        }
#if (LPWASTUDIO_SNIFFER_FORMAT >= (2100))
        AtPrintTrailer();
#else
        print_newline();
#endif
    }
}

#ifdef LORAMAC_CLASSB_ENABLED
void LoRaMacDebugLoRaMacClassBProcessBeacon( void *vp_beaconCtx,
                                             TimerTime_t beaconEventTime, 
                                             int32_t sysTimeErr,
                                             TimerTime_t currentTime )
{
    BeaconContext_t *p_beaconCtx = (BeaconContext_t *)vp_beaconCtx;

#if defined( LORACOMBO_ENABLED )
    if( LoRaMacIsStarted() == false )
    {
        return;  // currently LoRaWAN is stopped
    }
#endif

    if (LoRaMacDebug.Mode & LORAMAC_DEBUG_MODE_BEACON)
    {
        print( "*CurTime:" );
        print_dec( (uint32_t)currentTime, 10, '\0' );
        print_newline();
        print( "*BCN_EvtTime:" );
        print_dec( (uint32_t)beaconEventTime, 10, '\0' );
        print_newline();
        print( "*BCN_EnlOff:" );
        print_dec( p_beaconCtx->BeaconRxEnlargeWindowOffset, 10, '\0' );
        print_newline();
        print( "*BCN_STimErr:" );
        print_dec( sysTimeErr, 10, '\0' );
        print_newline();
        print( "*BCN_LastRx:" );
        print_dec( (uint32_t)SysTimeToMs(p_beaconCtx->LastBeaconRx), 10, '\0' );
        print_newline();
        print( "*BCN_LastSysTim:" );
        print_dec( (uint32_t)p_beaconCtx->LastSystimeSetTimeMs, 10, '\0' ); //lower 32bit
        print_newline();
        print( "*BCN_NextRxAdj:" );
        print_dec( (uint32_t)p_beaconCtx->NextBeaconRxAdjusted, 10, '\0' ); //lower 32bit
        print_newline();
    }
}

void LoRaMacDebugLoRaMacClassBProcessPingSlot(TimerTime_t pingSlotTime)
{
#if defined( LORACOMBO_ENABLED )
    if( LoRaMacIsStarted() == false )
    {
        return;  // currently LoRaWAN is stopped
    }
#endif

    if (LoRaMacDebug.Mode & LORAMAC_DEBUG_MODE_PING_UNICAST)
    {
        print( "*PNG:U," );
        print_dec( (uint32_t)pingSlotTime, 10, '\0' );
        print_newline();
    }
}

void LoRaMacDebugLoRaMacClassBProcessMulticastSlot(TimerTime_t multicastSlotTime)
{
#if defined( LORACOMBO_ENABLED )
    if( LoRaMacIsStarted() == false )
    {
        return;  // currently LoRaWAN is stopped
    }
#endif

    if (LoRaMacDebug.Mode & LORAMAC_DEBUG_MODE_PING_MULTICAST)
    {
        print( "*PNG:M," ); 
        print_dec( (uint32_t)multicastSlotTime, 10, '\0' );
        print_newline();
    }
}
#endif

int16_t LoRaMacDebugSetPseudoLowPower( void )
{
    int16_t procResult = SUCCESS;
    uint32_t _boardMcuWokeUpBy_prev;

    BoardDisableAllIrq();
    
    if( RP_LOWPWR_COND_ENTRY )  // Do not enter STOP mode if there's no timer running
    {
        SetLowPowerFlag(WAKEUP_TRIGGER_READY);

        do
        {
            // for pseudo-sleep MCU
            _boardMcuWokeUpBy_prev = BoardMcuWokeUpBy;

            BoardEnableAllIrq( );
            while (1)
            {
                // loop until interrup
                if ((BoardMcuWokeUpBy != _boardMcuWokeUpBy_prev) ||
                    (BoardIsLowPowerAllowed() == false))
                {
                    break;
                }
            }
            BoardDisableAllIrq( );

            // Enter STOP mode again if MCU wakes up from adjustment interrupt by RTC or TRJ0.
        } while ( RP_LOWPWR_COND_LOOP );
        
        ClearLowPowerFlag();
    }
    
    BoardEnableAllIrq();
    
    return procResult;      // Only returns SUCCESS for now
}

static uint16_t LoRaMacDebugGetBandwidth( int8_t bandWidthIndex )
{
    int16_t bandWidth;

    switch( bandWidthIndex )
    {
        case 0:
            bandWidth = 125;        // BW125 kHz
            break;
        case 1:
            bandWidth = 250;        // BW250 kHz
            break;
        case 2:
            bandWidth = 500;        // BW500 kHz
            break;
        default:
            bandWidth = 0;          // unexpected value
            break;
    }
    return( bandWidth );
}
#endif

