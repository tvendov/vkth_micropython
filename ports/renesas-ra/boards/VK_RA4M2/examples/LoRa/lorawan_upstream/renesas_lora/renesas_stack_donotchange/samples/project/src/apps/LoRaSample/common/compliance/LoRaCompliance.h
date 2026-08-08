/*!
 * \file      LmhpCompliance.h
 *
 * \brief     Implements the LoRa-Alliance certification protocol handling
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
 *              (C)2013-2018 Semtech
 *
 * \endcode
 *
 * \author    Miguel Luis ( Semtech )
 */

 /*
    (C) 2017-2022 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#ifndef __LORA_COMPLIANCE__
#define __LORA_COMPLIANCE__

#ifdef __cplusplus
extern "C" {
#endif

#include "LoRaMac.h"

#define COMPLIANCE_PORT         224

/*-----------------------------------------------------------*/
// default coinfigurations
#define DEBUG_PRINT_COMPLIANCE_ENABLED  // Enable Debug Print

/*! 
 * Custom for RA0E1
 */
#if defined(__RA0E1__)
// Disable Debug Print
#undef DEBUG_PRINT_COMPLIANCE_ENABLED
#endif  //__RA0E1__

typedef struct LoRaCompliancePackage_s
{
    uint8_t Port;
    /*
     *=========================================================================
     * Below callbacks must be initialized in package variable declaration
     *=========================================================================
     */

    /*!
     * Initializes the package with provided parameters
     *
     * \param [IN] params            Pointer to the package parameters
     * \param [IN] dataBuffer        Pointer to main application buffer
     * \param [IN] dataBufferMaxSize Main application buffer maximum size
     */
#if (LORAMAC_VERSION == LORAWAN_VERSION_1_0_4)
    void ( *Init )( void *params );
#else
    void ( *Init )( void );
#endif
    /*!
     * Returns the current package initialization status.
     *
     * \retval status Package initialization status
     *                [true: Initialized, false: Not initialized]
     */
    bool ( *IsInitialized )( void );
    /*!
     * Returns if a package transmission is pending or not.
     *
     * \retval status Package transmission status
     *                [true: pending, false: Not pending]
     */
    bool ( *IsTxPending )( void );
    /*!
     * Processes the internal package events.
     */
    void ( *Process )( void );
    /*!
     * Processes the MCSP Confirm
     *
     * \param [IN] mcpsConfirm MCPS confirmation primitive data
     */
    void ( *OnMcpsConfirmProcess )( McpsConfirm_t *mcpsConfirm );
    /*!
     * Processes the MCPS Indication
     *
     * \param [IN] mcpsIndication     MCPS indication primitive data
     */
    void ( *OnMcpsIndicationProcess )( McpsIndication_t *mcpsIndication );
    /*!
     * Processes the MLME Confirm
     *
     * \param [IN] mlmeConfirm MLME confirmation primitive data
     */
    void ( *OnMlmeConfirmProcess )( MlmeConfirm_t *mlmeConfirm );
    /*!
     * Processes the MLME Indication
     *
     * \param [IN] mlmeIndication     MLME indication primitive data
     */
    void ( *OnMlmeIndicationProcess )( MlmeIndication_t *mlmeIndication );


    /*!
     * Notifies the upper layer that a MCPS request has been made to the MAC layer
     *
     * \param   [IN] status      - Request returned status
     * \param   [IN] mcpsRequest - Performed MCPS-Request. Refer to \ref McpsReq_t.
     * \param   [IN] nextTxDelay - Time to wait until another TX is possible.
     */
    void ( *OnMacMcpsRequest )( LoRaMacStatus_t status, McpsReq_t *mcpsReq, TimerTime_t nextTxDelay );
    /*!
     * Notifies the upper layer that a MLME request has been made to the MAC layer
     *
     * \param   [IN] status      - Request returned status
     * \param   [IN] mlmeRequest - Performed MLME-Request. Refer to \ref MlmeReq_t.
     * \param   [IN] nextTxDelay - Time to wait until another TX is possible.
     */
    void ( *OnMacMlmeRequest )( LoRaMacStatus_t status, MlmeReq_t *mlmeReq, TimerTime_t nextTxDelay );


#if (LORAMAC_VERSION == LORAWAN_VERSION_1_0_4)
#if( LMH_SYS_TIME_UPDATE_NEW_API == 1 )
    /*!
     * Notifies the upper layer that the system time has been updated.
     *
     * \param [in] isSynchronized Indicates if the system time is synchronized in the range +/-1 second
     * \param [in] timeCorrection Received time correction value
     */
    void ( *OnSysTimeUpdate )( bool isSynchronized, int32_t timeCorrection );
#else
    /*!
     * Notifies the upper layer that the system time has been updated.
     */
    void ( *OnSysTimeUpdate )( void );
#endif
#endif
}LoRaCompliancePackage_t;


#if (LORAMAC_VERSION == LORAWAN_VERSION_1_0_4) 
typedef struct LoRaCompliacneParams_s
{
    /*!
     * Current firmware version
     */
    Version_t FwVersion;
    Version_t lrwanVersion;
    Version_t lrwanRpVersion;

    uint8_t             DataBufferMaxSize;
    uint8_t*            DataBuffer;
    /*!
     *
     */
    void ( *OnTxPeriodicityChanged )( uint32_t periodicity );
    /*!
     *
     */
    void ( *OnTxFrameCtrlChanged )( bool isTxConfirmed );
    /*!
     *
     */
    void ( *OnPingSlotPeriodicityChanged )( uint8_t pingSlotPeriodicity );

}LoRaComplianceParams_t;
#endif

/*!
 * Application data structure
 */
typedef struct LoRaComplianceAppData_s
{
    uint8_t Port;
    uint8_t BufferSize;
    uint8_t *Buffer;
}LoRaComplianceAppData_t;


#if (LORAMAC_VERSION == LORAWAN_VERSION_1_0_3)  
/*!
 * Device states
 */
typedef enum eLoRaComplianceDeviceState
{
    DEVICE_STATE_NONE,
    DEVICE_STATE_JOIN,
    DEVICE_STATE_SEND,
    DEVICE_STATE_CYCLE,
    DEVICE_STATE_SLEEP,
    // reserved
    DEVICE_STATE_REQ_DEVICE_TIME,
    DEVICE_STATE_REQ_PING_SLOT_INFO,
    DEVICE_STATE_REQ_BEACON_TIMING,
    DEVICE_STATE_BEACON_ACQUISITION,
    //DEVICE_STATE_SWITCH_CLASS,
} LoRaComplianceDeviceState_t;

typedef struct LoRaComplianceTestState_s
{
    bool            Initialized;

    bool            Running;
    LoRaComplianceDeviceState_t DeviceState;
    LoRaComplianceDeviceState_t WakeUpState;
    uint8_t         State;          // Used to select uplink data format
    bool            IsTxConfirmed;
    uint32_t        TxDutyCycleTime;
    bool            IsTxPending;            // Reserved

    LoRaComplianceAppData_t AppData;

    /*!
     * Application data buffer maximum size
     */
    uint8_t         AppDataMaxSize;

    LoRaComplianceAppData_t SentData;       // Used to display sent data when Mcps-Confirm event is notified

    uint16_t        DownLinkCounter;
    bool            LinkCheck;
    uint8_t         DemodMargin;
    uint8_t         NbGateways;

   // Message type (Unconfirmed/Confirmed), Fport, ADR, Duty cycle settings
   // could be changed when compliance test mode is activated.
   // These settings are restored when compliance test mode is deactivated.
    struct {
       uint8_t     fPort;
       bool        isTxConfirmed;
       bool        adr;
       uint8_t     dCycle;
    } Backups;

    
    /*!
     * Timer to handle the application data transmission duty cycle
     */
    TimerEvent_t    TxNextPacketTimer;

    struct sTimerEvents
    {
        uint8_t TxNextPacketTimer : 1;
    } Events;
} LoRaComplianceTestState_t;

extern LoRaComplianceTestState_t ComplianceTestState;
#endif

extern LoRaCompliancePackage_t CompliancePackage;

#ifdef __cplusplus
}
#endif

#endif // __LORA_COMPLIANCE__
