/*
    (C) 2017 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

/*
 * at_proc.h
 *
 *  Created on: 2017/07/21
 *      Author:
 */

#ifndef _AT_PROC_H_
#define _AT_PROC_H_

#include "LoRaMac.h"

#include "board.h"

#include "at-command.h"
#include "at-parser.h"

/* macro    */
#define APP_AT_CHAR_LF  ('\n')
#define APP_AT_CHAR_CR  ('\r')
#define APP_AT_CHAR_BS  ('\b')
#define APP_AT_CHAR_NL  ('\0')
#define APP_AT_CHAR_SP  (' ')

// type of last message sent from application
#define APP_AT_LAST_SENT_MESSAGE_DATA      0   // data only
#define APP_AT_LAST_SENT_MESSAGE_CMD       1   // MAC command only
#define APP_AT_LAST_SENT_MESSAGE_DATA_CMD  2   // both data and commad
#define APP_AT_LAST_SENT_MESSAGE_JOIN      3   // join req message
#if defined(FUOTA_ENABLED)
#define APP_AT_LAST_SENT_MESSAGE_NONE      4   // (none or not send from AT command)
#endif

// Handle of mlme request and confirm
#define APP_AT_MLME_HANDLE_NONE            0x00
#define APP_AT_MLME_HANDLE_DEVTIME         0x01  // DeviceTimeReq
#define APP_AT_MLME_HANDLE_LINKCHK         0x02  // LinkCheckReq
#define APP_AT_MLME_HANDLE_PNGSLINFO       0x04  // PingSlotInfo
#define APP_AT_MLME_HANDLE_BCONTIM         0x08  // BeaconTimingReq

/* struct   */
/*!
 * @struct  AppAtCmd_t
 * @breif   extended command table
 */
typedef struct app_at_cmd_tag {
    /*! command name "+XXX" */
    uint8_t *command;

    /*! set command handler */
    AtExtend_t set;

    /*! read command handler    */
    AtExtend_t read;

    /*! action command handler  */
    AtExtend_t act;
} AppAtCmd_t;

// index of AT command of data
extern uint16_t appAtLastAtCmdIndexData;

// type of last message sent from application
extern uint16_t appAtLastSentMessage;

// handle of mlme request and confirm
extern uint8_t appAtMlmeHandle;

/*!
 * AT command initialization
 */
void AppAtInit(void);

/*!
 * @fn
 * UART event handler to receive char.
 * @param [IN] recvByte
 */
void AppGetCharByte ( uint8_t recvByte );

#define APP_EVT_UART0_RECV      (0x00000001)

uint32_t AppGetEvent ( uint32_t evtMask );

uint32_t AppSetEvent ( uint32_t evtMask );

/**
 * @fn
 * print result code for LoRaMacEventInfoStatus
 * @param status    LoRaMacEventInfoStatus
 */
void AppAtMacEventResult(LoRaMacEventInfoStatus_t status);

/**
 * @fn
 * print result code for LoRaMacStatus
 * @param status
 */
void AppAtMacStatusResult(LoRaMacStatus_t status);

/**
 * convert hex number to hex string
 * @param hexStr    destination hex string array
 * @param hexArray  source hex number array
 * @param len       length of source hex number array
 */
void AppAtHexArray2HexStr(uint8_t *hexStr, uint8_t *hexArray, uint16_t len);

/*!
 * output resultcode
 */
void AppAtOutputResultCode(char *result);
void AppAtOutputResultDec(long DecNum, unsigned char Len, unsigned char SupCh);

/*!
 * \brief print the header of extended at-command function specified in argument
 */
void AtPrintCmdHeader(char *cmdName);


#endif /* _AT_PROC_H_ */
