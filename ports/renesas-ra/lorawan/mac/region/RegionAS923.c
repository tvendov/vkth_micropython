/*!
 * \file      RegionAS923.c
 *
 * \brief     Region implementation for AS923
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
 *               ___ _____ _   ___ _  _____ ___  ___  ___ ___
 *              / __|_   _/_\ / __| |/ / __/ _ \| _ \/ __| __|
 *              \__ \ | |/ _ \ (__| ' <| _| (_) |   / (__| _|
 *              |___/ |_/_/ \_\___|_|\_\_| \___/|_|_\\___|___|
 *              embedded.connectivity.solutions===============
 *
 * \endcode
 *
 * \author    Miguel Luis ( Semtech )
 *
 * \author    Gregory Cristian ( Semtech )
 *
 * \author    Daniel Jaeckle ( STACKFORCE )
*/
/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
#if defined(REGION_AS923)

#include "RegionCommon.h"
#include "RegionAS923.h"

#include "utilities.h"

// Definitions
#define CHANNELS_MASK_SIZE              1

/*!
 * (Japan)
 * Specifies the reception bandwidth to be used while executing the LBT
 * Max channel bandwidth is 200 kHz
 */
#define AS923_LBT_RX_BANDWIDTH          200000

uint32_t AS923_CcaBw200 = 234300; //!< CCA bandwidth for 200 kHz channel
uint32_t AS923_CcaBw400 = 467000; //!< CCA bandwidth for 400 kHz channel

/*!
 * Region specific context
 */
typedef struct sRegionAS923NvmCtx
{
    /*!
     * LoRaMAC channels
     */
    ChannelParams_t Channels[ AS923_MAX_NB_CHANNELS ];
    /*!
     * LoRaMac bands
     */
    Band_t Bands[ AS923_MAX_NB_BANDS ];
    /*!
     * LoRaMac channels mask
     */
    uint16_t ChannelsMask[ CHANNELS_MASK_SIZE ];
    /*!
     * LoRaMac channels default mask
     */
    uint16_t ChannelsDefaultMask[ CHANNELS_MASK_SIZE ];
    /*!
     * RSSI free channel threshold
     */
    int16_t RssiFreeThreshold;
    /*!
     * Carrier sense time
     */
    uint32_t CarrierSenseTime;
}RegionAS923NvmCtx_t;

/*
 * Non-volatile module context.
 */
static RegionAS923NvmCtx_t NvmCtx;

/* duty cycle enabled   */
uint8_t AS923_DutyCycleEnabled = AS923_DUTY_CYCLE_ENABLED;

/* AS923 channel plan */
#define AS923_GROUP_DEFAULT     AS923_GROUP_1
uint8_t AS923_groupSel = AS923_GROUP_DEFAULT;

const uint32_t AS923FreqOffsetTable[ AS923_GROUP_MAX ] =
{
    AS923_FREQ_OFFSET,              // Group AS923-1
    AS923_JAPAN_FREQ_OFFSET,        // Group AS923-1 (Japan)
#if (REGION_VERSION >= REGION_VERSION_2_1_0_2)
    AS923_GROUP2_FREQ_OFFSET,       // Group AS923-2
    AS923_GROUP3_FREQ_OFFSET,       // Group AS923-3
#endif
#if (REGION_VERSION >= REGION_VERSION_2_1_0_3)
    AS923_GROUP4_FREQ_OFFSET,       // Group AS923-4
#endif
};

const uint32_t AS923FreqMinMaxTable[ AS923_GROUP_MAX ][ 2 ] =
{
    /* [0]= minimum frequency       [1]= maximum frequency */
    { AS923_MIN_RF_FREQUENCY,       AS923_MAX_RF_FREQUENCY },       // Group AS923-1
    { AS923_JAPAN_MIN_RF_FREQUENCY, AS923_JAPAN_MAX_RF_FREQUENCY }, // Group AS923-1 (Japan)
#if (REGION_VERSION >= REGION_VERSION_2_1_0_2)
    { AS923_MIN_RF_FREQUENCY,       AS923_MAX_RF_FREQUENCY },       // Group AS923-2
    { AS923_MIN_RF_FREQUENCY,       AS923_MAX_RF_FREQUENCY },       // Group AS923-3
#endif
#if (REGION_VERSION >= REGION_VERSION_2_1_0_3)
    { AS923_MIN_RF_FREQUENCY,       AS923_MAX_RF_FREQUENCY },       // Group AS923-4
#endif
};

const int8_t AS923TxMaxDatarateTable[ AS923_GROUP_MAX ] =
{
    AS923_TX_MAX_DATARATE,          // Group AS923-1
    AS923_JAPAN_TX_MAX_DATARATE,    // Group AS923-1 (Japan)
#if (REGION_VERSION >= REGION_VERSION_2_1_0_2)
    AS923_TX_MAX_DATARATE,          // Group AS923-2
    AS923_TX_MAX_DATARATE,          // Group AS923-3
#endif
#if (REGION_VERSION >= REGION_VERSION_2_1_0_3)
    AS923_TX_MAX_DATARATE,          // Group AS923-4
#endif
};

const int8_t AS923RxMaxDatarateTable[ AS923_GROUP_MAX ] =
{
    AS923_RX_MAX_DATARATE,          // Group AS923-1
    AS923_JAPAN_RX_MAX_DATARATE,    // Group AS923-1 (Japan)
#if (REGION_VERSION >= REGION_VERSION_2_1_0_2)
    AS923_RX_MAX_DATARATE,          // Group AS923-2
    AS923_RX_MAX_DATARATE,          // Group AS923-3
#endif
#if (REGION_VERSION >= REGION_VERSION_2_1_0_3)
    AS923_RX_MAX_DATARATE,          // Group AS923-4
#endif
};

const float AS923MaxEIRPTable[ AS923_GROUP_MAX ] =
{
    AS923_DEFAULT_MAX_EIRP,         // Group AS923-1
    AS923_JAPAN_DEFAULT_MAX_EIRP,   // Group AS923-1 (Japan)
#if (REGION_VERSION >= REGION_VERSION_2_1_0_2)
    AS923_DEFAULT_MAX_EIRP,         // Group AS923-2
    AS923_DEFAULT_MAX_EIRP,         // Group AS923-3
#endif
#if (REGION_VERSION >= REGION_VERSION_2_1_0_3)
    AS923_DEFAULT_MAX_EIRP,         // Group AS923-4
#endif
};


// Static functions
static int8_t GetNextLowerTxDr( int8_t dr, int8_t minDr )
{
    uint8_t nextLowerDr = 0;

    if( dr == minDr )
    {
        nextLowerDr = minDr;
    }
    else
    {
        nextLowerDr = dr - 1;
    }
    return nextLowerDr;
}

static uint32_t GetBandwidth( uint32_t drIndex )
{
    switch( BandwidthsAS923[drIndex] )
    {
        default:
        case 125000:
            return 0;
        case 250000:
            return 1;
        case 500000:
            return 2;
    }
}

static int8_t LimitTxPower( int8_t txPower, int8_t maxBandTxPower, int8_t datarate, uint16_t* channelsMask )
{
    int8_t txPowerResult = txPower;

    // Limit tx power to the band max
    txPowerResult =  R_MAX( txPower, maxBandTxPower );

    return txPowerResult;
}

static bool VerifyRfFreq( uint32_t freq )
{
    // Check radio driver support
    if( Radio.CheckRfFrequency( freq ) == false )
    {
        return false;
    }

    if( ( freq < AS923FreqMinMaxTable[ AS923_groupSel ][ 0 ] ) ||   //[0]=min
        ( freq > AS923FreqMinMaxTable[ AS923_groupSel ][ 1 ] ) )    //[1]=max
    {
        return false;
    }
    return true;
}

static uint8_t CountNbOfEnabledChannels( bool joined, uint8_t datarate, uint16_t* channelsMask, ChannelParams_t* channels, Band_t* bands, uint8_t* enabledChannels, uint8_t* delayTx )
{
    uint8_t nbEnabledChannels = 0;
    uint8_t delayTransmission = 0;

    for( uint8_t i = 0, k = 0; i < AS923_MAX_NB_CHANNELS; i += 16, k++ )
    {
        for( uint8_t j = 0; j < 16; j++ )
        {
            if( ( channelsMask[k] & ( 1 << j ) ) != 0 )
            {
                if( channels[i + j].Frequency == 0 )
                { // Check if the channel is enabled
                    continue;
                }
                if( joined == false )
                {
                    if( ( AS923_JOIN_CHANNELS & ( 1 << j ) ) == 0 )
                    {
                        continue;
                    }
                }
                if( RegionCommonValueInRange( datarate, channels[i + j].DrRange.Fields.Min,
                                              channels[i + j].DrRange.Fields.Max ) == false )
                { // Check if the current channel selection supports the given datarate
                    continue;
                }
                if( bands[channels[i + j].Band].TimeOff > 0 )
                { // Check if the band is available for transmission
                    delayTransmission++;
                    continue;
                }
                enabledChannels[nbEnabledChannels++] = i + j;
            }
        }
    }

    *delayTx = delayTransmission;
    return nbEnabledChannels;
}

PhyParam_t RegionAS923GetPhyParam( GetPhyParams_t* getPhy )
{
    PhyParam_t phyParam = { 0 };

    switch( getPhy->Attribute )
    {
        case PHY_MIN_RX_DR:
        {
            if( getPhy->DownlinkDwellTime == 0 )
            {
                phyParam.Value = AS923_RX_MIN_DATARATE;
            }
            else
            {
                phyParam.Value = AS923_DWELL_LIMIT_DATARATE;
            }
            break;
        }
        case PHY_MIN_TX_DR:
        {
            if( getPhy->UplinkDwellTime == 0 )
            {
                phyParam.Value = AS923_TX_MIN_DATARATE;
            }
            else
            {
                phyParam.Value = AS923_DWELL_LIMIT_DATARATE;
            }
            break;
        }
        case PHY_DEF_TX_DR:
        {
            phyParam.Value = AS923_DEFAULT_DATARATE;
            break;
        }
        case PHY_MAX_TX_DR:
        {
            phyParam.Value = AS923_TX_MAX_DATARATE;
            break;
        }
        case PHY_NEXT_LOWER_TX_DR:
        {
            if( getPhy->UplinkDwellTime == 0 )
            {
                phyParam.Value = GetNextLowerTxDr( getPhy->Datarate, AS923_TX_MIN_DATARATE );
            }
            else
            {
                phyParam.Value = GetNextLowerTxDr( getPhy->Datarate, AS923_DWELL_LIMIT_DATARATE );
            }
            break;
        }
        case PHY_MAX_TX_POWER:
        {
            phyParam.Value = AS923_MAX_TX_POWER;
            break;
        }
        case PHY_DEF_TX_POWER:
        {
            phyParam.Value = AS923_DEFAULT_TX_POWER;
            break;
        }
        case PHY_MAX_PAYLOAD:
        {
            if( getPhy->UplinkDwellTime == 0 )
            {
                phyParam.Value = MaxPayloadOfDatarateDwell0AS923[getPhy->Datarate];
            }
            else
            {
                phyParam.Value = MaxPayloadOfDatarateDwell1AS923[getPhy->Datarate];
            }
            break;
        }
        case PHY_DUTY_CYCLE:
        {
            /* duty cycle enabled   */
            phyParam.Value = AS923_DutyCycleEnabled;
            break;
        }
#if (REGION_VERSION == REGION_VERSION_1_0_3_x)  // LW1.0.3
        case PHY_MAX_FCNT_GAP:
        {
            phyParam.Value = AS923_MAX_FCNT_GAP;
            break;
        }
#endif
        case PHY_ACK_TIMEOUT:
        {
            phyParam.Value = ( AS923_ACKTIMEOUT + randr( -AS923_ACK_TIMEOUT_RND, AS923_ACK_TIMEOUT_RND ) );
            break;
        }
        case PHY_CHANNELS_MASK:
        {
            phyParam.ChannelsMask = NvmCtx.ChannelsMask;
            break;
        }
        case PHY_CHANNELS_DEFAULT_MASK:
        {
            phyParam.ChannelsMask = NvmCtx.ChannelsDefaultMask;
            break;
        }
        case PHY_MAX_NB_CHANNELS:
        {
            phyParam.Value = AS923_MAX_NB_CHANNELS;
            break;
        }
        case PHY_CHANNELS:
        {
            phyParam.Channels = NvmCtx.Channels;
            break;
        }
#ifdef LORAMAC_CLASSB_ENABLED
        case PHY_BEACON_CHANNEL_FREQ:
        {
            phyParam.Value = AS923_BEACON_CHANNEL_FREQ - AS923FreqOffsetTable[ AS923_groupSel ];
            break;
        }
        case PHY_BEACON_FORMAT:
        {
            phyParam.BeaconFormat.BeaconSize = AS923_BEACON_SIZE;
            phyParam.BeaconFormat.Rfu1Size = AS923_RFU1_SIZE;
            phyParam.BeaconFormat.Rfu2Size = AS923_RFU2_SIZE;
            break;
        }
        case PHY_BEACON_CHANNEL_DR:
        {
            phyParam.Value = AS923_BEACON_CHANNEL_DR;
            break;
        }
        case PHY_PING_SLOT_CHANNEL_FREQ:
        {
            phyParam.Value = AS923_PING_SLOT_CHANNEL_FREQ - AS923FreqOffsetTable[ AS923_groupSel ];
            break;
        }
        case PHY_PING_SLOT_CHANNEL_DR:
        {
            phyParam.Value = AS923_PING_SLOT_CHANNEL_DR;
            break;
        }
#endif
        case PHY_RSSI_FREE_THRESHOLD:
        {
            phyParam.RssiFreeThreshold = NvmCtx.RssiFreeThreshold;
            break;
        }
        case PHY_CARRIER_SENSE_TIME:
        {
            phyParam.CarrierSenseTime = NvmCtx.CarrierSenseTime;
            break;
        }
#ifdef LORAMAC_USE_UNUSEDPIB  // unused PIB
        case PHY_MAX_RX_WINDOW:
        {
            phyParam.Value = AS923_MAX_RX_WINDOW;
            break;
        }
        case PHY_RECEIVE_DELAY1:
        {
            phyParam.Value = AS923_RECEIVE_DELAY1;
            break;
        }
        case PHY_RECEIVE_DELAY2:
        {
            phyParam.Value = AS923_RECEIVE_DELAY2;
            break;
        }
        case PHY_JOIN_ACCEPT_DELAY1:
        {
            phyParam.Value = AS923_JOIN_ACCEPT_DELAY1;
            break;
        }
        case PHY_JOIN_ACCEPT_DELAY2:
        {
            phyParam.Value = AS923_JOIN_ACCEPT_DELAY2;
            break;
        }
        case PHY_DEF_DR1_OFFSET:
        {
            phyParam.Value = AS923_DEFAULT_RX1_DR_OFFSET;
            break;
        }
        case PHY_DEF_RX2_FREQUENCY:
        {
            phyParam.Value = AS923_RX_WND_2_FREQ - AS923FreqOffsetTable[ AS923_groupSel ];
            break;
        }
        case PHY_DEF_RX2_DR:
        {
            phyParam.Value = AS923_RX_WND_2_DR;
            break;
        }
        case PHY_DEF_UPLINK_DWELL_TIME:
        {
            phyParam.Value = AS923_DEFAULT_UPLINK_DWELL_TIME;
            break;
        }
        case PHY_DEF_DOWNLINK_DWELL_TIME:
        {
            phyParam.Value = AS923_DEFAULT_DOWNLINK_DWELL_TIME;
            break;
        }
        case PHY_DEF_MAX_EIRP:
        {
            phyParam.fValue = AS923MaxEIRPTable[ AS923_groupSel ];
            break;
        }
        case PHY_DEF_ANTENNA_GAIN:
        {
            phyParam.fValue = AS923_DEFAULT_ANTENNA_GAIN;
            break;
        }
        case PHY_DEF_ADR_ACK_LIMIT:
        {
            phyParam.Value = AS923_ADR_ACK_LIMIT;
            break;
        }
        case PHY_DEF_ADR_ACK_DELAY:
        {
            phyParam.Value = AS923_ADR_ACK_DELAY;
            break;
        }
#endif
        default:
        {
            break;
        }
    }

    return phyParam;
}

void RegionAS923SetPhyParam( SetPhyParams_t* setPhy )
{
    switch( setPhy->Attribute )
    {
        case PHY_DUTY_CYCLE:
        {
            if( AS923_DutyCycleEnabled != setPhy->param.dcycle_enabled )
            {
                AS923_DutyCycleEnabled = setPhy->param.dcycle_enabled;
#if defined(RP_USE_RADIO_CFG_CHECK)
                Radio.SetPib( PIB_RADIO_CFG_CHECK_ENABLE, (uint8_t *)&AS923_DutyCycleEnabled );
#endif
            }
            break;
        }
        case PHY_RSSI_FREE_THRESHOLD:
        {
            NvmCtx.RssiFreeThreshold = setPhy->param.RssiFreeThreshold;
            break;
        }
        case PHY_CARRIER_SENSE_TIME:
        {
            NvmCtx.CarrierSenseTime = setPhy->param.CarrierSenseTime;
            break;
        }
        default:
        {
            break;
        }
    }
}

void RegionAS923SetBandTxDone( SetBandTxDoneParams_t* txDone )
{
    RegionCommonSetBandTxDone( txDone->Joined, &NvmCtx.Bands[NvmCtx.Channels[txDone->Channel].Band], txDone->LastTxDoneTime );
}

void RegionAS923InitDefaults( InitDefaultsParams_t* params )
{
    Band_t bands[AS923_MAX_NB_BANDS] =
    {
        AS923_BAND0
    };

    switch( params->Type )
    {
        case INIT_TYPE_INIT:
            // Set AS923 group first
            AS923_groupSel = params->initAs923Group;

            // Initialize bands
            memcpy1( ( uint8_t* )NvmCtx.Bands, ( uint8_t* )bands, sizeof( Band_t ) * AS923_MAX_NB_BANDS );

            // Initialize the channels default mask
            NvmCtx.ChannelsDefaultMask[0] = LC( 1 ) + LC( 2 );
            // Update the channels mask
            RegionCommonChanMaskCopy( NvmCtx.ChannelsMask, NvmCtx.ChannelsDefaultMask, 1 );

            // RSSI, carrier sense
            NvmCtx.RssiFreeThreshold = AS923_RSSI_FREE_TH;
            NvmCtx.CarrierSenseTime = AS923_CARRIER_SENSE_TIME;

            // DutyCycle setting
            AS923_DutyCycleEnabled = AS923_DUTY_CYCLE_ENABLED;

            // (output) default PhyParams
            params->DefaultDutyCycleOn       = AS923_DutyCycleEnabled;                 // PHY_DUTY_CYCLE
            params->DefaultChannelsTxPower   = AS923_DEFAULT_TX_POWER;                 // PHY_DEF_TX_POWER
            params->DefaultChannelsDatarate  = AS923_DEFAULT_DATARATE;                 // PHY_DEF_TX_DR
            params->DefaultMaxRxWindow       = AS923_MAX_RX_WINDOW;                    // PHY_MAX_RX_WINDOW
            params->DefaultReceiveDelay1     = AS923_RECEIVE_DELAY1;                   // PHY_RECEIVE_DELAY1
            params->DefaultReceiveDelay2     = AS923_RECEIVE_DELAY2;                   // PHY_RECEIVE_DELAY2
            params->DefaultJoinAcceptDelay1  = AS923_JOIN_ACCEPT_DELAY1;               // PHY_JOIN_ACCEPT_DELAY1
            params->DefaultJoinAcceptDelay2  = AS923_JOIN_ACCEPT_DELAY2;               // PHY_JOIN_ACCEPT_DELAY2
            params->DefaultRx1DrOffset       = AS923_DEFAULT_RX1_DR_OFFSET;            // PHY_DEF_DR1_OFFSET
            params->DefaultRx2Frequency      = AS923_RX_WND_2_FREQ - AS923FreqOffsetTable[ AS923_groupSel ];   // PHY_DEF_RX2_FREQUENCY
            params->DefaultRx2Dr             = AS923_RX_WND_2_DR;                      // PHY_DEF_RX2_DR
            params->DefaultUplinkDwellTime   = AS923_DEFAULT_UPLINK_DWELL_TIME;        // PHY_DEF_UPLINK_DWELL_TIME
            params->DefaultDownlinkDwellTime = AS923_DEFAULT_DOWNLINK_DWELL_TIME;      // PHY_DEF_DOWNLINK_DWELL_TIME
            params->DefaultMaxEirp           = AS923MaxEIRPTable[ AS923_groupSel ];    // PHY_DEF_MAX_EIRP
            params->DefaultAntennaGain       = AS923_DEFAULT_ANTENNA_GAIN;             // PHY_DEF_ANTENNA_GAIN
            params->DefaultAdrAckLimit       = AS923_ADR_ACK_LIMIT;                    // PHY_DEF_ADR_ACK_LIMIT
            params->DefaultAdrAckDelay       = AS923_ADR_ACK_DELAY;                    // PHY_DEF_ADR_ACK_DELAY

            // Intentional fall through to set channels and apply frequency offset
            // no break

        case INIT_TYPE_RESTORE_DEFAULT_CHANNELS:
        {
            // Restore channels default mask
            NvmCtx.ChannelsMask[0] |= NvmCtx.ChannelsDefaultMask[0];

            // Channels
            NvmCtx.Channels[0] = ( ChannelParams_t ) AS923_LC1;
            NvmCtx.Channels[1] = ( ChannelParams_t ) AS923_LC2;

            // Apply frequency offset
            NvmCtx.Channels[0].Frequency -= AS923FreqOffsetTable[ AS923_groupSel ];
            NvmCtx.Channels[1].Frequency -= AS923FreqOffsetTable[ AS923_groupSel ];
            break;
        }
        case INIT_TYPE_BANDS_DCYCLE:
        {
            // Bands
            for (int i = 0 ; i < AS923_MAX_NB_BANDS ; i++)
            {
                NvmCtx.Bands[i].DCycle = bands[i].DCycle;
                NvmCtx.Bands[i].LastJoinTxDoneTime = bands[i].LastJoinTxDoneTime;
                NvmCtx.Bands[i].LastTxDoneTime = bands[i].LastTxDoneTime;
                NvmCtx.Bands[i].TimeOff = bands[i].TimeOff;
            }
            break;
        }
#ifdef LORAMAC_CTX_SAVERESTORE_ENALED
        case INIT_TYPE_RESTORE_CTX:
        {
            if( params->NvmCtx != 0 )
            {
                memcpy1( (uint8_t*) &NvmCtx, (uint8_t*) params->NvmCtx, sizeof( NvmCtx ) );
            }
            break;
        }
#endif
#if defined(RP_USE_RADIO_CFG_CHECK)
        case INIT_TYPE_RADIO_CFG:
        {
            bool                pibRadioCfgChkEnabled;
            RadioConfigRegion_t pibRadioCfgRgn;
            bool                pibRadioCfgUseFreqHopping;

            //---------------------------------
            // PIB_RADIO_CFG_REGION
            switch( AS923_groupSel )
            {
                case AS923_GROUP_1:
                    pibRadioCfgRgn = RADIO_CFG_AS1;
                    break;
                case AS923_GROUP_1_JPN:
                    pibRadioCfgRgn = RADIO_CFG_JP;
                    break;
#if (REGION_VERSION >= REGION_VERSION_2_1_0_2)
                case AS923_GROUP_2:
                    pibRadioCfgRgn = RADIO_CFG_AS2;
                    break;
                case AS923_GROUP_3:
                    pibRadioCfgRgn = RADIO_CFG_AS3;
                    break;
#endif
#if (REGION_VERSION >= REGION_VERSION_2_1_0_3)
                case AS923_GROUP_4:
                    pibRadioCfgRgn = RADIO_CFG_AS4;
                    break;
#endif
                default:
                    pibRadioCfgRgn = RADIO_CFG_AS1;
                    break;
            }
            Radio.SetPib( PIB_RADIO_CFG_REGION, (uint8_t *)&pibRadioCfgRgn );

            //---------------------------------
            // PIB_RADIO_CFG_CHECK_ENABLE
            pibRadioCfgChkEnabled = AS923_DutyCycleEnabled;
            Radio.SetPib( PIB_RADIO_CFG_CHECK_ENABLE, (uint8_t *)&pibRadioCfgChkEnabled );

            //---------------------------------
            // PIB_RADIO_CFG_FREQ_HOPPING_USED
            pibRadioCfgUseFreqHopping = false;
            Radio.SetPib( PIB_RADIO_CFG_FREQ_HOPPING_USED, (uint8_t *)&pibRadioCfgUseFreqHopping );

            break;
        }
#endif  // RP_USE_RADIO_CFG_CHECK
        default:
        {
            break;
        }
    }
}

#ifdef LORAMAC_CTX_SAVERESTORE_ENALED
void* RegionAS923GetNvmCtx( GetNvmCtxParams_t* params )
{
    params->nvmCtxSize = sizeof( RegionAS923NvmCtx_t );
    return &NvmCtx;
}
#endif

bool RegionAS923Verify( VerifyParams_t* verify, PhyAttribute_t phyAttribute )
{
    switch( phyAttribute )
    {
        case PHY_FREQUENCY:
        {
            return VerifyRfFreq( verify->Frequency );
        }
        case PHY_TX_DR:
        {
            if( verify->DatarateParams.UplinkDwellTime == 0 )
            {
                return RegionCommonValueInRange( verify->DatarateParams.Datarate,
                                                 AS923_TX_MIN_DATARATE,
                                                 AS923TxMaxDatarateTable[ AS923_groupSel ] );
            }
            else
            {
                return RegionCommonValueInRange( verify->DatarateParams.Datarate,
                                                 AS923_DWELL_LIMIT_DATARATE,
                                                 AS923TxMaxDatarateTable[ AS923_groupSel ] );
            }
        }
        case PHY_DEF_TX_DR:
        {
            return RegionCommonValueInRange( verify->DatarateParams.Datarate, DR_0, DR_5 );
        }
        case PHY_RX_DR:
        {
            if( verify->DatarateParams.DownlinkDwellTime == 0 )
            {
                return RegionCommonValueInRange( verify->DatarateParams.Datarate,
                                                 AS923_RX_MIN_DATARATE, AS923RxMaxDatarateTable[ AS923_groupSel ] );
            }
            else
            {
                return RegionCommonValueInRange( verify->DatarateParams.Datarate,
                                                 AS923_DWELL_LIMIT_DATARATE, AS923RxMaxDatarateTable[ AS923_groupSel ] );
            }
        }
        case PHY_DEF_TX_POWER:
        case PHY_TX_POWER:
        {
            // Remark: switched min and max!
            return RegionCommonValueInRange( verify->TxPower, AS923_MAX_TX_POWER, AS923_MIN_TX_POWER );
        }
        case PHY_DUTY_CYCLE:
        {
            /* duty cycle enabled   */
            return AS923_DutyCycleEnabled;
        }
        default:
            return false;
    }
}

void RegionAS923ApplyCFList( ApplyCFListParams_t* applyCFList )
{
    ChannelParams_t newChannel;
    ChannelAddParams_t channelAdd;
    ChannelRemoveParams_t channelRemove;
    uint8_t chanIdx, pos;

    // Remove all channels except for the default
    for( chanIdx = AS923_NUMB_DEFAULT_CHANNELS; chanIdx < AS923_MAX_NB_CHANNELS; chanIdx++ )
    {
        channelRemove.ChannelId = chanIdx;
        RegionAS923ChannelsRemove( &channelRemove );
    }

    // check CFList
    if( (applyCFList->Size != 16) || (applyCFList->Payload[15] != 0) )
    {
        return;
    }

    // Setup default datarate range
    newChannel.DrRange.Value = ( DR_5 << 4 ) | DR_0;

    // Last byte is RFU, don't take it into account
    pos = 0;
    for( chanIdx = AS923_NUMB_DEFAULT_CHANNELS; chanIdx < ( AS923_NUMB_CHANNELS_CF_LIST + AS923_NUMB_DEFAULT_CHANNELS ); chanIdx++ )
    {
        // Channel frequency
        newChannel.Frequency  = ( (uint32_t) applyCFList->Payload[pos++] );
        newChannel.Frequency |= ( (uint32_t) applyCFList->Payload[pos++] << 8 );
        newChannel.Frequency |= ( (uint32_t) applyCFList->Payload[pos++] << 16 );
        newChannel.Frequency *= 100;

        // Initialize alternative frequency to 0
        newChannel.Rx1Frequency = 0;

        if( newChannel.Frequency != 0 )
        {
            channelAdd.NewChannel = &newChannel;
            channelAdd.ChannelId = chanIdx;

            // Try to add all channels
            RegionAS923ChannelAdd( &channelAdd );
        }
    }
}

bool RegionAS923ChanMaskSet( ChanMaskSetParams_t* chanMaskSet )
{
    switch( chanMaskSet->ChannelsMaskType )
    {
        case CHANNELS_MASK:
        {
            RegionCommonChanMaskCopy( NvmCtx.ChannelsMask, chanMaskSet->ChannelsMaskIn, 1 );
            break;
        }
        case CHANNELS_DEFAULT_MASK:
        {
            RegionCommonChanMaskCopy( NvmCtx.ChannelsDefaultMask, chanMaskSet->ChannelsMaskIn, 1 );
            break;
        }
        default:
            return false;
    }
    return true;
}

void RegionAS923ComputeRxWindowParameters( int8_t datarate, uint8_t minRxSymbols, uint32_t rxError, RxConfigParams_t *rxConfigParams )
{
    uint32_t tSymbolInUs = 0;

    // Get the datarate, perform a boundary check
    rxConfigParams->Datarate = R_MIN( datarate, AS923RxMaxDatarateTable[ AS923_groupSel ] );
    rxConfigParams->Bandwidth = GetBandwidth( rxConfigParams->Datarate );

    if( rxConfigParams->Datarate == DR_7 )
    { // FSK
        tSymbolInUs = RegionCommonComputeSymbolTimeFsk( DataratesAS923[rxConfigParams->Datarate] );
    }
    else
    { // LoRa
        tSymbolInUs = RegionCommonComputeSymbolTimeLoRa( DataratesAS923[rxConfigParams->Datarate], BandwidthsAS923[rxConfigParams->Datarate] );
    }

    // Radio wake up time is considered by MAC
#ifdef LORAMAC_CLASSB_ENABLED
    if( rxConfigParams->RxSlot == RX_SLOT_WIN_BEACON )
    {
        RegionCommonComputeBeaconRxWindowParameters( tSymbolInUs, minRxSymbols, rxError, 0, &rxConfigParams->WindowTimeout, &rxConfigParams->WindowOffset );
    }
    else
#endif
    {
        RegionCommonComputeRxWindowParameters( tSymbolInUs, minRxSymbols, rxError, 0, &rxConfigParams->WindowTimeout, &rxConfigParams->WindowOffset );
    }

    // correct symbol timeout to match the sx126x. (for LoRa modulation)
    if( rxConfigParams->Datarate != DR_7 )
    {
        rxConfigParams->WindowTimeout = RegionCommonCorrectSymbolTimeoutLoRa( rxConfigParams->WindowTimeout );
    }
}

bool RegionAS923RxConfig( RxConfigParams_t* rxConfig, int8_t* datarate )
{
    RadioModems_t modem;
    int8_t dr = rxConfig->Datarate;
    uint8_t maxPayload = 0;
    int8_t phyDr = 0;
    uint32_t frequency = rxConfig->Frequency;

    if( Radio.GetStatus( ) != RF_IDLE )
    {
        return false;
    }

    if( rxConfig->RxSlot == RX_SLOT_WIN_1 )
    {
        // Apply window 1 frequency
        frequency = NvmCtx.Channels[rxConfig->Channel].Frequency;
        // Apply the alternative RX 1 window frequency, if it is available
        if( NvmCtx.Channels[rxConfig->Channel].Rx1Frequency != 0 )
        {
            frequency = NvmCtx.Channels[rxConfig->Channel].Rx1Frequency;
        }
    }

    // Read the physical datarate from the datarates table
    phyDr = DataratesAS923[dr];

    Radio.SetChannel( frequency );

    // Radio configuration
    if( dr == DR_7 )
    {
        modem = MODEM_FSK;
        Radio.SetRxConfig( modem, 50000, (uint32_t)phyDr * 1000UL, 0, 83333, 5, rxConfig->WindowTimeout, false, 0, true, 0, 0, false, rxConfig->RxContinuous );
    }
    else
    {
        modem = MODEM_LORA;
        Radio.SetRxConfig( modem, rxConfig->Bandwidth, phyDr, 1, 0, 8, rxConfig->WindowTimeout, false, 0, false, 0, 0, true, rxConfig->RxContinuous );
    }

    maxPayload = MaxPayloadOfDatarateDwell0AS923[dr];

    Radio.SetMaxPayloadLength( modem, maxPayload + LORA_MAC_FRMPAYLOAD_OVERHEAD );

    *datarate = (uint8_t) dr;
    return true;
}

bool RegionAS923TxConfig( TxConfigParams_t* txConfig, int8_t* txPower, TimerTime_t* txTimeOnAir )
{
    RadioModems_t modem;
    int8_t phyDr = DataratesAS923[txConfig->Datarate];
    int8_t txPowerLimited = LimitTxPower( txConfig->TxPower, NvmCtx.Bands[NvmCtx.Channels[txConfig->Channel].Band].TxMaxPower, txConfig->Datarate, NvmCtx.ChannelsMask );
    uint32_t bandwidth = GetBandwidth( txConfig->Datarate );
    int8_t phyTxPower = 0;

    // Calculate physical TX power
    phyTxPower = RegionCommonComputeTxPower( txPowerLimited, txConfig->MaxEirp, txConfig->AntennaGain );

    // Setup the radio frequency
    Radio.SetChannel( NvmCtx.Channels[txConfig->Channel].Frequency );

    if( txConfig->Datarate == DR_7 )
    { // High Speed FSK channel
        modem = MODEM_FSK;
        Radio.SetTxConfig( modem, phyTxPower, 25000, bandwidth, (uint32_t)phyDr * 1000UL, 0, 5, false, true, 0, 0, false, 4000 );
    }
    else
    {
        modem = MODEM_LORA;
        Radio.SetTxConfig( modem, phyTxPower, 0, bandwidth, phyDr, 1, 8, false, true, 0, 0, false, 4000 );
    }

    // Setup maximum payload length of the radio driver
    Radio.SetMaxPayloadLength( modem, txConfig->PktLen );
    // Get the time-on-air of the next tx frame
    *txTimeOnAir = Radio.TimeOnAir( modem, txConfig->PktLen );

    *txPower = txPowerLimited;
    return true;
}

uint8_t RegionAS923LinkAdrReq( LinkAdrReqParams_t* linkAdrReq, int8_t* drOut, int8_t* txPowOut, uint8_t* nbRepOut, uint8_t* nbBytesParsed )
{
    uint8_t status = 0x07;
    RegionCommonLinkAdrParams_t linkAdrParams = {0};
    uint8_t nextIndex = 0;
    uint8_t bytesProcessed = 0;
    uint16_t chMask = 0;
    GetPhyParams_t getPhy;
    PhyParam_t phyParam;
    RegionCommonLinkAdrReqVerifyParams_t linkAdrVerifyParams;
    uint8_t i;

    while( bytesProcessed < linkAdrReq->PayloadSize )
    {
        // Get ADR request parameters
        nextIndex = RegionCommonParseLinkAdrReq( &( linkAdrReq->Payload[bytesProcessed] ), &linkAdrParams );

        if( nextIndex == 0 )
            break; // break loop, since no more request has been found

        // Update bytes processed
        bytesProcessed += nextIndex;

        // Revert status, as we only check the last ADR request for the channel mask KO
        status = 0x07;

        // Setup temporary channels mask
        chMask = linkAdrParams.ChMask;

        // Verify channels mask
        if( linkAdrParams.ChMaskCtrl != 6 )
        {
            if( ( linkAdrParams.ChMaskCtrl == 0 ) && ( chMask != 0 ) )
            {
                for( i = 0; i < AS923_MAX_NB_CHANNELS; i++ )
                {
                    if( ( ( chMask & ( 1 << i ) ) != 0 ) &&
                        ( NvmCtx.Channels[i].Frequency == 0 ) )
                    {
                        // Trying to enable an undefined channel
                        status &= 0xFE; // Channel mask KO
                        break;
                    }
                }
            }
            else
            {
                status &= 0xFE; // Channel mask KO
            }
        }
        else
        {
            // ChMaskCtrl 6 : All channels ON
            for( i = 0; i < AS923_MAX_NB_CHANNELS; i++ )
            {
                if( NvmCtx.Channels[i].Frequency != 0 )
                {
                    chMask |= 1 << i;
                }
            }
        }
    }

    // Get the minimum possible datarate
    getPhy.Attribute = PHY_MIN_TX_DR;
    getPhy.UplinkDwellTime = linkAdrReq->UplinkDwellTime;
    phyParam = RegionAS923GetPhyParam( &getPhy );

    linkAdrVerifyParams.Status = status;
    linkAdrVerifyParams.AdrEnabled = linkAdrReq->AdrEnabled;
    linkAdrVerifyParams.Datarate = linkAdrParams.Datarate;
    linkAdrVerifyParams.TxPower = linkAdrParams.TxPower;
    linkAdrVerifyParams.NbRep = linkAdrParams.NbRep;
    linkAdrVerifyParams.CurrentDatarate = linkAdrReq->CurrentDatarate;
    linkAdrVerifyParams.CurrentTxPower = linkAdrReq->CurrentTxPower;
    linkAdrVerifyParams.CurrentNbRep = linkAdrReq->CurrentNbRep;
    linkAdrVerifyParams.NbChannels = AS923_MAX_NB_CHANNELS;
    linkAdrVerifyParams.ChannelsMask = &chMask;
    linkAdrVerifyParams.MinDatarate = ( int8_t )phyParam.Value;
    linkAdrVerifyParams.MaxDatarate = AS923TxMaxDatarateTable[ AS923_groupSel ];
    linkAdrVerifyParams.Channels = NvmCtx.Channels;
    linkAdrVerifyParams.MinTxPower = AS923_MIN_TX_POWER;
    linkAdrVerifyParams.MaxTxPower = AS923_MAX_TX_POWER;
    linkAdrVerifyParams.Version = linkAdrReq->Version;

    // Verify the parameters and update, if necessary
    status = RegionCommonLinkAdrReqVerifyParams( &linkAdrVerifyParams, &linkAdrParams.Datarate, &linkAdrParams.TxPower, &linkAdrParams.NbRep );

    // Update channelsMask if everything is correct
    if( status == 0x07 )
    {
        // Set the channels mask to a default value
        memset1( ( uint8_t* ) NvmCtx.ChannelsMask, 0, sizeof( NvmCtx.ChannelsMask ) );
        // Update the channels mask
        NvmCtx.ChannelsMask[0] = chMask;
    }

    // Update status variables
    *drOut = linkAdrParams.Datarate;
    *txPowOut = linkAdrParams.TxPower;
    *nbRepOut = linkAdrParams.NbRep;
    *nbBytesParsed = bytesProcessed;

    return status;
}

uint8_t RegionAS923RxParamSetupReq( RxParamSetupReqParams_t* rxParamSetupReq )
{
    uint8_t status = 0x07;

    // Verify radio frequency
    if( VerifyRfFreq( rxParamSetupReq->Frequency ) == false )
    {
        status &= 0xFE; // Channel frequency KO
    }

    // Verify datarate
    if( RegionCommonValueInRange( rxParamSetupReq->Datarate,
                                  AS923_RX_MIN_DATARATE, AS923RxMaxDatarateTable[ AS923_groupSel ] ) == false )
    {
        status &= 0xFD; // Datarate KO
    }

    // Verify datarate offset
    if( RegionCommonValueInRange( rxParamSetupReq->DrOffset, AS923_MIN_RX1_DR_OFFSET, AS923_MAX_RX1_DR_OFFSET ) == false )
    {
        status &= 0xFB; // Rx1DrOffset range KO
    }

    return status;
}

uint8_t RegionAS923NewChannelReq( NewChannelReqParams_t* newChannelReq )
{
    uint8_t status = 0x03;
    ChannelAddParams_t channelAdd;
    ChannelRemoveParams_t channelRemove;

    if( newChannelReq->NewChannel->Frequency == 0 )
    {
        channelRemove.ChannelId = newChannelReq->ChannelId;

        // Remove
        if( RegionAS923ChannelsRemove( &channelRemove ) == false )
        {
            status &= 0xFC;
        }
    }
    else
    {
        channelAdd.NewChannel = newChannelReq->NewChannel;
        channelAdd.ChannelId = newChannelReq->ChannelId;

        switch( RegionAS923ChannelAdd( &channelAdd ) )
        {
            case LORAMAC_STATUS_OK:
            {
                break;
            }
            case LORAMAC_STATUS_FREQUENCY_INVALID:
            {
                status &= 0xFE;
                break;
            }
            case LORAMAC_STATUS_DATARATE_INVALID:
            {
                status &= 0xFD;
                break;
            }
            case LORAMAC_STATUS_FREQ_AND_DR_INVALID:
            {
                status &= 0xFC;
                break;
            }
            default:
            {
                status &= 0xFC;
                break;
            }
        }
    }

    return status;
}

int8_t RegionAS923TxParamSetupReq( TxParamSetupReqParams_t* txParamSetupReq )
{
    // Accept the request
    return 0;
}

uint8_t RegionAS923DlChannelReq( DlChannelReqParams_t* dlChannelReq )
{
    uint8_t status = 0x03;

    if( dlChannelReq->ChannelId >= ( CHANNELS_MASK_SIZE * 16 ) )
    {
        return 0;
    }

    // Verify if the frequency is supported
    if( VerifyRfFreq( dlChannelReq->Rx1Frequency ) == false )
    {
        status &= 0xFE;
    }

    // Verify if an uplink frequency exists
    if( NvmCtx.Channels[dlChannelReq->ChannelId].Frequency == 0 )
    {
        status &= 0xFD;
    }

    // Apply Rx1 frequency, if the status is OK
    if( status == 0x03 )
    {
        NvmCtx.Channels[dlChannelReq->ChannelId].Rx1Frequency = dlChannelReq->Rx1Frequency;
    }

    return status;
}

int8_t RegionAS923AlternateDr( int8_t currentDr, AlternateDrType_t type )
{
    // Only AS923_DWELL_LIMIT_DATARATE is supported
    return AS923_DWELL_LIMIT_DATARATE;
}

void RegionAS923CalcBackOff( CalcBackOffParams_t* calcBackOff )
{
    RegionCommonCalcBackOffParams_t calcBackOffParams;

    calcBackOffParams.Channels = NvmCtx.Channels;
    calcBackOffParams.Bands = NvmCtx.Bands;
    calcBackOffParams.LastTxIsJoinRequest = calcBackOff->LastTxIsJoinRequest;
    calcBackOffParams.Joined = calcBackOff->Joined;
    calcBackOffParams.DutyCycleEnabled = calcBackOff->DutyCycleEnabled;
    calcBackOffParams.Channel = calcBackOff->Channel;
    calcBackOffParams.ElapsedTime = calcBackOff->ElapsedTime;
    calcBackOffParams.TxTimeOnAir = calcBackOff->TxTimeOnAir;

    RegionCommonCalcBackOff( &calcBackOffParams );
}

LoRaMacStatus_t RegionAS923NextChannel( NextChanParams_t* nextChanParams, uint8_t* channel, TimerTime_t* time, TimerTime_t* aggregatedTimeOff )
{
    uint8_t channelNext = 0;
    uint8_t nbEnabledChannels = 0;
    uint8_t delayTx = 0;
    uint8_t enabledChannels[AS923_MAX_NB_CHANNELS] = { 0 };
    TimerTime_t nextTxDelay = 0;
    bool     enableCca;

    LoRaMacGetMibVal( MIB_AS923_ENABLE_CCA, &enableCca );
    if( RegionCommonCountChannels( NvmCtx.ChannelsMask, 0, 1 ) == 0 )
    { // Reactivate default channels
        NvmCtx.ChannelsMask[0] |= LC( 1 ) + LC( 2 );
    }

    TimerTime_t elapsed = TimerGetElapsedTime( nextChanParams->LastAggrTx );
    if( ( nextChanParams->LastAggrTx == 0 ) || ( nextChanParams->AggrTimeOff <= elapsed ) )
    {
        // Reset Aggregated time off
        *aggregatedTimeOff = 0;

        // Update bands Time OFF
        nextTxDelay = RegionCommonUpdateBandTimeOff( nextChanParams->Joined, nextChanParams->DutyCycleEnabled, NvmCtx.Bands, AS923_MAX_NB_BANDS );

        // Search how many channels are enabled
        nbEnabledChannels = CountNbOfEnabledChannels( nextChanParams->Joined, nextChanParams->Datarate,
                                                      NvmCtx.ChannelsMask, NvmCtx.Channels,
                                                      NvmCtx.Bands, enabledChannels, &delayTx );
    }
    else
    {
        delayTx++;
        nextTxDelay = nextChanParams->AggrTimeOff - elapsed;
    }

    if( nbEnabledChannels > 0 )
    {
        if( enableCca == true )
        {
            for( uint8_t  i = 0, j = randr( 0, nbEnabledChannels - 1 ); i < AS923_MAX_NB_CHANNELS; i++ )
            {
                channelNext = enabledChannels[j];
                j = ( j + 1 ) % nbEnabledChannels;

                // Perform carrier sense for AS923_CARRIER_SENSE_TIME
                // If the channel is free, we can stop the LBT mechanism
                if (nextChanParams->Datarate != DR_6)
                {
                    Radio.SetPib(PIB_CCA_BANDWIDTH, (uint8_t *)&AS923_CcaBw200);
                }
                else
                {
                    Radio.SetPib(PIB_CCA_BANDWIDTH, (uint8_t *)&AS923_CcaBw400);
                }

                if( Radio.IsChannelFree( MODEM_LORA, NvmCtx.Channels[channelNext].Frequency,
                                         NvmCtx.RssiFreeThreshold, NvmCtx.CarrierSenseTime ) == true )
                {
                    // Free channel found
                    *channel = channelNext;
                    *time = 0;
                    return LORAMAC_STATUS_OK;
                }
            }
            return LORAMAC_STATUS_NO_FREE_CHANNEL_FOUND;
        }
        else
        {
            *channel = enabledChannels[randr( 0, nbEnabledChannels - 1 )];
            *time = 0;

            return LORAMAC_STATUS_OK;
        }
    }
    else
    {
        if( delayTx > 0 )
        {
            // Delay transmission due to AggregatedTimeOff or to a band time off
            *time = nextTxDelay;
            return LORAMAC_STATUS_DUTYCYCLE_RESTRICTED;
        }
        // Datarate not supported by any channel, restore defaults
        NvmCtx.ChannelsMask[0] |= LC( 1 ) + LC( 2 );
        *time = 0;
        return LORAMAC_STATUS_NO_CHANNEL_FOUND;
    }
}

LoRaMacStatus_t RegionAS923ChannelAdd( ChannelAddParams_t* channelAdd )
{
    bool drInvalid = false;
    bool freqInvalid = false;
    uint8_t id = channelAdd->ChannelId;

    if( id < AS923_NUMB_DEFAULT_CHANNELS )
    {
        return LORAMAC_STATUS_FREQ_AND_DR_INVALID;
    }

    if( id >= AS923_MAX_NB_CHANNELS )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

    // Validate the datarate range
#if (AS923_TX_MIN_DATARATE > 0)  // Avoid warning; comparing uint8 value with 0 is meaningless
    if( channelAdd->NewChannel->DrRange.Fields.Min < AS923_TX_MIN_DATARATE )
    {
        drInvalid = true;
    }
#endif
    if( ( channelAdd->NewChannel->DrRange.Fields.Min > channelAdd->NewChannel->DrRange.Fields.Max ) ||
        ( channelAdd->NewChannel->DrRange.Fields.Max > AS923TxMaxDatarateTable[ AS923_groupSel ] ) )
    {
        drInvalid = true;
    }

    // Check frequency
    if( freqInvalid == false )
    {
        if( VerifyRfFreq( channelAdd->NewChannel->Frequency ) == false )
        {
            freqInvalid = true;
        }
    }

    // Check status
    if( ( drInvalid == true ) && ( freqInvalid == true ) )
    {
        return LORAMAC_STATUS_FREQ_AND_DR_INVALID;
    }
    if( drInvalid == true )
    {
        return LORAMAC_STATUS_DATARATE_INVALID;
    }
    if( freqInvalid == true )
    {
        return LORAMAC_STATUS_FREQUENCY_INVALID;
    }

    memcpy1( ( uint8_t* ) &(NvmCtx.Channels[id]), ( uint8_t* ) channelAdd->NewChannel, sizeof( NvmCtx.Channels[id] ) );
    NvmCtx.Channels[id].Band = 0;
    NvmCtx.ChannelsMask[0] |= ( 1 << id );
    return LORAMAC_STATUS_OK;
}

bool RegionAS923ChannelsRemove( ChannelRemoveParams_t* channelRemove )
{
    uint8_t id = channelRemove->ChannelId;
    bool ret;

    if( id < AS923_NUMB_DEFAULT_CHANNELS )
    {
        return false;
    }

    // Remove the channel from the list of channels
    ret = RegionCommonChanDisable( NvmCtx.ChannelsMask, id, AS923_MAX_NB_CHANNELS );
    if( ret == true )
    {
        NvmCtx.Channels[id] = ( ChannelParams_t ){ 0, 0, { 0 }, 0 };
    }
    return ret;
}

void RegionAS923SetContinuousWave( ContinuousWaveParams_t* continuousWave )
{
    int8_t txPowerLimited = LimitTxPower( continuousWave->TxPower, NvmCtx.Bands[NvmCtx.Channels[continuousWave->Channel].Band].TxMaxPower, continuousWave->Datarate, NvmCtx.ChannelsMask );
    int8_t phyTxPower = 0;
    uint32_t frequency = NvmCtx.Channels[continuousWave->Channel].Frequency;

    // Calculate physical TX power
    phyTxPower = RegionCommonComputeTxPower( txPowerLimited, continuousWave->MaxEirp, continuousWave->AntennaGain );

    Radio.SetTxContinuousWave( frequency, phyTxPower, continuousWave->Timeout );
}

uint8_t RegionAS923ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset )
{
#if (REGION_VERSION >= REGION_VERSION_2_1_0_2)  // RP002-1.0.x
    // Initialize minDr
    int8_t minDr;

    if( downlinkDwellTime == 0 )
    {
        // Update the minDR for a downlink dwell time configuration of 0
        minDr = EffectiveRx1DrOffsetDownlinkDwell0AS923[dr][drOffset];
    }
    else
    {
        // Update the minDR for a downlink dwell time configuration of 1
        minDr = EffectiveRx1DrOffsetDownlinkDwell1AS923[dr][drOffset];
    }

    return minDr;

#else  // LW1.0.3
    // Initialize minDr for a downlink dwell time configuration of 0
    int8_t minDr = DR_0;

    // Update the minDR for a downlink dwell time configuration of 1
    if( downlinkDwellTime == 1 )
    {
        minDr = AS923_DWELL_LIMIT_DATARATE;
    }

    // Apply offset formula
    return R_MIN( DR_5, R_MAX( minDr, dr - EffectiveRx1DrOffsetAS923[drOffset] ) );

#endif
}

#ifdef LORAMAC_CLASSB_ENABLED
void RegionAS923RxBeaconSetup( RxBeaconSetup_t* rxBeaconSetup, uint8_t* outDr )
{
    RegionCommonRxBeaconSetupParams_t regionCommonRxBeaconSetup;

    regionCommonRxBeaconSetup.Datarates = DataratesAS923;
    regionCommonRxBeaconSetup.Frequency = rxBeaconSetup->Frequency;
    regionCommonRxBeaconSetup.BeaconSize = AS923_BEACON_SIZE;
    regionCommonRxBeaconSetup.BeaconDatarate = AS923_BEACON_CHANNEL_DR;
    regionCommonRxBeaconSetup.BeaconChannelBW = AS923_BEACON_CHANNEL_BW;
    regionCommonRxBeaconSetup.RxTime = rxBeaconSetup->RxTime;
    regionCommonRxBeaconSetup.SymbolTimeout = rxBeaconSetup->SymbolTimeout;

    RegionCommonRxBeaconSetup( &regionCommonRxBeaconSetup );

    // Store downlink datarate
    *outDr = AS923_BEACON_CHANNEL_DR;
}
#endif

#endif      // REGION_AS923
