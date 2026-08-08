/*!
 * \file      LmHandlerMsgDisplay.h
 *
 * \brief     Common set of functions to display default messages from
 *            LoRaMacHandler.
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
 *              (C)2013-2019 Semtech
 *
 * \endcode
 *
 * \author    Miguel Luis ( Semtech )
 */
 /*
     Copyright (c) 2022 Renesas Electronics Corporation
     This software is released under the terms and conditions
     described in LICENSE_RENESAS.TXT included in the project.
 */

#ifndef __APP_COMPLIANCE_MSG_DISPLAY_H__
#define __APP_COMPLIANCE_MSG_DISPLAY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "utilities.h"


/*!
 * \brief Displays updated McpsRequest
 *
 * \param [IN] status McpsRequest execution status
 * \param [IN] mcpsReq McpsRequest command executed
 * \param [IN] nextTxIn Time to wait for the next uplink transmission
 */
void DisplayMacMcpsRequestUpdate( LoRaMacStatus_t status, McpsReq_t *mcpsReq, TimerTime_t nextTxIn );

/*!
 * \brief Displays updated MlmeRequest
 *
 * \param [IN] status MlmeRequest execution status
 * \param [IN] mlmeReq MlmeRequest command executed
 * \param [IN] nextTxIn Time to wait for the next uplink transmission
 */
void DisplayMacMlmeRequestUpdate( LoRaMacStatus_t status, MlmeReq_t *mlmeReq, TimerTime_t nextTxIn );

/*!
 * \brief Displays updated JoinRequest
 *
 * \param [IN] params Executed JoinRequest parameters
 */
void DisplayMacMlmeJoinConfirmUpdate( MlmeConfirm_t *mlmeConfirm );

/*!
 * \brief Displays Tx params
 *
 * \param [IN] params Tx parameters
 */
void DisplayMacMcpsConfirmUpdate( LoRaComplianceAppData_t *appData, McpsConfirm_t *mcpsConfirm );
void DisplayMacMlmeConfirmUpdate( MlmeConfirm_t *mlmeConfirm );

/*!
 * \brief Displays Rx params
 *
 * \param [IN] appData Receive data payload and port number
 * \param [IN] params Rx parameters
 */
void DisplayMacMcpsIndicationUpdate( McpsIndication_t *mcpsIndication );
void DisplayMacMlmeIndicationUpdate( MlmeIndication_t *mlmeIndication );

/*!
 * \brief Displays beacon status update
 *
 * \param [IN] params Beacon parameters
 */
void DisplayBeaconUpdate( MlmeIndication_t* params );

/*!
 * \brief Displays end-device class update
 *
 * \param [IN] deviceClass Current end-device class
 */
void DisplayClassUpdate( DeviceClass_t deviceClass );

/*!
 * \brief Displays application information
 */
void DisplayAppInfo( const Version_t* appVersion );

#ifdef __cplusplus
}
#endif

#endif // __APP_COMPLIANCE_MSG_DISPLAY_H__
