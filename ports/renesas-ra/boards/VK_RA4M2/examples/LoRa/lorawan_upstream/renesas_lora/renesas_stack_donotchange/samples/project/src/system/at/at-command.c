/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/
#include "board.h"

#include "at-command.h"
#include "at-parser.h"

#include "buffer.h"


/*!
 * \brief prototypes of the basic at-command functions
 */
void AtBasicEchoBack (int16_t);
void AtBasicRespFormat (int16_t);


/*!
 * \brief characters table for at-command syntax check
 */
const int8_t atCmdCheckTable[] = {'A', 'T', '\0', '+',};

/*!
 * \brief table for searching for corresponding function to at-command character
 */
const AtBasicTab_t atBasicTable[] = {
                                    //{ AT_BASIC_ECHOBACK,   AtBasicEchoBack   }, // Not for RA
                                      { AT_BASIC_RESPFORMAT, AtBasicRespFormat },
                                      { AT_CMD_NUL,          NULL              },};

/*!
 * \brief table for searching for corresponding string to at-command result
 */
const AtResultCodeTab_t atResultCodeTable[] = { { AT_RC_OK,     AT_RCS_OK    },
                                                { AT_RC_ERR,    AT_RCS_ERR   },
                                                { AT_RC_BUSY,   AT_RCS_BUSY  },
                                                { AT_RC_NOANS,  AT_RCS_NOANS },};

/*!
 * \brief management of at-command component
 */
static uint8_t at_notification = 0;
static Buffer_t at;

static AtControlBlock_t atControlBlock;
AtControlBlock_t *atCb = &atControlBlock;


/*!
 * \brief enabling / disabling uart reception on at-command level
 */
static int8_t uart_initialized = 0;

/*!
 * \brief global variables to register at-command xtend sets
 */
#if (AT_EXTCMD_TAB_ARRAYSIZE == 0)
// ROM table
static AtExtendTab_t *atExtendTable;
#else
// RAM table
static AtExtendTab_t atExtendTable [ AT_EXTCMD_TAB_ARRAYSIZE ];
#endif

/*!
 * \brief prototypes
 */
void AtEchoBack (uint8_t);
int8_t AtRespFormat (void);
void AtCmdClearStatus (void);
void AtCmdSetErrStatus (void);
AtResultCode_t AtCmdGetRcStatus (void);
uint16_t AtCmdGetCurrentCmd (void);


/*!
 * \brief obtain character from uart reception interrupt, and buffering at-command string
 */
void AtGetCharByte (uint8_t recvByte)
{
    static uint8_t  terminator;

    if (! uart_initialized) return;

    if (BufferIsLocked(&at)) return;

    if (BufferIsFull(&at)) {
        if (AT_CHAR_BS == recvByte) {
            BufferPopBack(&at);
            AtEchoBack(recvByte);
        }
        return;
    }

    if (LOWERCASE(recvByte)) recvByte -= 32;
    switch (recvByte) {
        case AT_CHAR_LF:
            {
                BufferPushBack(&at, recvByte);
                AtEchoBack(recvByte);
                if (terminator) {
                    BufferPopBack(&at);
                    BufferPopBack(&at);
                    at_notification = 1;
                    terminator = 0;
                }
                break;
            }
        case AT_CHAR_CR:
            {
                BufferPushBack(&at, recvByte);
                AtEchoBack(recvByte);
                terminator = 1;
                break;
            }
        case AT_CHAR_BS:
            {
                if (! BufferIsEmpty(&at)) {
                    BufferPopBack(&at);
                    AtEchoBack(recvByte);
                }
                terminator = 0;
                break;
            }
        default:
        case AT_CHAR_SP:
            {
                //if (UPPERCASE(recvByte) || NUMBERS(recvByte) || ACCEPTABLE(recvByte)) {
                if (UPPERCASE(recvByte) || NUMBERS(recvByte) || NOACCEPTABLE(recvByte)) {
                    BufferPushBack(&at, recvByte);
                    AtEchoBack(recvByte);
                    terminator = 0;
                }
                break;
            }
    } /* switch (recvByte) */
}

/*!
 * \brief parsing at-command string
 */
int8_t AtCmdParse (void)
{
    uint16_t atExtendTableCount;
    uint8_t  c;
    uint8_t  i;

    if (BufferIsLocked(&at)) {
        AtCmdSetEcStatus(AT_EC_LOGIC);
        return ((int8_t )AtCmdGetEcStatus());
    }

    BufferSetLocked(&at);

    AtCmdSetErrStatus();

    if (! BufferIsEmpty(&at)) {
        for (i=0; i < sizeof(atCmdCheckTable); i++) {
            c = (int8_t )BufferPopFront(&at);
            if (atCmdCheckTable[i] != c)
                break;
        }
        switch (i) {
            case 3:
                {
                    /* 'A' + 'T' + NUL */
                    AtCmdClearStatus();
                    break;
                }
            case 2:
                {
                    /* 'A' + 'T' + not NUL */
                    if (atCmdCheckTable[3] != c) {
                        /* AT Basic */
                        AtParseBasic (&atBasicTable[0], (int8_t *)BufferHead(&at));
                    }
                    else {
                        /* AT Extended */
                        atExtendTableCount = atCb->atExtendTableCount;
                        AtParseExtend(&atExtendTable[0], atExtendTableCount, (int8_t *)BufferHead(&at));
                    }
                    break;
                }
            case 0:
            case 1:
                {
                    /* ERR: not 'A' or not 'T' */
                    AtCmdSetEcStatus(AT_EC_OK);
                    break;
                }
            default:
                {
                    /* ERR: ASSERT */
                    break;
                }
        }
    }

    AtPrintResultCode();

    BufferSetUnlock(&at);
    BufferReset(&at);

    return ((int8_t )AtCmdGetEcStatus());
}

/*!
 * \brief disabling suppression of uart reception interrupt in at-command layer
 */
void AtInitUart (void)
{
    if (! uart_initialized) {
        uart_initialized = 1;
    }
}

/*!
 * \brief enabling suppression of uart reception interrupt in at-command layer
 */
void AtDeInitUart (void)
{
    uart_initialized = 0;
}

/*!
 * \brief notification of received at-command string
 */
bool AtNotifyCommandReceived (void)
{
    bool    ret = false;

    if (at_notification) {
        ret = true;
        at_notification = 0;
    }

    return (ret);
}

/*!
 * \brief obtain the state whether at-command is received
 */
bool AtGetStateIsCommandReceived (void)
{
    bool    ret = false;

    if (at_notification) {
        ret = true;
    }

    return (ret);
}

/*!
 * \brief obtain the status of basic at-command Echo Back function
 */
int8_t AtEchoBackEnable (void)
{
    return ((int8_t )atCb->echoBack);
}

/*!
 * \brief print to uart if Echo Back function enabled
 */
void AtEchoBack (uint8_t recvByte)
{
    if (AtEchoBackEnable()) {
        putchar(recvByte);
    }
}

/*!
 * \brief obtain the status of basic at-command Response format function
 */
int8_t AtRespFormat (void)
{
    return ((int8_t )atCb->respFormat);
}

/*!
 * \brief configure the xtend at-command verbose mode function
 */
void AtSetVerboseMode (uint8_t opt)
{
    BufferSetOpt(&at, opt);
}

/*!
 * \brief obtain the status of xtend at-command verbose mode function
 */
uint8_t AtGetVerboseMode (void)
{
    uint8_t opt = BufferGetOpt(&at);

    return (opt);
}

/*!
 * \brief print the header of xtend at-command function
 */
void AtPrintHeader (void)
{
    if (AtRespFormat()) {
        print((char *)AT_RCS_CRLF);
    }
    print((char *)atExtendTable[ AtCmdGetCurrentCmd() ].name);
    print((char *)":");
}

/*!
 * \brief print the result of at-command function
 */
void AtPrintResultCode (void)
{
    AtResultCode_t  rc = AtCmdGetRcStatus();
    uint16_t         i;

    if (AT_RC_NOANS == rc) return;

    if (AtRespFormat()) {
        print((char *)AT_RCS_CRLF);
    }

    for (i=0; i < (sizeof(atResultCodeTable)/sizeof(AtResultCodeTab_t)); i++) {
        if (atResultCodeTable[i].rc == rc) {
            break;
        }
    }
    print((char *)atResultCodeTable[i].str);
    print((char *)AT_RCS_CRLF);
}

/*!
 * \brief print the trailer of at-command function
 */
void AtPrintTrailer (void)
{
    print((char *)AT_RCS_CRLF);
}

/*!
 * \brief initialize the at-command component
 */
#if (AT_EXTCMD_TAB_ARRAYSIZE == 0)
void AtInit (uint8_t *bp, int16_t size, AtExtendTab_t *pcmdtbl, uint16_t numcmd )
{
    atExtendTable = pcmdtbl;

    if (NULL != bp) {

        if (size < 1) return;

        BufferInit(&at, bp, size);

        atCb = &atControlBlock;

        atCb->atExtendTableCount = 0;
        atCb->ec         = AT_EC_OK;
        atCb->rc         = AT_RC_OK;
        atCb->echoBack   = AT_BASIC_DEFAULT_ECHOBACK;
        atCb->respFormat = AT_BASIC_DEFAULT_RESPFORMAT;
        atCb->atExtendTableCount = numcmd;

        AtParseInit();
    }
}
#else
void AtInit (uint8_t *bp, int16_t size)
{
    AtExtendTab_t  *tp = &atExtendTable[0];
    uint16_t    i;

    if (NULL != bp) {

        if (size < 1) return;

        BufferInit(&at, bp, size);

        atCb = &atControlBlock;

        for (i=0; i < AT_EXTCMD_TAB_ARRAYSIZE; i++) {
            tp->len  = 0;
            tp++;
        }
        atCb->atExtendTableCount = 0;
        atCb->ec         = AT_EC_OK;
        atCb->rc         = AT_RC_OK;
        atCb->echoBack   = AT_BASIC_DEFAULT_ECHOBACK;
        atCb->respFormat = AT_BASIC_DEFAULT_RESPFORMAT;

        AtParseInit();
    }
}
#endif

#if (AT_EXTCMD_TAB_ARRAYSIZE > 0)
/*!
 * \brief register the xtend at-command
 */
int8_t AtExtendCmdRegist (char *cmd, AtExtend_t set, AtExtend_t get, AtExtend_t act)
{
    uint16_t cur_cnt = atCb->atExtendTableCount;

    if (AT_EXTCMD_TAB_ARRAYSIZE <= cur_cnt) return ((int8_t )AT_EC_MEM);

    atExtendTable[ cur_cnt ].set  = set;
    atExtendTable[ cur_cnt ].get  = get;
    atExtendTable[ cur_cnt ].act  = act;
    atExtendTable[ cur_cnt ].len  = strlen( (const char *)cmd );
    atExtendTable[ cur_cnt ].name = (int8_t *)cmd;
    atCb->atExtendTableCount++;

    return ((int8_t )AT_EC_OK);
}
#endif

/*!
 * \brief clear the result and status of at-command component
 */
void AtCmdClearStatus (void)
{
    atCb->ec = AT_EC_OK;
    atCb->rc = AT_RC_OK;
}
/*!
 * \brief set the status of at-command component
 */
void AtCmdSetErrStatus (void)
{
    atCb->ec = AT_EC_ERR;
    atCb->rc = AT_RC_ERR;
}

/*!
 * \brief set the error code of at-command component
 */
void AtCmdSetEcStatus (AtErrorCode_t ec)
{
    atCb->ec = ec;
}
/*!
 * \brief get the error code of at-command component
 */
AtErrorCode_t AtCmdGetEcStatus (void)
{
    return (atCb->ec);
}

/*!
 * \brief set the result code of at-command component
 */
void AtCmdSetRcStatus (AtResultCode_t rc)
{
    atCb->rc = rc;
}
/*!
 * \brief get the result code of at-command component
 */
AtResultCode_t AtCmdGetRcStatus (void)
{
    return (atCb->rc);
}
/*!
 * \brief save the invoked xtend at-command
 */
void AtCmdSetCurrentCmd (uint16_t ind)
{
    atCb->curExtendCmd = ind;
}
/*!
 * \brief get the saved xtend at-command
 */
uint16_t AtCmdGetCurrentCmd (void)
{
    return (atCb->curExtendCmd);
}

/*!
 * \brief basic at-command 'echo back' function
 */
void AtBasicEchoBack (int16_t val)
{
    if ((val == 0) || (val == 1)) {
        AtCmdClearStatus();
        atCb->echoBack = (AtEchoBack_t )val;
    }
    else {
        AtCmdSetEcStatus(AT_EC_OK);
        AtCmdSetRcStatus(AT_RC_ERR);
    }
}

/*!
 * \brief basic at-command 'response format' function
 */
void AtBasicRespFormat (int16_t val)
{
    if ((val == 0) || (val == 1)) {
        AtCmdClearStatus();
        atCb->respFormat = (AtRespFormat_t )val;
    }
    else {
        AtCmdSetEcStatus(AT_EC_OK);
        AtCmdSetRcStatus(AT_RC_ERR);
    }
}
