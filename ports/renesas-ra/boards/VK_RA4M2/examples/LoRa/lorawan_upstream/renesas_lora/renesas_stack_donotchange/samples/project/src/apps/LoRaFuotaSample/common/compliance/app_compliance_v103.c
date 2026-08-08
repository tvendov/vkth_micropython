/*!
 * \file      main.c
 *
 * \brief     LoRaMac classA/B/C device implementation
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

#if defined(APP_COMPLIANCE)

#include <stdio.h>
#include "board.h"

#include "lorawan_proc.h"
#include "app_compliance.h"

#if (LORAMAC_VERSION == LORAWAN_VERSION_1_0_3)

/*!
 * LoRaWAN compliance tests support data
 */
LoRaComplianceTestState_t ComplianceTestState;

static void McpsConfirm( McpsConfirm_t *mcpsConfirm );
static void McpsIndication( McpsIndication_t *mcpsIndication );
static void MlmeConfirm( MlmeConfirm_t *mlmeConfirm );
static void MlmeIndication( MlmeIndication_t *mlmeIndication );

static void OnMacMcpsRequest( LoRaMacStatus_t status, McpsReq_t *mcpsReq, TimerTime_t nextTxIn );
static void OnMacMlmeRequest( LoRaMacStatus_t status, MlmeReq_t *mlmeReq, TimerTime_t nextTxIn );
static void OnMacMcpsConfirm( McpsConfirm_t *mcpsConfirm );
static void OnMacMlmeConfirm( MlmeConfirm_t *mlmeConfirm );
static void OnMacMcpsIndication( McpsIndication_t *mcpsIndication );
static void OnMacMlmeIndication( MlmeIndication_t *mlmeIndication );

LoRaMacStatus_t AppComplianceJoin( void );
LoRaMacStatus_t AppComplianceSend( LoRaComplianceAppData_t *appData, bool isTxConfirmed );


/*!
 * \brief   compliabnce tests applilcation initialization 
 */
void AppCompliacneInit( void )
{
    LoRaMacPrimitives_t macPrimitives;

    if( appLoraWanSettings.complianceTestMode != APP_COMPLIANCE_TESTMODE_NONE )
    {
        macPrimitives.MacMcpsConfirm = McpsConfirm;
        macPrimitives.MacMcpsIndication = McpsIndication;
        macPrimitives.MacMlmeConfirm = MlmeConfirm;
        macPrimitives.MacMlmeIndication = MlmeIndication;

        AppLoraWanInit( &macPrimitives, NULL );

        // Set callback functions for the compliance package to notify to the application layer
        // that McpsRequest or MlmeRequest is called in ompliance package
        CompliancePackage.OnMacMcpsRequest        = OnMacMcpsRequest;
        CompliancePackage.OnMacMlmeRequest        = OnMacMlmeRequest;

        CompliancePackage.Init();
    }
}

 /*!
 * \brief   Compliabnce tests application main process 
 */
void AppComplianceProcess( void )
{
    if( appLoraWanSettings.complianceTestMode == APP_COMPLIANCE_TESTMODE_NONE )
    {
        return;
    }

    CompliancePackage.Process( );
}


/*!
 * \brief   MCPS-Confirm event function
 *
 * \param   [IN] mcpsConfirm - Pointer to the confirm structure, containing confirm attributes.
 */
static void McpsConfirm( McpsConfirm_t *mcpsConfirm )
{
    OnMacMcpsConfirm( mcpsConfirm );

    CompliancePackage.OnMcpsConfirmProcess( mcpsConfirm );
}

/*!
 * \brief   MCPS-Indication event function
 *
 * \param   [IN] mcpsIndication - Pointer to the indication structure, containing indication attributes.
 */
static void McpsIndication( McpsIndication_t *mcpsIndication )
{
    if( mcpsIndication->Status != LORAMAC_EVENT_INFO_STATUS_OK )
    {
        return;
    }

    OnMacMcpsIndication( mcpsIndication );
 
    CompliancePackage.OnMcpsIndicationProcess( mcpsIndication );
}

/*!
 * \brief   MLME-Confirm event function
 *
 * \param   [IN] mlmeConfirm - Pointer to the confirm structure, containing confirm attributes.
 */
static void MlmeConfirm( MlmeConfirm_t *mlmeConfirm )
{
    OnMacMlmeConfirm( mlmeConfirm );

    CompliancePackage.OnMlmeConfirmProcess( mlmeConfirm );
}

/*!
 * \brief   MLME-Indication event function
 *
 * \param   [IN] mlmeIndication - Pointer to the indication structure, containing indication attributes
 */
static void MlmeIndication( MlmeIndication_t *mlmeIndication )
{
    OnMacMlmeIndication( mlmeIndication );

    CompliancePackage.OnMlmeIndicationProcess( mlmeIndication );
}

/*!
 * Executes the network Join request
 */
LoRaMacStatus_t AppComplianceJoin( void )
{
    LoRaMacStatus_t status;
    TimerTime_t DutyCycleWaitTime;

    if( appLoraWanSettings.actMode == APP_LORAWAN_ACTMODE_OTAA )
    {
        status = AppLoraWanJoinReq( &DutyCycleWaitTime );

        if( status == LORAMAC_STATUS_OK )
        {                 
            ComplianceTestState.DeviceState = DEVICE_STATE_SLEEP;
            ComplianceTestState.WakeUpState = DEVICE_STATE_SLEEP;
            // Wait for MlmeConfirm(Join)
        }
        else
        {
            ComplianceTestState.DeviceState = DEVICE_STATE_CYCLE;
            ComplianceTestState.WakeUpState = DEVICE_STATE_JOIN;
            // Retry Join after waiting for the interval
        }

        // Notify to the application MlmeRequest(Join) is issued
        MlmeReq_t mlmeReq;
        mlmeReq.Type = MLME_JOIN;
        OnMacMlmeRequest( status, &mlmeReq, DutyCycleWaitTime );
    }
#ifdef LORAMAC_ACTMODE_ABP_ENABLED    
    else // if( appLoraWanSettings.actMode == APP_LORAWAN_ACTMODE_ABP )
    {
        // Set network jointed to true
        status = AppLoraWanSetNetworkJoined( true );
        DutyCycleWaitTime = 0;

        if( status == LORAMAC_STATUS_OK )
        {
            ComplianceTestState.DeviceState = DEVICE_STATE_CYCLE;
            ComplianceTestState.WakeUpState = DEVICE_STATE_SEND;
            // Send uplink frame after waiting for the interval
        }
        else
        {
            ComplianceTestState.DeviceState = DEVICE_STATE_CYCLE;
            ComplianceTestState.WakeUpState = DEVICE_STATE_JOIN;           
            // Retry Join after waiting for the interval
        }
    }
#else
    else
    {
        status = LORAMAC_STATUS_SERVICE_UNKNOWN;
    }
#endif
    return( status );
}


LoRaMacStatus_t AppComplianceJoinStatus( void )
{
    MibRequestConfirm_t mibReq;
    LoRaMacStatus_t status;

    mibReq.Type = MIB_NETWORK_ACTIVATION;
    status = LoRaMacMibGetRequestConfirm( &mibReq );

    if( status == LORAMAC_STATUS_OK )
    {
        if( mibReq.Param.NetworkActivation == ACTIVATION_TYPE_NONE )
        {
            status = LORAMAC_STATUS_NO_NETWORK_JOINED;
        }
    }

    return status;
}

LoRaMacStatus_t AppComplianceSend( LoRaComplianceAppData_t *appData, bool isTxConfirmed )
{
    LoRaMacStatus_t status;
    TimerTime_t DutyCycleWaitTime;
     
    if( AppComplianceJoinStatus( ) == LORAMAC_STATUS_NO_NETWORK_JOINED )
    {
        // The network isn't joined, try again.
        AppComplianceJoin( );
        return LORAMAC_STATUS_NO_NETWORK_JOINED;
    }

    status = AppLoraWanSendData( appData->Buffer, appData->BufferSize, appData->Port, 
                                    (isTxConfirmed? MCPS_CONFIRMED: MCPS_UNCONFIRMED),
                                    &DutyCycleWaitTime);

    McpsReq_t mcpsReq;
    mcpsReq.Type = (isTxConfirmed? MCPS_CONFIRMED: MCPS_UNCONFIRMED); 
    OnMacMcpsRequest( status, &mcpsReq, DutyCycleWaitTime );

    // Save parameters to display sent data in McpsConfirm later
    ComplianceTestState.SentData.Buffer = appData->Buffer;
    ComplianceTestState.SentData.BufferSize = appData->BufferSize;
    ComplianceTestState.SentData.BufferSize = appData->Port;
    if( status == LORAMAC_STATUS_SKIPPED_APP_DATA )
    {
        ComplianceTestState.SentData.BufferSize = 0;
        ComplianceTestState.SentData.Port = 0;
    }

    return status;
}

/*!
 * \brief   Callback function to display MCPS-Request parameters
 *
 * \param   [IN] mcpsReq - Pointer to the request structure, containing request attributes.
 */
static void OnMacMcpsRequest( LoRaMacStatus_t status, McpsReq_t *mcpsReq, TimerTime_t nextTxIn )
{
    // Display McpsRequest parameters
    DisplayMacMcpsRequestUpdate( status, mcpsReq, nextTxIn );
}

/*!
 * \brief   Callback function to display MCPS-Confirm parameters
 *
 * \param   [IN] mcpsConfirm - Pointer to the confirm structure, containing confirm attributes..
 */
static void OnMacMcpsConfirm( McpsConfirm_t *mcpsConfirm )
{
    // Display McpsConfirm parameters
    DisplayMacMcpsConfirmUpdate( &ComplianceTestState.AppData, mcpsConfirm );
}

/*!
 * \brief   Callback function to display MCPS-Indication parameters
 *
 * \param   [IN] mcpsIndication - Pointer to the indication structure, containing indication attributes.
 */
static void OnMacMcpsIndication( McpsIndication_t *mcpsIndication )
{
    DisplayMacMcpsIndicationUpdate( mcpsIndication );
}

/*!
 * \brief   Callback function to display MLME-Request parameters
 *
 * \param   [IN] mlmeReq - Pointer to the request structure, containing request attributes.
 */
static void OnMacMlmeRequest( LoRaMacStatus_t status, MlmeReq_t *mlmeReq, TimerTime_t nextTxIn )
{
    // Display MlmeRequest parameters
    DisplayMacMlmeRequestUpdate( status, mlmeReq, nextTxIn );
}

/*!
 * \brief   Callback function to display MLME-Confirm parametes
 *
 * \param   [IN] mlmeConfirm - Pointer to the confirm structure, containing confirm attributes.
 */
static void OnMacMlmeConfirm( MlmeConfirm_t *mlmeConfirm )
{
    // Display MlmeConfirm status
    DisplayMacMlmeConfirmUpdate( mlmeConfirm );
}

/*!
 * \brief   Callback function to display MLME-Indication parametes
 *
 * \param   [IN] mlmeIndication - Pointer to the indication structure, containing indication attributes.
 */
static void OnMacMlmeIndication( MlmeIndication_t *mlmeIndication )
{
    if( mlmeIndication->Status != LORAMAC_EVENT_INFO_STATUS_BEACON_LOCKED )
    {
        DisplayMacMlmeIndicationUpdate( mlmeIndication );
   }
}

#endif // if (LORAMAC_VERSION == LORAWAN_VERSION_1_0_3)

#endif // defined(APP_COMPLIANCE)

