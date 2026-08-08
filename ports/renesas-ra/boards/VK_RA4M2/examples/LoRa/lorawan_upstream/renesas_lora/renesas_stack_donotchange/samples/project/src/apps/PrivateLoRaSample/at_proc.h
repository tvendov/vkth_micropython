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

#include "PrivateLoRa.h"

#include "board.h"

#include "at-command.h"
#include "at-parser.h"

/* macro    */
#define APP_AT_CHAR_LF  ('\n')
#define APP_AT_CHAR_CR  ('\r')
#define APP_AT_CHAR_BS  ('\b')
#define APP_AT_CHAR_NL  ('\0')
#define APP_AT_CHAR_SP  (' ')

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


/*-----------*/
/* Functions */

// AT command initialization
extern void AppAtInit(void);

// tools prototype
extern int8_t *AppAtGetString( int8_t *str, int16_t *len );
extern uint16_t AppAtHexStr2Hex( int8_t *hexStr );
extern void AppAtHexArray2HexStr( uint8_t *hexStr, uint8_t *hexArray, uint16_t len );
extern AtResultCode_t AppAtHexStr2HexDataArrayWithPadding( uint8_t *hexDataArray, 
                                                           uint16_t hexDataArrayLen, 
                                                           int8_t *hexStr, 
                                                           uint16_t hexStrLen );
extern AtResultCode_t AppAtHexStr2HexDataArray( uint8_t *hexDataArray, int8_t *hexStr, uint16_t hexStrLen );
extern int32_t AppAtStr2Dec( int8_t *str, uint8_t len );

// print the header of extended at-command function specified in argument
extern void AtPrintCmdHeader( char *cmdName );
// output resultcode
extern void AppAtOutputResultCode( char *result );
extern void AppAtOutputResultDec( long DecNum, unsigned char Len, unsigned char SupCh );

#endif /* _AT_PROC_H_ */
