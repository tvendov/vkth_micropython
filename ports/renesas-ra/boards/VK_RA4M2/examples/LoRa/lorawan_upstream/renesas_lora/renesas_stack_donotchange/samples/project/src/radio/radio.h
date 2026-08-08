/*!
 * \file      radio.h
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

#ifndef __RADIO_H__
#define __RADIO_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <stdbool.h>
#include "r_radio_region_api.h"

/*!
 * Radio driver supported PIB ids
 */
typedef enum
{
    PIB_RSSI_OFFSET,
    PIB_CCA_BANDWIDTH,
    PIB_CALL_RX_DONE_IN_PAYLOAD_CRC_ERROR,
    PIB_GAIN_BOOSTED,
    PIB_XTAL_XTA_TRIM,
    PIB_XTAL_XTB_TRIM,
    PIB_RADIO_CFG_CHECK_ENABLE,
    PIB_STDBY_MODE,                     // RFU
#if defined(RP_USE_RADIO_CFG_CHECK)
    PIB_RADIO_CFG_REGION,               // Region/country used for the radio configuration check
    PIB_RADIO_CFG_FREQ_HOPPING_USED     // Whether the upper layer uses the frequency hopping or not
#endif
}PIB_t;

/*!
 * Radio Error occurred on receiving
 */
#define     RADIO_ERROR_NONE           (0x0000)
#define     RADIO_PAYLOAD_CRC_ERROR    (0x0040)

/*!
 * Radio API Result definition
 */
typedef enum{
   RADIO_SUCCESS = 0,
   RADIO_ARG_IS_NULL,
   RADIO_ARG_IS_INVALID,
   RADIO_FAIL,
   // "4" is reserved
   RADIO_CHECK_FAIL_RX_CFG          = 100,
   RADIO_CHECK_FAIL_TX_CFG          = 101,
   RADIO_CHECK_FAIL_TX_DUTY_CYCLE   = 102,
   RADIO_CHECK_FAIL_TX_CHANNEL_BUSY = 103,
}RadioResult_t;


/*!
 * Radio driver supported modems
 */
typedef enum
{
    MODEM_FSK = 0,
    MODEM_LORA,
}RadioModems_t;

/*!
 * Radio driver internal state machine states definition
 */
typedef enum
{
    RF_IDLE = 0,   //!< The radio is idle
    RF_RX_RUNNING, //!< The radio is in reception state
    RF_TX_RUNNING, //!< The radio is in transmission state
    RF_CAD,        //!< The radio is doing channel activity detection
    RF_COLD_SLEEP, //!< The radio is cold sleep state.
    RF_WARM_SLEEP, //!< The radio is warm sleep state.
}RadioState_t;

/*!
 * \brief Radio driver callback functions
 */
typedef struct
{
    /*!
     * \brief  Tx Done callback prototype.
     */
    void    (*TxDone )( void );
    /*!
     * \brief  Tx Timeout callback prototype.
     */
    void    (*TxTimeout )( void );
    /*!
     * \brief Rx Done callback prototype.
     *
     * \param [IN] payload Received buffer pointer
     * \param [IN] size    Received buffer size
     * \param [IN] rssi    RSSI value computed while receiving the frame [dBm]
     * \param [IN] snr     SNR value computed while receiving the frame [dB]
     *                     FSK : N/A ( set to 0 )
     *                     LoRa: SNR value in dB
     */
    void    (*RxDone )( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr );
    /*!
     * \brief  Rx Timeout callback prototype.
     */
    void    (*RxTimeout )( void );
    /*!
     * \brief Rx Error callback prototype.
     */
    void    (*RxError )( void );
    /*!
     * \brief  FHSS Change Channel callback prototype.
     *
     * \param [IN] currentChannel   Index number of the current channel
     */
    void (*FhssChangeChannel )( uint8_t currentChannel );

    /*!
     * \brief CAD Done callback prototype.
     *
     * \param [IN] channelDetected    Channel Activity detected during the CAD
     */
    void (*CadDone ) ( bool channelActivityDetected );
    
    /*!
     * \brief  Gnss Done Done callback prototype.
    */
    void    ( *GnssDone )( void );
    
    /*!
     * \brief  Gnss Done Done callback prototype.
    */
    void    ( *WifiDone )( void );
}RadioEvents_t;

/*!
 * \brief Radio driver definition
 */
struct Radio_s
{
    /*!
     * \brief Initializes the radio
     *
     * \param [IN] events Structure containing the driver callback functions
     *
     * \retval result of API
     */
    RadioResult_t    (*Init )( RadioEvents_t *event );
    /*!
     * Return current radio status
     *
     * \param status Radio status.[RF_IDLE, RF_RX_RUNNING, RF_TX_RUNNING]
     */
    RadioState_t (*GetStatus )( void );
    /*!
     * \brief Configures the radio with the given modem
     *
     * \param [IN] modem Modem to be used [0: FSK, 1: LoRa]
     */
    void    (*SetModem )( RadioModems_t modem );
    /*!
     * \brief Sets the channel frequency
     *
     * \param [IN] freq         Channel RF frequency
     */
    void    (*SetChannel )( uint32_t freq );
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
    bool    (*IsChannelFree )( RadioModems_t modem, uint32_t freq, int16_t rssiThresh, uint32_t maxCarrierSenseTime );
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
    uint32_t (*Random )( void );
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
     * \param [IN] freqHopOn    Enables disables the intra-packet frequency hopping
     *                          FSK : N/A ( set to 0 )
     *                          LoRa: N/A ( set to 0 )
     * \param [IN] hopPeriod    Number of symbols between each hop
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
    RadioResult_t    (*SetRxConfig )( RadioModems_t modem, uint32_t bandwidth,
                              uint32_t datarate, uint8_t coderate,
                              uint32_t bandwidthAfc, uint16_t preambleLen,
                              uint16_t symbTimeout, bool fixLen,
                              uint8_t payloadLen,
                              bool crcOn, bool freqHopOn, uint8_t hopPeriod,
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
     * \param [IN] freqHopOn    Enables disables the intra-packet frequency hopping
     *                          FSK : N/A ( set to 0 )
     *                          LoRa: [0: OFF, 1: ON]
     * \param [IN] hopPeriod    Number of symbols between each hop
     *                          FSK : N/A ( set to 0 )
     *                          LoRa: Number of symbols
     * \param [IN] iqInverted   Inverts IQ signals (LoRa only)
     *                          FSK : N/A ( set to 0 )
     *                          LoRa: [0: not inverted, 1: inverted]
     * \param [IN] timeout      Transmission timeout [ms]
     *
     * \retval result of API
     */
    RadioResult_t    (*SetTxConfig )( RadioModems_t modem, int8_t power, uint32_t fdev,
                              uint32_t bandwidth, uint32_t datarate,
                              uint8_t coderate, uint16_t preambleLen,
                              bool fixLen, bool crcOn, bool freqHopOn,
                              uint8_t hopPeriod, bool iqInverted, uint32_t timeout );
    /*!
     * \brief Checks if the given RF frequency is supported by the hardware
     *
     * \param [IN] frequency RF frequency to be checked
     * \retval isSupported [true: supported, false: unsupported]
     */
    bool    (*CheckRfFrequency )( uint32_t frequency );
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
    uint32_t  (*TimeOnAir )( RadioModems_t modem, uint8_t pktLen );
    /*!
     * \brief Sends the buffer of size. Prepares the packet to be sent and sets
     *        the radio in transmission
     *
     * \param [IN]: buffer     Buffer pointer
     * \param [IN]: size       Buffer size
     *
     * \retval result of API
     */
    RadioResult_t    (*Send )( uint8_t *buffer, uint8_t size );
    /*!
     * \brief Sets the radio in sleep mode
     */
    void    (*Sleep )( void );
    /*!
     * \brief Sets the radio in standby mode
     */
    void    (*Standby )( void );
    /*!
     * \brief Sets the radio in reception mode for the given time
     * \param [IN] timeout Reception timeout [ms]
     *                     [0: continuous, others timeout]
     */
    RadioResult_t    (*Rx )( uint32_t timeout );
    /*!
     * \brief Start a Channel Activity Detection
     */
    void    (*StartCad )( void );
    /*!
     * \brief Sets the radio in continuous wave transmission mode
     *
     * \param [IN]: freq       Channel RF frequency
     * \param [IN]: power      Sets the output power [dBm]
     * \param [IN]: time       Transmission mode timeout [s]
     */
    RadioResult_t (*SetTxContinuousWave )( uint32_t freq, int8_t power, uint16_t time );
    /*!
     * \brief Reads the current RSSI value
     *
     * \retval rssiValue Current RSSI value in [dBm]
     */
    int16_t (*Rssi )( RadioModems_t modem );
    /*!
     * \brief Writes the radio register at the specified address
     *
     * \param [IN]: addr Register address
     * \param [IN]: data New register value
     */
    void    (*Write )( uint16_t addr, uint8_t data );
    /*!
     * \brief Reads the radio register at the specified address
     *
     * \param [IN]: addr Register address
     * \retval data Register value
     */
    uint8_t (*Read )( uint16_t addr );
    /*!
     * \brief Writes multiple radio registers starting at address
     *
     * \param [IN] addr   First Radio register address
     * \param [IN] buffer Buffer containing the new register's values
     * \param [IN] size   Number of registers to be written
     *
     * \retval result of API
     */
    RadioResult_t    (*WriteBuffer )( uint16_t addr, uint8_t *buffer, uint8_t size );
    /*!
     * \brief Reads multiple radio registers starting at address
     *
     * \param [IN] addr First Radio register address
     * \param [OUT] buffer Buffer where to copy the registers data
     * \param [IN] size Number of registers to be read
     *
     * \retval result of API
     */
    RadioResult_t    (*ReadBuffer )( uint16_t addr, uint8_t *buffer, uint16_t size );
    /*!
     * \brief Sets the maximum payload length.
     *
     * \param [IN] modem      Radio modem to be used [0: FSK, 1: LoRa]
     * \param [IN] max        Maximum payload length in bytes
     */
    void    (*SetMaxPayloadLength )( RadioModems_t modem, uint8_t max );
    /*!
     * \brief Sets the network to public or private. Updates the sync byte.
     *
     * \remark Applies to LoRa modem only
     *
     * \param [IN] enable if true, it enables a public network
     */
    void    (*SetPublicNetwork )( bool enable );
    /*!
     * \brief Gets the time required for the board plus radio to get out of sleep.[ms]
     *
     * \retval time Radio plus board wakeup time in ms.
     */
    uint32_t  (*GetWakeupTime )( void );
    /*!
     * \brief Process radio irq
     *
     * \retval IRQ has events or not.
     */
    bool (*IrqProcess )( void );
    /*
     * The next functions are available only on SX126x radios.
     */
    /*!
     * \brief Sets the radio in reception mode with Max LNA gain for the given time
     *
     * \remark Available on SX126x radios only.
     *
     * \param [IN] timeout Reception timeout [ms]
     *                     [0: continuous, others timeout]
     */
    void    (*RxBoosted )( uint32_t timeout );
    /*!
     * \brief Sets the Rx duty cycle management parameters
     *
     * \remark Available on SX126x radios only.
     *
     * \param [in]  rxTime        Structure describing reception timeout value
     * \param [in]  sleepTime     Structure describing sleep timeout value
     */
    void (*SetRxDutyCycle ) ( uint32_t rxTime, uint32_t sleepTime );


    /*
     * The following functions are added by renesas.
     */

    /*!
     * \brief Sets the radio in Cold sleep mode and RC64K off
     *
     */
    void    (*SleepCold )( void );
    /*!
     * \brief Sets the radio in continuous preamble transmission mode
     *
     * \param [IN]: freq       Channel RF frequency
     * \param [IN]: power      Sets the output power [dBm]
     * \param [IN]: time       Transmission mode timeout [s]
     */
    RadioResult_t (*SetTxInfinitePreamble )( uint32_t freq, int8_t power, uint16_t time );
    /*!
     * \brief Gets energy detection value.
     *
     * \param [IN]: freq       Channel RF frequency
     * \param [IN]: edTime     ED scan time[ms].
     *
     * \retval edValue maximum RSSI value of edTime duration in [dBm]
    */
    int16_t (*Ed )(uint32_t freq, uint32_t edTime);
    /*!
     * \brief Sets PIB value.
     *
     * \param [IN]: PIB_t    PIB id
     * \param [IN]: pInVal     address of PIB Value for set
     *
     * \retval result true:set success false:set fail
    */
    bool    (*SetPib )(PIB_t id, uint8_t * pInVal);
    /*!
     * \brief Gets PIB value.
     *
     * \param [IN]: PIB_t    PIB id
     * \param [OUT]: pOutVal   address of store PIB value
     *
     * \retval result true:get success false:get fail
    */
    bool    (*GetPib )(PIB_t id, uint8_t * pOutVal);
    /*!
     * \brief Wakeup Radio and Recover Board Setting.
     *
    */
    void    (*WakeUp )( void );
    /*!
     *  \brief Execute Image Calibration.
     *
     *  \param [IN]:freq     RF frequency
    */
    void (*CalibrateImage )(uint32_t);
    /*!
     * \brief Gets Radio Error flags.
     *
     * \retval Return Radio Error flags. The result of flasg only valid on RxDone callback.
    */
    uint16_t (*GetErrorFlag )( void );
    #if defined( RP_USE_RADIO_CFG_CHECK )
    /*!
     * \brief Details described in r_radio_region_api.h
    */
    int32_t (*GetTimeToNextTx)( void );
    #endif
};

/*!
 * \brief Radio driver
 *
 * \remark This variable is defined and initialized in the specific radio
 *         board implementation
 */
extern const struct Radio_s Radio;

// DIO1 interrupt callback function
void RadioOnDioIrq(void);

#define SleepWarm() Sleep()

#if defined(DEBUG_RADIO)

void RadioDebugRadioIsChannelFree( uint8_t paketType, uint32_t freq, 
         uint32_t actualBandWidthVal,int16_t rssi, int16_t rssiThresh, uint32_t maxCarrierSenseTime, bool isChannelFree );
void RadioDebugRadioSetRxConfig( RadioModems_t modem, uint32_t bandwidth,
                             uint32_t datarate, uint8_t coderate,
                             uint32_t bandwidthAfc, uint16_t preambleLen,
                             uint16_t symbTimeout, bool fixLen,
                             uint8_t payloadLen,
                             bool crcOn, bool freqHopOn, uint8_t hopPeriod,
                             bool iqInverted, bool rxContinuous );
void RadioDebugRadioSetTxConfig( RadioModems_t modem, int8_t power, uint32_t fdev,
                        uint32_t bandwidth, uint32_t datarate,
                        uint8_t coderate, uint16_t preambleLen,
                        bool fixLen, bool crcOn, bool freqHopOn,
                        uint8_t hopPeriod, bool iqInverted, uint32_t timeout );
void RadioDebugRadioSend( uint8_t packetType, uint32_t freq, uint8_t *buffer, uint8_t size );
void RadioDebugRadioRx( uint8_t packetType, uint32_t freq, uint32_t timeout, bool rxContinuous );
void RadioDebugRadioSleep( void );
void RadioDebugRadioSleepCold( void );
void RadioDebugRadioStandby( void );
void RadioDebugRadioWakeUp( uint8_t preOpeMode );

#endif

#ifdef __cplusplus
}
#endif

#endif // __RADIO_H__
