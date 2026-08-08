/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/
#ifndef AT_CMD_H
#define AT_CMD_H

#include <stdint.h>

#include "buffer.h"

/*!
 * Defines AT command module version, which is used in AT+VERS extended command
 */
#define REVAL_ATCMD_VERS    (11)

/*!
 * Defines the maximum array size of AT extended commands jump table
 */
#ifdef AT_EXTCMD_TAB_ARRAYSIZE_CONFIG
#   define AT_EXTCMD_TAB_ARRAYSIZE     AT_EXTCMD_TAB_ARRAYSIZE_CONFIG
#else
#   define AT_EXTCMD_TAB_ARRAYSIZE     (40)      // 0 = ROM table, 1- = RAM table
#endif

/*!
 * Defines charactors
 */
#define AT_CHAR_LF                      ('\n')
#define AT_CHAR_CR                      ('\r')
#define AT_CHAR_BS                      ('\b')
#define AT_CHAR_NL                      ('\0')
#define AT_CHAR_SP                      (' ')

/*!
 * Defines the resultcode string of AT commands
 */
#define AT_RCS_CRLF                     ("\r\n")
#define AT_RCS_OK                       ("OK")
#define AT_RCS_ERR                      ("ERROR")
#define AT_RCS_BUSY                     ("BUSY")
#define AT_RCS_NOANS                    ("NO ANSWER")

/*!
 * Defines the AT basic commands
 */
#define AT_BASIC_ECHOBACK               ('E')
#define AT_BASIC_RESPFORMAT             ('V')
#define AT_BASIC_RESET                  ('Z')
#define AT_BASIC_DEFAULT_ECHOBACK       (Disable)
#define AT_BASIC_DEFAULT_RESPFORMAT     (NO_HEAD)

/*!
 * Defines the termination of string
 */
#define AT_CMD_NUL                      ('\0')

/*!
 * Defines the delimiters of AT extended command arguments
 */
#define AT_EXTCMD_DELIM                 (',')

/*!
 */
#define LOWERCASE(c)                    ('a' <= c && c <= 'z')
#define UPPERCASE(c)                    ('A' <= c && c <= 'Z')
#define NUMBERS(c)                      ('0' <= c && c <= '9')
#define HEXLOWERCASE(c)                 ('a' <= c && c <= 'f')
#define HEXUPPERCASE(c)                 ('A' <= c && c <= 'F')
#define ACCEPTABLE(c)                   ('=' == c || '+' == c || '?' == c || ',' == c || '"' == c)
#define NOACCEPTABLE(c)                 (' ' != c)


/*!
 * Error code of Program status
 */
typedef enum AtErrTag {
    AT_EC_OK,                           /* OK   */
    AT_EC_ERR,                          /* ERR  */
    AT_EC_LOGIC,                        /* Program Logical Error    */
    AT_EC_MEM,                          /* Insufficient Memory      */
    AT_EC_PARSE,                        /* Parse Error              */
    AT_EC_NOTSUP,                       /* Out of Support Command   */
    AT_EC_MAX,                          /* Out of range             */
} AtErrorCode_t;

/*!
 */
typedef enum AtResultCodeTag {
    AT_RC_OK    = 0,                    /* OK           */
    AT_RC_ERR   = 4,                    /* ERROR        */
    AT_RC_BUSY  = 7,                    /* BUSY         */
    AT_RC_NOANS = 8,                    /* NO Answer    */
    AT_RC_MAX,                          /* Out of range */
} AtResultCode_t;

/*!
 * Defines the Echoback Enabled or Disabled
 */
typedef enum AtEchoBackTag {
    Disable = 0,
    Enable  = 1,
} AtEchoBack_t;

/*!
 * Defines the Response format without heading CRLF or with heading CRLF
 */
typedef enum AtRespFormatTag {
    NO_HEAD   = 0,
    HEAD_CRLF = 1,
} AtRespFormat_t;

/*!
 */
typedef struct AtCmdStatusTag {
    uint8_t         stat;
    AtErrorCode_t   ec;
    AtResultCode_t  rc;
} AtCmdStatus_t;

/*!
 */
typedef struct AtResultCodeTabTag {
    AtResultCode_t  rc;
    char      *str;
} AtResultCodeTab_t;


/*!
 * Defines the function of AT extended commands
 */
typedef AtResultCode_t  (*AtExtend_t)( void * );
/*!
 * Defines the AT extended commands jump table
 */
typedef struct AtExtendTabTag {
    AtExtend_t          set;                /* Set commands; AT+CMD=    */
    AtExtend_t          get;                /* Get commands; AT+CMD?    */
    AtExtend_t          act;                /* Act commands; AT+CMD     */
    uint16_t            len;                /* Length of CMD            */
    int8_t              *name;              /* the name of CMD          */
} AtExtendTab_t;

/*!
 * Defines the function of AT basic commands
 */
typedef void    (*AtBasic_t)(int16_t);

/*!
 * Defines the AT basic commands jump table
 */
typedef struct AtBasicTabTag {
    int8_t      cmd;
    AtBasic_t   proc;
} AtBasicTab_t;

/*!
 * Defines the structure of AT commands control block
 */
typedef struct AtControlBlockTag {
    AtErrorCode_t   ec;
    AtResultCode_t  rc;
    AtEchoBack_t    echoBack;
    AtRespFormat_t  respFormat;
    uint16_t        curExtendCmd;
    uint16_t        atExtendTableCount;
    AtExtendTab_t   prevExtCmd;
    AtBasicTab_t    prevBasicCmd;
} AtControlBlock_t;

/*!
 * Defines the structure of buffer control block
 */
typedef struct BufferControlBlockTag {
    uint16_t    pos;
    uint16_t    arraySize;
    int8_t      locked;
    uint8_t     options;
} BufferCb_t;


/*!
 * Interface function; initialize the AT command module
 */
#if (AT_EXTCMD_TAB_ARRAYSIZE == 0)
void AtInit (uint8_t *, int16_t, AtExtendTab_t *, uint16_t);
#else
void AtInit (uint8_t *, int16_t);
#endif
void AtGetCharByte (uint8_t);
bool AtNotifyCommandReceived (void);
bool AtGetStateIsCommandReceived (void);

void AtInitUart (void);
void AtDeInitUart (void);

/*!
 * Interface function; parse the AT command
 */
int8_t AtCmdParse (void);

/*!
 * Interface function; register the AT extended commands
 */
int8_t AtExtendCmdRegist (char *, AtExtend_t, AtExtend_t, AtExtend_t);

/*!
 * Interface function; print the AT commands response header without CRLF or with CRLF
 */
void AtPrintHeader (void);
/*!
 * Interface function; 
 */
void AtPrintResultCode (void);
/*!
 * Interface function; print the AT commands response terminator
 */
void AtPrintTrailer (void);
/*!
 * Interface function; 
 */
/*!
 *
 */
void AtSetVerboseMode (uint8_t);
/*!
 *
 */
uint8_t AtGetVerboseMode (void);



/*!
 * Prototype defines
 */
void AtCmdSetEcStatus (AtErrorCode_t);
AtErrorCode_t AtCmdGetEcStatus (void);
void AtCmdSetRcStatus (AtResultCode_t);
void AtCmdSetCurrentCmd (uint16_t);

#endif/* AT_CMD_H */
