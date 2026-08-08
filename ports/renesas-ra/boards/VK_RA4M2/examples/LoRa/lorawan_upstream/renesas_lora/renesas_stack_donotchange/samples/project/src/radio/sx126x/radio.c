/*!
 * \file      radio.c
 *
 * \brief     Radio driver API definition
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

#include <sx126x.h>
#include <math.h>
#include <string.h>
#include "utilities.h"
#include "timer.h"
#include "delay.h"
#include "radio.h"
#include "sx126x.h"
#include "sx126x-board.h"
#include "board.h"
#include "r_radio_region_api.h"
#include "timer-board.h"

/*!
 *  (RA0E1 and RA0E2)
 *  Move some radio functions into the "flash_gap" section.
 *  "flash_gap" section is between the vector table (at 0x0) and option setting memory (at 0x400).
 */
#define RP_RADIO_CODE_TO_FLASHGAP_SECTION
#if !defined(RP_RADIO_CODE_TO_FLASHGAP_SECTION_DISABLED)
#if defined(__RA0E1__) || defined(__RA0E2__)
#undef  RP_RADIO_CODE_TO_FLASHGAP_SECTION
#define RP_RADIO_CODE_TO_FLASHGAP_SECTION   BSP_PLACE_IN_SECTION(".flash_gap")
#endif
#endif

const uint8_t RadioVersion[] = "4.00";

/*!
 * \brief Initializes the radio
 *
 * \param [IN] events Structure containing the driver callback functions
 *
 * \retval result of API
 */
RadioResult_t RadioInit( RadioEvents_t *events ) RP_RADIO_CODE_TO_FLASHGAP_SECTION;

/*!
 * Return current radio status
 *
 * \param status Radio status.[RF_IDLE, RF_RX_RUNNING, RF_TX_RUNNING]
 */
RadioState_t RadioGetStatus( void );

/*!
 * \brief Configures the radio with the given modem
 *
 * \param [IN] modem Modem to be used [0: FSK, 1: LoRa]
 */
void RadioSetModem( RadioModems_t modem );

/*!
 * \brief Sets the channel frequency
 *
 * \param [IN] freq         Channel RF frequency
 */
void RadioSetChannel( uint32_t freq );

/*!
 * \brief Checks if the channel is free for the given time
 *
 * \param [IN] modem      Radio modem to be used [0: FSK, 1: LoRa]
 * \param [IN] freq       Channel RF frequency
 * \param [IN] rssiThresh RSSI threshold
 * \param [IN] maxCarrierSenseTime Max time while the RSSI is measured
 *
 * \retval isFree         [true: Channel is free, false: Channel is not free]
 */
bool RadioIsChannelFree( RadioModems_t modem, uint32_t freq, int16_t rssiThresh, uint32_t maxCarrierSenseTime );

/*!
 * \brief Generates a 32 bits random value based on the RSSI readings
 *
 * \remark This function sets the radio in LoRa modem mode and disables
 *         all interrupts.
 *         After calling this function either Radio.SetRxConfig or
 *         Radio.SetTxConfig functions must be called.
 *
 * \retval randomValue    32 bits random value
 */
uint32_t RadioRandom( void );

/*!
 * \brief Sets the reception parameters
 *
 * \param [IN] modem        Radio modem to be used [0: FSK, 1: LoRa]
 * \param [IN] bandwidth    Sets the bandwidth
 *                          FSK : >= 4800 and <= 467000 Hz
 *                          LoRa: [0: 125 kHz, 1: 250 kHz,
 *                                 2: 500 kHz, 3:  62 kHz
 *                                 4:  41 kHz  5:  31 khz
 *                                 6:  20 kHz  7:  15 khz
 *                                 8:  10 kHz  9:   7 khz]
 * \param [IN] datarate     Sets the Datarate
 *                          FSK : 600..300000 bits/s
 *                          LoRa: [5: 32, 6: 64, 7: 128, 8: 256, 9: 512,
 *                                10: 1024, 11: 2048, 12: 4096  chips]
 * \param [IN] coderate     Sets the coding rate (LoRa only)
 *                          FSK : N/A ( set to 0 )
 *                          LoRa: [1: 4/5, 2: 4/6, 3: 4/7, 4: 4/8]
 * \param [IN] bandwidthAfc Sets the AFC Bandwidth (FSK only)
 *                          FSK : N/A ( set to 0 )
 *                          LoRa: N/A ( set to 0 )
 * \param [IN] preambleLen  Sets the Preamble length
 *                          FSK : Number of bytes
 *                          LoRa: Length in symbols (the hardware adds 4 more symbols)
 * \param [IN] symbTimeout  Sets the RxSingle timeout value
 *                          FSK : timeout in number of bytes
 *                          LoRa: timeout in symbols
 * \param [IN] fixLen       Fixed length packets [0: variable, 1: fixed]
 * \param [IN] payloadLen   Sets payload length when fixed length is used
 * \param [IN] crcOn        Enables/Disables the CRC [0: OFF, 1: ON]
 * \param [IN] FreqHopOn    Enables disables the intra-packet frequency hopping
 *                          FSK : N/A ( set to 0 )
 *                          LoRa: N/A ( set to 0 )
 * \param [IN] HopPeriod    Number of symbols between each hop
 *                          FSK : N/A ( set to 0 )
 *                          LoRa: N/A ( set to 0 )
 * \param [IN] iqInverted   Inverts IQ signals (LoRa only)
 *                          FSK : N/A ( set to 0 )
 *                          LoRa: [0: not inverted, 1: inverted]
 * \param [IN] rxContinuous Sets the reception in continuous mode
 *                          [false: single mode, true: continuous mode]
 *
 * \retval result of API
 */
RadioResult_t RadioSetRxConfig( RadioModems_t modem, uint32_t bandwidth,
                          uint32_t datarate, uint8_t coderate,
                          uint32_t bandwidthAfc, uint16_t preambleLen,
                          uint16_t symbTimeout, bool fixLen,
                          uint8_t payloadLen,
                          bool crcOn, bool FreqHopOn, uint8_t HopPeriod,
                          bool iqInverted, bool rxContinuous );

/*!
 * \brief Sets the transmission parameters
 *
 * \param [IN] modem        Radio modem to be used [0: FSK, 1: LoRa]
 * \param [IN] power        Sets the output power [dBm]
 * \param [IN] fdev         Sets the frequency deviation (FSK only)
 *                          FSK : [Hz]
 *                          LoRa: 0
 * \param [IN] bandwidth    Sets the bandwidth (LoRa only)
 *                          FSK : 0
 *                          LoRa: [0: 125 kHz, 1: 250 kHz,
 *                                 2: 500 kHz, 3:  62 kHz
 *                                 4:  41 kHz  5:  31 khz
 *                                 6:  20 kHz  7:  15 khz
 *                                 8:  10 kHz  9:   7 khz]
 * \param [IN] datarate     Sets the Datarate
 *                          FSK : 600..300000 bits/s
 *                          LoRa: [6: 64, 7: 128, 8: 256, 9: 512,
 *                                10: 1024, 11: 2048, 12: 4096  chips]
 * \param [IN] coderate     Sets the coding rate (LoRa only)
 *                          FSK : N/A ( set to 0 )
 *                          LoRa: [1: 4/5, 2: 4/6, 3: 4/7, 4: 4/8]
 * \param [IN] preambleLen  Sets the preamble length
 *                          FSK : Number of bytes
 *                          LoRa: Length in symbols (the hardware adds 4 more symbols)
 * \param [IN] fixLen       Fixed length packets [0: variable, 1: fixed]
 * \param [IN] crcOn        Enables disables the CRC [0: OFF, 1: ON]
 * \param [IN] FreqHopOn    Enables disables the intra-packet frequency hopping
 *                          FSK : N/A ( set to 0 )
 *                          LoRa: [0: OFF, 1: ON]
 * \param [IN] HopPeriod    Number of symbols between each hop
 *                          FSK : N/A ( set to 0 )
 *                          LoRa: Number of symbols
 * \param [IN] iqInverted   Inverts IQ signals (LoRa only)
 *                          FSK : N/A ( set to 0 )
 *                          LoRa: [0: not inverted, 1: inverted]
 * \param [IN] timeout      Transmission timeout [ms]
 *
 * \retval result of API
 */
RadioResult_t RadioSetTxConfig( RadioModems_t modem, int8_t power, uint32_t fdev,
                          uint32_t bandwidth, uint32_t datarate,
                          uint8_t coderate, uint16_t preambleLen,
                          bool fixLen, bool crcOn, bool FreqHopOn,
                          uint8_t HopPeriod, bool iqInverted, uint32_t timeout );

/*!
 * \brief Checks if the given RF frequency is supported by the hardware
 *
 * \param [IN] frequency RF frequency to be checked
 * \retval isSupported [true: supported, false: unsupported]
 */
bool RadioCheckRfFrequency( uint32_t frequency );

/*!
 * \brief Computes the packet time on air in ms for the given payload
 *
 * \Remark Can only be called once SetRxConfig or SetTxConfig have been called
 *
 * \param [IN] modem      Radio modem to be used [0: FSK, 1: LoRa]
 * \param [IN] pktLen     Packet payload length
 *
 * \retval airTime        Computed airTime (ms) for the given packet payload length
 */
uint32_t RadioTimeOnAir( RadioModems_t modem, uint8_t pktLen );

/*!
 * \brief Sends the buffer of size. Prepares the packet to be sent and sets
 *        the radio in transmission
 *
 * \param [IN]: buffer     Buffer pointer
 * \param [IN]: size       Buffer size

 * \retval result of API
 */
RadioResult_t RadioSend( uint8_t *buffer, uint8_t size );

/*!
 * \brief Sets the radio in sleep mode
 */
void RadioSleep( void );

/*!
 * \brief Sets the radio in standby mode
 */
void RadioStandby( void );

/*!
 * \brief Sets the radio in reception mode for the given time
 * \param [IN] timeout Reception timeout [ms]
 *                     [0: continuous, others timeout]
 */
RadioResult_t RadioRx( uint32_t timeout );

/*!
 * \brief Start a Channel Activity Detection
 */
void RadioStartCad( void );

/*!
 * \brief Sets the radio in continuous wave transmission mode
 *
 * \param [IN]: freq       Channel RF frequency
 * \param [IN]: power      Sets the output power [dBm]
 * \param [IN]: time       Transmission mode timeout [s]
 *
 * \return API process result.
 */
RadioResult_t RadioSetTxContinuousWave( uint32_t freq, int8_t power, uint16_t time );

/*!
 * \brief Reads the current RSSI value
 *
 * \retval rssiValue Current RSSI value in [dBm]
 */
int16_t RadioRssi( RadioModems_t modem );

/*!
 * \brief Writes the radio register at the specified address
 *
 * \param [IN]: addr Register address
 * \param [IN]: data New register value
 */
void RadioWrite( uint16_t addr, uint8_t data );

/*!
 * \brief Reads the radio register at the specified address
 *
 * \param [IN]: addr Register address
 * \retval data Register value
 */
uint8_t RadioRead( uint16_t addr );

/*!
 * \brief Writes multiple radio registers starting at address
 *
 * \param [IN] addr   First Radio register address
 * \param [IN] buffer Buffer containing the new register's values
 * \param [IN] size   Number of registers to be written
 *
 * \retval result of API
 */
RadioResult_t RadioWriteBuffer( uint16_t addr, uint8_t *buffer, uint8_t size );

/*!
 * \brief Reads multiple radio registers starting at address
 *
 * \param [IN] addr First Radio register address
 * \param [OUT] buffer Buffer where to copy the registers data
 * \param [IN] size Number of registers to be read
 *
 * \retval result of API
 */
RadioResult_t RadioReadBuffer( uint16_t addr, uint8_t *buffer, uint16_t size );

/*!
 * \brief Sets the maximum payload length.
 *
 * \param [IN] modem      Radio modem to be used [0: FSK, 1: LoRa]
 * \param [IN] max        Maximum payload length in bytes
 */
void RadioSetMaxPayloadLength( RadioModems_t modem, uint8_t max );

/*!
 * \brief Sets the network to public or private. Updates the sync byte.
 *
 * \remark Applies to LoRa modem only
 *
 * \param [IN] enable if true, it enables a public network
 */
void RadioSetPublicNetwork( bool enable );

/*!
 * \brief Gets the time required for the board plus radio to get out of sleep.[ms]
 *
 * \retval time Radio plus board wakeup time in ms.
 */
uint32_t RadioGetWakeupTime( void );

/*!
 * \brief Process radio irq
 *
 * \retval IRQ has events or not.
 */
bool RadioIrqProcess( void );

/*!
 * \brief Sets the radio in reception mode with Max LNA gain for the given time
 * \param [IN] timeout Reception timeout [ms]
 *                     [0: continuous, others timeout]
 */
void RadioRxBoosted( uint32_t timeout );

/*!
 * \brief Sets the Rx duty cycle management parameters
 *
 * \param [in]  rxTime        Structure describing reception timeout value
 * \param [in]  sleepTime     Structure describing sleep timeout value
 */
void RadioSetRxDutyCycle( uint32_t rxTime, uint32_t sleepTime );

/*!
 * \brief Add a register to the retention list
 *
 * \param [in] registerAddress The address of the register to be kept in retention
 */
void RadioAddRegisterToRetentionList( uint16_t registerAddress ) RP_RADIO_CODE_TO_FLASHGAP_SECTION;


/*
 * The following function prototypes are added by renesas.
*/

/*!
 * \brief Sets the radio in Cold sleep mode and RC64K off
 */
void RadioSleepCold( void );

/*!
 * \brief Sets the radio in continuous preamble transmission mode
 *
 * \param [IN]: freq       Channel RF frequency
 * \param [IN]: power      Sets the output power [dBm]
 * \param [IN]: time       Transmission mode timeout [s]
 *
 * \return API process result.
 */
RadioResult_t RadioSetTxInfinitePreamble( uint32_t freq, int8_t power, uint16_t time );

/*!
 * \brief Gets energy detection value.
 *
 * \param [IN]: freq       Channel RF frequency
 * \param [IN]: edTime     ED scan time[ms].
 *
 * \retval edValue maximum RSSI value of edTime duration in [dBm]
*/
int16_t RadioEd(uint32_t freq, uint32_t edTime);

/*!
 * \brief Sets PIB value.
 *
 * \param [IN]: PIB_t    PIB id
 * \param [IN]: pInVal    address of PIB Value for set
 *
 * \retval result true:set success false:set fail
*/
bool RadioSetPib(PIB_t id, uint8_t * pInVal);

/*!
 * \brief Gets PIB value.
 *
 * \param [IN]:  PIB_t    PIB id
 * \param [OUT]: pOutVal   address of store PIB value
 *
 * \retval result true:get success false:get fail
*/
bool RadioGetPib(PIB_t id, uint8_t * pOutVal);

/*!
 * \brief Wakeup Radio and Recover Board Setting.
 */
void RadioWakeUp(void);

/*!
 * \brief Execute Image Calibration.
 *
 * \param [IN]:freq         Target Channel RF frequency of Image Calibration.
 */
void RadioCalibrateImage(uint32_t freq);

/*!
* \brief Gets Radio Error flags.
*
* \retval Radio Error flags. The result of flasg only valid on RxDone callback.
*/
uint16_t RadioGetErrorFlag(void);

/*!
 * \brief return LoRa Bandwidth index.(Internal Use)
 *
 * \param [IN] bandwidth LoRa
 *
 * \retval LoRa bandwidth index
 */
uint8_t RadioGetLoRaBandwidthIndex(RadioLoRaBandwidths_t bandwidth);

/*!
 * \brief Initialize PIBs value.(Internal Use)
 *
 * \retval none
*/
void RadioInitPib(void)  RP_RADIO_CODE_TO_FLASHGAP_SECTION;

/*!
 * \brief return condition for LowDataRateOptimization.(Internal Use)
 *
 * \retval true or false
*/
static bool isLoRaLowDataRateOptimize( uint32_t , uint32_t );



/*!
 * Radio driver structure initialization
 */
const struct Radio_s Radio =
{
    RadioInit,
    RadioGetStatus,
    RadioSetModem,
    RadioSetChannel,
    RadioIsChannelFree,
    RadioRandom,
    RadioSetRxConfig,
    RadioSetTxConfig,
    RadioCheckRfFrequency,
    RadioTimeOnAir,
    RadioSend,
    RadioSleep,
    RadioStandby,
    RadioRx,
    RadioStartCad,
    RadioSetTxContinuousWave,
    RadioRssi,
    RadioWrite,
    RadioRead,
    RadioWriteBuffer,
    RadioReadBuffer,
    RadioSetMaxPayloadLength,
    RadioSetPublicNetwork,
    RadioGetWakeupTime,
    RadioIrqProcess,
    // Available on SX126x only
    RadioRxBoosted,
    RadioSetRxDutyCycle,
    // following functions are added by renesas
    RadioSleepCold,
    RadioSetTxInfinitePreamble,
    RadioEd,
    RadioSetPib,
    RadioGetPib,
    RadioWakeUp,
    RadioCalibrateImage,
    RadioGetErrorFlag,
    #if defined( RP_USE_RADIO_CFG_CHECK )
    RpRegionGetTimeToNextTx,
    #endif
};

/*
 * Local types definition
 */


 /*!
 * FSK bandwidth definition
 */
typedef struct
{
    uint32_t bandwidth;
    uint8_t  RegValue;
}FskBandwidth_t;

/*!
 * Precomputed FSK bandwidth registers values
 */
const FskBandwidth_t FskBandwidths[] =
{
    { 0     , 0x1F },
    { 4800  , 0x1F },
    { 5800  , 0x17 },
    { 7300  , 0x0F },
    { 9700  , 0x1E },
    { 11700 , 0x16 },
    { 14600 , 0x0E },
    { 19500 , 0x1D },
    { 23400 , 0x15 },
    { 29300 , 0x0D },
    { 39000 , 0x1C },
    { 46900 , 0x14 },
    { 58600 , 0x0C },
    { 78200 , 0x1B },
    { 93800 , 0x13 },
    { 117300, 0x0B },
    { 156200, 0x1A },
    { 187200, 0x12 },
    { 234300, 0x0A },
    { 312000, 0x19 },
    { 373600, 0x11 },
    { 467000, 0x09 },
    { 500000, 0x00 }, // Invalid Bandwidth
};

const RadioLoRaBandwidths_t Bandwidths[] = {
    LORA_BW_125,
    LORA_BW_250,
    LORA_BW_500,
    // add followings
    LORA_BW_062,
    LORA_BW_041,
    LORA_BW_031,
    LORA_BW_020,
    LORA_BW_015,
    LORA_BW_010,
    LORA_BW_007
};

 /*!
 * LoRa bandwidth definition
 */
typedef struct
{
    uint32_t eNumBandwidth;
    uint32_t bandwidth;
}LoRaBandwidth_t;

const LoRaBandwidth_t enmVsBndWithTbl[] ={
    {LORA_BW_125,    125000},
    {LORA_BW_250,    250000},
    {LORA_BW_500,    467000}, // No 500000 bandwidth register setting so, set 467000(Fsk max width)
    {LORA_BW_062,    62000},
    {LORA_BW_041,    41000},
    {LORA_BW_031,    31000},
    {LORA_BW_020,    20000},
    {LORA_BW_015,    15000},
    {LORA_BW_010,    10000},
    {LORA_BW_007,    7000}
};

const uint32_t LoraRegBandwidth[] = {
    7000,      // LORA_BW_007(0)
    15000,     // LORA_BW_015(1)
    31000,     // LORA_BW_031(2)
    62000,     // LORA_BW_062(3)
    125000,    // LORA_BW_125(4)
    250000,    // LORA_BW_250(5)
    500000,    // LORA_BW_500(6)
    0,         // RFU(7)
    10000,     // LORA_BW_010(8)
    20000,     // LORA_BW_020(9)
    41000      // LORA_BW_041(10)
};

uint8_t MaxPayloadLength = 0xFF;

uint32_t TxTimeout = 0;
uint32_t RxTimeout = 0;

bool RxContinuous = false;


PacketStatus_t RadioPktStatus;
uint8_t RadioRxPayload[255];

volatile bool IrqFired = false;
PIB_VAL_t PibValues;
uint16_t RpRxErrFlg = RADIO_ERROR_NONE;
uint16_t OverlappedIrqCount = 0U;

/*
 * SX126x DIO IRQ callback functions prototype
 */

/*!
 * \brief DIO 1 IRQ callback
 */
void RadioOnDioIrq( void );

/*!
 * \brief Tx timeout timer callback
 */
void RadioOnTxTimeoutIrq( void );

/*!
 * \brief Rx timeout timer callback
 */
void RadioOnRxTimeoutIrq( void );

/*
 * Private global variables
 */


/*!
 * Holds the current network type for the radio
 */
typedef struct
{
    bool Previous;
    bool Current;
}RadioPublicNetwork_t;

static RadioPublicNetwork_t RadioPublicNetwork = { false };

/*!
 * Radio callbacks variable
 */
static RadioEvents_t* RadioEvents;

/*
 * Public global variables
 */

/*!
 * Radio hardware and global parameters
 */
SX126x_t SX126x;

/*!
 * Tx and Rx timers
 */
TimerEvent_t TxTimeoutTimer;
TimerEvent_t RxTimeoutTimer;

/*!
 * Returns the known FSK bandwidth registers value
 *
 * \param [IN] bandwidth Bandwidth value in Hz
 * \retval regValue Bandwidth register value.
 */
static uint8_t RadioGetFskBandwidthRegValue( uint32_t bandwidth )
{
    uint8_t i;

    if( bandwidth == 0 )
    {
        return( 0x1F );
    }

    for( i = 0; i < ( (sizeof( FskBandwidths )/sizeof( FskBandwidth_t))- 1 ); i++ )
    {
        if ( ( FskBandwidths[i].bandwidth < bandwidth ) && ( bandwidth <= FskBandwidths[i + 1].bandwidth ) )
        {
            return FskBandwidths[i + 1].RegValue;
        }
    }
    // ERROR: Value not found
    return( 0x1F );
}

RadioResult_t RadioInit( RadioEvents_t *events )
{
    RadioResult_t retVal = RADIO_SUCCESS;

    RadioEvents = events;

    RadioPublicNetwork.Current = false;
    RadioPublicNetwork.Previous = false;

    SX126xInit( RadioOnDioIrq );
    SX126xSetStandby( STDBY_RC );

#if defined (RP_USE_DCDC_FOR_RADIO)
    SX126xSetRegulatorMode( USE_DCDC );
#else //#if defined (RP_USE_DCDC_FOR_RADIO)
    SX126xSetRegulatorMode( USE_LDO );
#endif //RP_USE_DCDC_FOR_RADIO

    SX126xSetBufferBaseAddress( 0x00, 0x00 );
    SX126xSetTxParams( 0, RADIO_RAMP_200_US );
    SX126xSetDioIrqParams( (uint16_t)IRQ_RADIO_ALL, (uint16_t)IRQ_RADIO_ALL, (uint16_t)IRQ_RADIO_NONE, (uint16_t)IRQ_RADIO_NONE );

    // Add registers to the retention list (4 is the maximum possible number)
    RadioAddRegisterToRetentionList( REG_RX_GAIN );
    RadioAddRegisterToRetentionList( REG_TX_MODULATION );

    RadioInitPib();

    // Initialize driver timeout timers
    TimerStop( &TxTimeoutTimer);
    TimerStop( &RxTimeoutTimer);
    TimerInit( &TxTimeoutTimer, RadioOnTxTimeoutIrq );
    TimerInit( &RxTimeoutTimer, RadioOnRxTimeoutIrq );

    IrqFired = false;

    {
        RadioError_t error;
        error = SX126xGetDeviceErrors();
        if(error.Value != 0)
        {
            retVal = RADIO_FAIL;
        }
    }

    #if defined( RP_USE_RADIO_CFG_CHECK )
    RpRegionInit(0xFFFF); // Enable all predefined radio bands
    #endif

    return retVal;
}

RadioState_t RadioGetStatus( void )
{
    switch( SX126xGetOperatingMode( ) )
    {
        case MODE_TX:
            return RF_TX_RUNNING;
        case MODE_RX:
            return RF_RX_RUNNING;
        case MODE_CAD:
            return RF_CAD;
        case MODE_COLD_SLEEP:
            return RF_COLD_SLEEP;
        case MODE_SLEEP:
            return RF_WARM_SLEEP;
        default:
            return RF_IDLE;
    }
}

void RadioSetModem( RadioModems_t modem )
{
    switch( modem )
    {
    default:
    case MODEM_FSK:
        SX126xSetPacketType( PACKET_TYPE_GFSK );
        // When switching to GFSK mode the LoRa SyncWord register value is reset
        // Thus, we also reset the RadioPublicNetwork variable
        RadioPublicNetwork.Current = false;
        break;
    case MODEM_LORA:
        SX126xSetPacketType( PACKET_TYPE_LORA );
        // Public/Private network register is reset when switching modems
        if( RadioPublicNetwork.Current != RadioPublicNetwork.Previous )
        {
            RadioPublicNetwork.Current = RadioPublicNetwork.Previous;
            RadioSetPublicNetwork( RadioPublicNetwork.Current );
        }
        break;
    }
}

void RadioSetChannel( uint32_t freq )
{
    #if defined( RP_USE_RADIO_CFG_CHECK )
    if( PibValues.radioCfgCheckEnable )
    {
        RpRegionSetChannel(freq);
    }
    #endif

    SX126xSetRfFrequency( freq );
}

bool RadioIsChannelFree( RadioModems_t modem, uint32_t freq, int16_t rssiThresh, uint32_t maxCarrierSenseTime )
{
    bool status = true;
    int16_t rssi = 0;
    uint16_t currentTime = 0;

    uint8_t currentModem;
    uint8_t backupFSKBandWidthReg  = SX126x.ModulationParams.Params.Gfsk.Bandwidth;
    uint32_t loRaBandWidthEnm  = SX126x.ModulationParams.Params.LoRa.Bandwidth;
    uint32_t actualBandWidthVal = 0;
    uint8_t i;

    if( maxCarrierSenseTime > RP_CCA_TIMER_MAX_TIME_MS ) // Guard For Timer Max(Parameter Error)
    {
        status = false;
    }
    else
    {
        SX126xSetDioIrqParams( IRQ_RADIO_NONE,
                               IRQ_RADIO_NONE,
                               IRQ_RADIO_NONE,
                               IRQ_RADIO_NONE);

        currentModem =  SX126xGetPacketType( );

        if( currentModem == PACKET_TYPE_LORA )
        {
            if (PibValues.ccaBandwidth != 0)
            {
                actualBandWidthVal = PibValues.ccaBandwidth;
            }
            else
            {
                for (i = 0;  i < sizeof(enmVsBndWithTbl)/sizeof(enmVsBndWithTbl[0]); i++ )
                {
                    if( enmVsBndWithTbl[i].eNumBandwidth == loRaBandWidthEnm )
                    {
                        actualBandWidthVal =  enmVsBndWithTbl[i].bandwidth;
                        break;
                    }
                }
            }

            RadioStandby( );
            RadioSetModem(MODEM_FSK);

            SX126x.ModulationParams.PacketType = PACKET_TYPE_GFSK;
            SX126x.ModulationParams.Params.Gfsk.Bandwidth = RadioGetFskBandwidthRegValue( actualBandWidthVal );
            SX126xSetModulationParams( &SX126x.ModulationParams );

            SX126x.PacketParams.PacketType = PACKET_TYPE_GFSK;
            SX126xSetPacketParams( &SX126x.PacketParams );
        }
        else
        {
            RadioStandby( );
            if (PibValues.ccaBandwidth != 0)
            {
                SX126x.ModulationParams.PacketType = PACKET_TYPE_GFSK;
                SX126x.ModulationParams.Params.Gfsk.Bandwidth = RadioGetFskBandwidthRegValue( PibValues.ccaBandwidth );
                SX126xSetModulationParams( &SX126x.ModulationParams );
            }
        }

        maxCarrierSenseTime *= 1000;    // msec -> usec

        RadioSetChannel( freq );
        SX126xSetRx( 0xFFFFFF );

        currentTime = RpMcuCcaGetCurrentCount();
        while( RpMcuCcaDiffTime( currentTime ) < 1 ); // (RA2) min 244us, max 488us  (RA0) min 4us, max 37us

        rssi = RadioRssi( MODEM_FSK );

        if ( rssi >= rssiThresh )
        {
            status = false;
        }
        else
        {
            currentTime = RpMcuCcaGetCurrentCount();

            do {
                rssi = RadioRssi( MODEM_FSK );
                if ( rssi >= rssiThresh )
                {
                    status = false;
                    break;
                }
            }while( RpMcuCcaDiffTime( currentTime ) < (uint16_t)maxCarrierSenseTime );
        }

        if (status == true )
        {
            rssi = RadioRssi( MODEM_FSK );

            if ( rssi >= rssiThresh )
            {
                status = false;
            }
        }

        RadioStandby( );

        if (currentModem == PACKET_TYPE_LORA)
        {
            RadioSetModem( MODEM_LORA );

            SX126x.ModulationParams.Params.Gfsk.Bandwidth = backupFSKBandWidthReg ;

            SX126x.ModulationParams.PacketType = PACKET_TYPE_LORA;
            SX126xSetModulationParams( &SX126x.ModulationParams );

            SX126x.PacketParams.PacketType     = PACKET_TYPE_LORA;
            SX126xSetPacketParams( &SX126x.PacketParams );
        }

        else if (PibValues.ccaBandwidth != 0)
        {
            SX126x.ModulationParams.Params.Gfsk.Bandwidth = backupFSKBandWidthReg ;
            SX126x.ModulationParams.PacketType = PACKET_TYPE_GFSK;
            SX126xSetModulationParams( &SX126x.ModulationParams );
        }
    }

#if defined(DEBUG_RADIO)
    RadioDebugRadioIsChannelFree( (uint8_t)PACKET_TYPE_GFSK, freq, actualBandWidthVal,
                                rssi, rssiThresh, maxCarrierSenseTime, status );
#endif

    return status;
}

uint32_t RadioRandom( void )
{
    uint32_t rnd = 0;

    /*
     * Radio setup for random number generation
     */
    // Set LoRa modem ON
    RadioSetModem( MODEM_LORA );

    // Disable LoRa modem interrupts
    SX126xSetDioIrqParams( IRQ_RADIO_NONE, IRQ_RADIO_NONE, IRQ_RADIO_NONE, IRQ_RADIO_NONE );

    rnd = SX126xGetRandom( );

    return rnd;
}

RadioResult_t RadioSetRxConfig( RadioModems_t modem, uint32_t bandwidth,
                         uint32_t datarate, uint8_t coderate,
                         uint32_t bandwidthAfc, uint16_t preambleLen,
                         uint16_t symbTimeout, bool fixLen,
                         uint8_t payloadLen,
                         bool crcOn, bool freqHopOn, uint8_t hopPeriod,
                         bool iqInverted, bool rxContinuous )
{
    #if defined( RP_USE_RADIO_CFG_CHECK )
    if( PibValues.radioCfgCheckEnable )
    {
        RpRegionSetRxConfig( modem, bandwidth, datarate, coderate, bandwidthAfc, preambleLen,
                             symbTimeout, fixLen, payloadLen, crcOn, freqHopOn, hopPeriod,
                             iqInverted, rxContinuous);
    }
    #endif

    RxContinuous = rxContinuous;
    if( rxContinuous == true )
    {
        symbTimeout = 0;
    }
    if( fixLen == true )
    {
        MaxPayloadLength = payloadLen;
    }
    else
    {
        MaxPayloadLength = 0xFF;
    }

    switch( modem )
    {
        case MODEM_FSK:
            SX126xSetStopRxTimerOnPreambleDetect( false );
            SX126x.ModulationParams.PacketType = PACKET_TYPE_GFSK;

            SX126x.ModulationParams.Params.Gfsk.BitRate = datarate;
            SX126x.ModulationParams.Params.Gfsk.ModulationShaping = MOD_SHAPING_G_BT_1;
            SX126x.ModulationParams.Params.Gfsk.Bandwidth = RadioGetFskBandwidthRegValue( bandwidth );

            SX126x.PacketParams.PacketType = PACKET_TYPE_GFSK;
            SX126x.PacketParams.Params.Gfsk.PreambleLength = ( preambleLen << 3 ); // convert byte into bit
            SX126x.PacketParams.Params.Gfsk.PreambleMinDetect = RADIO_PREAMBLE_DETECTOR_08_BITS;
            SX126x.PacketParams.Params.Gfsk.SyncWordLength = 3 << 3; // convert byte into bit
            SX126x.PacketParams.Params.Gfsk.AddrComp = RADIO_ADDRESSCOMP_FILT_OFF;
            SX126x.PacketParams.Params.Gfsk.HeaderType = ( fixLen == true ) ? RADIO_PACKET_FIXED_LENGTH : RADIO_PACKET_VARIABLE_LENGTH;
            SX126x.PacketParams.Params.Gfsk.PayloadLength = MaxPayloadLength;
            if( crcOn == true )
            {
                SX126x.PacketParams.Params.Gfsk.CrcLength = RADIO_CRC_2_BYTES_CCIT;
            }
            else
            {
                SX126x.PacketParams.Params.Gfsk.CrcLength = RADIO_CRC_OFF;
            }
            SX126x.PacketParams.Params.Gfsk.DcFree = RADIO_DC_FREEWHITENING;

            RadioStandby( );
            RadioSetModem( ( SX126x.ModulationParams.PacketType == PACKET_TYPE_GFSK ) ? MODEM_FSK : MODEM_LORA );
            SX126xSetModulationParams( &SX126x.ModulationParams );
            SX126xSetPacketParams( &SX126x.PacketParams );
            SX126xSetSyncWord( ( uint8_t[] ){ 0xC1, 0x94, 0xC1, 0x00, 0x00, 0x00, 0x00, 0x00 } );
            SX126xSetWhiteningSeed( 0x01FF );

            RxTimeout = ( uint32_t )symbTimeout * 8000UL / datarate;
            break;

        case MODEM_LORA:
            SX126xSetStopRxTimerOnPreambleDetect( false );
            SX126x.ModulationParams.PacketType = PACKET_TYPE_LORA;
            SX126x.ModulationParams.Params.LoRa.SpreadingFactor = ( RadioLoRaSpreadingFactors_t )datarate;
            SX126x.ModulationParams.Params.LoRa.Bandwidth = Bandwidths[bandwidth];
            SX126x.ModulationParams.Params.LoRa.CodingRate = ( RadioLoRaCodingRates_t )coderate;

            if ( isLoRaLowDataRateOptimize( bandwidth, datarate ) )
            {
                SX126x.ModulationParams.Params.LoRa.LowDatarateOptimize = 0x01;
            }
            else
            {
                SX126x.ModulationParams.Params.LoRa.LowDatarateOptimize = 0x00;
            }

            SX126x.PacketParams.PacketType = PACKET_TYPE_LORA;

            if( ( SX126x.ModulationParams.Params.LoRa.SpreadingFactor == LORA_SF5 ) ||
                ( SX126x.ModulationParams.Params.LoRa.SpreadingFactor == LORA_SF6 ) )
            {
                if( preambleLen < 12 )
                {
                    SX126x.PacketParams.Params.LoRa.PreambleLength = 12;
                }
                else
                {
                    SX126x.PacketParams.Params.LoRa.PreambleLength = preambleLen;
                }
            }
            else
            {
                SX126x.PacketParams.Params.LoRa.PreambleLength = preambleLen;
            }

            SX126x.PacketParams.Params.LoRa.HeaderType = ( RadioLoRaPacketLengthsMode_t )fixLen;

            SX126x.PacketParams.Params.LoRa.PayloadLength = MaxPayloadLength;
            SX126x.PacketParams.Params.LoRa.CrcMode = ( RadioLoRaCrcModes_t )crcOn;
            SX126x.PacketParams.Params.LoRa.InvertIQ = ( RadioLoRaIQModes_t )iqInverted;

            RadioStandby( );
            RadioSetModem( ( SX126x.ModulationParams.PacketType == PACKET_TYPE_GFSK ) ? MODEM_FSK : MODEM_LORA );
            SX126xSetModulationParams( &SX126x.ModulationParams );
            SX126xSetPacketParams( &SX126x.PacketParams );
            SX126xSetLoRaSymbNumTimeout( symbTimeout );

            // WORKAROUND - Optimizing the Inverted IQ Operation, see DS_SX1261-2_V1.2 datasheet chapter 15.4
            if( SX126x.PacketParams.Params.LoRa.InvertIQ == LORA_IQ_INVERTED )
            {
                SX126xWriteRegister( REG_IQ_POLARITY, SX126xReadRegister( REG_IQ_POLARITY ) & ~( 1 << 2 ) );
            }
            else
            {
                SX126xWriteRegister( REG_IQ_POLARITY, SX126xReadRegister( REG_IQ_POLARITY ) | ( 1 << 2 ) );
            }
            // WORKAROUND END

            if (symbTimeout == 0)
            {
                RxTimeout = 0;
            }
            else
            {
                RxTimeout = 0xFFFF;
            }
            break;
    }

#if defined(DEBUG_RADIO)
    RadioDebugRadioSetRxConfig( modem, bandwidth, datarate, coderate, bandwidthAfc, preambleLen,
                                 symbTimeout, fixLen, payloadLen, crcOn, freqHopOn, hopPeriod,
                                 iqInverted, rxContinuous );
#endif

    return RADIO_SUCCESS;
}

RadioResult_t RadioSetTxConfig( RadioModems_t modem, int8_t power, uint32_t fdev,
                        uint32_t bandwidth, uint32_t datarate,
                        uint8_t coderate, uint16_t preambleLen,
                        bool fixLen, bool crcOn, bool freqHopOn,
                        uint8_t hopPeriod, bool iqInverted, uint32_t timeout )
{
    #if defined( RP_USE_RADIO_CFG_CHECK )
    if( PibValues.radioCfgCheckEnable )
    {
        RpRegionSetTxConfig(modem, power, fdev, bandwidth, datarate, coderate, preambleLen,
                            fixLen, crcOn, freqHopOn, hopPeriod, iqInverted, timeout);
    }
    #endif

    switch( modem )
    {
        case MODEM_FSK:
            SX126x.ModulationParams.PacketType = PACKET_TYPE_GFSK;
            SX126x.ModulationParams.Params.Gfsk.BitRate = datarate;

            SX126x.ModulationParams.Params.Gfsk.ModulationShaping = MOD_SHAPING_G_BT_1;
            SX126x.ModulationParams.Params.Gfsk.Bandwidth = RadioGetFskBandwidthRegValue( bandwidth );
            SX126x.ModulationParams.Params.Gfsk.Fdev = fdev;

            SX126x.PacketParams.PacketType = PACKET_TYPE_GFSK;
            SX126x.PacketParams.Params.Gfsk.PreambleLength = ( preambleLen << 3 ); // convert byte into bit
            SX126x.PacketParams.Params.Gfsk.PreambleMinDetect = RADIO_PREAMBLE_DETECTOR_08_BITS;
            SX126x.PacketParams.Params.Gfsk.SyncWordLength = 3 << 3 ; // convert byte into bit
            SX126x.PacketParams.Params.Gfsk.AddrComp = RADIO_ADDRESSCOMP_FILT_OFF;
            SX126x.PacketParams.Params.Gfsk.HeaderType = ( fixLen == true ) ? RADIO_PACKET_FIXED_LENGTH : RADIO_PACKET_VARIABLE_LENGTH;

            if( crcOn == true )
            {
                SX126x.PacketParams.Params.Gfsk.CrcLength = RADIO_CRC_2_BYTES_CCIT;
            }
            else
            {
                SX126x.PacketParams.Params.Gfsk.CrcLength = RADIO_CRC_OFF;
            }
            SX126x.PacketParams.Params.Gfsk.DcFree = RADIO_DC_FREEWHITENING;

            RadioStandby( );
            RadioSetModem( ( SX126x.ModulationParams.PacketType == PACKET_TYPE_GFSK ) ? MODEM_FSK : MODEM_LORA );
            SX126xSetModulationParams( &SX126x.ModulationParams );
            SX126xSetPacketParams( &SX126x.PacketParams );
            SX126xSetSyncWord( ( uint8_t[] ){ 0xC1, 0x94, 0xC1, 0x00, 0x00, 0x00, 0x00, 0x00 } );
            SX126xSetWhiteningSeed( 0x01FF );
            break;

        case MODEM_LORA:
            SX126x.ModulationParams.PacketType = PACKET_TYPE_LORA;
            SX126x.ModulationParams.Params.LoRa.SpreadingFactor = ( RadioLoRaSpreadingFactors_t ) datarate;
            SX126x.ModulationParams.Params.LoRa.Bandwidth =  Bandwidths[bandwidth];
            SX126x.ModulationParams.Params.LoRa.CodingRate= ( RadioLoRaCodingRates_t )coderate;

            if ( isLoRaLowDataRateOptimize( bandwidth, datarate ) )
            {
                SX126x.ModulationParams.Params.LoRa.LowDatarateOptimize = 0x01;
            }
            else
            {
                SX126x.ModulationParams.Params.LoRa.LowDatarateOptimize = 0x00;
            }

            SX126x.PacketParams.PacketType = PACKET_TYPE_LORA;

            if( ( SX126x.ModulationParams.Params.LoRa.SpreadingFactor == LORA_SF5 ) ||
                ( SX126x.ModulationParams.Params.LoRa.SpreadingFactor == LORA_SF6 ) )
            {
                if( preambleLen < 12 )
                {
                    SX126x.PacketParams.Params.LoRa.PreambleLength = 12;
                }
                else
                {
                    SX126x.PacketParams.Params.LoRa.PreambleLength = preambleLen;
                }
            }
            else
            {
                SX126x.PacketParams.Params.LoRa.PreambleLength = preambleLen;
            }

            SX126x.PacketParams.Params.LoRa.HeaderType = ( RadioLoRaPacketLengthsMode_t )fixLen;
            SX126x.PacketParams.Params.LoRa.PayloadLength = MaxPayloadLength;
            SX126x.PacketParams.Params.LoRa.CrcMode = ( RadioLoRaCrcModes_t )crcOn;
            SX126x.PacketParams.Params.LoRa.InvertIQ = ( RadioLoRaIQModes_t )iqInverted;

            RadioStandby( );
            RadioSetModem( ( SX126x.ModulationParams.PacketType == PACKET_TYPE_GFSK ) ? MODEM_FSK : MODEM_LORA );
            SX126xSetModulationParams( &SX126x.ModulationParams );
            SX126xSetPacketParams( &SX126x.PacketParams );
            break;
    }

    // WORKAROUND - Modulation Quality with 500 kHz LoRa Bandwidth, see DS_SX1261-2_V1.2 datasheet chapter 15.1
    if( ( modem == MODEM_LORA ) && ( SX126x.ModulationParams.Params.LoRa.Bandwidth == LORA_BW_500 ) )
    {
        SX126xWriteRegister( REG_TX_MODULATION, SX126xReadRegister( REG_TX_MODULATION ) & ~( 1 << 2 ) );
    }
    else
    {
        SX126xWriteRegister( REG_TX_MODULATION, SX126xReadRegister( REG_TX_MODULATION ) | ( 1 << 2 ) );
    }
    // WORKAROUND END

    SX126xSetRfTxPower( power );
    TxTimeout = timeout;

#if defined(DEBUG_RADIO)
    RadioDebugRadioSetTxConfig( modem, power, fdev, bandwidth, datarate,
                            coderate, preambleLen, fixLen, crcOn, freqHopOn,
                            hopPeriod, iqInverted, timeout );
#endif

    return RADIO_SUCCESS;
}

bool RadioCheckRfFrequency( uint32_t frequency )
{
    bool ret = true;

    #if defined( RP_USE_RADIO_CFG_CHECK )
    if( PibValues.radioCfgCheckEnable )
    {
        ret = RpRegionCheckRfFrequency( frequency );
    }
    #endif

    return ret;
}

static bool isLoRaLowDataRateOptimize( uint32_t bandwidth, uint32_t datarate )
{
    bool result = false;

    switch( Bandwidths[bandwidth] )
    {
    case LORA_BW_125:
        result = ( datarate == 11 ) || ( datarate == 12 );
        break;
    case LORA_BW_250:
        result = ( datarate == 12 );
        break;
    case LORA_BW_500:
        break;
    case LORA_BW_062:
    case LORA_BW_041:
        result = ( datarate >= 10 ) && ( datarate <= 12 );
        break;
    case LORA_BW_031:
    case LORA_BW_020:
    case LORA_BW_015:
        result = ( datarate >= 9 ) && ( datarate <= 12 );
        break;
    case LORA_BW_010:
        result = ( datarate >= 8 ) && ( datarate <= 12 );
        break;
    case LORA_BW_007:
        result = ( datarate >= 7 ) && ( datarate <= 12 );
        break;
    }

    return result;
}

static uint32_t RadioGetLoRaBandwidthInHz( RadioLoRaBandwidths_t bw )
{
    uint32_t bandwidthInHz = 0;

    switch( bw )
    {
    case LORA_BW_007:
        bandwidthInHz = 7812UL;
        break;
    case LORA_BW_010:
        bandwidthInHz = 10417UL;
        break;
    case LORA_BW_015:
        bandwidthInHz = 15625UL;
        break;
    case LORA_BW_020:
        bandwidthInHz = 20833UL;
        break;
    case LORA_BW_031:
        bandwidthInHz = 31250UL;
        break;
    case LORA_BW_041:
        bandwidthInHz = 41667UL;
        break;
    case LORA_BW_062:
        bandwidthInHz = 62500UL;
        break;
    case LORA_BW_125:
        bandwidthInHz = 125000UL;
        break;
    case LORA_BW_250:
        bandwidthInHz = 250000UL;
        break;
    case LORA_BW_500:
        bandwidthInHz = 500000UL;
        break;
    }

    return bandwidthInHz;
}

static uint32_t RadioGetGfskTimeOnAirNumerator( uint32_t datarate, uint8_t coderate,
                              uint16_t preambleLen, bool fixLen, uint8_t payloadLen,
                              bool crcOn )
{
    const RadioAddressComp_t addrComp = RADIO_ADDRESSCOMP_FILT_OFF;
    const uint8_t syncWordLength = 3;

    return ( preambleLen << 3 ) +
           ( ( fixLen == false ) ? 8 : 0 ) +
             ( syncWordLength << 3 ) +
             ( ( payloadLen +
               ( addrComp == RADIO_ADDRESSCOMP_FILT_OFF ? 0 : 1 ) +
               ( ( crcOn == true ) ? 2 : 0 )
               ) << 3
             );
}

static uint32_t RadioGetLoRaTimeOnAirNumerator( uint32_t bandwidth,
                              uint32_t datarate, uint8_t coderate,
                              uint16_t preambleLen, bool fixLen, uint8_t payloadLen,
                              bool crcOn )
{
    int32_t crDenom           = coderate + 4;
    bool    lowDatareOptimize = false;

    // Ensure that the preamble length is at least 12 symbols when using SF5 or
    // SF6
    if( ( datarate == 5 ) || ( datarate == 6 ) )
    {
        if( preambleLen < 12 )
        {
            preambleLen = 12;
        }
    }

    if ( isLoRaLowDataRateOptimize( bandwidth, datarate ) )
    {
        lowDatareOptimize = true;
    }

    int32_t ceilDenominator;
    int32_t ceilNumerator = ( payloadLen << 3 ) +
                            ( crcOn ? 16 : 0 ) -
                            ( 4 * datarate ) +
                            ( fixLen ? 0 : 20 );

    if( datarate <= 6 )
    {
        ceilDenominator = 4 * datarate;
    }
    else
    {
        ceilNumerator += 8;

        if( lowDatareOptimize == true )
        {
            ceilDenominator = 4 * ( datarate - 2 );
        }
        else
        {
            ceilDenominator = 4 * datarate;
        }
    }

    if( ceilNumerator < 0 )
    {
        ceilNumerator = 0;
    }

    // Perform integral ceil()
    int32_t intermediate =
        ( ( ceilNumerator + ceilDenominator - 1 ) / ceilDenominator ) * crDenom + preambleLen + 12;

    if( datarate <= 6 )
    {
        intermediate += 2;
    }

    return ( uint32_t )( ( 4 * intermediate + 1 ) * ( 1 << ( datarate - 2 ) ) );
}

uint32_t RadioTimeOnAirNew( RadioModems_t modem, uint32_t bandwidth,
                              uint32_t datarate, uint8_t coderate,
                              uint16_t preambleLen, bool fixLen, uint8_t payloadLen,
                              bool crcOn )
{
    uint32_t numerator = 0;
    uint32_t denominator = 1;

    switch( modem )
    {
    case MODEM_FSK:
        {
            numerator   = 1000U * RadioGetGfskTimeOnAirNumerator( datarate, coderate,
                                                                  preambleLen, fixLen,
                                                                  payloadLen, crcOn );
            denominator = datarate;
        }
        break;
    case MODEM_LORA:
        {
            numerator   = 1000U * RadioGetLoRaTimeOnAirNumerator( bandwidth, datarate,
                                                                  coderate, preambleLen,
                                                                  fixLen, payloadLen, crcOn );
            denominator = RadioGetLoRaBandwidthInHz( Bandwidths[bandwidth] );
        }
        break;
    }
    // Perform integral ceil()
    return ( numerator + denominator - 1 ) / denominator;
}

uint32_t RadioTimeOnAir( RadioModems_t modem, uint8_t pktLen )
{
    uint32_t airTime = 0;

    switch( modem )
    {
    case MODEM_FSK:
        {
            airTime = RadioTimeOnAirNew( modem, 0UL,
                SX126x.ModulationParams.Params.Gfsk.BitRate, 0,
                (SX126x.PacketParams.Params.Gfsk.PreambleLength >> 3),
                (RADIO_PACKET_FIXED_LENGTH == SX126x.PacketParams.Params.Gfsk.HeaderType),
                pktLen, (RADIO_CRC_OFF != SX126x.PacketParams.Params.Gfsk.CrcLength) );

            airTime +=
                (SX126x.ModulationParams.Params.Gfsk.BitRate >= 7000) ? RP_TOA_OFFSET_FSK_7KBPS :
                (SX126x.ModulationParams.Params.Gfsk.BitRate >= 3000) ? RP_TOA_OFFSET_FSK_3KBPS :
                (SX126x.ModulationParams.Params.Gfsk.BitRate >= 2000) ? RP_TOA_OFFSET_FSK_2KBPS :
                (SX126x.ModulationParams.Params.Gfsk.BitRate >= 1000) ? RP_TOA_OFFSET_FSK_1KBPS :
                RP_TOA_OFFSET_FSK_600BPS;
        }
        break;
    case MODEM_LORA:
        {
            airTime = RadioTimeOnAirNew( modem,
                RadioGetLoRaBandwidthIndex(SX126x.ModulationParams.Params.LoRa.Bandwidth),
                SX126x.ModulationParams.Params.LoRa.SpreadingFactor,
                SX126x.ModulationParams.Params.LoRa.CodingRate,
                SX126x.PacketParams.Params.LoRa.PreambleLength,
                SX126x.PacketParams.Params.LoRa.HeaderType,
                pktLen, SX126x.PacketParams.Params.LoRa.CrcMode );

            airTime += RP_TOA_OFFSET_LORA;
        }
        break;
    }

    return airTime;
}

RadioResult_t RadioSend( uint8_t *buffer, uint8_t size )
{
    RadioResult_t retVal = RADIO_ARG_IS_NULL;

    #if defined( RP_USE_RADIO_CFG_CHECK )
    if( PibValues.radioCfgCheckEnable )
    {
        retVal = (RadioResult_t)RpRegionSendCheck(buffer, size);
        if(retVal != RADIO_SUCCESS)
        {
            return retVal;
        }
    }
    #endif

    if(buffer != NULL)
    {
        SX126xSetDioIrqParams( IRQ_TX_DONE | IRQ_RX_TX_TIMEOUT,
                               IRQ_TX_DONE | IRQ_RX_TX_TIMEOUT,
                               IRQ_RADIO_NONE,
                               IRQ_RADIO_NONE );

        if( SX126xGetPacketType( ) == PACKET_TYPE_LORA )
        {
            SX126x.PacketParams.Params.LoRa.PayloadLength = size;
        }
        else
        {
            SX126x.PacketParams.Params.Gfsk.PayloadLength = size;
        }
        SX126xSetPacketParams( &SX126x.PacketParams );

        SX126xSendPayload( buffer, size, 0 );
        TimerSetValue( &TxTimeoutTimer, TxTimeout );
        TimerStart( &TxTimeoutTimer );

#if defined(DEBUG_RADIO)
        RadioDebugRadioSend( (uint8_t)SX126xGetPacketType(), SX126xGetRfFrequency(), buffer, size );
#endif

        retVal = RADIO_SUCCESS;
    }

    return retVal;
}

void RadioSleep( void )
{
    SleepParams_t params = { 0 };

    params.Fields.WarmStart = 1;
    SX126xSetSleep( params );

    DelayMs( 2 );

#if defined(DEBUG_RADIO)
    RadioDebugRadioSleep();
#endif
}

void RadioStandby( void )
{
    SX126xSetStandby( PibValues.stdbymode );

#if defined(DEBUG_RADIO)
    RadioDebugRadioStandby();
#endif
}

RadioResult_t RadioRx( uint32_t timeout )
{
    RadioResult_t retVal = RADIO_SUCCESS;

    #if defined( RP_USE_RADIO_CFG_CHECK )
    if( PibValues.radioCfgCheckEnable )
    {
        retVal = (RadioResult_t)RpRegionRxCheck(timeout);
        if(retVal != RADIO_SUCCESS)
        {
            return retVal;
        }
    }
    #endif

    if(PibValues.gainBoosted == true)
    {
        RadioRxBoosted( timeout );
        return retVal;
    }

    SX126xSetDioIrqParams( IRQ_RX_DONE | IRQ_RX_TX_TIMEOUT | IRQ_CRC_ERROR,
                           IRQ_RX_DONE | IRQ_RX_TX_TIMEOUT | IRQ_CRC_ERROR,
                           IRQ_RADIO_NONE,
                           IRQ_RADIO_NONE );

    if( timeout != 0 )
    {
        TimerSetValue( &RxTimeoutTimer, timeout );
        TimerStart( &RxTimeoutTimer );
    }

    if( RxContinuous == true )
    {
        SX126xSetRxTxFallbackMode( FB_FS );
        SX126xSetRx( 0x0 ); // No timeout. Rx Single mode.
    }
    else
    {
        SX126xSetRx( RxTimeout << 6 );
    }

#if defined(DEBUG_RADIO)
    RadioDebugRadioRx( (uint8_t)SX126xGetPacketType(), SX126xGetRfFrequency(), timeout, RxContinuous );
#endif

    return retVal;
}

void RadioRxBoosted( uint32_t timeout )
{
    SX126xSetDioIrqParams( IRQ_RX_DONE | IRQ_RX_TX_TIMEOUT | IRQ_CRC_ERROR,
                           IRQ_RX_DONE | IRQ_RX_TX_TIMEOUT | IRQ_CRC_ERROR,
                           IRQ_RADIO_NONE,
                           IRQ_RADIO_NONE );

    if( timeout != 0 )
    {
        TimerSetValue( &RxTimeoutTimer, timeout );
        TimerStart( &RxTimeoutTimer );
    }

    if( RxContinuous == true )
    {
        SX126xSetRxTxFallbackMode( FB_FS );
        SX126xSetRxBoosted( 0x0 ); // No timeout. Rx Single mode.
    }
    else
    {
        SX126xSetRxBoosted( RxTimeout << 6 );
    }
}

void RadioSetRxDutyCycle( uint32_t rxTime, uint32_t sleepTime )
{
    SX126xSetRxDutyCycle( rxTime, sleepTime );
}

void RadioAddRegisterToRetentionList( uint16_t registerAddress )
{
    uint8_t buffer[9];

    // Read the address and registers already added to the list
    SX126xReadRegisters( REG_RETENTION_LIST_BASE_ADDRESS, buffer, 9 );

    const uint8_t nbOfRegisters = buffer[0];
    uint8_t* registerList   = &buffer[1];

    // Check if the register given as parameter is already added to the list
    for( uint8_t i = 0; i < nbOfRegisters; i++ )
    {
        if( registerAddress == ( ( uint16_t ) registerList[2 * i] << 8 ) + registerList[2 * i + 1] )
        {
            return;
        }
    }

    if( nbOfRegisters < MAX_NB_REG_IN_RETENTION )
    {
        buffer[0] += 1;
        registerList[2 * nbOfRegisters]     = ( uint8_t )( registerAddress >> 8 );
        registerList[2 * nbOfRegisters + 1] = ( uint8_t )( registerAddress >> 0 );

        // Update radio with modified list
        SX126xWriteRegisters( REG_RETENTION_LIST_BASE_ADDRESS, buffer, 9 );
    }
}

void RadioStartCad( void )
{
    SX126xSetDioIrqParams( IRQ_CAD_DONE | IRQ_CAD_ACTIVITY_DETECTED, IRQ_CAD_DONE | IRQ_CAD_ACTIVITY_DETECTED, IRQ_RADIO_NONE, IRQ_RADIO_NONE );
    SX126xSetCad( );
}

RadioResult_t RadioSetTxContinuousWave( uint32_t freq, int8_t power, uint16_t time )
{
    RadioResult_t retVal = RADIO_SUCCESS;
    uint32_t timeout = ( uint32_t )time * 1000;

    #if defined( RP_USE_RADIO_CFG_CHECK )
    if( PibValues.radioCfgCheckEnable )
    {
        retVal = (RadioResult_t)RpRegionTxContCheck(freq, power, time);
        if(retVal != RADIO_SUCCESS)
        {
            return retVal;
        }
    }
    #endif

    SX126xSetRfFrequency( freq );
    SX126xSetRfTxPower( power );
    SX126xSetTxContinuousWave( );

    TimerSetValue( &TxTimeoutTimer, timeout );
    TimerStart( &TxTimeoutTimer );

    return retVal;
}

int16_t RadioRssi( RadioModems_t modem )
{
    return SX126xGetRssiInst( );
}

void RadioWrite( uint16_t addr, uint8_t data )
{
    SX126xWriteRegister( addr, data );
}

uint8_t RadioRead( uint16_t addr )
{
    return SX126xReadRegister( addr );
}

RadioResult_t RadioWriteBuffer( uint16_t addr, uint8_t *buffer, uint8_t size )
{
    RadioResult_t retVal = RADIO_ARG_IS_NULL;

    if(buffer != NULL)
    {
        SX126xWriteRegisters( addr, buffer, size );
        retVal = RADIO_SUCCESS;
    }

    return retVal;
}

RadioResult_t RadioReadBuffer( uint16_t addr, uint8_t *buffer, uint16_t size )
{
    RadioResult_t retVal = RADIO_ARG_IS_NULL;

    if(buffer != NULL)
    {
        SX126xReadRegisters( addr, buffer, size );
        retVal = RADIO_SUCCESS;
    }

    return retVal;
}

void RadioSetMaxPayloadLength( RadioModems_t modem, uint8_t max )
{
    if( modem == MODEM_LORA )
    {
        SX126x.PacketParams.Params.LoRa.PayloadLength = MaxPayloadLength = max;
        SX126xSetPacketParams( &SX126x.PacketParams );
    }
    else
    {
        if( SX126x.PacketParams.Params.Gfsk.HeaderType == RADIO_PACKET_FIXED_LENGTH )
        {
            SX126x.PacketParams.Params.Gfsk.PayloadLength = MaxPayloadLength = max;
            SX126xSetPacketParams( &SX126x.PacketParams );
        }
    }
}

void RadioSetPublicNetwork( bool enable )
{
    RadioPublicNetwork.Current = RadioPublicNetwork.Previous = enable;

    RadioSetModem( MODEM_LORA );
    if( enable == true )
    {
        // Change LoRa modem SyncWord
        SX126xWriteRegister( REG_LR_SYNCWORD, ( LORA_MAC_PUBLIC_SYNCWORD >> 8 ) & 0xFF );
        SX126xWriteRegister( REG_LR_SYNCWORD + 1, LORA_MAC_PUBLIC_SYNCWORD & 0xFF );
    }
    else
    {
        // Change LoRa modem SyncWord
        SX126xWriteRegister( REG_LR_SYNCWORD, ( LORA_MAC_PRIVATE_SYNCWORD >> 8 ) & 0xFF );
        SX126xWriteRegister( REG_LR_SYNCWORD + 1, LORA_MAC_PRIVATE_SYNCWORD & 0xFF );
    }
}

uint32_t RadioGetWakeupTime( void )
{
    uint32_t wakeupTime = RADIO_WAKEUP_TIME;

    if (SX126xGetClockSelect() == RADIO_CLOCK_TCXO_SEL)
    {
        wakeupTime += ((uint32_t)((RP_TCXO_STAB_TIME * 15.625)/1000.0));
    }

    return (wakeupTime);
}

void RadioOnTxTimeoutIrq( void )
{
    // In MCU Timer Call back, RF Device is working, so stop it.
    SX126xSetStandby( STDBY_RC );
    if( ( RadioEvents != NULL ) && ( RadioEvents->TxTimeout != NULL ) )
    {
        RadioEvents->TxTimeout( );
    }
}

void RadioOnRxTimeoutIrq( void )
{
    // In MCU Timer Call back, RF Device is working, so stop it.
    SX126xSetStandby( STDBY_RC );
    if( ( RadioEvents != NULL ) && ( RadioEvents->RxTimeout != NULL ) )
    {
        RadioEvents->RxTimeout( );
    }
}

void RadioOnDioIrq( void )
{
    BoardDisableAllIrq();

    BoardRadioIrqPreprocess( );
    IrqFired = true;

    BoardEnableAllIrq();
}

bool RadioIrqProcess( void )
{
    BoardDisableAllIrq();

    // Clear IRQ flag
    const bool isIrqFired = IrqFired;
    IrqFired = false;

    BoardEnableAllIrq();

    if( isIrqFired == true )
    {
        uint16_t irqRegs = SX126xGetIrqStatus( );
        SX126xClearIrqStatus( (uint16_t)IRQ_RADIO_ALL );

        // Check if DIO1 pin is High. If it is the case revert IrqFired to true
        BoardDisableAllIrq();
        if( SX126xGetDio1PinState( ) == 1 )
        {
            BoardRadioIrqPreprocess( );
            IrqFired = true;
        }
        BoardEnableAllIrq();

        RpRxErrFlg = ( (irqRegs & IRQ_CRC_ERROR) == IRQ_CRC_ERROR ) ? RADIO_PAYLOAD_CRC_ERROR : RADIO_ERROR_NONE;

        if( ( irqRegs & IRQ_TX_DONE ) == IRQ_TX_DONE )
        {
            TimerStop( &TxTimeoutTimer );
            //!< Update operating mode state to a value lower than \ref MODE_STDBY_XOSC
            SX126xSetOperatingMode( MODE_STDBY_RC );
            if( ( RadioEvents != NULL ) && ( RadioEvents->TxDone != NULL ) )
            {
                RadioEvents->TxDone( );
            }
        }

        if( ( irqRegs & IRQ_RX_DONE ) == IRQ_RX_DONE )
        {
            uint8_t size;

            if( RxContinuous == false )
            {
                TimerStop( &RxTimeoutTimer );
                //!< Update operating mode state to a value lower than \ref MODE_STDBY_XOSC
                SX126xSetOperatingMode( MODE_STDBY_RC );

                // WORKAROUND - Implicit Header Mode Timeout Behavior, see DS_SX1261-2_V1.2 datasheet chapter 15.3
                SX126xWriteRegister( REG_RTC_CTRL, 0x00 );
                SX126xWriteRegister( REG_EVT_CLR, SX126xReadRegister( REG_EVT_CLR ) | ( 1 << 1 ) );
                // WORKAROUND END
            }
            SX126xGetPayload( RadioRxPayload, &size , 255 );
            SX126xGetPacketStatus( &RadioPktStatus );

            if ( ( ( irqRegs & IRQ_CRC_ERROR ) != IRQ_CRC_ERROR ) || (PibValues.callRxDoneInPayloadCrcError == true) )
            {
                if( ( RadioEvents != NULL ) && ( RadioEvents->RxDone != NULL ) )
                {
                    if ( PACKET_TYPE_GFSK == RadioPktStatus.packetType )
                    {
                        RadioEvents->RxDone( RadioRxPayload, (uint16_t )size, (int16_t )RadioPktStatus.Params.Gfsk.RssiAvg, RadioPktStatus.Params.Gfsk.RssiSync );
                    }
                    else
                    {
                        RadioEvents->RxDone( RadioRxPayload, (uint16_t )size, (uint16_t )RadioPktStatus.Params.LoRa.RssiPkt, RadioPktStatus.Params.LoRa.SnrPkt );
                    }
                }
            }

            if(( RxContinuous == true ) && ( SX126xGetOperatingMode() == MODE_RX ) )
            {
                if (PibValues.gainBoosted == true)
                {
                    SX126xSetRxBoosted( 0x0 ); // Rx single mode
                }
                else
                {
                    SX126xSetRx( 0x0 ); // Rx single mode
                }
            }
        }

        if( ( irqRegs & IRQ_CRC_ERROR ) == IRQ_CRC_ERROR )
        {
            if( ( RadioEvents != NULL ) && ( RadioEvents->RxError ) )
            {
                RadioEvents->RxError( );
            }
        }

        if( ( irqRegs & IRQ_CAD_DONE ) == IRQ_CAD_DONE )
        {
            //!< Update operating mode state to a value lower than \ref MODE_STDBY_XOSC
            SX126xSetOperatingMode( MODE_STDBY_RC );
            if( ( RadioEvents != NULL ) && ( RadioEvents->CadDone != NULL ) )
            {
                RadioEvents->CadDone( ( ( irqRegs & IRQ_CAD_ACTIVITY_DETECTED ) == IRQ_CAD_ACTIVITY_DETECTED ) );
            }
        }

        if( ( irqRegs & IRQ_RX_TX_TIMEOUT ) == IRQ_RX_TX_TIMEOUT )
        {
            if( SX126xGetOperatingMode( ) == MODE_TX )
            {
                TimerStop( &TxTimeoutTimer );
                //!< Update operating mode state to a value lower than \ref MODE_STDBY_XOSC
                SX126xSetOperatingMode( MODE_STDBY_RC );
                if( ( RadioEvents != NULL ) && ( RadioEvents->TxTimeout != NULL ) )
                {
                    RadioEvents->TxTimeout( );
                }
            }
            else if( SX126xGetOperatingMode( ) == MODE_RX )
            {
                if( RxContinuous == false )
                {
                    TimerStop( &RxTimeoutTimer );
                    //!< Update operating mode state to a value lower than \ref MODE_STDBY_XOSC
                    SX126xSetOperatingMode( MODE_STDBY_RC );
                }
                if( ( RadioEvents != NULL ) && ( RadioEvents->RxTimeout != NULL ) )
                {
                    RadioEvents->RxTimeout( );
                }
            }
        }

        if( ( irqRegs & IRQ_PREAMBLE_DETECTED ) == IRQ_PREAMBLE_DETECTED )
        {
            //__NOP( );
        }

        if( ( irqRegs & IRQ_SYNCWORD_VALID ) == IRQ_SYNCWORD_VALID )
        {
            //__NOP( );
        }

        if( ( irqRegs & IRQ_HEADER_VALID ) == IRQ_HEADER_VALID )
        {
            //__NOP( );
        }

        if( ( irqRegs & IRQ_HEADER_ERROR ) == IRQ_HEADER_ERROR )
        {
            //__NOP( );
        }

        RpRxErrFlg = RADIO_ERROR_NONE;
    }

    return IrqFired;
}


/*
 * following functions are added by renesas
 */

void RadioSleepCold( void )
{
    SleepParams_t params = { 0 };

    params.Fields.WarmStart = 0;
    SX126xSetSleep( params );

    DelayMs( 2 );

#if defined(DEBUG_RADIO)
    RadioDebugRadioSleepCold();
#endif
}

RadioResult_t RadioSetTxInfinitePreamble( uint32_t freq, int8_t power, uint16_t time )
{
    RadioResult_t retVal = RADIO_SUCCESS;

    #if defined( RP_USE_RADIO_CFG_CHECK )
    if( PibValues.radioCfgCheckEnable )
    {
        retVal = (RadioResult_t)RpRegionTxInfinitePreambleCheck(freq, power, time);
        if(retVal != RADIO_SUCCESS)
        {
            return retVal;
        }
    }
    #endif

    SX126xSetRfFrequency( freq );
    SX126xSetRfTxPower( power );
    SX126xSetTxInfinitePreamble( );

    TimerSetValue( &TxTimeoutTimer, (uint32_t )(time  * 1000UL) );
    TimerStart( &TxTimeoutTimer );

    return retVal;
}

int16_t RadioEd(uint32_t freq, uint32_t edTime)
{

    int16_t retEdVal = -32768;
    int16_t rssi = 0;
    uint16_t currentTime = 0;
    uint8_t currentModem;
    uint8_t backupFSKBandWidthReg  = SX126x.ModulationParams.Params.Gfsk.Bandwidth;
    uint32_t loRaBandWidthEnm  = SX126x.ModulationParams.Params.LoRa.Bandwidth;
    uint32_t actualBandWidthVal = 0;
    uint8_t i;

    if( edTime > RP_CCA_TIMER_MAX_TIME_MS)    // Guard For Timer Max(Parameter Error)
    {
        ;
    }
    else
    {
        SX126xSetDioIrqParams( IRQ_RADIO_NONE,
                                  IRQ_RADIO_NONE,
                                  IRQ_RADIO_NONE,
                                  IRQ_RADIO_NONE);

        currentModem =  SX126xGetPacketType( );

        if( currentModem == PACKET_TYPE_LORA )
        {
            if (PibValues.ccaBandwidth != 0)
            {
                actualBandWidthVal = PibValues.ccaBandwidth;
            }
            else
            {
                for (i = 0;  i < sizeof(enmVsBndWithTbl)/sizeof(enmVsBndWithTbl[0]); i++ )
                {
                    if( enmVsBndWithTbl[i].eNumBandwidth == loRaBandWidthEnm )
                    {
                        actualBandWidthVal =  enmVsBndWithTbl[i].bandwidth;
                        break;
                    }
                }
            }

            RadioStandby( );
            RadioSetModem(MODEM_FSK);

            SX126x.ModulationParams.PacketType = PACKET_TYPE_GFSK;
            SX126x.ModulationParams.Params.Gfsk.Bandwidth = RadioGetFskBandwidthRegValue( actualBandWidthVal );
            SX126xSetModulationParams( &SX126x.ModulationParams );

            SX126x.PacketParams.PacketType = PACKET_TYPE_GFSK;
            SX126xSetPacketParams( &SX126x.PacketParams );
        }
        else
        {
            RadioStandby( );
            if (PibValues.ccaBandwidth != 0)
            {
                SX126x.ModulationParams.PacketType = PACKET_TYPE_GFSK;
                SX126x.ModulationParams.Params.Gfsk.Bandwidth = RadioGetFskBandwidthRegValue( PibValues.ccaBandwidth );
                SX126xSetModulationParams( &SX126x.ModulationParams );
            }
        }

        RadioSetChannel( freq );

        if (PibValues.gainBoosted == true)
        {
            SX126xSetRxBoosted( 0xFFFFFF );
        }
        else
        {
            SX126xSetRx( 0xFFFFFF );
        }

        edTime *= 1000; // msec -> usec

        currentTime = RpMcuCcaGetCurrentCount();
        while( RpMcuCcaDiffTime( currentTime ) < 1 ); // (RA2) min 244us, max 488us  (RA0) min 4us, max 37us

        currentTime = RpMcuCcaGetCurrentCount();

        do {
            rssi = SX126xGetRssiInst();

            if( rssi > retEdVal )
            {
               retEdVal = rssi;
            }
        }while( RpMcuCcaDiffTime( currentTime ) < (uint16_t)edTime );

        RadioStandby( );

        if (currentModem == PACKET_TYPE_LORA)
        {
            RadioSetModem( MODEM_LORA );

            SX126x.ModulationParams.Params.Gfsk.Bandwidth = backupFSKBandWidthReg ;

            SX126x.ModulationParams.PacketType = PACKET_TYPE_LORA;
            SX126xSetModulationParams( &SX126x.ModulationParams );

            SX126x.PacketParams.PacketType     = PACKET_TYPE_LORA;
            SX126xSetPacketParams( &SX126x.PacketParams );
        }
        else if (PibValues.ccaBandwidth != 0)
        {
            SX126x.ModulationParams.Params.Gfsk.Bandwidth = backupFSKBandWidthReg ;
            SX126x.ModulationParams.PacketType = PACKET_TYPE_GFSK;
            SX126xSetModulationParams( &SX126x.ModulationParams );
        }

    }

    return retEdVal;
}

bool RadioSetPib(PIB_t id, uint8_t * pInVal)
{
    bool result = true;

    if(pInVal == NULL)
    {
        result = false;
    }
    else
    {
        CRITICAL_SECTION_BEGIN();

        switch(id)
        {
            case PIB_RSSI_OFFSET:
            {
                memcpy(&PibValues.rssiOffset, pInVal, sizeof(PibValues.rssiOffset) );
                break;
            }
            case PIB_CCA_BANDWIDTH:
            {
                uint32_t bandwidth =0;
                uint16_t i = 0;

                memcpy(&bandwidth, pInVal, sizeof(PibValues.ccaBandwidth));

                result = false;
                for( i = 0; i < ( (sizeof( FskBandwidths )/sizeof( FskBandwidth_t))- 1 ); i++ )
                {
                    if(FskBandwidths[i].bandwidth == bandwidth )
                    {
                        result = true;
                        break;
                    }
                }

                if(result == true )
                {
                   PibValues.ccaBandwidth = bandwidth;
                }
                break;
            }
            case PIB_CALL_RX_DONE_IN_PAYLOAD_CRC_ERROR:
            {
                memcpy(&PibValues.callRxDoneInPayloadCrcError, pInVal, sizeof(PibValues.callRxDoneInPayloadCrcError));
                break;
            }
            case PIB_GAIN_BOOSTED:
            {
                memcpy(&PibValues.gainBoosted, pInVal,  sizeof(PibValues.gainBoosted));
                break;
            }
            case PIB_XTAL_XTA_TRIM:
            {
                uint8_t xtalXtaTrim;
                memcpy(&xtalXtaTrim, pInVal, sizeof(PibValues.xtalXtaTrim));
                if ( xtalXtaTrim <= 0x2F )
                {
                    PibValues.xtalXtaTrim = xtalXtaTrim;
                }else{
                    result = false;
                }
                break;
            }
            case PIB_XTAL_XTB_TRIM:
            {
                uint8_t xtalXtbTrim;
                memcpy(&xtalXtbTrim, pInVal, sizeof(PibValues.xtalXtbTrim));
                if ( xtalXtbTrim <= 0x2F )
                {
                    PibValues.xtalXtbTrim = xtalXtbTrim;
                } else {
                    result = false;
                }
                break;
            }
            case PIB_RADIO_CFG_CHECK_ENABLE:
            {
                memcpy(&PibValues.radioCfgCheckEnable, pInVal,  sizeof(PibValues.radioCfgCheckEnable));
                break;
            }
            case PIB_STDBY_MODE:
            {
                memcpy(&PibValues.stdbymode, pInVal,  sizeof(PibValues.stdbymode));
                break;
            }
#if defined(RP_USE_RADIO_CFG_CHECK)
            case PIB_RADIO_CFG_REGION:      // Region/country used for the radio configuration check
            {
                RadioConfigRegion_t region;

                region = PibValues.region;              // backup current value
                memcpy(&PibValues.region, pInVal,  sizeof(PibValues.region));
                result = RpRegionInit(0xFFFF);          // Enable all predefined radio bands

                if ( result == false )
                {
                     PibValues.region = region;         // restore previous value
                }
                break;
            }
            case PIB_RADIO_CFG_FREQ_HOPPING_USED:    // Whether the upper layer uses the frequency hopping or not
            {
                memcpy(&PibValues.freqHoppingUsed, pInVal,  sizeof(PibValues.freqHoppingUsed));
                break;
            }
#endif
            default:
                result = false;
                break;
        }

        CRITICAL_SECTION_END();
    }

    return result;
}

bool RadioGetPib(PIB_t id, uint8_t * pOutVal)
{
    bool result = true;


    if(pOutVal == NULL)
    {
        result = false;
    }
    else
    {
        CRITICAL_SECTION_BEGIN();

        switch(id)
        {
            case PIB_RSSI_OFFSET:
            {
                memcpy(pOutVal, &PibValues.rssiOffset, sizeof(PibValues.rssiOffset));
                break;
            }
            case PIB_CCA_BANDWIDTH:
            {
                memcpy(pOutVal, &PibValues.ccaBandwidth, sizeof(PibValues.ccaBandwidth));
                break;
            }
            case PIB_CALL_RX_DONE_IN_PAYLOAD_CRC_ERROR:
            {
                memcpy(pOutVal, &PibValues.callRxDoneInPayloadCrcError, sizeof(PibValues.callRxDoneInPayloadCrcError));
                break;
            }
            case PIB_GAIN_BOOSTED:
            {
                memcpy(pOutVal, &PibValues.gainBoosted, sizeof(PibValues.gainBoosted));
                break;
            }
            case PIB_XTAL_XTA_TRIM:
            {
                memcpy(pOutVal, &PibValues.xtalXtaTrim, sizeof(PibValues.xtalXtaTrim));
                break;
            }
            case PIB_XTAL_XTB_TRIM:
            {
                memcpy(pOutVal, &PibValues.xtalXtbTrim, sizeof(PibValues.xtalXtbTrim));
                break;
            }
            case PIB_RADIO_CFG_CHECK_ENABLE:
            {
                memcpy(pOutVal, &PibValues.radioCfgCheckEnable, sizeof(PibValues.radioCfgCheckEnable));
                break;
            }
            case PIB_STDBY_MODE:
            {
                memcpy(pOutVal, &PibValues.stdbymode, sizeof(PibValues.stdbymode));
                break;
            }
#if defined(RP_USE_RADIO_CFG_CHECK)
            case PIB_RADIO_CFG_REGION:      // Region/country used for the radio configuration check
            {
                memcpy(pOutVal, &PibValues.region, sizeof(PibValues.region));
                break;
            }
            case PIB_RADIO_CFG_FREQ_HOPPING_USED:    // Whether the upper layer uses the frequency hopping or not
            {
                memcpy(pOutVal, &PibValues.freqHoppingUsed, sizeof(PibValues.freqHoppingUsed));
                break;
            }
#endif
            default:
            {
                result = false;
                break;
            }
        }

        CRITICAL_SECTION_END();
    }
    return result;
}

void RadioWakeUp(void)
{
    RadioOperatingModes_t opeMode;

    opeMode = SX126xGetOperatingMode( );

    if(opeMode == MODE_COLD_SLEEP)
    {
        RadioPublicNetwork.Current = false;
        RadioPublicNetwork.Previous = false;

        SX126xRecoverBoardConfig();
    }
    else if(opeMode == MODE_SLEEP)
    {
        SX126xSetStandby( STDBY_RC );
    }
    else // no sleep mode
    {
        // nop
    }

#if defined(DEBUG_RADIO)
    RadioDebugRadioWakeUp((uint8_t)opeMode);
#endif
}

void RadioCalibrateImage(uint32_t freq)
{
    SX126xCalibrateImage(freq);
}

uint16_t RadioGetErrorFlag(void)
{
    return RpRxErrFlg;
}

uint8_t RadioGetLoRaBandwidthIndex(RadioLoRaBandwidths_t bandwidth)
{
 uint8_t ret = 0;

    switch(bandwidth)
    {
        case LORA_BW_125:
            ret = 0;
            break;
        case LORA_BW_250:
            ret = 1;
            break;
        case LORA_BW_500:
            ret = 2;
            break;
        case LORA_BW_062:
            ret = 3;
            break;
        case LORA_BW_041:
            ret = 4;
            break;
        case LORA_BW_031:
            ret = 5;
            break;
        case LORA_BW_020:
            ret = 6;
            break;
        case LORA_BW_015:
            ret = 7;
            break;
        case LORA_BW_010:
            ret = 8;
            break;
        case LORA_BW_007:
            ret = 9;
            break;
    }

    return ret;
}

void RadioInitPib(void)
{
    memset(&PibValues, 0, sizeof(PibValues));

    if( RADIO_CLOCK_XTAL_SEL == SX126xGetClockSelect() )
    {
        PibValues.xtalXtaTrim = RP_XTAL_XTA_TRIM;
        PibValues.xtalXtbTrim = RP_XTAL_XTB_TRIM;
    }

#if defined(RP_USE_RADIO_CFG_CHECK)
    RpRegionInitPib();
#endif
}
