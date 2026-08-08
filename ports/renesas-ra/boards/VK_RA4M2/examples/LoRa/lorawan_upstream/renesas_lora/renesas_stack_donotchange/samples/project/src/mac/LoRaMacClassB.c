/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
    (C)2013 Semtech
 ___ _____ _   ___ _  _____ ___  ___  ___ ___
/ __|_   _/_\ / __| |/ / __/ _ \| _ \/ __| __|
\__ \ | |/ _ \ (__| ' <| _| (_) |   / (__| _|
|___/ |_/_/ \_\___|_|\_\_| \___/|_|_\\___|___|
embedded.connectivity.solutions===============

Description: LoRa MAC Class B layer implementation

License: Revised BSD License, see LICENSE.TXT file include in the project

Maintainer: Miguel Luis ( Semtech ), Gregory Cristian ( Semtech ) and Daniel Jaeckle ( STACKFORCE )
*/
/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
#include "stdio.h"
#include "board.h"

#include <math.h>
#include "secure-element.h"
#include "LoRaMac.h"
#include "LoRaMacClassB.h"
#include "LoRaMacClassBConfig.h"
#include "LoRaMacCrypto.h"
#include "LoRaMacConfirmQueue.h"
#include "utilities.h"


#ifdef LORAMAC_CLASSB_ENABLED


/*
 * LoRaMac Class B Context structure for NVM parameters
 * related to ping slots
 */
typedef struct sLoRaMacClassBPingSlotNvmCtx
{
    struct sPingSlotCtrlNvm
    {
        // not use Assigned bit
        uint8_t reserved         : 1;
        /*!
         * Set when a custom frequency is used
         */
        uint8_t CustomFreq       : 1;
    }Ctrl;
    /*!
     * Number of ping slots
     */
    uint8_t PingNb;
    /*!
     * Period of the ping slots
     */
    uint16_t PingPeriod;
    /*!
     * Reception frequency of the ping slot windows
     */
    uint32_t Frequency;
    /*!
     * Datarate of the ping slot
     */
    int8_t Datarate;
    /*!
     * Periodicity
     */
    uint8_t Periodicity;
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
    /*!
     * Set to 1, if the FPending bit is set
     */
    uint8_t FPendingSet;
#endif
} LoRaMacClassBPingSlotNvmCtx_t;

/*
 * LoRaMac Class B Context structure for NVM parameters
 * related to beaconing
 */
typedef struct sLoRaMacClassBBeaconNvmCtx
{
    struct sBeaconCtrlNvm
    {
        /*!
         * Set if the node has a custom frequency for beaconing and ping slots
         */
        uint8_t CustomFreq          : 1;
    }Ctrl;
    /*!
     * Beacon reception frequency
     */
    uint32_t Frequency;
} LoRaMacClassBBeaconNvmCtx_t;

/*
 * LoRaMac Class B Context structure
 */
typedef struct sLoRaMacClassBNvmCtx
{
    /*!
    * Class B ping slot context
    */
    LoRaMacClassBPingSlotNvmCtx_t PingSlotCtx;
    /*!
    * Class B beacon context
    */
    LoRaMacClassBBeaconNvmCtx_t BeaconCtx;
} LoRaMacClassBNvmCtx_t;

/*
 * LoRaMac Class B Context structure
 */
typedef struct sLoRaMacClassBCtx
{
    /*!
    * Class B ping slot context
    */
    PingSlotContext_t PingSlotCtx;
    /*!
    * Class B beacon context
    */
    BeaconContext_t BeaconCtx;
    /*!
    * State of the beaconing mechanism
    */
    BeaconState_t BeaconState;
    /*!
    * State of the ping slot mechanism
    */
    PingSlotState_t PingSlotState;
    /*!
    * Timer for CLASS B beacon acquisition and tracking.
    */
    TimerEvent_t BeaconTimer;
    /*!
    * Timer for CLASS B ping slot timer.
    */
    TimerEvent_t PingSlotTimer;
    /*!
    * Container for the callbacks related to class b.
    */
    LoRaMacClassBCallback_t LoRaMacClassBCallbacks;
    /*!
    * Data structure which holds the parameters which needs to be set
    * in class b operation.
    */
    LoRaMacClassBParams_t LoRaMacClassBParams;
#ifdef LORAMAC_CTX_SAVERESTORE_ENALED
    /*
     * Callback function to notify the upper layer about context change
     */
    LoRaMacClassBNvmEvent LoRaMacClassBNvmEvent;
#endif
    /*!
    * Non-volatile module context.
    */
    LoRaMacClassBNvmCtx_t* NvmCtx;
    RxConfigParams_t BeaconAcquisitionConfig;
    RxConfigParams_t BeaconRxBaseConfig;
    RxConfigParams_t PingSlotBaseConfig;

    bool UpdatePingOffsetReq_unicast;

#if (LORAMAC_MAX_MC_CTX > 0)
    /*!
    * State of the multicast slot mechanism
    */
    PingSlotState_t MulticastSlotState;
    /*!
    * Timer for CLASS B multicast ping slot timer.
    */
    TimerEvent_t MulticastSlotTimer;

    bool UpdatePingOffsetReq_multicast;
#endif
} LoRaMacClassBCtx_t;

/*!
 * Defines the LoRaMac radio events status
 */
typedef union uLoRaMacClassBEvents
{
    uint32_t Value;
    struct sEvents
    {
        uint32_t Beacon        : 1;
        uint32_t PingSlot      : 1;
#if (LORAMAC_MAX_MC_CTX > 0)
        uint32_t MulticastSlot : 1;
#endif
    }Events;
}LoRaMacClassBEvents_t;

extern DeviceClass_t LoRaMacGetDeviceClass( void );
extern void OnRadioRxError( void );

LoRaMacClassBEvents_t LoRaMacClassBEvents = { .Value = 0 };

/*
 * Non-volatile module context.
 */
static LoRaMacClassBNvmCtx_t NvmCtx;

/*
 * Module context.
 */
static LoRaMacClassBCtx_t Ctx;

/*
 * Delta time of system timer
 */
SysTime_t SysTimeDeltaTimePrev;
int32_t SysTimeErrorTimeMs;
uint32_t BeaconTimeoutContinuousCnt;

#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
/*
 * Beacon transmit time precision in milliseconds.
 * The usage of these values shall be determined by the
 * prec value in param field received in a beacon frame.
 * As the time base is milli seconds, the precision will be either 0 ms or 1 ms.
 */
static const uint8_t BeaconPrecTimeValue[4] = { 0, 1, 1, 1 };
#endif

/*!
 * Get processiong time for beacon and ping slot
 */
enum eLoRaMacClassBProcTimeKind
{
    CLASSB_STACK_PROCTIME_SEL_BEACON_ACQISITION = 0,
    CLASSB_STACK_PROCTIME_SEL_BEACON,
    CLASSB_STACK_PROCTIME_SEL_PING_SLOT_SHORT_PERIOD,
    CLASSB_STACK_PROCTIME_SEL_PING_SLOT_LONG_PERIOD
};
static int32_t LoRaMacClassBGetStackProcessTime( uint8_t kind );

/*!
 * Computes the Ping Offset
 *
 * \param [IN]  beaconTime      - Time of the recent received beacon
 * \param [IN]  address         - Frame address
 * \param [IN]  pingPeriod      - Ping period of the node
 * \param [OUT] pingOffset      - Pseudo random ping offset
 */
static void ComputePingOffset( uint64_t beaconTime, uint32_t address, uint16_t pingPeriod, uint16_t *pingOffset )
{
    uint8_t buffer[16];
    uint8_t cipher[16];
    uint32_t result = 0;
    /* Refer to chapter 15.2 of the LoRaWAN specification v1.1. The beacon time
     * GPS time in seconds modulo 2^32
     */
    uint32_t time = ( beaconTime % ( ( ( uint64_t ) 1 ) << 32 ) );

    memset1( buffer, 0, 16 );
    memset1( cipher, 0, 16 );

    buffer[0] = ( time ) & 0xFF;
    buffer[1] = ( time >> 8 ) & 0xFF;
    buffer[2] = ( time >> 16 ) & 0xFF;
    buffer[3] = ( time >> 24 ) & 0xFF;

    buffer[4] = ( address ) & 0xFF;
    buffer[5] = ( address >> 8 ) & 0xFF;
    buffer[6] = ( address >> 16 ) & 0xFF;
    buffer[7] = ( address >> 24 ) & 0xFF;

    SecureElementAesEncrypt( buffer, 16, SLOT_RAND_ZERO_KEY, cipher );

    result = ( ( ( uint32_t ) cipher[0] ) + ( ( ( uint32_t ) cipher[1] ) * 256 ) );

    *pingOffset = ( uint16_t )( result % pingPeriod );
}

/*!
 * \brief Calculates the downlink frequency for a given channel.
 *
 * \param [IN] channel The channel according to the channel plan.
 *
 * \retval The downlink frequency
 */
static uint32_t CalcDownlinkFrequency( uint8_t channel, bool isBeacon )
{
    GetPhyParams_t getPhy;
    PhyParam_t phyParam;

    getPhy.Attribute = PHY_PING_SLOT_CHANNEL_FREQ;

    if( isBeacon == true )
    {
        getPhy.Attribute = PHY_BEACON_CHANNEL_FREQ;
    }
    getPhy.Channel = channel;
    phyParam = RegionGetPhyParam( *Ctx.LoRaMacClassBParams.LoRaMacRegion, &getPhy );

    return phyParam.Value;    
}

/*!
 * \brief Calculates the downlink channel for the beacon and for
 *        ping slot downlinks.
 *
 * \param [IN] devAddr The address of the device. Assign 0 if its a beacon.
 *
 * \param [IN] beaconTime The beacon time of the beacon.
 *
 * \param [IN] beaconInterval The beacon interval.
 *
 * \param [IN] isBeacon Set to true, if the function shall
 *                      calculate the frequency for a beacon.
 *
 * \retval The downlink channel
 */
static uint32_t CalcDownlinkChannelAndFrequency( uint32_t devAddr, TimerTime_t beaconTime,
                                                 TimerTime_t beaconInterval, bool isBeacon )
{
    GetPhyParams_t getPhy;
    PhyParam_t phyParam;
    uint32_t channel = 0;
    uint8_t nbChannels = 0;
    uint8_t offset = 0;

    // Default initialization - ping slot channels
    getPhy.Attribute = PHY_PING_SLOT_NB_CHANNELS;

    if( isBeacon == true )
    {
        // Beacon channels
        getPhy.Attribute = PHY_BEACON_NB_CHANNELS;
    }
    phyParam = RegionGetPhyParam( *Ctx.LoRaMacClassBParams.LoRaMacRegion, &getPhy );
    nbChannels = ( uint8_t ) phyParam.Value;

    // nbChannels is > 1, when the channel plan requires more than one possible channel
    // defined by the calculation below.
    if( nbChannels > 1 )
    {
        getPhy.Attribute = PHY_BEACON_CHANNEL_OFFSET;
        phyParam = RegionGetPhyParam( *Ctx.LoRaMacClassBParams.LoRaMacRegion, &getPhy );
        offset = ( uint8_t ) phyParam.Value;

        // Calculate the channel for the next downlink
        channel  = devAddr + ( beaconTime / ( beaconInterval / 1000 ) );
        channel  = channel % nbChannels;
        channel += offset;  // offset is for CN470(RP2-1.0.3); the others are 0.
    }

    // Calculate the frequency for the next downlink. This holds
    // for beacons and ping slots.
    return CalcDownlinkFrequency( channel, isBeacon );
}

/*!
 * \brief Calculates the correct frequency and opens up the beacon reception window.
 *
 * \param [IN] rxTime The reception time which should be setup
 *
 * \param [IN] activateDefaultChannel Set to true, if the function shall setup the default channel
 */
static void RxBeaconSetup( TimerTime_t rxTime, bool activateDefaultChannel )
{
    RxBeaconSetup_t rxBeaconSetup;
    uint32_t frequency = 0;
    uint16_t windowTimeout;

    if( activateDefaultChannel == true )
    {
        // This is the default frequency in case we don't know when the next
        // beacon will be transmitted. We select channel 0 as default.
        frequency = CalcDownlinkFrequency( 0, true );
    }
    else
    {
        // This is the frequency according to the channel plan
        frequency = CalcDownlinkChannelAndFrequency( 0, Ctx.BeaconCtx.BeaconTime.Seconds + ( CLASSB_BEACON_INTERVAL / 1000 ),
                                                     CLASSB_BEACON_INTERVAL, true );
    }

    if( Ctx.NvmCtx->BeaconCtx.Ctrl.CustomFreq == 1 )
    {
        // Set the frequency from the BeaconFreqReq
        frequency = Ctx.NvmCtx->BeaconCtx.Frequency;
    }

    if( Ctx.BeaconCtx.Ctrl.BeaconChannelSet == 1 )
    {
        // Set the frequency which was provided by BeaconTimingAns MAC command
        Ctx.BeaconCtx.Ctrl.BeaconChannelSet = 0;
        frequency = CalcDownlinkFrequency( Ctx.BeaconCtx.BeaconTimingChannel, true );
    }

    if ((Ctx.BeaconCtx.Ctrl.AcquisitionPending == 1) && (rxTime != 0))
    {
        windowTimeout = Ctx.BeaconAcquisitionConfig.WindowTimeout;
    }
    else
    {
        windowTimeout = Ctx.BeaconRxBaseConfig.WindowTimeout + Ctx.BeaconCtx.BeaconRxEnlargeWindowTimeout;
    }

    rxBeaconSetup.SymbolTimeout = windowTimeout;
    rxBeaconSetup.RxTime = rxTime;
    rxBeaconSetup.Frequency = frequency;

    RegionRxBeaconSetup( *Ctx.LoRaMacClassBParams.LoRaMacRegion, &rxBeaconSetup, &Ctx.LoRaMacClassBParams.McpsIndication->RxDatarate );

    Ctx.LoRaMacClassBParams.MlmeIndication->BeaconInfo.Frequency = frequency;
    Ctx.LoRaMacClassBParams.MlmeIndication->BeaconInfo.Datarate = Ctx.LoRaMacClassBParams.McpsIndication->RxDatarate;
}

/*!
 * \brief Calculates the next ping slot time.
 *
 * \param [IN] slotOffset The ping slot offset
 * \param [IN] pingPeriod The ping period
 * \param [OUT] timeOffset Time offset of the next slot, based on current time
 *
 * \retval [true: ping slot found, false: no ping slot found]
 */
static bool CalcNextSlotTime( uint16_t slotOffset, uint16_t pingPeriod, uint16_t pingNb, TimerTime_t* timeOffset )
{
    uint8_t currentPingSlot = 0;
    TimerTime_t slotTime = 0;
    TimerTime_t currentTime = TimerGetCurrentTime( );

    // Calculate the point in time of the last beacon even if we missed it
    slotTime = ( ( currentTime - SysTimeToMs( Ctx.BeaconCtx.LastBeaconRx ) ) % CLASSB_BEACON_INTERVAL );
    slotTime = currentTime - slotTime;

    // Add the reserved time and the ping offset
    slotTime += CLASSB_BEACON_RESERVED;
    slotTime += ((uint32_t)slotOffset * CLASSB_PING_SLOT_WINDOW);

    if( slotTime < currentTime )
    {
        currentPingSlot = ( ( currentTime - slotTime ) /
                          ( (uint32_t)pingPeriod * CLASSB_PING_SLOT_WINDOW ) ) + 1;
        slotTime += ( ( TimerTime_t )(( (uint32_t)currentPingSlot * pingPeriod ) *
                    CLASSB_PING_SLOT_WINDOW ));
    }

    if( currentPingSlot < pingNb )
    {
        if( slotTime <= ( SysTimeToMs( Ctx.BeaconCtx.NextBeaconRx ) - CLASSB_BEACON_GUARD - CLASSB_PING_SLOT_WINDOW ) )
        {
            // Calculate the relative ping slot time
            slotTime -= currentTime;
            slotTime = TimerTempCompensation( slotTime, Ctx.BeaconCtx.Temperature );
            *timeOffset = slotTime;
            return true;
        }
    }
    return false;
}

/*!
 * \brief Calculates CRC's of the beacon frame
 *
 * \param [IN] buffer Pointer to the data
 * \param [IN] length Length of the data
 *
 * \retval CRC
 */
static uint16_t BeaconCrc( uint8_t *buffer, uint16_t length )
{
    // The CRC calculation follows CCITT
    const uint16_t polynom = 0x1021;
    // CRC initial value
    uint16_t crc = 0x0000;

    if( buffer == NULL )
    {
        return 0;
    }

    for( uint16_t i = 0; i < length; ++i )
    {
        crc ^= ( uint16_t ) buffer[i] << 8;
        for( uint16_t j = 0; j < 8; ++j )
        {
            crc = ( crc & 0x8000 ) ? ( crc << 1 ) ^ polynom : ( crc << 1 );
        }
    }

    return crc;
}

static void GetTemperatureLevel( LoRaMacClassBCallback_t *callbacks, BeaconContext_t *beaconCtx )
{
    // Measure temperature, if available
    if( ( callbacks != NULL ) && ( callbacks->GetTemperatureLevel != NULL ) )
    {
        beaconCtx->Temperature = callbacks->GetTemperatureLevel( );
    }
}

static void OnClassBMacProcessNotify( void )
{
    if( Ctx.LoRaMacClassBCallbacks.MacProcessNotify != NULL )
    {
        Ctx.LoRaMacClassBCallbacks.MacProcessNotify( );
    }
}

static void InitClassB( void )
{
    GetPhyParams_t getPhy;
    PhyParam_t phyParam;

    // Init events
    LoRaMacClassBEvents.Value = 0;

    // Init variables to default
    memset1( ( uint8_t* ) &NvmCtx, 0, sizeof( LoRaMacClassBNvmCtx_t ) );
    memset1( ( uint8_t* ) &Ctx.PingSlotCtx, 0, sizeof( PingSlotContext_t ) );
    memset1( ( uint8_t* ) &Ctx.BeaconCtx, 0, sizeof( BeaconContext_t ) );

    // Setup default temperature
    Ctx.BeaconCtx.Temperature = 25.0;
    GetTemperatureLevel( &Ctx.LoRaMacClassBCallbacks, &Ctx.BeaconCtx );

    // Setup default ping slot datarate
    getPhy.Attribute = PHY_PING_SLOT_CHANNEL_DR;
    phyParam = RegionGetPhyParam( *Ctx.LoRaMacClassBParams.LoRaMacRegion, &getPhy );
    Ctx.NvmCtx->PingSlotCtx.Datarate = (int8_t)( phyParam.Value );

    // Setup default ping slot periodicity
    LoRaMacClassBSetPingSlotInfo( CLASSB_PING_SLOT_PERIODICITY_DEFAULT );

    // Setup beacon/PingSlot Rx config
    LoRaMacClassBComputeBeaconAcquisitionWindowParameters();
    LoRaMacClassBComputeBeaconWindowParameters();
    LoRaMacClassBComputePingSlotWindowParameters();

    // Setup default states
    Ctx.BeaconState = BEACON_STATE_ACQUISITION;
    Ctx.PingSlotState = PINGSLOT_STATE_CALC_PING_OFFSET;
#if (LORAMAC_MAX_MC_CTX > 0)
    Ctx.MulticastSlotState = PINGSLOT_STATE_CALC_PING_OFFSET;
#endif
}

static void InitClassBDefaults( void )
{
    // This function shall reset the Class B settings to default,
    // but should keep important configurations
    LoRaMacClassBBeaconNvmCtx_t nvmBeaconCtx = Ctx.NvmCtx->BeaconCtx;
    LoRaMacClassBPingSlotNvmCtx_t nvmPingSlotCtx = Ctx.NvmCtx->PingSlotCtx;
    BeaconContext_t beaconCtx = Ctx.BeaconCtx;

    InitClassB( );

    // Parameters from BeaconFreqReq
    Ctx.NvmCtx->BeaconCtx.Frequency = nvmBeaconCtx.Frequency;
    Ctx.NvmCtx->BeaconCtx.Ctrl.CustomFreq = nvmBeaconCtx.Ctrl.CustomFreq;

    // PingSlot parameters 
    Ctx.NvmCtx->PingSlotCtx = nvmPingSlotCtx;

    // Beacon parameters 
    Ctx.BeaconCtx.Ctrl.BeaconDelaySet = beaconCtx.Ctrl.BeaconDelaySet;
    Ctx.BeaconCtx.BeaconTimingDelay = beaconCtx.BeaconTimingDelay;
    Ctx.BeaconCtx.NextBeaconRx = beaconCtx.NextBeaconRx;

    Ctx.BeaconCtx.LastSystimeSetTimeMs = beaconCtx.LastSystimeSetTimeMs;
    Ctx.BeaconCtx.LastSystimeSetIsBeacon = beaconCtx.LastSystimeSetIsBeacon;

}

static void EnlargeWindowTimeout( TimerTime_t currentTime, bool isFirstTrack )
{
    RxConfigParams_t beaconRxEnlargeConfig;
    RxConfigParams_t pingSlotRxEnlargeConfig;
    uint32_t         rxEnlargeRxErr;
    uint32_t         clkErrorTime;
    uint8_t          clkErrorPpm;

    if(isFirstTrack == true)
    {
        clkErrorPpm = BOARD_CLOCK_ERROR_PPM_MAX;
    }
    else
    {
        clkErrorPpm = BOARD_CLOCK_ERROR_PPM;
    }

    // calculate clock error time
    clkErrorTime = TimerGetClockErrorTime( currentTime - Ctx.BeaconCtx.LastSystimeSetTimeMs + CLASSB_BEACON_INTERVAL,
                                           clkErrorPpm );

    rxEnlargeRxErr = Ctx.LoRaMacClassBParams.LoRaMacParams->SystemMaxRxError + clkErrorTime;

    if( (Ctx.BeaconRxBaseConfig.WindowTimeout + Ctx.BeaconCtx.BeaconRxEnlargeWindowTimeout) < CLASSB_BEACON_SYMBOL_TO_EXPANSION_MAX )
    {
        // Update beacon movement and symbol timeout
        beaconRxEnlargeConfig.RxSlot = RX_SLOT_WIN_BEACON;  // *input* RX_SLOT_WIN_BEACON is __RL78__ only
        RegionComputeRxWindowParameters( *Ctx.LoRaMacClassBParams.LoRaMacRegion,
                                         Ctx.BeaconRxBaseConfig.Datarate,
                                         Ctx.LoRaMacClassBParams.LoRaMacParams->MinRxSymbols,
                                         rxEnlargeRxErr,
                                         &beaconRxEnlargeConfig );

        if( beaconRxEnlargeConfig.WindowTimeout >= CLASSB_BEACON_SYMBOL_TO_EXPANSION_MAX )
        {
            beaconRxEnlargeConfig.WindowTimeout = CLASSB_BEACON_SYMBOL_TO_EXPANSION_MAX;
        }
        beaconRxEnlargeConfig.WindowOffset -= LoRaMacClassBGetStackProcessTime( CLASSB_STACK_PROCTIME_SEL_BEACON );

        Ctx.BeaconCtx.BeaconRxEnlargeWindowTimeout = beaconRxEnlargeConfig.WindowTimeout - Ctx.BeaconRxBaseConfig.WindowTimeout;
        Ctx.BeaconCtx.BeaconRxEnlargeWindowOffset  = beaconRxEnlargeConfig.WindowOffset - Ctx.BeaconRxBaseConfig.WindowOffset;
    }

    if( (Ctx.PingSlotBaseConfig.WindowTimeout + Ctx.PingSlotCtx.PingSlotRxEnlargeWindowTimeout) < CLASSB_PING_SLOT_SYMBOL_TO_EXPANSION_MAX )
    {
        // Update ping slot movement and symbol timeout
        RegionComputeRxWindowParameters( *Ctx.LoRaMacClassBParams.LoRaMacRegion,
                                         Ctx.NvmCtx->PingSlotCtx.Datarate,
                                         Ctx.LoRaMacClassBParams.LoRaMacParams->MinRxSymbols,
                                         rxEnlargeRxErr,
                                         &pingSlotRxEnlargeConfig );

        if( pingSlotRxEnlargeConfig.WindowTimeout >= CLASSB_PING_SLOT_SYMBOL_TO_EXPANSION_MAX )
        {
            pingSlotRxEnlargeConfig.WindowTimeout = CLASSB_PING_SLOT_SYMBOL_TO_EXPANSION_MAX;
        }
        // Ping slot will be started after wake up from sleep-cold or sleep-warm; it depends on periodicty
        if( Ctx.NvmCtx->PingSlotCtx.Periodicity >= CLASSB_PING_SLOT_PERIODICITY_RFSLEEP_THRESHOLD )
        {
            pingSlotRxEnlargeConfig.WindowOffset -= LoRaMacClassBGetStackProcessTime( CLASSB_STACK_PROCTIME_SEL_PING_SLOT_LONG_PERIOD );
        }
        else
        {
            pingSlotRxEnlargeConfig.WindowOffset -= LoRaMacClassBGetStackProcessTime( CLASSB_STACK_PROCTIME_SEL_PING_SLOT_SHORT_PERIOD );
        }

        Ctx.PingSlotCtx.PingSlotRxEnlargeWindowTimeout = pingSlotRxEnlargeConfig.WindowTimeout - Ctx.PingSlotBaseConfig.WindowTimeout;
        Ctx.PingSlotCtx.PingSlotRxEnlargeWindowOffset  = pingSlotRxEnlargeConfig.WindowOffset - Ctx.PingSlotBaseConfig.WindowOffset;
    }
}

static void ResetWindowTimeout( void )
{
    Ctx.BeaconCtx.BeaconRxEnlargeWindowOffset  = 0;
    Ctx.BeaconCtx.BeaconRxEnlargeWindowTimeout = 0;

    Ctx.PingSlotCtx.PingSlotRxEnlargeWindowOffset  = 0;
    Ctx.PingSlotCtx.PingSlotRxEnlargeWindowTimeout = 0;
}

static TimerTime_t CalcDelayForNextBeacon( TimerTime_t currentTime, TimerTime_t lastBeaconRx )
{
    TimerTime_t nextBeaconRxTime = 0;

    // Calculate the point in time of the next beacon
    nextBeaconRxTime = ( ( currentTime - lastBeaconRx ) % CLASSB_BEACON_INTERVAL );
    return ( CLASSB_BEACON_INTERVAL - nextBeaconRxTime );
}

static void IndicateBeaconStatus( LoRaMacEventInfoStatus_t status )
{
    if( Ctx.BeaconCtx.Ctrl.ResumeBeaconing == 0 )
    {
        Ctx.LoRaMacClassBParams.MlmeIndication->MlmeIndication = MLME_BEACON;
        Ctx.LoRaMacClassBParams.MlmeIndication->Status = status;
        Ctx.LoRaMacClassBParams.LoRaMacFlags->Bits.MlmeInd = 1;

        Ctx.LoRaMacClassBParams.LoRaMacFlags->Bits.MacDone = 1;
    }
    Ctx.BeaconCtx.Ctrl.ResumeBeaconing = 0;
}

static TimerTime_t ApplyGuardTime( TimerTime_t beaconEventTime )
{
    TimerTime_t timeGuard = beaconEventTime;

    if( timeGuard > CLASSB_BEACON_GUARD )
    {
        timeGuard -= CLASSB_BEACON_GUARD;
    }
    return timeGuard;
}

static TimerTime_t UpdateBeaconState( LoRaMacEventInfoStatus_t status,
                                      int32_t enlargeOffset, TimerTime_t currentTime )
{
    uint32_t beaconEventTime = 0;
    int32_t  adjustGuardStart;

    // Calculate the next beacon RX time
    beaconEventTime = (uint32_t)CalcDelayForNextBeacon( currentTime, SysTimeToMs( Ctx.BeaconCtx.LastBeaconRx ) );
    Ctx.BeaconCtx.NextBeaconRx = SysTimeFromMs( currentTime + beaconEventTime );

    // Take temperature compensation into account
    beaconEventTime = (uint32_t)TimerTempCompensation( beaconEventTime, Ctx.BeaconCtx.Temperature );

    // Move the window
    adjustGuardStart = enlargeOffset - SysTimeErrorTimeMs;
    if( ((int32_t)beaconEventTime + adjustGuardStart) > 0 )
    {
        beaconEventTime += adjustGuardStart;
    }
    Ctx.BeaconCtx.NextBeaconRxAdjusted = currentTime + beaconEventTime;

    // Start the RX slot state machine for ping and multicast slots
    if ( LoRaMacGetDeviceClass() == CLASS_B )
    {
        LoRaMacClassBStartRxSlots( );
    }

    // Setup an MLME_BEACON indication to inform the upper layer
    IndicateBeaconStatus( status );

    // Apply guard time
    return ApplyGuardTime( beaconEventTime );
}

static uint8_t CalcPingNb( uint16_t periodicity )
{
    return 128 / ( 1 << periodicity );
}

static uint16_t CalcPingPeriod( uint8_t pingNb )
{
    return CLASSB_BEACON_WINDOW_SLOTS / pingNb;
}

#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
static bool CheckSlotPriority( uint32_t currentAddress, uint8_t currentFPendingSet, uint8_t currentIsMulticast,
                               uint32_t address, uint8_t fPendingSet, uint8_t isMulticast )
{
    if( currentFPendingSet != fPendingSet )
    {
        if( currentFPendingSet < fPendingSet )
        {
            // New slot sequence has priority. It does not matter
            // which type it is
            return true;
        }
        return false;
    }
    else
    {
        // FPendingSet has the same priority level, decide
        // based on multicast or unicast setting
        if( currentIsMulticast != isMulticast )
        {
            if( currentIsMulticast < isMulticast )
            {
                // New slot sequence has priority. Multicasts have
                // more priority than unicasts
                return true;
            }
            return false;
        }
        else
        {
            // IsMulticast has the same priority level, decide
            // based on the highest address
            if( currentAddress < address )
            {
                // New slot sequence has priority. The sequence with
                // the highest address has priority
                return true;
            }
        }
    }
    return false;
}
#endif

/*
 * Dummy callback in case if the user provides NULL function pointer
 */
static void NvmContextChange( void )
{
#ifdef LORAMAC_CTX_SAVERESTORE_ENALED
    if( Ctx.LoRaMacClassBNvmEvent != NULL )
    {
        Ctx.LoRaMacClassBNvmEvent( );
    }
#endif
}

#endif // LORAMAC_CLASSB_ENABLED

void LoRaMacClassBInit( LoRaMacClassBParams_t *classBParams, LoRaMacClassBCallback_t *callbacks, LoRaMacClassBNvmEvent classBNvmCtxChanged )
{
#ifdef LORAMAC_CLASSB_ENABLED
    // Store callbacks
    Ctx.LoRaMacClassBCallbacks = *callbacks;

    // Store parameter pointers
    Ctx.LoRaMacClassBParams = *classBParams;

    // Assign non-volatile context
    Ctx.NvmCtx = &NvmCtx;

#ifdef LORAMAC_CTX_SAVERESTORE_ENALED
    // Assign callback
    Ctx.LoRaMacClassBNvmEvent = classBNvmCtxChanged;
#endif

    // Initialize timers
    TimerInit( &Ctx.BeaconTimer, LoRaMacClassBBeaconTimerEvent );
    TimerInit( &Ctx.PingSlotTimer, LoRaMacClassBPingSlotTimerEvent );
#if (LORAMAC_MAX_MC_CTX > 0)
    TimerInit( &Ctx.MulticastSlotTimer, LoRaMacClassBMulticastSlotTimerEvent );
#endif

    InitClassB( );
#endif // LORAMAC_CLASSB_ENABLED
}

#ifdef LORAMAC_CTX_SAVERESTORE_ENALED
bool LoRaMacClassBRestoreNvmCtx( void* classBNvmCtx )
{
#ifdef LORAMAC_CLASSB_ENABLED
    // Restore module context
    if( classBNvmCtx != NULL )
    {
        memcpy1( ( uint8_t* ) &NvmCtx, ( uint8_t* ) classBNvmCtx, sizeof( NvmCtx ) );
        return true;
    }
    else
    {
        return false;
    }
#else
    return true;
#endif // LORAMAC_CLASSB_ENABLED
}

void* LoRaMacClassBGetNvmCtx( size_t* classBNvmCtxSize )
{
#ifdef LORAMAC_CLASSB_ENABLED
    *classBNvmCtxSize = sizeof( NvmCtx );
    return &NvmCtx;
#else
    *classBNvmCtxSize = 0;
    return NULL;
#endif // LORAMAC_CLASSB_ENABLED
}
#endif

void LoRaMacClassBComputeBeaconAcquisitionWindowParameters( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    GetPhyParams_t  getPhy;
    PhyParam_t      phyParam;
    uint32_t        systemMaxRxError;
    uint32_t        clkErrorTime;
    TimerTime_t     currentTime;

    currentTime = TimerGetCurrentTime();

    if( (Ctx.BeaconCtx.LastSystimeSetTimeMs > 0) &&
        ( (Ctx.BeaconCtx.LastSystimeSetTimeMs + CLASSB_MAX_BEACON_LESS_PERIOD) >= currentTime ) )
    {
        // calculate clock error time
        clkErrorTime = TimerGetClockErrorTime( currentTime - Ctx.BeaconCtx.LastSystimeSetTimeMs,
                                               BOARD_CLOCK_ERROR_PPM_MAX );

        if( Ctx.BeaconCtx.LastSystimeSetIsBeacon == true )
        {
            // use GPS time information that is on received beacon frame
            Ctx.BeaconCtx.Ctrl.BeaconDelaySet = 1;
            Ctx.BeaconCtx.BeaconTimingDelay = SysTimeToMs( Ctx.BeaconCtx.NextBeaconRx ) - currentTime;
        }
        else
        {
            // DeviceTimeAns/BeaconTimingAns was received
            // -> network time (DeviceTimeAns) has worst case accuracy of +/-100msec.
            clkErrorTime += CLASSB_NETWORKTIME_ACCURACY;

            if( Ctx.BeaconCtx.Ctrl.BeaconDelaySet == 0 )
            {
                // Previous beacon acquisition is failed.
                Ctx.BeaconCtx.Ctrl.BeaconDelaySet = 1;
                Ctx.BeaconCtx.BeaconTimingDelay = SysTimeToMs( Ctx.BeaconCtx.NextBeaconRx ) - currentTime;
            }
        }
    }
    else
    {
        // SysTime is not set
        // or starting beacon acquisition is too late from SysTimeSet timimg
        clkErrorTime = 0;

        // Clear systime setting information
        Ctx.BeaconCtx.LastSystimeSetTimeMs = 0;
        Ctx.BeaconCtx.LastSystimeSetIsBeacon = false;

        // Perform beacon acquisition with continuous Rx.
        Ctx.BeaconCtx.Ctrl.BeaconDelaySet = 0;
        Ctx.BeaconCtx.BeaconTimingDelay = 0;
        Ctx.BeaconCtx.Ctrl.BeaconChannelSet = 0;  // Reset status provides by BeaconTimingAns
    }

    systemMaxRxError = Ctx.LoRaMacClassBParams.LoRaMacParams->SystemMaxRxError
                     + clkErrorTime;

    /* BeaconAcquisition RX window parameters */
    getPhy.Attribute = PHY_BEACON_CHANNEL_DR;
    phyParam = RegionGetPhyParam( *Ctx.LoRaMacClassBParams.LoRaMacRegion, &getPhy );

    Ctx.BeaconAcquisitionConfig.RxSlot = RX_SLOT_WIN_BEACON;  // *input* RX_SLOT_WIN_BEACON is __RL78__ only
    RegionComputeRxWindowParameters( *Ctx.LoRaMacClassBParams.LoRaMacRegion,
                                    ( int8_t )phyParam.Value, // datarate
                                    Ctx.LoRaMacClassBParams.LoRaMacParams->MinRxSymbols,
                                    systemMaxRxError,
                                    &(Ctx.BeaconAcquisitionConfig) );

    // Acquisition will be started after radio wake up
    Ctx.BeaconAcquisitionConfig.WindowOffset -= LoRaMacClassBGetStackProcessTime( CLASSB_STACK_PROCTIME_SEL_BEACON_ACQISITION );
#endif  // LORAMAC_CLASSB_ENABLED
}

void LoRaMacClassBComputeBeaconWindowParameters( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    GetPhyParams_t  getPhy;
    PhyParam_t      phyParam;
    uint32_t        systemMaxRxError;
    uint32_t        clkErrorTime;

    // calculate clock error time
    clkErrorTime = TimerGetClockErrorTime( CLASSB_BEACON_INTERVAL, BOARD_CLOCK_ERROR_PPM );

    systemMaxRxError = Ctx.LoRaMacClassBParams.LoRaMacParams->SystemMaxRxError + clkErrorTime;
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
    systemMaxRxError += Ctx.BeaconCtx.BeaconTimePrecision.SubSeconds;
#endif

    /* Beacon RX window parameters */
    getPhy.Attribute = PHY_BEACON_CHANNEL_DR;
    phyParam = RegionGetPhyParam( *Ctx.LoRaMacClassBParams.LoRaMacRegion, &getPhy );

    Ctx.BeaconRxBaseConfig.RxSlot = RX_SLOT_WIN_BEACON;  // *input* RX_SLOT_WIN_BEACON is __RL78__ only
    RegionComputeRxWindowParameters( *Ctx.LoRaMacClassBParams.LoRaMacRegion,
                                     ( int8_t )phyParam.Value, // datarate
                                     Ctx.LoRaMacClassBParams.LoRaMacParams->MinRxSymbols,
                                     systemMaxRxError,
                                     &(Ctx.BeaconRxBaseConfig) );

    // Beacon tracking will be started after radio wake up
    Ctx.BeaconRxBaseConfig.WindowOffset -= LoRaMacClassBGetStackProcessTime( CLASSB_STACK_PROCTIME_SEL_BEACON );
#endif  // LORAMAC_CLASSB_ENABLED
}

void LoRaMacClassBComputePingSlotWindowParameters( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    uint32_t        systemMaxRxError;
    uint32_t        clkErrorTime;

    // calculate clock error time
    clkErrorTime = TimerGetClockErrorTime( CLASSB_BEACON_INTERVAL, BOARD_CLOCK_ERROR_PPM );

    systemMaxRxError = Ctx.LoRaMacClassBParams.LoRaMacParams->SystemMaxRxError + clkErrorTime;
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
    systemMaxRxError += Ctx.BeaconCtx.BeaconTimePrecision.SubSeconds;
#endif

    RegionComputeRxWindowParameters( *Ctx.LoRaMacClassBParams.LoRaMacRegion,
                                     Ctx.NvmCtx->PingSlotCtx.Datarate,
                                     Ctx.LoRaMacClassBParams.LoRaMacParams->MinRxSymbols,
                                     systemMaxRxError,
                                     &(Ctx.PingSlotBaseConfig) );

    // Ping slot will be started after wake up from sleep-cold or sleep-warm; it depends on periodicty
    if( Ctx.NvmCtx->PingSlotCtx.Periodicity >= CLASSB_PING_SLOT_PERIODICITY_RFSLEEP_THRESHOLD )
    {
        Ctx.PingSlotBaseConfig.WindowOffset -= LoRaMacClassBGetStackProcessTime( CLASSB_STACK_PROCTIME_SEL_PING_SLOT_LONG_PERIOD );
    }
    else
    {
        Ctx.PingSlotBaseConfig.WindowOffset -= LoRaMacClassBGetStackProcessTime( CLASSB_STACK_PROCTIME_SEL_PING_SLOT_SHORT_PERIOD );
    }
#endif  // LORAMAC_CLASSB_ENABLED
}

void LoRaMacClassBSetBeaconState( BeaconState_t beaconState )
{
#ifdef LORAMAC_CLASSB_ENABLED
    if( beaconState == BEACON_STATE_ACQUISITION )
    {
        // If the MAC has received a time reference for the beacon,
        // apply the state BEACON_STATE_ACQUISITION_BY_TIME.
        if( ( Ctx.BeaconCtx.Ctrl.BeaconDelaySet == 1 ) &&
            ( LoRaMacClassBIsAcquisitionPending( ) == false ) )
        {
            Ctx.BeaconState = BEACON_STATE_ACQUISITION_BY_TIME;
        }
        else
        {
           Ctx.BeaconState = beaconState;
        }
    }
    else
    {
        if( ( Ctx.BeaconState != BEACON_STATE_ACQUISITION ) &&
            ( Ctx.BeaconState != BEACON_STATE_ACQUISITION_BY_TIME ) )
        {
            Ctx.BeaconState = beaconState;
        }
    }
#endif // LORAMAC_CLASSB_ENABLED
}

void LoRaMacClassBSetPingSlotState( PingSlotState_t pingSlotState )
{
#ifdef LORAMAC_CLASSB_ENABLED
    Ctx.PingSlotState = pingSlotState;
#endif // LORAMAC_CLASSB_ENABLED
}

void LoRaMacClassBSetMulticastSlotState( PingSlotState_t multicastSlotState )
{
#ifdef LORAMAC_CLASSB_ENABLED
#if (LORAMAC_MAX_MC_CTX > 0)
    Ctx.MulticastSlotState = multicastSlotState;
#endif
#endif // LORAMAC_CLASSB_ENABLED
}

bool LoRaMacClassBIsAcquisitionInProgress( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    if( Ctx.BeaconState == BEACON_STATE_ACQUISITION_BY_TIME )
    {
        // In this case the acquisition is in progress, as the MAC has
        // a time reference for the next beacon RX.
        return true;
    }
    if( LoRaMacClassBIsAcquisitionPending( ) == true )
    {
        // In this case the acquisition is in progress, as the MAC
        // searches for a beacon.
        return true;
    }
    return false;
#else
    return false;
#endif // LORAMAC_CLASSB_ENABLED
}

void LoRaMacClassBBeaconTimerEvent( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    TimerStop( &Ctx.BeaconTimer );
    LoRaMacClassBEvents.Events.Beacon = 1;

    OnClassBMacProcessNotify();
#endif // LORAMAC_CLASSB_ENABLED
}

#ifdef LORAMAC_CLASSB_ENABLED
static void LoRaMacClassBProcessBeacon( void )
{
    bool activateTimer = false;
    TimerTime_t beaconEventTime = 0;
    TimerTime_t currentTime = TimerGetCurrentTime();
    TimerTime_t nextBeaconTimeMs;

    // Beacon state machine
    switch( Ctx.BeaconState )
    {
        case BEACON_STATE_ACQUISITION_BY_TIME:
        {
            activateTimer = true;

            if( Ctx.BeaconCtx.Ctrl.AcquisitionPending == 1 )
            {
                LORAMAC_RADIOSLEEP_BEACON_NOTFOUND( 0 );
                Ctx.BeaconState = BEACON_STATE_LOST;
            }
            else
            {
                // Default symbol timeouts
                ResetWindowTimeout( );

                if( Ctx.BeaconCtx.Ctrl.BeaconDelaySet == 1 )
                {
                    if( Ctx.BeaconCtx.BeaconTimingDelay > 0 )
                    {
                        nextBeaconTimeMs = SysTimeToMs( Ctx.BeaconCtx.NextBeaconRx );

                        // (re-)calcurate next beacon timing to care that aquisition is late from getting network time.
                        // network time (DeviceTimeAns) has worst case accuracy of +/-100msec.
                        while( (nextBeaconTimeMs + Ctx.BeaconAcquisitionConfig.WindowOffset) <= currentTime )
                        {
                            nextBeaconTimeMs += CLASSB_BEACON_INTERVAL;
                            // Reset status provides by BeaconTimingAns
                            Ctx.BeaconCtx.Ctrl.BeaconChannelSet = 0;
                        }

                        beaconEventTime = nextBeaconTimeMs + Ctx.BeaconAcquisitionConfig.WindowOffset - currentTime;
                        beaconEventTime = TimerTempCompensation( beaconEventTime, Ctx.BeaconCtx.Temperature );

                        Ctx.BeaconCtx.BeaconTimingDelay = 0;
                    }
                    if (beaconEventTime == 0) // go to the starting acquisition
                    {
                        activateTimer = false;

                        // Reset status provides by BeaconTimingAns
                        Ctx.BeaconCtx.Ctrl.BeaconDelaySet = 0;
                        // Set the node into acquisition mode
                        Ctx.BeaconCtx.Ctrl.AcquisitionPending = 1;

                        // Don't use the default channel. We know on which
                        // channel the next beacon will be transmitted
                        RxBeaconSetup( CLASSB_BEACON_RESERVED, false );
                    }
                }
                else
                {
                    Ctx.BeaconCtx.NextBeaconRx.Seconds = 0;
                    Ctx.BeaconCtx.NextBeaconRx.SubSeconds = 0;
                    Ctx.BeaconCtx.BeaconTimingDelay = 0;

                    Ctx.BeaconState = BEACON_STATE_ACQUISITION;
                }
            }
            break;
        }
        case BEACON_STATE_ACQUISITION:
        {
            activateTimer = true;

            if( Ctx.BeaconCtx.Ctrl.AcquisitionPending == 1 )
            {
                LORAMAC_RADIOSLEEP_BEACON_NOTFOUND( 0 );
                Ctx.BeaconState = BEACON_STATE_LOST;
            }
            else
            {
                // Default symbol timeouts
                ResetWindowTimeout( );

                Ctx.BeaconCtx.Ctrl.AcquisitionPending = 1;
                beaconEventTime = CLASSB_BEACON_INTERVAL;

                // Start the beacon acquisition. When the MAC has received a beacon in function
                // RxBeacon successfully, the next state is BEACON_STATE_LOCKED. If the MAC does not
                // find a beacon, the state machine will stay in state BEACON_STATE_ACQUISITION.
                // This state detects that a acquisition was pending previously and will change the next
                // state to BEACON_STATE_LOST.
                RxBeaconSetup( 0, true );
            }
            break;
        }
        case BEACON_STATE_TIMEOUT:
        {
            // We have to update the beacon time, since we missed a beacon
            Ctx.BeaconCtx.BeaconTime.Seconds += ( CLASSB_BEACON_INTERVAL / 1000 );
            Ctx.BeaconCtx.BeaconTime.SubSeconds = 0;

            // Enlarge window timeouts to increase the chance to receive the next beacon
            EnlargeWindowTimeout( currentTime, false );
            BeaconTimeoutContinuousCnt += 1;

            // Setup next state
            Ctx.BeaconState = BEACON_STATE_REACQUISITION;
        }
            // Intentional fall through
        case BEACON_STATE_REACQUISITION:
        {
            activateTimer = true;

            // The beacon is no longer acquired
            Ctx.BeaconCtx.Ctrl.BeaconAcquired = 0;

            // Verify if the maximum beacon less period has been elapsed
            if( ( currentTime - SysTimeToMs( Ctx.BeaconCtx.LastBeaconRx ) ) > CLASSB_MAX_BEACON_LESS_PERIOD )
            {
                Ctx.BeaconState = BEACON_STATE_LOST;
            }
            else
            {
                // Handle beacon miss
                beaconEventTime = UpdateBeaconState( LORAMAC_EVENT_INFO_STATUS_BEACON_LOST,
                                                     Ctx.BeaconCtx.BeaconRxEnlargeWindowOffset, currentTime );

                // Setup next state
                Ctx.BeaconState = BEACON_STATE_IDLE;
            }
            break;
        }
        case BEACON_STATE_LOCKED:
        {
            activateTimer = true;

            // We have received a beacon. Acquisition is no longer pending.
            Ctx.BeaconCtx.Ctrl.AcquisitionPending = 0;

            // Handle beacon reception
            beaconEventTime = UpdateBeaconState( LORAMAC_EVENT_INFO_STATUS_BEACON_LOCKED,
                                                 Ctx.BeaconCtx.BeaconRxEnlargeWindowOffset, currentTime );

            // Setup the MLME confirm for the MLME_BEACON_ACQUISITION
            if( Ctx.LoRaMacClassBParams.LoRaMacFlags->Bits.MlmeReq == 1 )
            {
                if( LoRaMacConfirmQueueIsCmdActive( MLME_BEACON_ACQUISITION ) == true )
                {
                    LoRaMacConfirmQueueSetStatus( LORAMAC_EVENT_INFO_STATUS_OK, MLME_BEACON_ACQUISITION );
                    Ctx.LoRaMacClassBParams.MlmeConfirm->TxTimeOnAir = 0;
                }
            }

            // Setup next state
            Ctx.BeaconState = BEACON_STATE_IDLE;
            break;
        }
        case BEACON_STATE_IDLE:
        {
            activateTimer = true;
            GetTemperatureLevel( &Ctx.LoRaMacClassBCallbacks, &Ctx.BeaconCtx );
            beaconEventTime = Ctx.BeaconCtx.NextBeaconRxAdjusted + Ctx.BeaconRxBaseConfig.WindowOffset;
            // currentTime has already set

            if( beaconEventTime > currentTime )
            {
                Ctx.BeaconState = BEACON_STATE_GUARD;
                beaconEventTime -= currentTime;
                beaconEventTime = TimerTempCompensation( beaconEventTime, Ctx.BeaconCtx.Temperature );
            }
            else
            {
                Ctx.BeaconState = BEACON_STATE_REACQUISITION;
                beaconEventTime = 0;
            }
            break;
        }
        case BEACON_STATE_GUARD:
        {
            Ctx.BeaconState = BEACON_STATE_RX;

            // Stop slot timers
            LoRaMacClassBStopRxSlots( );

            // Don't use the default channel. We know on which
            // channel the next beacon will be transmitted
            RxBeaconSetup( CLASSB_BEACON_RESERVED, false );
            break;
        }
        case BEACON_STATE_LOST:
        {
            // Handle events
            if( Ctx.LoRaMacClassBParams.LoRaMacFlags->Bits.MlmeReq == 1 )
            {
                if( LoRaMacConfirmQueueIsCmdActive( MLME_BEACON_ACQUISITION ) == true )
                {
                    LoRaMacConfirmQueueSetStatus( LORAMAC_EVENT_INFO_STATUS_BEACON_NOT_FOUND, MLME_BEACON_ACQUISITION );
                }
            }
            else
            {
                Ctx.LoRaMacClassBParams.MlmeIndication->MlmeIndication = MLME_BEACON_LOST;
                Ctx.LoRaMacClassBParams.MlmeIndication->Status = LORAMAC_EVENT_INFO_STATUS_OK;
                Ctx.LoRaMacClassBParams.LoRaMacFlags->Bits.MlmeInd = 1;
            }

            // Stop slot timers
            LoRaMacClassBStopRxSlots( );

            // Initialize default state for class b
            InitClassBDefaults( );

            LORAMAC_RADIOSLEEP_BEACON_LOST( 0 );

            Ctx.LoRaMacClassBParams.LoRaMacFlags->Bits.MacDone = 1;

            break;
        }
        default:
        {
            Ctx.BeaconState = BEACON_STATE_ACQUISITION;
            break;
        }
    }

    if( activateTimer == true )
    {
        TimerSetValue( &Ctx.BeaconTimer, beaconEventTime );
        TimerStart( &Ctx.BeaconTimer );
#if defined(DEBUG_LORAMAC)
        LoRaMacDebugLoRaMacClassBProcessBeacon( &(Ctx.BeaconCtx), beaconEventTime, 
                                                SysTimeErrorTimeMs, currentTime );
#endif
    }
}
#endif // LORAMAC_CLASSB_ENABLED

void LoRaMacClassBPingSlotTimerEvent( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    LoRaMacClassBEvents.Events.PingSlot = 1;

    OnClassBMacProcessNotify();
#endif // LORAMAC_CLASSB_ENABLED
}

#ifdef LORAMAC_CLASSB_ENABLED
static void LoRaMacClassBProcessPingSlot( void )
{
    static RxConfigParams_t pingSlotRxConfig;
    TimerTime_t pingSlotTime = 0;
    bool slotHasPriority = false;  // need to initialize

    // clear ping slot event flag to avoid multiple event
    LoRaMacClassBEvents.Events.PingSlot = 0;

    switch( Ctx.PingSlotState )
    {
        case PINGSLOT_STATE_CALC_PING_OFFSET:
        {
            if( Ctx.UpdatePingOffsetReq_unicast == true )
            {
                Ctx.UpdatePingOffsetReq_unicast = false;

                ComputePingOffset( Ctx.BeaconCtx.BeaconTime.Seconds,
                                   *Ctx.LoRaMacClassBParams.LoRaMacDevAddr,
                                   Ctx.NvmCtx->PingSlotCtx.PingPeriod,
                                   &( Ctx.PingSlotCtx.PingOffset ) );
            }
            Ctx.PingSlotState = PINGSLOT_STATE_SET_TIMER;
        }
            // Intentional fall through
        case PINGSLOT_STATE_SET_TIMER:
        {
            if( CalcNextSlotTime( Ctx.PingSlotCtx.PingOffset, Ctx.NvmCtx->PingSlotCtx.PingPeriod, Ctx.NvmCtx->PingSlotCtx.PingNb, &pingSlotTime ) == true )
            {
                pingSlotRxConfig.Bandwidth = Ctx.PingSlotBaseConfig.Bandwidth;

                pingSlotRxConfig.WindowTimeout = Ctx.PingSlotBaseConfig.WindowTimeout
                                               + Ctx.PingSlotCtx.PingSlotRxEnlargeWindowTimeout;
                pingSlotRxConfig.WindowOffset  = Ctx.PingSlotBaseConfig.WindowOffset
                                               + Ctx.PingSlotCtx.PingSlotRxEnlargeWindowOffset;

                if( ( int32_t )pingSlotTime > pingSlotRxConfig.WindowOffset )
                {   // Apply the window offset
                    pingSlotTime += pingSlotRxConfig.WindowOffset;
                }

                // Start the timer if the ping slot time is in range
                Ctx.PingSlotState = PINGSLOT_STATE_IDLE;
                TimerSetValue( &Ctx.PingSlotTimer, pingSlotTime );
                TimerStart( &Ctx.PingSlotTimer );

#if defined(DEBUG_LORAMAC)
                LoRaMacDebugLoRaMacClassBProcessPingSlot( pingSlotTime );
#endif
            }
            break;
        }
        case PINGSLOT_STATE_IDLE:
        {
            uint32_t frequency = Ctx.NvmCtx->PingSlotCtx.Frequency;

            // Apply a custom frequency if the following bit is set
            if( Ctx.NvmCtx->PingSlotCtx.Ctrl.CustomFreq == 0 )
            {
                // Restore floor plan
                frequency = CalcDownlinkChannelAndFrequency( *Ctx.LoRaMacClassBParams.LoRaMacDevAddr, Ctx.BeaconCtx.BeaconTime.Seconds, 
                                                             CLASSB_BEACON_INTERVAL, false );
            }

#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
#if (LORAMAC_MAX_MC_CTX > 0)
            if( Ctx.PingSlotCtx.NextMulticastChannel != NULL )
            {
                // Verify, if the unicast has priority.
                slotHasPriority = CheckSlotPriority( *Ctx.LoRaMacClassBParams.LoRaMacDevAddr, 
                                                     Ctx.NvmCtx->PingSlotCtx.FPendingSet, 0,
                                                     Ctx.PingSlotCtx.NextMulticastChannel->ChannelParams.Address, 
                                                     Ctx.PingSlotCtx.NextMulticastChannel->FPendingSet, 1 );
            }
#endif
#endif

            // Open the ping slot window only, if there is no multicast ping slot
            // open. Multicast ping slots have always priority
            // (LW1.0.3) slotHasPriority is always false
#if (LORAMAC_MAX_MC_CTX > 0)
            if( ( Ctx.MulticastSlotState != PINGSLOT_STATE_RX ) || ( slotHasPriority == true ) )
#endif
            {
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
#if (LORAMAC_MAX_MC_CTX > 0)
                if( Ctx.MulticastSlotState == PINGSLOT_STATE_RX )
                {
                    // Close multicast slot window, if necessary. Multicast slots have priority
                    Radio.Standby( );
                    Ctx.MulticastSlotState = PINGSLOT_STATE_CALC_PING_OFFSET;
                    TimerSetValue( &Ctx.MulticastSlotTimer, CLASSB_PING_SLOT_WINDOW );
                    TimerStart( &Ctx.MulticastSlotTimer );
                }
#endif  // LORAMAC_MAX_MC_CTX
#endif
                Ctx.PingSlotState = PINGSLOT_STATE_RX;

                pingSlotRxConfig.Datarate = Ctx.NvmCtx->PingSlotCtx.Datarate;
                pingSlotRxConfig.DownlinkDwellTime = Ctx.LoRaMacClassBParams.LoRaMacParams->DownlinkDwellTime;
                pingSlotRxConfig.Frequency = frequency;
                pingSlotRxConfig.RxContinuous = false;
                pingSlotRxConfig.RxSlot = RX_SLOT_WIN_CLASS_B_PING_SLOT;
                pingSlotRxConfig.NetworkActivation = *Ctx.LoRaMacClassBParams.NetworkActivation;

                LORAMAC_RADIOWAKEUP_PINGSLOT( 0 );
                RegionRxConfig( *Ctx.LoRaMacClassBParams.LoRaMacRegion, &pingSlotRxConfig, ( int8_t* )&Ctx.LoRaMacClassBParams.McpsIndication->RxDatarate );

                {
                    RadioResult_t   result;
                    
                    result = Radio.Rx( Ctx.LoRaMacClassBParams.LoRaMacParams->MaxRxWindow );
                    if( result != RADIO_SUCCESS )
                    {
                        // If it fails to set RX mode due to invalid Rx parameter, 
                        // process same as Rx error and notify to application layer via callback
                        OnRadioRxError();

                        LoRaMacErrorNotify( LORAMAC_ERROR_NOTIFICATION_STATUS_RADIO_CHECK_FAIL_RX_CFG );                     
                    }                
                }
            }
#if (LORAMAC_MAX_MC_CTX > 0)
            else
            {
                // Multicast slots have priority. Skip Rx
                Ctx.PingSlotState = PINGSLOT_STATE_CALC_PING_OFFSET;
                TimerSetValue( &Ctx.PingSlotTimer, CLASSB_PING_SLOT_WINDOW );
                TimerStart( &Ctx.PingSlotTimer );
            }
#endif
            break;
        }
        default:
        {
            Ctx.PingSlotState = PINGSLOT_STATE_CALC_PING_OFFSET;
            break;
        }
    }
}
#endif // LORAMAC_CLASSB_ENABLED

void LoRaMacClassBMulticastSlotTimerEvent( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
#if (LORAMAC_MAX_MC_CTX > 0)
    LoRaMacClassBEvents.Events.MulticastSlot = 1;
    OnClassBMacProcessNotify();
#endif  // LORAMAC_MAX_MC_CTX
#endif // LORAMAC_CLASSB_ENABLED
}

#ifdef LORAMAC_CLASSB_ENABLED
#if (LORAMAC_MAX_MC_CTX > 0)
static void LoRaMacClassBProcessMulticastSlot( void )
{
    static RxConfigParams_t multicastSlotRxConfig;
    TimerTime_t multicastSlotTime = 0;
    TimerTime_t slotTime = 0;
    MulticastCtx_t *cur = Ctx.LoRaMacClassBParams.MulticastChannels;
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
    bool slotHasPriority;
#endif

    if( cur == NULL )
    {
        return;
    }

    if( Ctx.MulticastSlotState == PINGSLOT_STATE_RX )
    {
        // A multicast slot is already open
        return;
    }

    // clear multicast event flag to avoid multiple event
    LoRaMacClassBEvents.Events.MulticastSlot = 0;

    switch( Ctx.MulticastSlotState )
    {
        case PINGSLOT_STATE_CALC_PING_OFFSET:
        {
            if( Ctx.UpdatePingOffsetReq_multicast == true )
            {
                Ctx.UpdatePingOffsetReq_multicast = false;

                // Compute all offsets for every multicast slots
                for( uint8_t i = 0; i < LORAMAC_MAX_MC_CTX; i++ )
                {
                    if( ( cur->ChannelParams.IsEnabled == true ) &&
                        ( cur->ChannelParams.Class == CLASS_B ) )
                    {
                        ComputePingOffset( Ctx.BeaconCtx.BeaconTime.Seconds,
                                           cur->ChannelParams.Address,
                                           cur->PingPeriod,
                                           &( cur->PingOffset ) );
                        // Update state only if valid multicast entry is in the table.
                        Ctx.MulticastSlotState = PINGSLOT_STATE_SET_TIMER;
                    }
                    cur++;
                }
            }
            else
            {
                // Intentional fall through
                Ctx.MulticastSlotState = PINGSLOT_STATE_SET_TIMER;
            }
        }
            // Intentional fall through
        case PINGSLOT_STATE_SET_TIMER:
        {
            cur = Ctx.LoRaMacClassBParams.MulticastChannels;
            Ctx.PingSlotCtx.NextMulticastChannel = NULL;

            for( uint8_t i = 0; i < LORAMAC_MAX_MC_CTX; i++ )
            {
                if( ( cur->ChannelParams.IsEnabled == true ) &&
                    ( cur->ChannelParams.Class == CLASS_B ) )
                {
                    // Calculate the next slot time for every multicast slot
                    if( CalcNextSlotTime( cur->PingOffset, cur->PingPeriod, cur->PingNb, &slotTime ) == true )
                    {
                        if( ( multicastSlotTime == 0 ) || ( multicastSlotTime > slotTime ) )
                        {
                            // Update the slot time and the next multicast channel
                            multicastSlotTime = slotTime;
                            Ctx.PingSlotCtx.NextMulticastChannel = cur;
                        }
                    }
                }
                cur++;
            }

            // Schedule the next multicast slot
            if( Ctx.PingSlotCtx.NextMulticastChannel != NULL )
            {
                if( Ctx.BeaconCtx.Ctrl.BeaconAcquired == 1 )
                {
                    RegionComputeRxWindowParameters( *Ctx.LoRaMacClassBParams.LoRaMacRegion,
                                                    Ctx.NvmCtx->PingSlotCtx.Datarate,
                                                    Ctx.LoRaMacClassBParams.LoRaMacParams->MinRxSymbols,
                                                    Ctx.LoRaMacClassBParams.LoRaMacParams->SystemMaxRxError,
                                                    &multicastSlotRxConfig );
                    multicastSlotRxConfig.WindowOffset -= Radio.GetWakeupTime();
                }

                if( ( int32_t )multicastSlotTime > multicastSlotRxConfig.WindowOffset )
                {// Apply the window offset
                    multicastSlotTime += multicastSlotRxConfig.WindowOffset;
                }

                // Start the timer if the ping slot time is in range
                Ctx.MulticastSlotState = PINGSLOT_STATE_IDLE;
                TimerSetValue( &Ctx.MulticastSlotTimer, multicastSlotTime );
                TimerStart( &Ctx.MulticastSlotTimer );

#if defined(DEBUG_LORAMAC)
                LoRaMacDebugLoRaMacClassBProcessMulticastSlot( multicastSlotTime );
#endif
            }
            break;
        }
        case PINGSLOT_STATE_IDLE:
        {
            uint32_t frequency = 0;

            // Verify if the multicast channel is valid
            // If this multicast entry has been invalidate right before, 
            // NextMulticastChannel never be NULL but check for fail-safe
            if( ( Ctx.PingSlotCtx.NextMulticastChannel == NULL ) ||
                ( Ctx.PingSlotCtx.NextMulticastChannel->ChannelParams.IsEnabled == false ) )
            {
                Ctx.MulticastSlotState = PINGSLOT_STATE_CALC_PING_OFFSET;
                TimerSetValue( &Ctx.MulticastSlotTimer, 0 );
                TimerStart( &Ctx.MulticastSlotTimer );
                break;
            }

            // Apply frequency
            frequency = Ctx.PingSlotCtx.NextMulticastChannel->ChannelParams.RxParams.ClassB.Frequency;

            // Restore the floor plan frequency if there is no individual frequency assigned
            if( frequency == 0 )
            {
                // Restore floor plan
                frequency = CalcDownlinkChannelAndFrequency( Ctx.PingSlotCtx.NextMulticastChannel->ChannelParams.Address, 
                                                             Ctx.BeaconCtx.BeaconTime.Seconds, CLASSB_BEACON_INTERVAL, false );
            }

#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
            // Verify, if the unicast has priority.
            slotHasPriority = CheckSlotPriority( Ctx.PingSlotCtx.NextMulticastChannel->ChannelParams.Address, 
                                                 Ctx.PingSlotCtx.NextMulticastChannel->FPendingSet, 1,
                                                 *Ctx.LoRaMacClassBParams.LoRaMacDevAddr, 
                                                 Ctx.NvmCtx->PingSlotCtx.FPendingSet, 0 );

            // Open the ping slot window only, if there is no multicast ping slot
            // open or if the unicast has priority.
            if( ( Ctx.PingSlotState == PINGSLOT_STATE_RX ) && ( slotHasPriority == false ) )
            {
                // Unicast slots have priority. Skip Rx
                Ctx.MulticastSlotState = PINGSLOT_STATE_CALC_PING_OFFSET;
                TimerSetValue( &Ctx.MulticastSlotTimer, CLASSB_PING_SLOT_WINDOW );
                TimerStart( &Ctx.MulticastSlotTimer );
                break;
            }
#endif
            Ctx.MulticastSlotState = PINGSLOT_STATE_RX;

            multicastSlotRxConfig.Datarate = Ctx.PingSlotCtx.NextMulticastChannel->ChannelParams.RxParams.ClassB.Datarate;
            multicastSlotRxConfig.DownlinkDwellTime = Ctx.LoRaMacClassBParams.LoRaMacParams->DownlinkDwellTime;
            multicastSlotRxConfig.Frequency = frequency;
            multicastSlotRxConfig.RxContinuous = false;
            multicastSlotRxConfig.RxSlot = RX_SLOT_WIN_CLASS_B_MULTICAST_SLOT;
            multicastSlotRxConfig.NetworkActivation = *Ctx.LoRaMacClassBParams.NetworkActivation;

            LORAMAC_RADIOWAKEUP_MULTICASTSLOT();
            RegionRxConfig( *Ctx.LoRaMacClassBParams.LoRaMacRegion, &multicastSlotRxConfig, ( int8_t* )&Ctx.LoRaMacClassBParams.McpsIndication->RxDatarate );

            if( Ctx.PingSlotState == PINGSLOT_STATE_RX )
            {
                // Close ping slot window, if necessary. Multicast slots have priority
                // done
                Ctx.PingSlotState = PINGSLOT_STATE_CALC_PING_OFFSET;
                TimerSetValue( &Ctx.PingSlotTimer, CLASSB_PING_SLOT_WINDOW );
                TimerStart( &Ctx.PingSlotTimer );
            }

            {
                RadioResult_t   result;
                
                result = Radio.Rx( Ctx.LoRaMacClassBParams.LoRaMacParams->MaxRxWindow );
                if( result != RADIO_SUCCESS )
                {
                    // If it fails to set RX mode due to invalid Rx parameter, 
                    // process same as Rx error and notify to application layer via callback
                    OnRadioRxError();
                    
                    LoRaMacErrorNotify( LORAMAC_ERROR_NOTIFICATION_STATUS_RADIO_CHECK_FAIL_RX_CFG );
                }                
            }
            break;
        }
        default:
        {
            Ctx.MulticastSlotState = PINGSLOT_STATE_CALC_PING_OFFSET;
            break;
        }
    }
}
#endif  // LORAMAC_MAX_MC_CTX
#endif // LORAMAC_CLASSB_ENABLED

bool LoRaMacClassBRxBeacon( uint8_t *payload, uint16_t size, bool *isDoneBeaconRx )
{
#ifdef LORAMAC_CLASSB_ENABLED
    GetPhyParams_t getPhy;
    PhyParam_t phyParam;
    bool beaconProcessed = false;
    uint16_t crc0 = 0;
    uint16_t crc1 = 0;
    uint16_t beaconCrc0 = 0;
    uint16_t beaconCrc1 = 0;
    SysTime_t sysTimeDelta;
    uint8_t  paramSize;
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
    uint8_t  precTimeMs;
#endif

    (*isDoneBeaconRx) = false;  // init

    getPhy.Attribute = PHY_BEACON_FORMAT;
    phyParam = RegionGetPhyParam( *Ctx.LoRaMacClassBParams.LoRaMacRegion, &getPhy );

    // Verify if we are in the state where we expect a beacon
    if( ( Ctx.BeaconState == BEACON_STATE_RX ) || ( Ctx.BeaconCtx.Ctrl.AcquisitionPending == 1 ) )
    {
        if( size == phyParam.BeaconFormat.BeaconSize )
        {
            // A beacon frame is defined as:
            // (LW1.0.3)
            // Bytes: |  x   |  4   |  2   |     7      |  y   |  2   |
            //        |------|------|------|------------|------|------|
            // Field: | RFU1 | Time | CRC1 | GwSpecific | RFU2 | CRC2 |
            // (LW1.0.4)
            // Bytes: |  x   |   1   |  4   |  2   |     7      |  y   |  2   |
            //        |------|-------|------|------|------------|------|------|
            // Field: | RFU1 | Param | Time | CRC1 | GwSpecific | RFU2 | CRC2 |
            //
            // Field RFU1 and RFU2 have variable sizes. It depends on the region specific implementation
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
            paramSize = 1;
#else
            paramSize = 0;
#endif
            // Read CRC1 field from the frame
            beaconCrc0 = ( ( uint16_t )payload[phyParam.BeaconFormat.Rfu1Size + paramSize + 4] ) & 0x00FF;
            beaconCrc0 |= ( ( uint16_t )payload[phyParam.BeaconFormat.Rfu1Size + paramSize + 4 + 1] << 8 ) & 0xFF00;
            crc0 = BeaconCrc( payload, phyParam.BeaconFormat.Rfu1Size + paramSize + 4 );

            // Validate the first crc of the beacon frame
            if( crc0 == beaconCrc0 )
            {
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
                // Copy the param field for app layer
                Ctx.LoRaMacClassBParams.MlmeIndication->BeaconInfo.Param = ( payload[phyParam.BeaconFormat.Rfu1Size] );
                // Fetch the precise time value in milliseconds that will be used for Rx ping slot delay.
                precTimeMs = BeaconPrecTimeValue[Ctx.LoRaMacClassBParams.MlmeIndication->BeaconInfo.Param];
#endif
                // Read Time field from the frame
                Ctx.BeaconCtx.BeaconTime.Seconds  = ( ( uint32_t )payload[phyParam.BeaconFormat.Rfu1Size + paramSize] ) & 0x000000FF;
                Ctx.BeaconCtx.BeaconTime.Seconds |= ( ( uint32_t )payload[phyParam.BeaconFormat.Rfu1Size + paramSize + 1] << 8 ) & 0x0000FF00;
                Ctx.BeaconCtx.BeaconTime.Seconds |= ( ( uint32_t )payload[phyParam.BeaconFormat.Rfu1Size + paramSize + 2] << 16 ) & 0x00FF0000;
                Ctx.BeaconCtx.BeaconTime.Seconds |= ( ( uint32_t )payload[phyParam.BeaconFormat.Rfu1Size + paramSize + 3] << 24 ) & 0xFF000000;
                Ctx.BeaconCtx.BeaconTime.SubSeconds = 0;
                Ctx.LoRaMacClassBParams.MlmeIndication->BeaconInfo.Time = Ctx.BeaconCtx.BeaconTime;
                beaconProcessed = true;
            }

            if (beaconProcessed == true)
            {
                // Read CRC2 field from the frame
                beaconCrc1 = ( ( uint16_t )payload[phyParam.BeaconFormat.Rfu1Size + paramSize + 4 + 2 + 7 + phyParam.BeaconFormat.Rfu2Size] ) & 0x00FF;
                beaconCrc1 |= ( ( uint16_t )payload[phyParam.BeaconFormat.Rfu1Size + paramSize + 4 + 2 + 7 + phyParam.BeaconFormat.Rfu2Size + 1] << 8 ) & 0xFF00;
                crc1 = BeaconCrc( &payload[phyParam.BeaconFormat.Rfu1Size + paramSize + 4 + 2], 7 + phyParam.BeaconFormat.Rfu2Size );
                
                // Validate the second crc of the beacon frame
                if( crc1 == beaconCrc1 )
                {
                    // Read GwSpecific field from the frame
                    // The GwSpecific field contains 1 byte InfoDesc and 6 bytes Info
                    Ctx.LoRaMacClassBParams.MlmeIndication->BeaconInfo.GwSpecific.InfoDesc = payload[phyParam.BeaconFormat.Rfu1Size + paramSize + 4 + 2];
                    memcpy1( Ctx.LoRaMacClassBParams.MlmeIndication->BeaconInfo.GwSpecific.Info, &payload[phyParam.BeaconFormat.Rfu1Size + paramSize + 4 + 2 + 1], 6 );
                    Ctx.LoRaMacClassBParams.MlmeIndication->BeaconInfo.isValidGwSpecific = true;
                }
                else
                {
                    Ctx.LoRaMacClassBParams.MlmeIndication->BeaconInfo.isValidGwSpecific = false;
                    memset1( (uint8_t *)&(Ctx.LoRaMacClassBParams.MlmeIndication->BeaconInfo.GwSpecific),
                             0x00,
                             sizeof(Ctx.LoRaMacClassBParams.MlmeIndication->BeaconInfo.GwSpecific) );
                }
            }

            // Reset beacon variables, if one of the crc is valid
            if( beaconProcessed == true )
            {
                TimerTime_t time = Radio.TimeOnAir( MODEM_LORA, size );
                SysTime_t timeOnAir;
                time -= RP_TOA_OFFSET_LORA;
                timeOnAir.Seconds = time / 1000;
                timeOnAir.SubSeconds = time - timeOnAir.Seconds * 1000;

                Ctx.BeaconCtx.LastBeaconRx = Ctx.BeaconCtx.BeaconTime;
                Ctx.BeaconCtx.LastBeaconRx.Seconds += UNIX_GPS_EPOCH_OFFSET;

                // Update system time.
                SysTimeSet( SysTimeAdd( Ctx.BeaconCtx.LastBeaconRx, timeOnAir ) );
                Ctx.BeaconCtx.LastSystimeSetTimeMs = SysTimeToMs( SysTimeGet() );
                Ctx.BeaconCtx.LastSystimeSetIsBeacon = true;

                sysTimeDelta = SysTimeGetDeltaTime();
                if( Ctx.BeaconState == BEACON_STATE_RX )
                {
                    SysTimeErrorTimeMs  = (int32_t)( SysTimeToMs( sysTimeDelta ) - SysTimeToMs( SysTimeDeltaTimePrev ) );
                    SysTimeErrorTimeMs /= (int32_t)(BeaconTimeoutContinuousCnt + 1);

                    // fail-safe; not accept if absolute value of SysTimeErrorTimeMs is too big
                    if( SysTimeErrorTimeMs > CLASSB_BEACON_SYSTIMEDELTA_MAXERROR )
                    {
                        SysTimeErrorTimeMs = CLASSB_BEACON_SYSTIMEDELTA_MAXERROR;
                    }
                    else if( SysTimeErrorTimeMs < CLASSB_BEACON_SYSTIMEDELTA_MINERROR )
                    {
                        SysTimeErrorTimeMs = CLASSB_BEACON_SYSTIMEDELTA_MINERROR;
                    }
                    else
                    {
                        // nothing to do
                    }
                }
                else  // Acquisition
                {
                    SysTimeErrorTimeMs = CLASSB_BEACON_SYSTIMEDELTA_INITERROR;  // init
                }
                SysTimeDeltaTimePrev = sysTimeDelta;
                BeaconTimeoutContinuousCnt = 0;  // init & clear

#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
                if( precTimeMs != Ctx.BeaconCtx.BeaconTimePrecision.SubSeconds )
                {
                    // Reculculate beacon & ping RxWindow with considering the change of precTime.
                    Ctx.BeaconCtx.BeaconTimePrecision.SubSeconds = precTimeMs;
                    LoRaMacClassBComputeBeaconWindowParameters();
                    LoRaMacClassBComputePingSlotWindowParameters();
                }
#endif
                Ctx.BeaconCtx.Ctrl.BeaconAcquired = 1;
                Ctx.BeaconCtx.Ctrl.BeaconMode = 1;
                ResetWindowTimeout( );
                Ctx.BeaconState = BEACON_STATE_LOCKED;
                LoRaMacClassBBeaconTimerEvent();

                // indicate that beacon Rx is done (frame is beacon)
                (*isDoneBeaconRx) = true;

                if( Ctx.BeaconCtx.Ctrl.AcquisitionPending == 1 )
                {
                    // Enlarge beacon window only for 1st beacon tracking
                    EnlargeWindowTimeout( Ctx.BeaconCtx.LastSystimeSetTimeMs, true );
                }
            }
        }

        if( Ctx.BeaconState != BEACON_STATE_LOCKED )
        {
            // beacon frame is incorrect or frame is NOT beacon
            if( ( Ctx.BeaconState == BEACON_STATE_ACQUISITION_BY_TIME ) ||
                ( Ctx.BeaconState == BEACON_STATE_RX ) )
            {
                // indicate that beacon Rx is done
                (*isDoneBeaconRx) = true;

                LoRaMacClassBSetBeaconState( BEACON_STATE_TIMEOUT );
                LoRaMacClassBBeaconTimerEvent();
            }
            else //if( Ctx.BeaconState == BEACON_STATE_ACQUISITION )
            {
                // nothing to do; expect next beacon Rx.
            }
        }
        // When the MAC listens for a beacon, it is not allowed to process any other
        // downlink except the beacon frame itself. The reason for this is that no valid downlink window is open.
        // If it receives a frame which is
        // 1. not a beacon or
        // 2. a beacon with a crc fail
        // the MAC shall ignore the frame completely. Thus, the function must always return true, even if no
        // valid beacon has been received.
        beaconProcessed = true;
    }
    return beaconProcessed;
#else
    return false;
#endif // LORAMAC_CLASSB_ENABLED
}

bool LoRaMacClassBIsBeaconExpected( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    if( ( Ctx.BeaconCtx.Ctrl.AcquisitionPending == 1 ) ||
        ( Ctx.BeaconState == BEACON_STATE_RX ) )
    {
        return true;
    }
    return false;
#else
    return false;
#endif // LORAMAC_CLASSB_ENABLED
}

bool LoRaMacClassBIsPingExpected( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    if( Ctx.PingSlotState == PINGSLOT_STATE_RX )
    {
        return true;
    }
    return false;
#else
    return false;
#endif // LORAMAC_CLASSB_ENABLED
}

bool LoRaMacClassBIsMulticastExpected( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
#if (LORAMAC_MAX_MC_CTX > 0)
    if( Ctx.MulticastSlotState == PINGSLOT_STATE_RX )
    {
        return true;
    }
#endif
    return false;
#else
    return false;
#endif // LORAMAC_CLASSB_ENABLED
}

bool LoRaMacClassBIsAcquisitionPending( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    if( Ctx.BeaconCtx.Ctrl.AcquisitionPending == 1 )
    {
        return true;
    }
    return false;
#else
    return false;
#endif // LORAMAC_CLASSB_ENABLED
}

bool LoRaMacClassBIsBeaconModeActive( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    if( ( Ctx.BeaconCtx.Ctrl.BeaconMode == 1 ) ||
        ( Ctx.BeaconState == BEACON_STATE_ACQUISITION_BY_TIME ) )
    {
        return true;
    }
    return false;
#else
    return false;
#endif // LORAMAC_CLASSB_ENABLED
}

void LoRaMacClassBSetPingSlotInfo( uint8_t periodicity )
{
#ifdef LORAMAC_CLASSB_ENABLED
    Ctx.NvmCtx->PingSlotCtx.PingNb = CalcPingNb( periodicity );
    Ctx.NvmCtx->PingSlotCtx.PingPeriod = CalcPingPeriod( Ctx.NvmCtx->PingSlotCtx.PingNb );

    if( ( Ctx.NvmCtx->PingSlotCtx.Periodicity >= CLASSB_PING_SLOT_PERIODICITY_RFSLEEP_THRESHOLD ) &&
        ( periodicity < CLASSB_PING_SLOT_PERIODICITY_RFSLEEP_THRESHOLD ) )
    {
        Ctx.PingSlotBaseConfig.WindowOffset += LoRaMacClassBGetStackProcessTime( CLASSB_STACK_PROCTIME_SEL_PING_SLOT_LONG_PERIOD );  // cancel
        Ctx.PingSlotBaseConfig.WindowOffset -= LoRaMacClassBGetStackProcessTime( CLASSB_STACK_PROCTIME_SEL_PING_SLOT_SHORT_PERIOD );  // apply
    }
    else if( ( Ctx.NvmCtx->PingSlotCtx.Periodicity < CLASSB_PING_SLOT_PERIODICITY_RFSLEEP_THRESHOLD ) &&
              ( periodicity >= CLASSB_PING_SLOT_PERIODICITY_RFSLEEP_THRESHOLD ) )
    {
        Ctx.PingSlotBaseConfig.WindowOffset += LoRaMacClassBGetStackProcessTime( CLASSB_STACK_PROCTIME_SEL_PING_SLOT_SHORT_PERIOD );  // cancel
        Ctx.PingSlotBaseConfig.WindowOffset -= LoRaMacClassBGetStackProcessTime( CLASSB_STACK_PROCTIME_SEL_PING_SLOT_LONG_PERIOD );  // apply
    }

    // store periodicity
    Ctx.NvmCtx->PingSlotCtx.Periodicity = periodicity;

    NvmContextChange( );
#endif // LORAMAC_CLASSB_ENABLED
}

void LoRaMacClassBStopBeaconig( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    if( Ctx.LoRaMacClassBParams.LoRaMacFlags->Bits.MlmeReq == 1 )
    {
        if( LoRaMacConfirmQueueIsCmdActive( MLME_BEACON_ACQUISITION ) == true )
        {
            LoRaMacConfirmQueueSetStatus( LORAMAC_EVENT_INFO_STATUS_BEACON_NOT_FOUND, MLME_BEACON_ACQUISITION );
        }
    }

    TimerStop( &Ctx.BeaconTimer );
    CRITICAL_SECTION_BEGIN( );
    LoRaMacClassBEvents.Events.Beacon = 0;
    CRITICAL_SECTION_END( );

    LoRaMacClassBStopRxSlots( );
    InitClassBDefaults( );

    LORAMAC_RADIOSLEEP_BEACON_STOP( 0 );
#endif
}

void LoRaMacClassBHaltBeaconing( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    if( Ctx.BeaconCtx.Ctrl.BeaconMode == 1 )
    {
        // stop beacon timer
        TimerStop( &Ctx.BeaconTimer );

        CRITICAL_SECTION_BEGIN( );
        LoRaMacClassBEvents.Events.Beacon = 0;
        CRITICAL_SECTION_END( );

        // Halt ping and multicast slot state machines (stop time first)
        LoRaMacClassBStopRxSlots( );

        // Halt beacon state machine
        if( Ctx.BeaconState == BEACON_STATE_TIMEOUT )
        {
            Ctx.BeaconState = BEACON_STATE_HALT_TIMEOUT;
        }
        else if( Ctx.BeaconState == BEACON_STATE_LOST )
        {
            Ctx.BeaconState = BEACON_STATE_HALT_LOST;
        }
        else
        {
            Ctx.BeaconState = BEACON_STATE_HALT;
        }

    }
#endif // LORAMAC_CLASSB_ENABLED
}

void LoRaMacClassBResumeBeaconing( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    if( ( Ctx.BeaconState == BEACON_STATE_HALT ) ||
        ( Ctx.BeaconState == BEACON_STATE_HALT_TIMEOUT) ||
        ( Ctx.BeaconState == BEACON_STATE_HALT_LOST) )
    {
        Ctx.BeaconCtx.Ctrl.ResumeBeaconing = 1;

        // Set state
        if( Ctx.BeaconState == BEACON_STATE_HALT_TIMEOUT )
        {
            Ctx.BeaconState = BEACON_STATE_TIMEOUT;
        }
        else if( Ctx.BeaconState == BEACON_STATE_HALT_LOST )
        {
            Ctx.BeaconState = BEACON_STATE_LOST;
        }
        else
        {
            if( Ctx.BeaconCtx.Ctrl.BeaconAcquired == 0 )
            {
                // Set the default state for beacon less operation
                Ctx.BeaconState = BEACON_STATE_REACQUISITION;
            }
            else
            {
                Ctx.BeaconState = BEACON_STATE_LOCKED;
            }
        }

        LoRaMacClassBBeaconTimerEvent();
    }

#endif // LORAMAC_CLASSB_ENABLED
}

LoRaMacStatus_t LoRaMacClassBSwitchClass( DeviceClass_t nextClass )
{
#ifdef LORAMAC_CLASSB_ENABLED
    if( nextClass == CLASS_B )
    {// Switch to from class a to class b
        if( Ctx.BeaconCtx.Ctrl.BeaconMode == 1 )
        {
            // Start unicast/multicast ping slot
            LoRaMacClassBStartRxSlots();
            return LORAMAC_STATUS_OK;
        }
    }
    if( nextClass == CLASS_A )
    {// Switch from class b to class a
        LoRaMacClassBStopBeaconig();

        return LORAMAC_STATUS_OK;
    }
    return LORAMAC_STATUS_SERVICE_UNKNOWN;
#else
    return LORAMAC_STATUS_SERVICE_UNKNOWN;
#endif // LORAMAC_CLASSB_ENABLED
}

LoRaMacStatus_t LoRaMacClassBMibGetRequestConfirm( MibRequestConfirm_t *mibGet )
{
#ifdef LORAMAC_CLASSB_ENABLED
    LoRaMacStatus_t status = LORAMAC_STATUS_OK;

    switch( mibGet->Type )
    {
        case MIB_PING_SLOT_DATARATE:
        {
            mibGet->Param.PingSlotDatarate = Ctx.NvmCtx->PingSlotCtx.Datarate;
            break;
        }
        case MIB_PING_SLOT_PERIODICITY:
        {
            mibGet->Param.PingSlotPeriodicity = Ctx.NvmCtx->PingSlotCtx.Periodicity;
            break;
        }
        default:
        {
            status = LORAMAC_STATUS_SERVICE_UNKNOWN;
            break;
        }
    }
    return status;
#else
    return LORAMAC_STATUS_SERVICE_UNKNOWN;
#endif // LORAMAC_CLASSB_ENABLED
}

LoRaMacStatus_t LoRaMacMibClassBSetRequestConfirm( MibRequestConfirm_t *mibSet )
{
#ifdef LORAMAC_CLASSB_ENABLED
    LoRaMacStatus_t status = LORAMAC_STATUS_OK;

    switch( mibSet->Type )
    {
        case MIB_PING_SLOT_DATARATE:
        {
            Ctx.NvmCtx->PingSlotCtx.Datarate = mibSet->Param.PingSlotDatarate;
            LoRaMacClassBComputePingSlotWindowParameters();
            NvmContextChange( );
            break;
        }
        case MIB_PING_SLOT_PERIODICITY:
        {
            if( LoRaMacGetDeviceClass() == CLASS_A )
            {
                if (mibSet->Param.PingSlotPeriodicity <= 7)
                {
                    LoRaMacClassBSetPingSlotInfo( mibSet->Param.PingSlotPeriodicity );
                    // Ctx.NvmCtx->PingSlotCtx.Periodicity has been set at upper function
                }
                else
                {
                    status = LORAMAC_STATUS_PARAMETER_INVALID;
                }
            }
            else
            {
                status = LORAMAC_STATUS_SERVICE_UNKNOWN;
            }
            break;
        }
        default:
        {
            status = LORAMAC_STATUS_SERVICE_UNKNOWN;
            break;
        }
    }
    return status;
#else
    return LORAMAC_STATUS_SERVICE_UNKNOWN;
#endif // LORAMAC_CLASSB_ENABLED
}

void LoRaMacClassBPingSlotInfoAns( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    // confirm-queue check is not needed by accepting GitHub Issue #857 
    // Assinged bit had been disabled.
    NvmContextChange( );
#endif // LORAMAC_CLASSB_ENABLED
}

uint8_t LoRaMacClassBPingSlotChannelReq( uint8_t datarate, uint32_t frequency )
{
#ifdef LORAMAC_CLASSB_ENABLED
    uint8_t status = 0x03;
    VerifyParams_t verify;
    bool isCustomFreq = false;

    if( frequency != 0 )
    {
        isCustomFreq = true;
        verify.Frequency = frequency;
        if( RegionVerify( *Ctx.LoRaMacClassBParams.LoRaMacRegion, &verify, PHY_FREQUENCY ) == false )
        {
            status &= 0xFE; // Channel frequency KO
        }
    }

    verify.DatarateParams.Datarate = datarate;
    verify.DatarateParams.DownlinkDwellTime = Ctx.LoRaMacClassBParams.LoRaMacParams->DownlinkDwellTime;

    if( RegionVerify( *Ctx.LoRaMacClassBParams.LoRaMacRegion, &verify, PHY_RX_DR ) == false )
    {
        status &= 0xFD; // Datarate range KO
    }

    if( status == 0x03 )
    {
        if( isCustomFreq == true )
        {
            Ctx.NvmCtx->PingSlotCtx.Ctrl.CustomFreq = 1;
            Ctx.NvmCtx->PingSlotCtx.Frequency = frequency;
        }
        else
        {
            Ctx.NvmCtx->PingSlotCtx.Ctrl.CustomFreq = 0;
            Ctx.NvmCtx->PingSlotCtx.Frequency = 0;
        }

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
        if( Ctx.NvmCtx->PingSlotCtx.Datarate != datarate )
        {
            Ctx.NvmCtx->PingSlotCtx.Datarate = datarate;
            LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_PING_SLOT_DATARATE );
        }
#else
        Ctx.NvmCtx->PingSlotCtx.Datarate = datarate;
#endif
        LoRaMacClassBComputePingSlotWindowParameters();
    }

    return status;
#else
    return 0;
#endif // LORAMAC_CLASSB_ENABLED
}

void LoRaMacClassBBeaconTimingAns( uint16_t beaconTimingDelay, uint8_t beaconTimingChannel, TimerTime_t lastRxDone )
{
#ifdef LORAMAC_CLASSB_ENABLED
    Ctx.BeaconCtx.BeaconTimingDelay = ( CLASSB_BEACON_DELAY_BEACON_TIMING_ANS * beaconTimingDelay );
    Ctx.BeaconCtx.BeaconTimingChannel = beaconTimingChannel;

    {
        {
            Ctx.BeaconCtx.Ctrl.BeaconDelaySet = 1;
            Ctx.BeaconCtx.Ctrl.BeaconChannelSet = 1;
            Ctx.BeaconCtx.NextBeaconRx = SysTimeFromMs( lastRxDone + Ctx.BeaconCtx.BeaconTimingDelay );
        }

        Ctx.LoRaMacClassBParams.MlmeConfirm->BeaconTimingDelay = Ctx.BeaconCtx.BeaconTimingDelay;
        Ctx.LoRaMacClassBParams.MlmeConfirm->BeaconTimingChannel = Ctx.BeaconCtx.BeaconTimingChannel;
    }
#endif // LORAMAC_CLASSB_ENABLED
}

void LoRaMacClassBDeviceTimeAns( void )
{
#ifdef LORAMAC_CLASSB_ENABLED

    SysTime_t nextBeacon = SysTimeGet( );
    TimerTime_t currentTimeMs = SysTimeToMs( nextBeacon );

    nextBeacon.Seconds = nextBeacon.Seconds + ( 128 - ( nextBeacon.Seconds % 128 ) );
    nextBeacon.SubSeconds = 0;

    Ctx.BeaconCtx.NextBeaconRx = nextBeacon;
    Ctx.BeaconCtx.LastBeaconRx = SysTimeSub( Ctx.BeaconCtx.NextBeaconRx, ( SysTime_t ){ .Seconds = CLASSB_BEACON_INTERVAL / 1000, .SubSeconds = 0 } );

    {
        {
            Ctx.BeaconCtx.Ctrl.BeaconDelaySet = 1;
            Ctx.BeaconCtx.BeaconTimingDelay = SysTimeToMs( Ctx.BeaconCtx.NextBeaconRx ) - currentTimeMs;
            Ctx.BeaconCtx.BeaconTime.Seconds = nextBeacon.Seconds - UNIX_GPS_EPOCH_OFFSET - 128;
            Ctx.BeaconCtx.BeaconTime.SubSeconds = 0;
        }
    }
#endif // LORAMAC_CLASSB_ENABLED
}

bool LoRaMacClassBBeaconFreqReq( uint32_t frequency )
{
#ifdef LORAMAC_CLASSB_ENABLED
    VerifyParams_t verify;

    if( frequency != 0 )
    {
        verify.Frequency = frequency;

        if( RegionVerify( *Ctx.LoRaMacClassBParams.LoRaMacRegion, &verify, PHY_FREQUENCY ) == true )
        {
            Ctx.NvmCtx->BeaconCtx.Ctrl.CustomFreq = 1;
            Ctx.NvmCtx->BeaconCtx.Frequency = frequency;
            NvmContextChange( );
            return true;
        }
    }
    else
    {
        Ctx.NvmCtx->BeaconCtx.Ctrl.CustomFreq = 0;
        NvmContextChange( );
        return true;
    }
    return false;
#else
    return false;
#endif // LORAMAC_CLASSB_ENABLED
}

TimerTime_t LoRaMacClassBIsUplinkCollision( TimerTime_t txTimeOnAir )
{
#ifdef LORAMAC_CLASSB_ENABLED
    TimerTime_t currentTime = TimerGetCurrentTime( );
    TimerTime_t beaconReserved = 0;
    TimerTime_t nextBeacon = SysTimeToMs( Ctx.BeaconCtx.NextBeaconRx );

    beaconReserved = nextBeacon -
                     CLASSB_BEACON_GUARD -
                     Ctx.LoRaMacClassBParams.LoRaMacParams->ReceiveDelay1 -
                     Ctx.LoRaMacClassBParams.LoRaMacParams->ReceiveDelay2 -
                     txTimeOnAir;

    // Check if the next beacon will be received during the next uplink.
    if( ( currentTime >= beaconReserved ) && ( currentTime < ( nextBeacon + CLASSB_BEACON_RESERVED ) ) )
    {// Next beacon will be sent during the next uplink.
        return CLASSB_BEACON_RESERVED;
    }
    return 0;
#else
    return 0;
#endif // LORAMAC_CLASSB_ENABLED
}

void LoRaMacClassBStopRxSlots( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    TimerStop( &Ctx.PingSlotTimer );
#if (LORAMAC_MAX_MC_CTX > 0)
    TimerStop( &Ctx.MulticastSlotTimer );
#endif

    CRITICAL_SECTION_BEGIN( );
    LoRaMacClassBEvents.Events.PingSlot = 0;
#if (LORAMAC_MAX_MC_CTX > 0)
    LoRaMacClassBEvents.Events.MulticastSlot = 0;
#endif
    CRITICAL_SECTION_END( );
#endif // LORAMAC_CLASSB_ENABLED
}

void LoRaMacClassBStopMulticastRxSlots( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
#if (LORAMAC_MAX_MC_CTX > 0)
    TimerStop( &Ctx.MulticastSlotTimer );

    CRITICAL_SECTION_BEGIN( );
    LoRaMacClassBEvents.Events.MulticastSlot = 0;
    CRITICAL_SECTION_END( );
#endif
#endif
}

void LoRaMacClassBStartRxSlots( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    Ctx.PingSlotState = PINGSLOT_STATE_CALC_PING_OFFSET;
    Ctx.UpdatePingOffsetReq_unicast = true;
    TimerSetValue( &Ctx.PingSlotTimer, 0 );
    TimerStart( &Ctx.PingSlotTimer );

    // Start multicast slots only if valid multicast entry is included in the table.
    LoRaMacClassBStartMulticastRxSlots();

#endif // LORAMAC_CLASSB_ENABLED
}

void LoRaMacClassBStartMulticastRxSlots( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
#if (LORAMAC_MAX_MC_CTX > 0)
    MulticastCtx_t  *cur;
    uint8_t         i;

    // Start multicast slots only if valid multicast entry is included in the table.
    cur = Ctx.LoRaMacClassBParams.MulticastChannels;
    for( i = 0; i < LORAMAC_MAX_MC_CTX; i++ )
    {
        if( ( cur->ChannelParams.IsEnabled == true ) &&
            ( cur->ChannelParams.Class == CLASS_B ) )
        {
            Ctx.MulticastSlotState = PINGSLOT_STATE_CALC_PING_OFFSET;
            Ctx.UpdatePingOffsetReq_multicast= true;
            TimerSetValue( &Ctx.MulticastSlotTimer, 0 );
            TimerStart( &Ctx.MulticastSlotTimer );

            break;
        }
        cur++;
    }
#endif  // LORAMAC_MAX_MC_CTX
#endif
}

void LoRaMacClassBSetFPendingBit( uint32_t address, uint8_t fPendingSet )
{
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
#ifdef LORAMAC_CLASSB_ENABLED
    if( address == *Ctx.LoRaMacClassBParams.LoRaMacDevAddr )
    {
        // Unicast
        Ctx.NvmCtx->PingSlotCtx.FPendingSet = fPendingSet;
    }
#if (LORAMAC_MAX_MC_CTX > 0)
    else
    {
        MulticastCtx_t *cur = Ctx.LoRaMacClassBParams.MulticastChannels;
 
        for( uint8_t i = 0; i < LORAMAC_MAX_MC_CTX; i++ )
        {
            if( cur != NULL )
            {
                // Set the fPending bit, if its a multicast
                if( address == cur->ChannelParams.Address )
                {
                    cur->FPendingSet = fPendingSet;
                }
            }
            cur++;
        }
    }
#endif  // LORAMAC_MAX_MC_CTX
#endif
#endif
}

void LoRaMacClassBSetMulticastPeriodicity( MulticastCtx_t* multicastChannel )
{
#ifdef LORAMAC_CLASSB_ENABLED
#if (LORAMAC_MAX_MC_CTX > 0)
    if( multicastChannel != NULL )
    {
        multicastChannel->PingNb = CalcPingNb( multicastChannel->ChannelParams.RxParams.ClassB.Periodicity );
        multicastChannel->PingPeriod = CalcPingPeriod( multicastChannel->PingNb );
    }
#endif
#endif // LORAMAC_CLASSB_ENABLED
}

void LoRaMacClassBProcess( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    LoRaMacClassBEvents_t events;

    CRITICAL_SECTION_BEGIN( );
    events = LoRaMacClassBEvents;
    LoRaMacClassBEvents.Value = 0;
    CRITICAL_SECTION_END( );

    if( events.Value != 0 )
    {
        if( events.Events.Beacon == 1 )
        {
            LoRaMacClassBProcessBeacon( );
        }
        if( events.Events.PingSlot == 1 )
        {
            LoRaMacClassBProcessPingSlot( );
        }
#if (LORAMAC_MAX_MC_CTX > 0)
        if( events.Events.MulticastSlot == 1 )
        {
            LoRaMacClassBProcessMulticastSlot( );
        }
#endif
    }
#endif // LORAMAC_CLASSB_ENABLED
}

void LoRaMacClassBSetRxBeaconTimingTime( TimerTime_t time )
{
#ifdef LORAMAC_CLASSB_ENABLED
    Ctx.BeaconCtx.LastSystimeSetTimeMs = time;
    Ctx.BeaconCtx.LastSystimeSetIsBeacon = false;
#endif
}

#ifdef LORAMAC_CLASSB_ENABLED
static int32_t LoRaMacClassBGetStackProcessTime( uint8_t kind )
{
    int32_t retVal;

    switch( kind )
    {
        case CLASSB_STACK_PROCTIME_SEL_BEACON_ACQISITION:
            retVal = CLASSB_STACK_PROCTIMEMS_BEACON_ACQISITION;
            break;

        case CLASSB_STACK_PROCTIME_SEL_BEACON:
            retVal = CLASSB_STACK_PROCTIMEMS_BEACON;
            break;

        case CLASSB_STACK_PROCTIME_SEL_PING_SLOT_SHORT_PERIOD:
            retVal = CLASSB_STACK_PROCTIMEMS_PING_SLOT_SHORT_PERIOD;
            break;

        case CLASSB_STACK_PROCTIME_SEL_PING_SLOT_LONG_PERIOD:
            retVal = CLASSB_STACK_PROCTIMEMS_PING_SLOT_LONG_PERIOD;
            break;

        default:
            return 0;  // never comes here
    }

    if (SX126xGetClockSelect() == RADIO_CLOCK_TCXO_SEL)
    {
        retVal += ((uint32_t)((RP_TCXO_STAB_TIME * 15.625)/1000.0));
    }

    return retVal;
}
#endif  // LORAMAC_CLASSB_ENABLED

//-----------------------------------------
// for MCU low power
#ifdef LORAMAC_CLASSB_ENABLED
static LoRaMacStatus_t LoRaMacClassBSetLowPower_Beacon( void );
static LoRaMacStatus_t LoRaMacClassBSetLowPower_PingSlot( void );
#if (LORAMAC_MAX_MC_CTX > 0)
static LoRaMacStatus_t LoRaMacClassBSetLowPower_MulticastSlot( void );
#endif
#endif

LoRaMacStatus_t LoRaMacClassBSetLowPower( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    LoRaMacStatus_t retVal;

    retVal  = LoRaMacClassBSetLowPower_Beacon();
    retVal |= LoRaMacClassBSetLowPower_PingSlot();
#if (LORAMAC_MAX_MC_CTX > 0)
    retVal |= LoRaMacClassBSetLowPower_MulticastSlot();
#endif

    if (retVal != LORAMAC_STATUS_OK)
    {
        retVal = LORAMAC_STATUS_BUSY;
    }

    return retVal;
#else
    return LORAMAC_STATUS_OK;
#endif
}

#ifdef LORAMAC_CLASSB_ENABLED
static LoRaMacStatus_t LoRaMacClassBSetLowPower_Beacon( void )
{
    LoRaMacStatus_t retVal;

    // init
    retVal = LORAMAC_STATUS_OK;

    /* check beacon state / event */
    if (LoRaMacClassBEvents.Events.Beacon == 1)
    {
        retVal = LORAMAC_STATUS_BUSY;
    }
#if defined(DEBUG_LORAMAC)
    else  // for-debug / fail-safe
    {
        if (TimerExists(&Ctx.BeaconTimer) == true)
        {
            if ((Ctx.BeaconState == BEACON_STATE_LOCKED) ||
                (Ctx.BeaconState == BEACON_STATE_RX) ||
                (Ctx.BeaconState == BEACON_STATE_TIMEOUT) ||
                (Ctx.BeaconState == BEACON_STATE_HALT))
            {
                retVal = LORAMAC_STATUS_BUSY;
                print( "*MCUPWR:Unexpected beacon case (" );
                print_dec( Ctx.BeaconState, 3, '\0' );
                print( ")" );
                print_newline();
            }
        }
    }
#endif  //DEBUG_LORAMAC

    return retVal;
}

static LoRaMacStatus_t LoRaMacClassBSetLowPower_PingSlot( void )
{
    LoRaMacStatus_t retVal;

    // init
    retVal = LORAMAC_STATUS_OK;

    /* check beacon state / event */
    if (LoRaMacClassBEvents.Events.PingSlot == 1)
    {
        retVal = LORAMAC_STATUS_BUSY;
    }
#if defined(DEBUG_LORAMAC)
    else  // for-debug / fail-safe
    {
        if (TimerExists(&Ctx.PingSlotTimer) == true)
        {
            if ((Ctx.PingSlotState == PINGSLOT_STATE_SET_TIMER) ||
                (Ctx.PingSlotState == PINGSLOT_STATE_RX))
            {
                retVal = LORAMAC_STATUS_BUSY;
                print( "*MCUPWR:Unexpected pingslot case (" );
                print_dec( Ctx.PingSlotState, 3, '\0' );
                print( ")" );
                print_newline();
            }
        }
    }
#endif  //DEBUG_LORAMAC

    return retVal;
}

#if (LORAMAC_MAX_MC_CTX > 0)
static LoRaMacStatus_t LoRaMacClassBSetLowPower_MulticastSlot( void )
{
    LoRaMacStatus_t retVal;

    // init
    retVal = LORAMAC_STATUS_OK;

    /* check beacon state / event */
    if (LoRaMacClassBEvents.Events.MulticastSlot == 1)
    {
        retVal = LORAMAC_STATUS_BUSY;
    }
#if defined(DEBUG_LORAMAC)
    else  // for-debug / fail-safe
    {
        if (TimerExists(&Ctx.MulticastSlotTimer) == true)
        {
            if ((Ctx.MulticastSlotState == PINGSLOT_STATE_SET_TIMER) ||
                (Ctx.MulticastSlotState == PINGSLOT_STATE_RX))
            {
                retVal = LORAMAC_STATUS_BUSY;
                print( "*MCUPWR:Unexpected multicastslot case (" );
                print_dec( Ctx.MulticastSlotState, 3, '\0' );
                print( ")" );
                print_newline();
            }
        }
    }
#endif  //DEBUG_LORAMAC

    return retVal;
}
#endif  // LORAMAC_MAX_MC_CTX
#endif  // LORAMAC_CLASSB_ENABLED

//-----------------------------------------
// for RFIC low power
bool LoRaMacClassBIsReadyRxSlots( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    bool            retIsReadyRx;
    DeviceClass_t   deviceClass;

    // init
    retIsReadyRx = false;
    deviceClass  = LoRaMacGetDeviceClass();

    // do not SleepCold() if ping slot period is short.
    if( ( deviceClass == CLASS_B ) && 
        ( Ctx.NvmCtx->PingSlotCtx.Periodicity < CLASSB_PING_SLOT_PERIODICITY_RFSLEEP_THRESHOLD ) )
    {
        retIsReadyRx = true;
    }

    return retIsReadyRx;
#else
    return false;
#endif
}
