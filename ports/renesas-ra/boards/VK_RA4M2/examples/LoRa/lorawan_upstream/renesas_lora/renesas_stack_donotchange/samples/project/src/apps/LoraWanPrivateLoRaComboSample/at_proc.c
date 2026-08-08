/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

/*
 * at_proc.c
 *
 *  Created on: 2017/07/21
 *      Author:
 */
#include <stdio.h>
#include "board.h"
#include "lora_sample.h"
#include "at_proc.h"
#include "lorawan_proc.h"
#if defined(APP_COMPLIANCE)
#include "app_compliance.h"
#endif
#include "privatelora_sample.h"
#include "at_commandtable.h"    // Command table

/* macro    */
#define APP_AT_MAX_RX1DELAY_DEC_SIZE        5
#define APP_AT_MAX_RX1DELAY                 50000
#define APP_AT_MIN_RX1DELAY                 500

#define APP_AT_MAX_DR_DEC_SIZE              2
#define APP_AT_MAX_DR                       15

#define APP_AT_MAX_FPORT_DEC_SIZE           3
#define APP_AT_MIN_FPORT                    1
#define APP_AT_MAX_FPORT                    224

#define APP_AT_MAX_CH_SIZE                  2
#define APP_AT_MIN_CH                       0
#define APP_AT_MAX_CH                       15

#define APP_AT_MAX_FREQ_SIZE                10
#define APP_AT_MIN_FREQ                     0
#define APP_AT_MAX_FREQ                     1000000000

#define APP_AT_MAX_PNGSL_PERIOD_DEC_SIZE    1
#define APP_AT_MAX_PNGSL_PERIOD             7

#define APP_AT_MAX_GROUPID_SIZE             1
#define APP_AT_MIN_GROUPID                  0
#define APP_AT_MAX_GROUPID                  3

#define APP_AT_MAX_NBTRANS_DEC_SIZE         1
#define APP_AT_MIN_NBTRANS                  1
#define APP_AT_MAX_NBTRANS                  15

#define APP_AT_MAX_TXPOWER_DEC_SIZE         2
#define APP_AT_MIN_TXPOWER                  0
#define APP_AT_MAX_TXPOWER                  15

#define APP_AT_MAX_MAXDCYCLE_DEC_SIZE       1
#define APP_AT_MIN_MAXDCYCLE                0
#define APP_AT_MAX_MAXDCYCLE                15

#define APP_AT_MAX_RX1DROFFSET_DEC_SIZE     1
#define APP_AT_MIN_RX1DROFFSET              0
#define APP_AT_MAX_RX1DROFFSET              7

#define APP_AT_MAX_RX2FREQ_DEC_SIZE         9

#define APP_AT_MAX_MAXEIRP_DEC_SIZE         2

#if defined(DEBUG_LORAMAC)
#define APP_LEN_DEBUG_LORAMAC_MODE      4       // byte
#endif

// LoRaMacEventInfoStatus message table
const char *const appAtLoRaMacEventInfoStatusMsg[ MAXNUM_LORAMAC_EVENT_INFO_STATUS + 1 ] =
{
    "STATUS_OK",                      // LORAMAC_EVENT_INFO_STATUS_OK
    "ERROR",                          // LORAMAC_EVENT_INFO_STATUS_ERROR
    "TX_TIMEOUT",                     // LORAMAC_EVENT_INFO_STATUS_TX_TIMEOUT
    "RX1_TIMEOUT",                    // LORAMAC_EVENT_INFO_STATUS_RX1_TIMEOUT
    "NO_RESPONSE",                    // LORAMAC_EVENT_INFO_STATUS_RX2_TIMEOUT
    "RX1_ERROR",                      // LORAMAC_EVENT_INFO_STATUS_RX1_ERROR
    "RX2_ERROR",                      // LORAMAC_EVENT_INFO_STATUS_RX2_ERROR
    "JOIN_FAIL",                      // LORAMAC_EVENT_INFO_STATUS_JOIN_FAIL
    "JOIN_NONCE_FAIL",                // LORAMAC_EVENT_INFO_STATUS_JOIN_NONCE_FAIL
    "DOWNLINK_REPEATED",              // LORAMAC_EVENT_INFO_STATUS_DOWNLINK_REPEATED
    "TX_DR_PAYLOAD_SIZE_ERROR",       // LORAMAC_EVENT_INFO_STATUS_TX_DR_PAYLOAD_SIZE_ERROR
 #if (LORAMAC_VERSION < LORAWAN_VERSION_1_0_4)  // LW1.0.3
    "DOWNLINK_TOO_MANY_FRAMES_LOSS",  // LORAMAC_EVENT_INFO_STATUS_DOWNLINK_TOO_MANY_FRAMES_LOSS
 #endif
    "ADDRESS_FAIL",                   // LORAMAC_EVENT_INFO_STATUS_ADDRESS_FAIL
    "MIC_FAIL",                       // LORAMAC_EVENT_INFO_STATUS_MIC_FAIL
    "MULTICAST_FAIL",                 // LORAMAC_EVENT_INFO_STATUS_MULTICAST_FAIL
    "BEACON_LOCKED",                  // LORAMAC_EVENT_INFO_STATUS_BEACON_LOCKED
    "BEACON_LOST",                    // LORAMAC_EVENT_INFO_STATUS_BEACON_LOST
    "BEACON_NOT_FOUND",               // LORAMAC_EVENT_INFO_STATUS_BEACON_NOT_FOUND
    "UNKONWN_EVENT_INFO_STATUS",      // (unknown)
};

// LoRaMacStatus message table
const char *const appAtLoRaMacStatusMsg[ MAXNUM_LORAMAC_STATUS + 1 ] =
{
    "STATUS_OK",                   // LORAMAC_STATUS_OK
    "BUSY",                        // LORAMAC_STATUS_BUSY
    "SERVICE_UNKNOWN",             // LORAMAC_STATUS_SERVICE_UNKNOWN
    "PARAMETER_INVALID",           // LORAMAC_STATUS_PARAMETER_INVALID
    "FREQUENCY_INVALID",           // LORAMAC_STATUS_FREQUENCY_INVALID
    "DATARATE_INVALID",            // LORAMAC_STATUS_DATARATE_INVALID
    "FREQ_AND_DR_INVALID",         // LORAMAC_STATUS_FREQ_AND_DR_INVALID
    "NO_NETWORK_JOINED",           // LORAMAC_STATUS_NO_NETWORK_JOINED
    "LENGTH_ERROR",                // LORAMAC_STATUS_LENGTH_ERROR
    "REGION_NOT_SUPPORTED",        // LORAMAC_STATUS_REGION_NOT_SUPPORTED
    "SKIPPED_APP_DATA",            // LORAMAC_STATUS_SKIPPED_APP_DATA
    "DUTYCYCLE_RESTRICTED",        // LORAMAC_STATUS_DUTYCYCLE_RESTRICTED
    "NO_CHANNEL_FOUND",            // LORAMAC_STATUS_NO_CHANNEL_FOUND
    "NO_FREE_CHANNEL_FOUND",       // LORAMAC_STATUS_NO_FREE_CHANNEL_FOUND
#ifdef LORAMAC_CLASSB_ENABLED
    "BUSY_BEACON_RESERVED_TIME",   // LORAMAC_STATUS_BUSY_BEACON_RESERVED_TIME
    "BUSY_PING_SLOT_WINDOW_TIME",  // LORAMAC_STATUS_BUSY_PING_SLOT_WINDOW_TIME
    "BUSY_UPLINK_COLLISION",       // LORAMAC_STATUS_BUSY_UPLINK_COLLISION
    "CLASS_B_ERROR",               // LORAMAC_STATUS_CLASS_B_ERROR
#endif
    "CRYPTO_ERROR",                // LORAMAC_STATUS_CRYPTO_ERROR
    "FCNT_HANDLER_ERROR",          // LORAMAC_STATUS_FCNT_HANDLER_ERROR
    "MAC_COMMAD_ERROR",            // LORAMAC_STATUS_MAC_COMMAD_ERROR
    "CONFIRM_QUEUE_ERROR",         // LORAMAC_STATUS_CONFIRM_QUEUE_ERROR
#if (LORAMAC_MAX_MC_CTX > 0)
    "MC_GROUP_UNDEFINED",          // LORAMAC_STATUS_MC_GROUP_UNDEFINED
#endif
    "MAC_ERROR",                   // LORAMAC_STATUS_ERROR
    "RADIO_FAIL",                  // LORAMAC_STATUS_RADIO_FAIL
    "RADIO_PARAMETER_INVALID",     // LORAMAC_STATUS_RADIO_PARAMETER_INVALID
    "UNKNOWN_STATUS",              // (unknown)
};

// string buffer
#ifndef APP_AT_OCT_BUFF_SIZE
#define APP_AT_OCT_BUFF_SIZE    512
#endif
uint8_t appAtOctBuff[APP_AT_OCT_BUFF_SIZE];

// index of AT command of data
uint16_t appAtLastAtCmdIndexData = 0;

// type of last message sent from application
uint16_t appAtLastSentMessage = APP_AT_LAST_SENT_MESSAGE_DATA;

// handle of mlme request and confirm
uint8_t appAtMlmeHandle = APP_AT_MLME_HANDLE_NONE;

// prototypes

/* tools prototype  */
uint16_t AppAtHexStr2Hex(int8_t *);
AtResultCode_t AppAtHexStr2HexDataArrayWithPadding(uint8_t *hexDataArray, uint16_t hexDataArrayLen, int8_t *hexStr, uint16_t hexStrLen);
AtResultCode_t AppAtHexStr2HexDataArray(uint8_t *hexDataArray, int8_t *hexStr, uint16_t hexStrLen);
int32_t AppAtStr2Dec(int8_t *str, uint8_t len);
int8_t AtRespFormat(void);
uint16_t AtCmdGetCurrentCmd(void);

/* global variables */


/*!
 * AT command initialization
 */
void AppAtInit(void)
{
    uint8_t             loraMode;

    /* disable UART interrupt    */
    AtDeInitUart();


    loraMode = AppGetLoRaMode();
    switch( loraMode )
    {
        /* init AT for LoRaWAN */
        case APP_LORA_MODE_LORAWAN:
#if (AT_EXTCMD_TAB_ARRAYSIZE == 0)
            /* command buffer and at init   */
            AtInit(appAtOctBuff, APP_AT_OCT_BUFF_SIZE,
                (AtExtendTab_t *)AppAtCommands,
                sizeof(AppAtCommands) / sizeof(AtExtendTab_t));

#else
            /* command buffer and at init   */
            AtInit(appAtOctBuff, APP_AT_OCT_BUFF_SIZE);

            /* register extended commands   */
            AtExtendCmdRegist("+RESET", AppAtResetAct, NULL, NULL);
            AtExtendCmdRegist("+VER", NULL, AppAtVerRead, NULL);
            AtExtendCmdRegist("+SAVE", NULL, NULL, AppAtSaveAct);
            AtExtendCmdRegist("+LOAD", AppAtLoadAct, NULL, AppAtLoadAct);
            AtExtendCmdRegist("+DEVEUI", AppAtDevEUISet, AppAtDevEUIRead, NULL);
            AtExtendCmdRegist("+CLASS", AppAtClassSet, AppAtClassRead, NULL);
            AtExtendCmdRegist("+DEVADDR", AppAtDevAddrSet, AppAtDevAddrRead, NULL);
            AtExtendCmdRegist("+NETID", AppAtNetIDSet, AppAtNetIDRead, NULL);
            AtExtendCmdRegist("+APPEUI", AppAtAppEUISet, AppAtAppEUIRead, NULL);
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
            AtExtendCmdRegist("+NWKSKEY", AppAtNwkSKeySet, AppAtNwkSKeyRead, NULL);
            AtExtendCmdRegist("+APPSKEY", AppAtAppSKeySet, AppAtAppSKeyRead, NULL);
#endif
            AtExtendCmdRegist("+APPKEY", AppAtAppKeySet, AppAtAppKeyRead, NULL);
            AtExtendCmdRegist("+ACTMODE", AppAtActModeSet, AppAtActModeRead, NULL);
            AtExtendCmdRegist("+SEND", AppAtSendAct, NULL, NULL);
            AtExtendCmdRegist("+MTYPE", AppAtMtypeSet, AppAtMtypeRead, NULL);
            AtExtendCmdRegist("+JOIN", AppAtJoinAct, NULL, AppAtJoinAct);
            AtExtendCmdRegist("+REGION", AppAtRegionSet, AppAtRegionRead, NULL);
            AtExtendCmdRegist("+SENDHEX", AppAtSendHexAct, NULL, NULL);
            AtExtendCmdRegist("+ADR", AppAtAdrSet, AppAtAdrRead, NULL);
            AtExtendCmdRegist("+RSSI", AppAtRssiSet, AppAtRssiRead, NULL);
            AtExtendCmdRegist("+RX1DELAY", AppAtRx1DelaySet, AppAtRx1DelayRead, NULL);
            AtExtendCmdRegist("+DR", AppAtDRSet, AppAtDRRead, NULL);
            AtExtendCmdRegist("+LINKCHK", NULL, NULL, AppAtLinkchkAct);
            AtExtendCmdRegist("+FPORT", AppAtFPortSet, AppAtFPortRead, NULL);
            AtExtendCmdRegist("+DCYCLE", AppAtDCycleSet, AppAtDCycleRead, NULL);
            AtExtendCmdRegist("+DEVTIME", NULL, NULL, AppAtDevTimeAct);
#ifdef LORAMAC_CLASSB_ENABLED
            AtExtendCmdRegist("+BCONACQ", NULL, NULL, AppAtBconAcqAct);
            AtExtendCmdRegist("+PNGSLINFO", NULL, NULL, AppAtPngSlInfoAct);
            AtExtendCmdRegist("+BCONTIM", NULL, NULL, AppAtBconTimAct);
            AtExtendCmdRegist("+PNGSLPERIOD", AppAtPngSlPeriodSet, AppAtPngSlPeriodRead, NULL);
#endif
#if (LORAMAC_MAX_MC_CTX > 0)
            AtExtendCmdRegist("+GENAPPKEY", AppAtGenAppKeySet, AppAtGenAppKeyRead, NULL);
#endif
            AtExtendCmdRegist("+CHDEFMASK", AppAtChannelsDefaultMaskSet, AppAtChannelsDefaultMaskRead, NULL);
            AtExtendCmdRegist("+DEVNONCE", AppAtDevNonceSet, AppAtDevNonceRead, NULL);
            AtExtendCmdRegist("+APPNONCE", AppAtAppNonceSet, AppAtAppNonceRead, NULL);
            AtExtendCmdRegist("+DOWNFCNT", AppAtDownlinkFCntSet, AppAtDownlinkFCntRead, NULL);
            AtExtendCmdRegist("+UPFCNT", AppAtUplinkFCntSet, AppAtUplinkFCntRead, NULL);
#if defined(DEBUG_LORAMAC)
            AtExtendCmdRegist("+DEBUG", AppAtDebugSet, AppAtDebugRead, NULL);
#endif

    // Experimental
#if defined(DEBUG_AT_COMMAND_EXPERIMENTAL)
            AtExtendCmdRegist("+CH", NULL, AppAtAppChannelsRead, NULL);
            AtExtendCmdRegist("+CHMASK", NULL, AppAtAppChannelMaskRead, NULL);
            AtExtendCmdRegist("+NBTRANS", AppAtNbTransSet, AppAtNbTransRead, NULL);
            AtExtendCmdRegist("+TXPOWER", AppAtTxPowerSet, AppAtTxPowerRead, NULL);
            AtExtendCmdRegist("+MAXDCYCLE", AppAtMaxDCycleSet, AppAtMaxDCycleRead, NULL);
            AtExtendCmdRegist("+RX1DROFFSET", AppAtRx1DrOffsetSet, AppAtRx1DrOffsetRead, NULL);
            AtExtendCmdRegist("+RX2FREQ", AppAtRx2FreqSet, AppAtRx2FreqRead, NULL);
            AtExtendCmdRegist("+RX2DR", AppAtRx2DrSet, AppAtRx2DrRead, NULL);
            AtExtendCmdRegist("+MAXEIRP", AppAtMaxEirpSet, AppAtMaxEirpRead, NULL);
            AtExtendCmdRegist("+DOWNDWELL", AppAtDownlinkDwellTimeSet, AppAtDownlinkDwellTimeRead, NULL);
            AtExtendCmdRegist("+UPDWELL", AppAtUplinkDwellTimeSet, AppAtUplinkDwellTimeRead, NULL);
            AtExtendCmdRegist("+PNGSLDR", AppAtPngSlDrSet, AppAtPngSlDrRead, NULL);
#endif
#if defined(APP_COMPLIANCE)
            AppComplianceAtExtendCmdRegist();
#endif
            // PrivateLoRa/LoRaWAN
            AtExtendCmdRegist("+LORAMODE", AppAtLoRaModeSet, AppAtLoRaModeRead, NULL);
#endif
            break;

        /* init AT for PrivateLoRa */
        case APP_LORA_MODE_PRIVATELORA:
            AppAtPrvLoRaAtInit( appAtOctBuff, (int16_t)APP_AT_OCT_BUFF_SIZE );
            break;

        default:
            break;
    }

    /* enable UART interrupt    */
    AtInitUart();
}

/**
 * @fn
 * print result code for LoRaMacEventInfoStatus
 * @param status    LoRaMacEventInfoStatus
 */
void AppAtMacEventResult(LoRaMacEventInfoStatus_t status)
{
    const char  *p_statusMsg;

    // this function is called when status in not LORAMAC_EVENT_INFO_STATUS_OK
    if( status != LORAMAC_EVENT_INFO_STATUS_OK )
    {
        if( status < MAXNUM_LORAMAC_EVENT_INFO_STATUS )
        {
            p_statusMsg = appAtLoRaMacEventInfoStatusMsg[ status ];
        }
        else
        {
            p_statusMsg = appAtLoRaMacEventInfoStatusMsg[ MAXNUM_LORAMAC_EVENT_INFO_STATUS ];
        }

        AppAtOutputResultCode( (char *)p_statusMsg );
    }
}

void AppAtMacStatusResult(LoRaMacStatus_t status)
{
    const char  *p_statusMsg;

    // this function is called when status in not LORAMAC_STATUS_OK
    if( status != LORAMAC_STATUS_OK )
    {
        if( status < MAXNUM_LORAMAC_STATUS )
        {
            p_statusMsg = appAtLoRaMacStatusMsg[ status ];
        }
        else
        {
            p_statusMsg = appAtLoRaMacStatusMsg[ MAXNUM_LORAMAC_STATUS ];
        }

        AppAtOutputResultCode( (char *)p_statusMsg );
    }
}

/*!
 * @fn
 * get the top address for string data
 * @param str   string argument
 * @return  top address for the string data, NULL if some error, *len == 0 if no string
 */
int8_t *AppAtGetString(int8_t *str, int16_t *len)
{
    int8_t *ret = NULL;
    int16_t i;

    if (*len < 2) {
        // error: at least two double quotes exist.
        return NULL;
    }

    // first character must be double quote.
    if (str[0] == '\"') {
        ret = &(str[1]);

        for (i = 1 ; i < *len ; i++) {
            if (str[i] == '\"') {
                if (i < *len - 1) {
                    // some characters exist after double quote
                    return NULL;
                }
                else {
                    // normal string
                    *len = i - 1;
                    return ret;
                }
            }
        }
    }

    /* no string or illegal format  */
    return NULL;
}

/*!
 * @fn
 * convert 2 bytes hex string to hex value
 * string other than '0'-'9' or 'a'-'f' or 'A'-'F' is converted to 0
 * @param hexStr    hex string (2 bytes. ex.1f)
 * @return  hexadecimal value
 */
uint16_t AppAtHexStr2Hex(int8_t *hexStr)
{
    uint16_t hex = 0x0000;
    uint8_t i, j;

    for (i = 0 ; i < 2 ; i++) {
        hex <<= 4;
        j = hexStr[i];
        if (j >= '0' && j <= '9') {
            j -= '0';
        }
        else if (j >= 'a' && j <= 'f') {
            j = j - 'a' + 0x0a;
        }
        else if (j >= 'A' && j <= 'F') {
            j = j - 'A' + 0x0a;
        }
        else {
            hex = 0xffff;
            break;
        }

        hex |= j;
    }

    return hex;
}

/*!
 * @fn
 * convert array of hexadecimal values to hexadecimal string
 * @param hexStr    destination hexadecimal string
 * @param hexArray  source array of hexadecimal values
 * @param len       length of the source array
 */
void AppAtHexArray2HexStr(uint8_t *hexStr, uint8_t *hexArray, uint16_t len)
{
    uint16_t i, j;

    j = 0;
    for (i = 0 ; i < len ; i++) {
        uint8_t k;

        k = (hexArray[i] & 0xf0) >> 4;
        hexStr[j++] = (k >= 10) ? ((k - 10) + 'A') : (k + '0');
        k = (hexArray[i] & 0x0f);
        hexStr[j++] = (k >= 10) ? ((k - 10) + 'A') : (k + '0');
    }

    hexStr[j] = '\0';
}

/*!
 * @fn
 * convert hex string to hex data array. if length of hex string is less than expected length,
 * character '0's are used for padding.
 * ex. "0A" -> { 0x00, 0x00, 0x00, 0x0A } in case length of hex data is 4 bytes.
 * @param hexDataArray  pointer to hex data array
 * @param hexDataArrayLen   length of hex data array
 * @param hexStr        pointer to hex string
 * @param hexStrLen     length of hex string
 * @return convert result
 */
AtResultCode_t AppAtHexStr2HexDataArrayWithPadding(uint8_t *hexDataArray, uint16_t hexDataArrayLen, int8_t *hexStr, uint16_t hexStrLen)
{
    int16_t i, j, k, padLen;
    int8_t hexStrTmp[2];
    uint16_t hexDataTmp;
    AtResultCode_t ret = AT_RC_OK;  /* result code */

    if ((hexStrLen == 0) || ((hexDataArrayLen * 2) < hexStrLen)) {
        ret = AT_RC_ERR;
    }
    else {
        padLen = (hexDataArrayLen * 2) - hexStrLen;     // length of padding charactor '0'

        for (i = 0, j = 0 ; i < hexDataArrayLen; i++) {
            for (k = 0; k < 2; k++) {
                if (padLen) {
                    hexStrTmp[k] = '0';
                    padLen--;
                }
                else {
                    hexStrTmp[k] = hexStr[j++];
                }
            }
            hexDataTmp = AppAtHexStr2Hex(hexStrTmp);
            if (hexDataTmp != 0xffff) {
                hexDataArray[i] = (uint8_t)(hexDataTmp & 0x00ff);
            }
            else {
                // invalid character
                ret = AT_RC_ERR;
                break;
            }
        }
    }

    return ret;
}

/*!
 * @fn
 * convert hex string to hex array
 * ex. "0A0B1E1F" -> { 0x0A, 0x0B, 0x1E, 0x1F }
 * @param hexDataArray  buffer array to store hex values
 * @param hexStr    hex string
 * @param hexStrlen length of array of hex string (length of hexStr is 2x)
 * @return convert result
 */
AtResultCode_t AppAtHexStr2HexDataArray(uint8_t *hexDataArray, int8_t *hexStr, uint16_t hexStrLen)
{
    uint16_t i, j;
    uint16_t hexDataLen;
    AtResultCode_t ret = AT_RC_OK;

    if (((hexStrLen / 2) == 0) || ((hexStrLen % 2) != 0)) {
        ret = AT_RC_ERR;
    }
    else {
        hexDataLen = hexStrLen / 2;
        for (i = 0 ; i < hexDataLen ; i++) {
            j = AppAtHexStr2Hex(&(hexStr[i * 2]));
            if (j != 0xffff) {
                hexDataArray[i] = (uint8_t)(j & 0x00ff);
            }
            else {
                ret = AT_RC_ERR;
                break;
            }
        }
    }

    return ret;
}

/*!
 * @fn
 * convert numeric string to number
 * @param str   numeric string
 * @param len   length of the numeric string
 * @return  decimal number, -1: error
 */
int32_t AppAtStr2Dec(int8_t *str, uint8_t len)
{
    int32_t ret = 0;
    uint8_t i, j;

    if (len == 0)
    {
        ret = -1;
    }
    else {
        for (i = 0 ; i < len ; i++) {
            j = str[i];
            ret *= 10;
            if ((j >= '0') && (j <= '9')) {     // j is not decimal
                ret += j - '0';
            }
            else {
                ret = -1;
                break;
            }
        }
    }

    return ret;
}

/*!
 * \brief print the header of extended at-command function specified in argument
 */
void AtPrintCmdHeader(char *cmdName)
{
    if (AtRespFormat()) {
        print((char *)AT_RCS_CRLF);
    }
    print((char *)cmdName);
    print((char *)":");
}

void AppAtOutputResultCode(char *result)
{
    AtPrintHeader();
    print(" ");
    print(result);
    AtPrintTrailer();
}

void AppAtOutputResultDec(long num, unsigned char len, unsigned char supCh)
{
    AtPrintHeader();
    print(" ");
    print_dec(num, len, supCh);
    AtPrintTrailer();
}

AtResultCode_t AppAtResetAct(void *p)
{
    uint8_t argc;                                   /* number of argument   */
    int8_t *argv;                                   /* argument             */
    int16_t argvLen;
    AtResultCode_t ret = AT_RC_ERR;                 /* result code          */
    uint8_t mode;
    LoRaMacRegion_t region;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &argvLen);
        if (argvLen == 1) {
            mode = AppAtStr2Dec(argv, argvLen);
            if ((mode == 0) || (mode == 1) || (mode == 7)) {
                ret = AT_RC_OK;
            }
        }
    }

    if (ret == AT_RC_OK) {
        if (mode == 0) {    // mode : 0 = reset LoRaWAN stack only
            region = AppLoraWanGetRegion();
            AppLoraWanSetRegion( region );
        }
        else {         // mode : 1 = S/W reset, 7 = S/W reset with data flash initialization
            print("OK");
            AtPrintTrailer();

            if (mode == 7) {
                AppFactoryResetParams();
                AppPrvLoRaMainFactoryResetParams( true );  // must be true
            }

            BoardResetMcu();
        }
    }

    return ret;
}

AtResultCode_t AppAtVerRead(void *p)
{
    AtPrintHeader();
    print(" LoRa Sample App Ver.");
    print_hex(AppFwVersion.Fields.Major, 2);
    print(".");
    print_hex(AppFwVersion.Fields.Minor, 2);
    AtPrintTrailer();

    return AT_RC_OK;
}

AtResultCode_t AppAtSaveAct(void *p)
{
    AppSaveParams();

    return AT_RC_OK;
}

AtResultCode_t AppAtLoadAct(void *p)
{
    uint8_t argc;                                   /* number of argument   */
    int8_t *argv;                                   /* argument             */
    int16_t argvLen;
    AtResultCode_t ret = AT_RC_ERR;                 /* result code          */
    uint8_t mode;
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 0) {
        mode = 0;
        ret = AT_RC_OK;
    }
    else if (argc == 1) {
        AtParsePopOne(&argv, &argvLen);
        if (argvLen == 1) {
            mode = AppAtStr2Dec(argv, argvLen);
            if ((mode == 0) || (mode == 1)) {
                ret = AT_RC_OK;
            }
        }
    }

    if (ret == AT_RC_OK) {
        AppLoadParams(mode);    // mode : 0: load from data flash, 1: set default settings

        status = AppLoraWanInit(NULL, NULL);  /* init LoRaWan     */
        if (status != LORAMAC_STATUS_OK) {
            AppAtMacStatusResult(status);
            ret = AT_RC_ERR;
        }
    }

    return ret;
}

AtResultCode_t AppAtJoinAct(void *p)
{
    uint8_t argc;                               /* number of argument   */
    AtResultCode_t ret = AT_RC_ERR;             /* result code  */
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 0) {
        status = AppJoinReq();
        if (status == LORAMAC_STATUS_OK) {
            appAtLastSentMessage = APP_AT_LAST_SENT_MESSAGE_JOIN;
            ret = AT_RC_OK;
        }
        else {
            AppAtMacStatusResult(status);
        }
    }

    return ret;
}

AtResultCode_t AppAtSendAct(void *p)
{
    uint8_t argc;
    int8_t *argv;
    uint8_t *data;
    uint8_t len;
    uint8_t fport = AppLoraWanGetFPort();
    Mcps_t mtype = AppLoraWanGetMessageType();
    AtResultCode_t ret = AT_RC_ERR;
    int16_t argv_len;
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1)
    {
        AtParsePopOne(&argv, &argv_len);
        data = (uint8_t *)AppAtGetString(argv, &argv_len);
        if ((data != NULL) && (argv_len != 0)) {
            len = (uint8_t)(argv_len & 0x00ff);

            status = AppLoraWanSendData(data, len, fport, mtype, NULL);
            if (status == LORAMAC_STATUS_OK) {
                ret = AT_RC_OK;
                // save current AT command index and message type
                appAtLastAtCmdIndexData = AtCmdGetCurrentCmd();
                appAtLastSentMessage = APP_AT_LAST_SENT_MESSAGE_DATA;
            }
            else {
                AppAtMacStatusResult(status);
            }
        }
    }

    return ret;
}

/*!
 * @fn
 * AT+MTYPE: set message type
 * @param
 * @return  result code (AT_RC_OK or AT_RC_ERROR)
 */
AtResultCode_t AppAtMtypeSet(void *p)
{
    uint8_t argc;                                   /* number of argument   */
    int8_t *argv;                                   /* argument             */
    uint8_t mtype;                                  /* message type         */
    AtResultCode_t ret = AT_RC_ERR;                 /* result code          */
    int16_t len;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);
        if (len == 1) {
            mtype = AppAtStr2Dec(argv, len);

            switch (mtype) {
                case 0:     /* UnConfirmed  */
                    AppLoraWanSetMessageType(MCPS_UNCONFIRMED);
                    ret = AT_RC_OK;
                    break;
                case 1:     /* Confirmed    */
                    AppLoraWanSetMessageType(MCPS_CONFIRMED);
                    ret = AT_RC_OK;
                    break;
                default:
                    break;
            }
        }
    }

    return ret;
}

/*!
 * @fn
 * AT+MTYPE: read message type
 * @param
 * @return  result code (AT_RC_OK or AT_RC_ERROR)
 */
AtResultCode_t AppAtMtypeRead(void *p)
{
    Mcps_t mtype;
    AtResultCode_t ret = AT_RC_ERR;

    mtype = AppLoraWanGetMessageType();
    switch (mtype) {
        case MCPS_UNCONFIRMED:
            AppAtOutputResultCode("0:UNCONFIRMED_DATA");
            ret = AT_RC_OK;
            break;
        case MCPS_CONFIRMED:
            AppAtOutputResultCode("1:CONFIRMED_DATA");
            ret = AT_RC_OK;
            break;
        default:
            // in case parameter stored in data flash is wrong
            break;
    }

    return ret;
}

AtResultCode_t AppAtDevEUISet(void *p)
{
    uint8_t argc;                               /* number of arguments  */
    int8_t *argv;                                   /* argument             */
    int16_t argvLen;                            /* length of an argument */
    uint8_t devEui[APP_LORAWAN_LEN_DEVEUI];     /* DevEUI               */
    AtResultCode_t ret = AT_RC_ERR;                 /* result code          */

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &argvLen);
        ret = AppAtHexStr2HexDataArrayWithPadding(devEui, APP_LORAWAN_LEN_DEVEUI, argv, argvLen);
        if (ret == AT_RC_OK) {
            AppLoraWanSetDevEUI(devEui);
        }
    }

    return ret;
}

AtResultCode_t AppAtDevEUIRead(void *p)
{
    uint8_t devEui[APP_LORAWAN_LEN_DEVEUI];
    uint8_t devEuiStr[APP_LORAWAN_LEN_DEVEUI * 2 + 1];

    AppLoraWanGetDevEUI(devEui);
    AppAtHexArray2HexStr(devEuiStr, devEui, APP_LORAWAN_LEN_DEVEUI);

    AppAtOutputResultCode((char *)devEuiStr);

    return AT_RC_OK;
}

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
AtResultCode_t AppAtDevAddrSet(void *p)
{
    uint8_t argc;                                       /* number of arguments  */
    int8_t *argv;                                       /* argument             */
    int16_t argvLen;                                    /* length of an argument */
    uint32_t devAddr;                                   /* DevAddr              */
    uint8_t tmpDevAddr[APP_LORAWAN_LEN_DEVADDR];        /* temporary DevAddr    */
    AtResultCode_t ret = AT_RC_ERR;                     /* result code          */
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &argvLen);

        ret = AppAtHexStr2HexDataArrayWithPadding(tmpDevAddr, APP_LORAWAN_LEN_DEVADDR, argv, argvLen);
        if (ret == AT_RC_OK) {
            devAddr = ((uint32_t)tmpDevAddr[0] << 24) | ((uint32_t)tmpDevAddr[1] << 16)
                      | ((uint32_t)tmpDevAddr[2] << 8) | (uint32_t)tmpDevAddr[3];
            status = AppLoraWanSetDevAddr(devAddr);
            if (status != LORAMAC_STATUS_OK) {
                AppAtMacStatusResult(status);
                ret = AT_RC_ERR;
            }
        }
    }

    return ret;
}
#endif

AtResultCode_t AppAtDevAddrRead(void *p)
{
    uint32_t devAddr, shift = 24;
    uint8_t devAddrArray[APP_LORAWAN_LEN_DEVADDR];
    uint8_t devAddrStr[APP_LORAWAN_LEN_DEVADDR * 2 + 1];
    uint8_t i;

    devAddr = AppLoraWanGetDevAddr();
    for (i = 0 ; i < APP_LORAWAN_LEN_DEVADDR ; i++) {
        devAddrArray[i] = (uint8_t)( devAddr >> shift );
        shift -= 8;
    }
    AppAtHexArray2HexStr(devAddrStr, devAddrArray, APP_LORAWAN_LEN_DEVADDR);

    AppAtOutputResultCode((char *)devAddrStr);

    return AT_RC_OK;
}

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
/*!
 * @fn
 * AT+NETID: set NetID
 */
AtResultCode_t AppAtNetIDSet(void *p)
{
    uint8_t argc;                                       /* number of arguments  */
    int8_t *argv;                                       /* argument             */
    int16_t argvLen;                                    /* length of an argument */
    uint32_t netID;                                     /* NetID                */
    uint8_t tmpNetID[APP_LORAWAN_LEN_NETID];            /* temporary NedID      */
    AtResultCode_t ret = AT_RC_ERR;                     /* result code          */
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &argvLen);

        ret = AppAtHexStr2HexDataArrayWithPadding(tmpNetID, APP_LORAWAN_LEN_NETID, argv, argvLen);
        if (ret == AT_RC_OK) {
            netID = ((uint32_t)tmpNetID[0] << 16) | ((uint32_t)tmpNetID[1] << 8) | (uint32_t)tmpNetID[2];

            status = AppLoraWanSetNetID(netID);
            if (status != LORAMAC_STATUS_OK) {
                AppAtMacStatusResult(status);
                ret = AT_RC_ERR;
            }
        }
    }

    return ret;
}
#endif

/*!
 * @fn
 * AT+NETID: read NetID
 */
AtResultCode_t AppAtNetIDRead(void *p)
{
    uint32_t netId, shift = 16;
    uint8_t netIdArray[APP_LORAWAN_LEN_NETID];
    uint8_t netIdStr[APP_LORAWAN_LEN_NETID * 2 + 1];
    uint8_t i;

    netId = AppLoraWanGetNetID();
    for (i = 0 ; i < APP_LORAWAN_LEN_NETID ; i++) {
        netIdArray[i] = (uint8_t)( netId >> shift );
        shift -= 8;
    }
    AppAtHexArray2HexStr(netIdStr, netIdArray, APP_LORAWAN_LEN_NETID);

    AppAtOutputResultCode((char *)netIdStr);

    return AT_RC_OK;
}


AtResultCode_t AppAtAppEUISet(void *p)
{
    uint8_t argc;                                       /* number of arguments  */
    int8_t *argv;                                       /* argument             */
    int16_t argvLen;                                    /* length of an argument */
    uint8_t appEUI[APP_LORAWAN_LEN_APPEUI];             /* AppEUI               */
    AtResultCode_t ret = AT_RC_ERR;                     /* result code          */

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &argvLen);

        ret = AppAtHexStr2HexDataArrayWithPadding(appEUI, APP_LORAWAN_LEN_APPEUI, argv, argvLen);
        if (ret == AT_RC_OK) {
            AppLoraWanSetAppEUI(appEUI);
        }
    }

    return ret;
}

AtResultCode_t AppAtAppEUIRead(void *p)
{
    uint8_t appEui[APP_LORAWAN_LEN_APPEUI];
    uint8_t appEuiStr[APP_LORAWAN_LEN_APPEUI * 2 + 1];

    AppLoraWanGetAppEUI(appEui);
    AppAtHexArray2HexStr(appEuiStr, appEui, APP_LORAWAN_LEN_APPEUI);

    AppAtOutputResultCode((char *)appEuiStr);

    return AT_RC_OK;
}

AtResultCode_t AppAtAppKeySet(void *p)
{
    uint8_t argc;                                   /* number of arguments  */
    int8_t *argv;                                       /* argument             */
    int16_t argvLen;                                /* length of an argument */
    uint8_t appKey[APP_LORAWAN_LEN_APPKEY];         /* AppKey               */
    AtResultCode_t ret = AT_RC_ERR;                     /* result code          */

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &argvLen);

        ret = AppAtHexStr2HexDataArrayWithPadding(appKey, APP_LORAWAN_LEN_APPKEY, argv, argvLen);
        if (ret == AT_RC_OK) {
            AppLoraWanSetAppKey(appKey);
        }
    }

    return ret;
}

AtResultCode_t AppAtAppKeyRead(void *p)
{
#if defined(APP_AT_KEY_READ_ENABLED)

    uint8_t appKey[APP_LORAWAN_LEN_APPKEY];
    uint8_t appKeyStr[APP_LORAWAN_LEN_APPKEY * 2 + 1];

    AppLoraWanGetAppkey(appKey);
    AppAtHexArray2HexStr(appKeyStr, appKey, APP_LORAWAN_LEN_APPKEY);

    AppAtOutputResultCode((char *)appKeyStr);

    return AT_RC_OK;
#else

    return AT_RC_ERR;
#endif
}

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
AtResultCode_t AppAtNwkSKeySet(void *p)
{
    uint8_t argc;                                       /* number of argument   */
    int8_t *argv;                                       /* argument             */
    int16_t argvLen;                                /* length of an argument */
    uint8_t nwkSKey[APP_LORAWAN_LEN_NWKSKEY];       /* temporary NwkSKey    */
    AtResultCode_t ret = AT_RC_ERR;                     /* result code          */
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &argvLen);

        ret = AppAtHexStr2HexDataArrayWithPadding(nwkSKey, APP_LORAWAN_LEN_NWKSKEY, argv, argvLen);
        if (ret == AT_RC_OK) {
            status = AppLoraWanSetNwkSKey(nwkSKey);
            if (status != LORAMAC_STATUS_OK) {
                AppAtMacStatusResult(status);
                ret = AT_RC_ERR;
            }
        }
    }

    return ret;
}

AtResultCode_t AppAtNwkSKeyRead(void *p)
{
#if defined(APP_AT_KEY_READ_ENABLED)

    uint8_t nwkSKey[APP_LORAWAN_LEN_NWKSKEY];
    uint8_t nwkSKeyStr[APP_LORAWAN_LEN_NWKSKEY * 2 + 1];

    AppLoraWanGetNwkSKey(nwkSKey);
    AppAtHexArray2HexStr(nwkSKeyStr, nwkSKey, APP_LORAWAN_LEN_NWKSKEY);

    AppAtOutputResultCode((char *)nwkSKeyStr);

    return AT_RC_OK;
#else

    return AT_RC_ERR;
#endif
}

AtResultCode_t AppAtAppSKeySet(void *p)
{
    uint8_t argc;                                   /* number of arguments  */
    int8_t *argv;                                       /* argument             */
    int16_t argvLen;                                /* length of an argument */
    uint8_t appSKey[APP_LORAWAN_LEN_APPSKEY];       /* AppSKey  */
    AtResultCode_t ret = AT_RC_ERR;                     /* result code          */
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &argvLen);

        ret = AppAtHexStr2HexDataArrayWithPadding(appSKey, APP_LORAWAN_LEN_APPSKEY, argv, argvLen);
        if (ret == AT_RC_OK) {
            status = AppLoraWanSetAppSKey(appSKey);
            if (status != LORAMAC_STATUS_OK) {
                AppAtMacStatusResult(status);
                ret = AT_RC_ERR;
            }
        }
    }

    return ret;
}

AtResultCode_t AppAtAppSKeyRead(void *p)
{
#if defined(APP_AT_KEY_READ_ENABLED)

    uint8_t appSKey[APP_LORAWAN_LEN_APPSKEY];
    uint8_t appSKeyStr[APP_LORAWAN_LEN_APPSKEY * 2 + 1];

    AppLoraWanGetAppSKey(appSKey);
    AppAtHexArray2HexStr(appSKeyStr, appSKey, APP_LORAWAN_LEN_APPSKEY);

    AppAtOutputResultCode((char *)appSKeyStr);

    return AT_RC_OK;
#else

    return AT_RC_ERR;
#endif
}
#endif

AtResultCode_t AppAtActModeSet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_ERR;     /* result code          */
    int16_t len;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);
        if (len == 1) {
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
            if (argv[0] == '0') {
                AppLoraWanSetActMode(APP_LORAWAN_ACTMODE_ABP);
                ret = AT_RC_OK;
            }
            else
#endif
            if (argv[0] == '1') {
                AppLoraWanSetActMode(APP_LORAWAN_ACTMODE_OTAA);
                ret = AT_RC_OK;
            }
        }
    }

    return ret;
}

AtResultCode_t AppAtActModeRead(void *p)
{
    AtResultCode_t ret = AT_RC_OK;      /* result code          */
    uint8_t act;

    act = AppLoraWanGetActMode();
    if (act == APP_LORAWAN_ACTMODE_OTAA) {
        AppAtOutputResultCode("1:OTAA");
    }
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    else if (act == APP_LORAWAN_ACTMODE_ABP) {
        AppAtOutputResultCode("0:ABP");
    }
#endif
    else {
        // in case parameter stored in data flash is wrong
        ret = AT_RC_ERR;
    }

    return ret;
}

AtResultCode_t AppAtClassSet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_ERR;     /* result code          */
    int16_t len;
    DeviceClass_t class;
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);
        if (len == 1) {
            if (argv[0] == '0') {
                class = CLASS_A;
                ret = AT_RC_OK;
            }
            else if (argv[0] == '1') {
                class = CLASS_B;
                ret = AT_RC_OK;
            }
            else if (argv[0] == '2') {
                class = CLASS_C;
                ret = AT_RC_OK;
            }

            if (ret == AT_RC_OK) {
                status = AppLoraWanSetDeviceClass(class);
                if (status != LORAMAC_STATUS_OK) {
                    AppAtMacStatusResult(status);
                    ret = AT_RC_ERR;
                }
            }
        }
    }

    return ret;
}

AtResultCode_t AppAtClassRead(void *p)
{
    DeviceClass_t class;
    AtResultCode_t ret = AT_RC_OK;      /* result code          */

    class = AppLoraWanGetDeviceClass();
    if (class == CLASS_A) {
        AppAtOutputResultCode("0:CLASS_A");
    }
    else if (class == CLASS_B) {
        AppAtOutputResultCode("1:CLASS_B");
    }
    else if (class == CLASS_C) {
        AppAtOutputResultCode("2:CLASS_C");
    }
    else {
        // in case parameter stored in data flash is wrong
        ret = AT_RC_ERR;
    }

    return ret;
}

AtResultCode_t AppAtRegionSet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_OK;      /* result code          */
    int16_t len;
    uint8_t region;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);
        region = AppAtStr2Dec(argv, len);

        switch (region) {
#if defined(REGION_EU868)
            case 0:
                AppLoraWanSetRegion(LORAMAC_REGION_EU868);
                break;
#endif
#if defined(REGION_US915)
            case 1:
                AppLoraWanSetRegion(LORAMAC_REGION_US915);
                break;
#endif
#if defined(REGION_CN779)
            case 2:
                AppLoraWanSetRegion(LORAMAC_REGION_CN779);
                break;
#endif
#if defined(REGION_EU433)
            case 3:
                AppLoraWanSetRegion(LORAMAC_REGION_EU433);
                break;
#endif
#if defined(REGION_AU915)
            case 4:
                AppLoraWanSetRegion(LORAMAC_REGION_AU915);
                break;
#endif
#if defined(REGION_CN470)
            case 5:
                AppLoraWanSetRegion(LORAMAC_REGION_CN470);
                break;
#endif
#if defined(REGION_AS923)
            case 6:  // AS923-1
                AppLoraWanSetRegion(LORAMAC_REGION_AS923);
                break;

  #if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
  #if 0  // (reserved)
            case 21:  // AS923-1 (same with case 6)
                AppLoraWanSetRegion(LORAMAC_REGION_AS923);
                break;
  #endif
            case 22:  // AS923-2
                AppLoraWanSetRegion(LORAMAC_REGION_AS923_2);
                break;

            case 23:  // AS923-3
                AppLoraWanSetRegion(LORAMAC_REGION_AS923_3);
                break;

            case 24:  // AS923-4
                AppLoraWanSetRegion(LORAMAC_REGION_AS923_4);
                break;
  #endif  /* LORAMAC_VERSION */

            case 30:  // Japan (AS923-1)
                AppLoraWanSetRegion(LORAMAC_REGION_AS923_JPN);
                break;
#endif
#if defined(REGION_KR920)
            case 7:
                AppLoraWanSetRegion(LORAMAC_REGION_KR920);
                break;
#endif
#if defined(REGION_IN865)
            case 8:
                AppLoraWanSetRegion(LORAMAC_REGION_IN865);
                break;
#endif
#if defined(REGION_RU864)
            case 9:
                AppLoraWanSetRegion(LORAMAC_REGION_RU864);
                break;
#endif
            default:
                ret = AT_RC_ERR;
                break;
        }
    }

    return ret;
}

AtResultCode_t AppAtRegionRead(void *p)
{
    LoRaMacRegion_t region;
    AtResultCode_t ret = AT_RC_OK;      /* result code */

    region = AppLoraWanGetRegion();
    switch (region) {
#if defined(REGION_EU868)
        case LORAMAC_REGION_EU868:
            AppAtOutputResultCode("0:EU868");
            break;
#endif
#if defined(REGION_US915)
        case LORAMAC_REGION_US915:
            AppAtOutputResultCode("1:US915");
            break;
#endif
#if defined(REGION_CN779)
        case LORAMAC_REGION_CN779:
            AppAtOutputResultCode("2:CN779");
            break;
#endif
#if defined(REGION_EU433)
        case LORAMAC_REGION_EU433:
            AppAtOutputResultCode("3:EU433");
            break;
#endif
#if defined(REGION_AU915)
        case LORAMAC_REGION_AU915:
            AppAtOutputResultCode("4:AU915");
            break;
#endif
#if defined(REGION_CN470)
        case LORAMAC_REGION_CN470:
            AppAtOutputResultCode("5:CN470");
            break;
#endif
#if defined(REGION_AS923)
    #if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
        case LORAMAC_REGION_AS923:
            AppAtOutputResultCode("6:AS923-Group1");
            break;

        case LORAMAC_REGION_AS923_2:
            AppAtOutputResultCode("22:AS923-Group2");
            break;

        case LORAMAC_REGION_AS923_3:
            AppAtOutputResultCode("23:AS923-Group3");
            break;

        case LORAMAC_REGION_AS923_4:
            AppAtOutputResultCode("24:AS923-Group4");
            break;

    #else  // (LORAMAC_VERSION == LORAWAN_VERSION_1_0_3)
        case LORAMAC_REGION_AS923:
            AppAtOutputResultCode("6:AS923");
            break;

    #endif  /* LORAMAC_VERSION */

    case LORAMAC_REGION_AS923_JPN:
        AppAtOutputResultCode("30:AS923-Japan");
        break;
#endif
#if defined(REGION_KR920)
        case LORAMAC_REGION_KR920:
            AppAtOutputResultCode("7:KR920");
            break;
#endif
#if defined(REGION_IN865)
        case LORAMAC_REGION_IN865:
            AppAtOutputResultCode("8:IN865");
            break;
#endif
#if defined(REGION_RU864)
        case LORAMAC_REGION_RU864:
            AppAtOutputResultCode("9:RU864");
            break;
#endif
        default:
            ret = AT_RC_ERR;
            break;
    }

    return ret;
}

AtResultCode_t AppAtSendHexAct(void *p)
{
    int8_t *argv;
    int16_t argvLen;
    uint8_t fport = AppLoraWanGetFPort();
    Mcps_t mtype = AppLoraWanGetMessageType();
    AtResultCode_t ret = AT_RC_ERR;
    LoRaMacStatus_t status;

    AtParsePopOne(&argv, &argvLen);

    ret = AppAtHexStr2HexDataArray(appAtOctBuff, argv, argvLen);
    if (ret == AT_RC_OK) {
        status = AppLoraWanSendData(appAtOctBuff, argvLen / 2, fport, mtype, NULL);
        if (status == LORAMAC_STATUS_OK) {
            // save current AT command index and message type
            appAtLastAtCmdIndexData = AtCmdGetCurrentCmd();
            appAtLastSentMessage = APP_AT_LAST_SENT_MESSAGE_DATA;
        }
        else {
            AppAtMacStatusResult(status);
            ret = AT_RC_ERR;
        }
    }

    return ret;
}

AtResultCode_t AppAtAdrSet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_ERR;     /* result code          */
    int16_t len;
    bool adrEnabled;
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);
        if (len == 1) {
            if (argv[0] == '0') {
                adrEnabled = false;
                ret = AT_RC_OK;
            }
            else if (argv[0] == '1') {
                adrEnabled = true;
                ret = AT_RC_OK;
            }
        }
    }

    if (ret == AT_RC_OK) {
        status = AppLoraWanSetAdr(adrEnabled);
        if (status != LORAMAC_STATUS_OK) {
            AppAtMacStatusResult(status);
            ret = AT_RC_ERR;
        }
    }

    return ret;
}


AtResultCode_t AppAtAdrRead(void *p)
{
    bool adrEnabled;
    AtResultCode_t ret = AT_RC_OK;      /* result code  */

    adrEnabled = AppLoraWanGetAdr();
    if (adrEnabled == true) {
        AppAtOutputResultCode("1:ADR_ENABLED");
    }
    else if (adrEnabled == false) {
        AppAtOutputResultCode("0:ADR_DISABLED");
    }
    else {
        // in case parameter stored in data flash is wrong
        ret = AT_RC_ERR;
    }

    return ret;
}

AtResultCode_t AppAtRssiSet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_ERR;     /* result code          */
    int16_t len;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);
        if (len == 1) {
            if (argv[0] == '0') {
                AppLoraWanSetRssi(APP_LORAWAN_RSSI_OFF);
                ret = AT_RC_OK;
            }
            else if (argv[0] == '1') {
                AppLoraWanSetRssi(APP_LORAWAN_RSSI_ON);
                ret = AT_RC_OK;
            }
        }
    }

    return ret;
}

AtResultCode_t AppAtRssiRead(void *p)
{
    uint8_t rssiMode;
    AtResultCode_t ret = AT_RC_OK;      /* result code */

    rssiMode = AppLoraWanGetRssi();
    if (rssiMode == APP_LORAWAN_RSSI_OFF) {
        AppAtOutputResultCode("0:RSSI_DISABLED");
    }
    else if (rssiMode == APP_LORAWAN_RSSI_ON) {
        AppAtOutputResultCode("1:RSSI_ENABLED");
    }
    else {
        // in case parameter stored in data flash is wrong
        ret = AT_RC_ERR;
    }

    return ret;
}

AtResultCode_t AppAtRx1DelaySet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_ERR;     /* result code          */
    int16_t len;
    uint16_t delay;
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);

        // check argv
        if (len > APP_AT_MAX_RX1DELAY_DEC_SIZE) {
            return AT_RC_ERR;       // parameter too long
        }

        delay = AppAtStr2Dec(argv, len);
        if ((delay > APP_AT_MAX_RX1DELAY || (delay < APP_AT_MIN_RX1DELAY))) {
            return AT_RC_ERR;       // unexpected duration
        }

        status = AppLoraWanSetRx1Delay(delay);
        if (status == LORAMAC_STATUS_OK) {
            ret = AT_RC_OK;
        }
        else {
            AppAtMacStatusResult(status);
        }
    }

    return ret;
}

AtResultCode_t AppAtRx1DelayRead(void *p)
{
    uint16_t delay;

    delay = AppLoraWanGetRx1Delay();
    AppAtOutputResultDec(delay, APP_AT_MAX_RX1DELAY_DEC_SIZE, '\0');

    return AT_RC_OK;
}

AtResultCode_t AppAtDRSet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_ERR;     /* result code          */
    int16_t len;
    uint16_t dr;
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);

        // check argv
        if (len > APP_AT_MAX_DR_DEC_SIZE) {
            return AT_RC_ERR;       // parameter too long
        }

        dr = AppAtStr2Dec(argv, len);
        if (dr > APP_AT_MAX_DR) {
            return AT_RC_ERR;       // unexpected duration
        }

        status = AppLoraWanSetDR(dr);
        if (status == LORAMAC_STATUS_OK) {
            ret = AT_RC_OK;
        }
        else {
            AppAtMacStatusResult(status);
        }
    }

    return ret;
}

AtResultCode_t AppAtDRRead(void *p)
{
    uint16_t dr;

    dr = AppLoraWanGetDR();
    AppAtOutputResultDec(dr, APP_AT_MAX_DR_DEC_SIZE, '\0');

    return AT_RC_OK;
}

/*!
 * @fn
 * at+linkchk: require send LinkCheckReq
 */
AtResultCode_t AppAtLinkchkAct(void *p)
{
    uint8_t argc;                               /* number of argument   */
    AtResultCode_t ret = AT_RC_ERR;             /* result code  */
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 0) {
        status = AppLoraWanLinkCheck();
        if (status == LORAMAC_STATUS_OK) {
            appAtLastSentMessage = APP_AT_LAST_SENT_MESSAGE_CMD;
            appAtMlmeHandle |= APP_AT_MLME_HANDLE_LINKCHK;
            ret = AT_RC_OK;
        }
        else {
            AppAtMacStatusResult(status);
            appAtMlmeHandle &= ~(APP_AT_MLME_HANDLE_LINKCHK);
        }
    }

    return ret;
}

/*!
 * @fn
 * at+fport: set FPort to send data messages
 */
AtResultCode_t AppAtFPortSet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_ERR;     /* result code          */
    int16_t len, i, fport;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);

        // check argv
        if (len > APP_AT_MAX_FPORT_DEC_SIZE) {
            return AT_RC_ERR;       // parameter too long
        }

        for (i = 0 ; i < len ; i++) {
            if ((argv[i] < '0') || (argv[i] > '9')) {
                return AT_RC_ERR;   // parameter isn't decimal
            }
        }

        fport = AppAtStr2Dec(argv, len);
        if ((fport >= APP_AT_MIN_FPORT) && (fport <= APP_AT_MAX_FPORT)) {
            AppLoraWanSetFPort((uint8_t)(fport & 0xff));
            ret = AT_RC_OK;
        }
    }

    return ret;
}

/*!
 * @fn
 * at+fport: read FPort to send data messages
 */
AtResultCode_t AppAtFPortRead(void *p)
{
    uint8_t fport;

    fport = AppLoraWanGetFPort();
    AppAtOutputResultDec(fport, APP_AT_MAX_FPORT_DEC_SIZE, '\0');

    return AT_RC_OK;
}

/* +DCYCLE  */
AtResultCode_t AppAtDCycleSet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_ERR;     /* result code          */
    int16_t len;
    uint8_t dCyle;
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);
        if (len == 1) {
            if (argv[0] == '0') {
                dCyle = APP_LORAWAN_DCYCLE_OFF;
                ret = AT_RC_OK;
            }
            else if (argv[0] == '1') {
                dCyle = APP_LORAWAN_DCYCLE_ON;
                ret = AT_RC_OK;
            }

            if (ret == AT_RC_OK) {
                status = AppLoraWanSetDCycle(dCyle);
                if (status != LORAMAC_STATUS_OK) {
                    AppAtMacStatusResult(status);
                    ret = AT_RC_ERR;
                }
            }
        }
    }

    return ret;
}

AtResultCode_t AppAtDCycleRead(void *p)
{
    uint8_t dcycleEnabled;
    AtResultCode_t ret = AT_RC_OK;      /* result code          */

    dcycleEnabled = AppLoraWanGetDCycle();

    if (dcycleEnabled == 1) {
        AppAtOutputResultCode("1:DCYCLE_ENABLED");
    }
    else if (dcycleEnabled == 0) {
        AppAtOutputResultCode("0:DCYCLE_DISABLED");
    }
    else {
        // in case parameter stored in data flash is wrong
        ret = AT_RC_ERR;
    }

    return ret;
}

/*!
 * @fn
 * at+devtime:request send DeviceTimeReq
 */
AtResultCode_t AppAtDevTimeAct(void *p)
{
    uint8_t argc;                               /* number of argument   */
    AtResultCode_t ret = AT_RC_ERR;             /* result code          */
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 0) {
        status = AppLoraWanDeviceTime();
        if (status == LORAMAC_STATUS_OK) {
            appAtLastSentMessage = APP_AT_LAST_SENT_MESSAGE_CMD;
            appAtMlmeHandle |= APP_AT_MLME_HANDLE_DEVTIME;
            ret = AT_RC_OK;
        }
        else{
            AppAtMacStatusResult(status);
            appAtMlmeHandle &= ~(APP_AT_MLME_HANDLE_DEVTIME);
        }
    }
    return ret;
}

#ifdef LORAMAC_CLASSB_ENABLED
/*!
 * @fn
 * at+bconacq:request start Beacon acquision
 */
AtResultCode_t AppAtBconAcqAct(void *p)
{
    uint8_t argc;                               /* number of argument   */
    AtResultCode_t ret = AT_RC_ERR;             /* result code          */
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 0) {
        status = AppLoraWanBeaconAcquisition();
        if  (status == LORAMAC_STATUS_OK) {
            ret = AT_RC_OK;
        }
        else{
            AppAtMacStatusResult(status);
        }
    }
    return ret;
}

/*!
 * @fn
 * at+pinslinf:request start PingSlotInfo
 */
AtResultCode_t AppAtPngSlInfoAct(void *p)
{
    uint8_t argc;                               /* number of argument   */
    AtResultCode_t ret = AT_RC_ERR;             /* result code          */
    uint8_t PinSlinf;
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 0) {
        PinSlinf = AppLoraWanGetPeriodicity();
        status = AppLoraWanPingSlotInfo(PinSlinf);

        if(status == LORAMAC_STATUS_OK){
            appAtLastSentMessage = APP_AT_LAST_SENT_MESSAGE_CMD;
            appAtMlmeHandle |= APP_AT_MLME_HANDLE_PNGSLINFO;
            ret = AT_RC_OK;
        }
        else{
            AppAtMacStatusResult(status);
            appAtMlmeHandle &= ~(APP_AT_MLME_HANDLE_PNGSLINFO);
        }
    }
    return ret;
}

/*!
 * @fn
 * at+bcontim:request send BeaconTimingReq
 */
AtResultCode_t AppAtBconTimAct(void *p)
{
    uint8_t argc;                               /* number of argument   */
    AtResultCode_t ret = AT_RC_ERR;             /* result code          */
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 0) {
        status = AppLoraWanBeaconTiming();
        if(status == LORAMAC_STATUS_OK) {
            appAtLastSentMessage = APP_AT_LAST_SENT_MESSAGE_CMD;
            ret = AT_RC_OK;
            appAtMlmeHandle |= APP_AT_MLME_HANDLE_BCONTIM;
        }
        else{
            AppAtMacStatusResult(status);
            appAtMlmeHandle &= ~(APP_AT_MLME_HANDLE_BCONTIM);
        }
    }
    return ret;
}

/*!
 * @fn
 * at+pngperiod: set Periodicity
 */
AtResultCode_t AppAtPngSlPeriodSet(void *p)
{
    uint8_t argc;                               /* number of argument   */
    int8_t *argv;                               /* argument             */
    AtResultCode_t ret = AT_RC_ERR;             /* result code*/
    uint16_t i;
    int16_t  len;
    uint8_t  PngPeriod;
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if(argc == 1) {
        AtParsePopOne(&argv,&len);

        if(len > APP_AT_MAX_PNGSL_PERIOD_DEC_SIZE) {
            return AT_RC_ERR;   // parameter too long
        }
        for(i = 0; i < len ; i++) {
            if((argv[i] < '0') || (argv[i] > '7')) {
                return AT_RC_ERR;   //parameter isn't decimal
            }
        }

        PngPeriod = (uint8_t)AppAtStr2Dec(argv, len);
        if(PngPeriod <= APP_AT_MAX_PNGSL_PERIOD) {
            status = AppLoraWanSetPeriodicity(PngPeriod);

            if(status == LORAMAC_STATUS_OK) {
                ret = AT_RC_OK;
            }
            else {
                AppAtMacStatusResult(status);
            }
        }
    }
    return ret;
}

/*!
 * @fn
 * at+pngperiod:read Periodicity
 */
AtResultCode_t AppAtPngSlPeriodRead(void *p)
{
    AtResultCode_t ret = AT_RC_OK;
    uint8_t  PngPeriod;

    PngPeriod = AppLoraWanGetPeriodicity();
    AppAtOutputResultDec(PngPeriod, APP_AT_MAX_PNGSL_PERIOD_DEC_SIZE, '\0');

    return ret;
}
#endif

/*!
 * @fn
 * at+cert: set certification port setting
 */
AtResultCode_t AppAtCertModeSet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_ERR;     /* result code          */
    int16_t len;
    uint8_t certMode;
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);
        if (len == 1) {
            if (argv[0] == '0') {
                certMode = APP_LORAWAN_CERT_OFF;
                ret = AT_RC_OK;
            }
            else if (argv[0] == '1') {
                certMode = APP_LORAWAN_CERT_ON;
                ret = AT_RC_OK;
            }

            if (ret == AT_RC_OK) {
                status = AppLoraWanSetCertFPortOn(certMode);
                if (status != LORAMAC_STATUS_OK) {
                    AppAtMacStatusResult(status);
                    ret = AT_RC_ERR;
                }
            }
        }
    }

    return ret;
}

/*!
 * @fn
 * at+cert: get certification port setting
 */
AtResultCode_t AppAtCertModeRead(void *p)
{
    uint8_t certEnabled;
    AtResultCode_t ret = AT_RC_OK;      /* result code          */

    certEnabled = AppLoraWanGetCertFPortOn();

    if (certEnabled == 1) {
        AppAtOutputResultCode("1:CERT_ENABLED");
    }
    else if (certEnabled == 0) {
        AppAtOutputResultCode("0:CERT_DISABLED");
    }
    else {
        // in case parameter stored in data flash is wrong
        ret = AT_RC_ERR;
    }

    return ret;
}

/*!
 * @fn
 * at+chdefmask: set channels default mask
 */
AtResultCode_t AppAtChannelsDefaultMaskSet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_ERR;     /* result code          */
    int16_t argvLen;
    LoRaMacStatus_t status;
    uint16_t channelsMask[APP_LORAWAN_CHANNELS_MASK_ENTRIES];
    uint8_t channelsMaskEntries = 0;
    uint8_t entries;
    uint8_t i;
    uint8_t tmp[2];

    memset(channelsMask, 0, sizeof(channelsMask));

    argc = AtParseListLen();

    if (argc <= APP_LORAWAN_CHANNELS_MASK_ENTRIES) {
        for (i = 0; i < argc; i++) {
            AtParsePopOne(&argv, &argvLen);

            ret = AppAtHexStr2HexDataArrayWithPadding(tmp, 2, argv, argvLen);
            if (ret != AT_RC_OK) {
                break;
            }
            else {
                channelsMask[channelsMaskEntries] = ((uint16_t)tmp[0] << 8) | ((uint16_t)tmp[1]);
                channelsMaskEntries++;
            }
        }
    }

    entries = AppLoraWanGetChannelsMaskEntries();
    if (channelsMaskEntries > entries) {
        ret = AT_RC_ERR;
    }
    channelsMaskEntries = entries;      // set all channeles default mask (set 0 if not specified)

    if (ret == AT_RC_OK) {
        status = AppLoraWanSetChannelsDefaultMask(channelsMask, channelsMaskEntries);
        if (status != LORAMAC_STATUS_OK) {
            AppAtMacStatusResult(status);
            ret = AT_RC_ERR;
        }
    }

    return ret;
}

/*!
 * @fn
 * at+chdefmask: get channels default mask
 */
AtResultCode_t AppAtChannelsDefaultMaskRead(void *p)
{
    AtResultCode_t ret = AT_RC_OK;      /* result code  */
    uint16_t channelsMask[APP_LORAWAN_CHANNELS_MASK_ENTRIES ];
    uint8_t channelsMaskEntries = 0;
    uint8_t i;

    AppLoraWanGetChannelsDefaultMask(channelsMask, &channelsMaskEntries);
    AtPrintHeader();
    print(" ");
    for(i = 0; i < channelsMaskEntries; i++) {
        print_hex( channelsMask[i], 4 );
        if(i != (channelsMaskEntries - 1)) {
            print(",");
        }
    }
    AtPrintTrailer();

    return ret;
}

#if (LORAMAC_MAX_MC_CTX > 0)
/*!
 * @fn
 * at+genappkey: set GenAppKey
 */
AtResultCode_t AppAtGenAppKeySet(void *p)
{
    uint8_t argc;                                   /* number of arguments  */
    int8_t *argv;                                   /* argument             */
    int16_t argvLen;                                /* length of an argument */
    uint8_t genAppKey[APP_LORAWAN_LEN_GENAPPKEY];   /* GenAppKey               */
    AtResultCode_t ret = AT_RC_ERR;                 /* result code          */

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &argvLen);

        ret = AppAtHexStr2HexDataArrayWithPadding(genAppKey, APP_LORAWAN_LEN_GENAPPKEY, argv, argvLen);
        if (ret == AT_RC_OK) {
            AppLoraWanSetGenAppKey(genAppKey);
        }
    }

    return ret;
}

/*!
 * @fn
 * at+genappkey: read GenAppKey
 */
AtResultCode_t AppAtGenAppKeyRead(void *p)
{
#if defined(APP_AT_KEY_READ_ENABLED)

    uint8_t genAppKey[APP_LORAWAN_LEN_GENAPPKEY];
    uint8_t genAppKeyStr[APP_LORAWAN_LEN_GENAPPKEY * 2 + 1];

    AppLoraWanGetGenAppKey(genAppKey);
    AppAtHexArray2HexStr(genAppKeyStr, genAppKey, APP_LORAWAN_LEN_GENAPPKEY);

    AppAtOutputResultCode((char *)genAppKeyStr);

    return AT_RC_OK;
#else

    return AT_RC_ERR;
#endif
}
#endif

#if defined(DEBUG_LORAMAC)
AtResultCode_t AppAtDebugSet(void *p)
{
    uint8_t argc;                                       /* number of arguments   */
    int8_t *argv;                                       /* argument              */
    int16_t argvLen;                                    /* length of an argument */
    uint32_t mode;                                      /* debug mode            */
    uint8_t tmpMode[APP_LEN_DEBUG_LORAMAC_MODE];        /* temporary debug mode  */
    uint8_t i;
    uint16_t shift = 24;
    AtResultCode_t ret = AT_RC_ERR;                     /* result code           */

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &argvLen);

        ret = AppAtHexStr2HexDataArrayWithPadding(tmpMode, APP_LEN_DEBUG_LORAMAC_MODE, argv, argvLen);
        if (ret == AT_RC_OK) {
            mode = 0;
            shift = 24;
            for (i = 0 ; i < APP_LEN_DEBUG_LORAMAC_MODE ; i++) {
                mode |= ((uint32_t)tmpMode[i] << shift);
                shift -= 8;
            }

            LoRaMacDebugSetMode(mode);
        }
    }

    return ret;
}

AtResultCode_t AppAtDebugRead(void *p)
{
    uint32_t mode;
    uint16_t shift = 24;
    uint8_t modeArray[APP_LEN_DEBUG_LORAMAC_MODE];
    uint8_t modeStr[APP_LEN_DEBUG_LORAMAC_MODE * 2 + 1];
    uint8_t i;

    mode = LoRaMacDebugGetMode();
    for (i = 0 ; i < APP_LEN_DEBUG_LORAMAC_MODE ; i++) {
        modeArray[i] = (uint8_t)((mode >> shift) & 0xff);
        shift -= 8;
    }
    AppAtHexArray2HexStr(modeStr, modeArray, APP_LEN_DEBUG_LORAMAC_MODE);

    AppAtOutputResultCode((char *)modeStr);

    return AT_RC_OK;

}
#endif

/*!
 * @fn
 * at+devnonce: set DevNonce
 */
AtResultCode_t AppAtDevNonceSet(void *p)
{
    uint8_t argc;                                       /* number of arguments  */
    int8_t *argv;                                       /* argument             */
    int16_t argvLen;                                    /* length of an argument */
    uint16_t devNonce;                                  /* devNonce              */
    uint8_t tmpDevNonce[APP_LORAWAN_LEN_DEVNONCE];      /* temporary devNonce    */
    AtResultCode_t ret = AT_RC_ERR;                     /* result code          */
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &argvLen);

        ret = AppAtHexStr2HexDataArrayWithPadding(tmpDevNonce, APP_LORAWAN_LEN_DEVNONCE, argv, argvLen);
        if (ret == AT_RC_OK) {
            devNonce = ((uint16_t)tmpDevNonce[0] << 8) | (uint16_t)tmpDevNonce[1];

            status = AppLoraWanSetDevNonce(devNonce);
            if (status == LORAMAC_STATUS_OK) {
                AppLoraWanNvmDataMgmtSave(LORAMAC_NVM_MIBFLG_DEV_NONCE);
            }
            else {
                AppAtMacStatusResult(status);
                ret = AT_RC_ERR;
            }
        }
    }

    return ret;
}

/*!
 * @fn
 * at+devnonce: reada DevNonce
 */
AtResultCode_t AppAtDevNonceRead(void *p)
{
    uint16_t devNonce;
    uint8_t devNonceArray[APP_LORAWAN_LEN_DEVNONCE];
    uint8_t devNonceStr[APP_LORAWAN_LEN_DEVNONCE * 2 + 1];

    devNonce = AppLoraWanGetDevNonce();
    devNonceArray[0] = (uint8_t)( devNonce >> 8 );
    devNonceArray[1] = (uint8_t)devNonce;
    AppAtHexArray2HexStr(devNonceStr, devNonceArray, APP_LORAWAN_LEN_DEVNONCE);

    AppAtOutputResultCode((char *)devNonceStr);

    return AT_RC_OK;
}


/*!
 * @fn
 * at+appnonce: set AppNonce
 */
AtResultCode_t AppAtAppNonceSet(void *p)
{
    uint8_t argc;                                       /* number of arguments  */
    int8_t *argv;                                       /* argument             */
    int16_t argvLen;                                    /* length of an argument */
    uint32_t appNonce;                                  /* appNonce (joinNonce) */
    uint8_t tmpAppNonce[APP_LORAWAN_LEN_APPNONCE];      /* temporary appNonce   */
    AtResultCode_t ret = AT_RC_ERR;                     /* result code          */
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &argvLen);

        ret = AppAtHexStr2HexDataArrayWithPadding(tmpAppNonce, APP_LORAWAN_LEN_APPNONCE, argv, argvLen);
        if (ret == AT_RC_OK) {
            appNonce = ((uint32_t)tmpAppNonce[0] << 16) | ((uint32_t)tmpAppNonce[1] << 8) |
                       (uint32_t)tmpAppNonce[2];

            status = AppLoraWanSetAppNonce(appNonce);
            if (status == LORAMAC_STATUS_OK) {
                AppLoraWanNvmDataMgmtSave(LORAMAC_NVM_MIBFLG_APP_NONCE);
            }
            else {
                AppAtMacStatusResult(status);
                ret = AT_RC_ERR;
            }
        }
    }

    return ret;
}

/*!
 * @fn
 * at+appnonce: read AppNonce
 */
AtResultCode_t AppAtAppNonceRead(void *p)
{
    uint32_t appNonce, shift = 16;
    uint8_t appNonceArray[APP_LORAWAN_LEN_APPNONCE];
    uint8_t appNonceStr[APP_LORAWAN_LEN_APPNONCE * 2 + 1];
    uint8_t i;

    appNonce = AppLoraWanGetAppNonce();
    for (i = 0 ; i < APP_LORAWAN_LEN_APPNONCE ; i++) {
        appNonceArray[i] = (uint8_t)( appNonce >> shift );
        shift -= 8;
    }
    AppAtHexArray2HexStr(appNonceStr, appNonceArray, APP_LORAWAN_LEN_APPNONCE);

    AppAtOutputResultCode((char *)appNonceStr);

    return AT_RC_OK;
}

/*!
 * @fn
 * at+downfcnt set DownlinkFCnt
 */
AtResultCode_t AppAtDownlinkFCntSet(void *p)
{
    uint8_t argc;                                       /* number of arguments  */
    int8_t *argv;                                       /* argument             */
    int16_t argvLen;                                    /* length of an argument */
    uint32_t downlinkFcnt;                              /* DownlinkFCnt         */
    uint8_t tmpDownlinkFcnt[APP_LORAWAN_LEN_FCNT];      /* temporary DownlinkFCnt */
    AtResultCode_t ret = AT_RC_ERR;                     /* result code          */
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &argvLen);

        ret = AppAtHexStr2HexDataArrayWithPadding(tmpDownlinkFcnt, APP_LORAWAN_LEN_FCNT, argv, argvLen);
        if (ret == AT_RC_OK) {
            downlinkFcnt = ((uint32_t)tmpDownlinkFcnt[0] << 24) | ((uint32_t)tmpDownlinkFcnt[1] << 16)
                         | ((uint32_t)tmpDownlinkFcnt[2] << 8) | (uint32_t)tmpDownlinkFcnt[3];
            status = AppLoraWanSetDownlinkFCnt(downlinkFcnt);
            if (status == LORAMAC_STATUS_OK) {
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
                AppLoraWanNvmDataMgmtSave(LORAMAC_NVM_MIBFLG_DOWNLINK_FCNT);
#endif
            }
            else {
                AppAtMacStatusResult(status);
                ret = AT_RC_ERR;
            }
        }
    }

    return ret;
}

/*!
 * @fn
 * at+downfcnt: read DownlinkFCnt
 */
AtResultCode_t AppAtDownlinkFCntRead(void *p)
{
    uint32_t downlinkFcnt, shift = 24;
    uint8_t downlinkFcntArray[APP_LORAWAN_LEN_FCNT];
    uint8_t downlinkFcntStr[APP_LORAWAN_LEN_FCNT * 2 + 1];
    uint8_t i;

    downlinkFcnt = AppLoraWanGetDownlinkFCnt();
    for (i = 0 ; i < APP_LORAWAN_LEN_FCNT ; i++) {
        downlinkFcntArray[i] = (uint8_t)( downlinkFcnt >> shift );
        shift -= 8;
    }
    AppAtHexArray2HexStr(downlinkFcntStr, downlinkFcntArray, APP_LORAWAN_LEN_FCNT);

    AppAtOutputResultCode((char *)downlinkFcntStr);

    return AT_RC_OK;
}

/*!
 * @fn
 * at+upfcnt set UplinkFCnt
 */
AtResultCode_t AppAtUplinkFCntSet(void *p)
{
    uint8_t argc;                                       /* number of arguments  */
    int8_t *argv;                                       /* argument             */
    int16_t argvLen;                                    /* length of an argument */
    uint32_t uplinkFcnt;                                /* UplinkFCnt           */
    uint8_t tmpUplinkFcnt[APP_LORAWAN_LEN_FCNT];        /* temporary UplinkFCnt */
    AtResultCode_t ret = AT_RC_ERR;                     /* result code          */
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &argvLen);

        ret = AppAtHexStr2HexDataArrayWithPadding(tmpUplinkFcnt, APP_LORAWAN_LEN_FCNT, argv, argvLen);
        if (ret == AT_RC_OK) {
            uplinkFcnt = ((uint32_t)tmpUplinkFcnt[0] << 24) | ((uint32_t)tmpUplinkFcnt[1] << 16)
                       | ((uint32_t)tmpUplinkFcnt[2] << 8) | (uint32_t)tmpUplinkFcnt[3];
            status = AppLoraWanSetUplinkFCnt(uplinkFcnt);
            if (status == LORAMAC_STATUS_OK) {
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
                AppLoraWanNvmDataMgmtSave(LORAMAC_NVM_MIBFLG_UPLINK_FCNT);
#endif
            }
            else {
                AppAtMacStatusResult(status);
                ret = AT_RC_ERR;
            }
        }
    }

    return ret;
}

/*!
 * @fn
 * at+upfcnt: read UplinkFCnt
 */
AtResultCode_t AppAtUplinkFCntRead(void *p)
{
    uint32_t uplinkFcnt, shift = 24;
    uint8_t uplinkFcntArray[APP_LORAWAN_LEN_FCNT];
    uint8_t uplinkFcntStr[APP_LORAWAN_LEN_FCNT * 2 + 1];
    uint8_t i;

    uplinkFcnt = AppLoraWanGetUplinkFCnt();
    for (i = 0 ; i < APP_LORAWAN_LEN_FCNT ; i++) {
        uplinkFcntArray[i] = (uint8_t)( uplinkFcnt >> shift );
        shift -= 8;
    }
    AppAtHexArray2HexStr(uplinkFcntStr, uplinkFcntArray, APP_LORAWAN_LEN_FCNT);

    AppAtOutputResultCode((char *)uplinkFcntStr);

    return AT_RC_OK;
}

#if defined(DEBUG_AT_COMMAND_EXPERIMENTAL)
/*!
 * @fn
 * at+ch: read channels
 */
AtResultCode_t AppAtAppChannelsRead(void *p)
{
    uint8_t ch, getNumCh;
    ChannelParams_t channel;

    // init
    ch = 0;

    AtPrintHeader();
    print(" ");
    do {
        getNumCh = AppLoraWanGetChannels(ch, 1, &channel);
        if (getNumCh != 0) {
            if (ch > 0) {
                print(",");
            }
            print_dec(channel.Frequency, 10, '\0');
#ifdef LORAMAC_SET_CH_RX1FREQ_ENABLED
            print(",");
            print_dec(channel.Rx1Frequency, 10, '\0');
#endif
            print(",0x");
            print_hex(channel.DrRange.Value, 2);
            print(",");
            print_dec(channel.Band, 2, '\0');

            // next
            ch++;
        }
   } while (getNumCh != 0);
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * @fn
 * at+chmask: read channel mask
 */
AtResultCode_t AppAtAppChannelMaskRead(void *p)
{
    AtResultCode_t ret = AT_RC_OK;      /* result code  */
    uint16_t channelsMask[APP_LORAWAN_CHANNELS_MASK_ENTRIES];
    uint8_t channelsMaskEntries = 0;
    uint8_t i;

    AppLoraWanGetChannelsMask(channelsMask, &channelsMaskEntries);
    AtPrintHeader();
    print(" ");
    for(i = 0; i < channelsMaskEntries; i++) {
        print_hex(channelsMask[i], 4);
        if(i != (channelsMaskEntries - 1)) {
            print(",");
        }
    }
    AtPrintTrailer();

    return ret;
}

/*!
 * @fn
 * at+nbtrans: set NbTrans
 */
AtResultCode_t AppAtNbTransSet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_ERR;     /* result code          */
    int16_t len, nbTrans;
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);

        // check argv
        if (len > APP_AT_MAX_NBTRANS_DEC_SIZE) {
            return AT_RC_ERR;       // parameter too long
        }

        nbTrans = AppAtStr2Dec(argv, len);
        if ((nbTrans >= APP_AT_MIN_NBTRANS) && (nbTrans <= APP_AT_MAX_NBTRANS)) {
            status = AppLoraWanSetNbTrans((uint8_t)nbTrans);
            if (status == LORAMAC_STATUS_OK) {
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
                AppLoraWanNvmDataMgmtSave(LORAMAC_NVM_MIBFLG_CHANNELS_NB_TRANS);
#endif
                ret = AT_RC_OK;
            }
            else {
                AppAtMacStatusResult(status);
            }
        }
    }

    return ret;
}

/*!
 * @fn
 * at+nbtrans: read NbTrans
 */
AtResultCode_t AppAtNbTransRead(void *p)
{
    uint8_t nbTrans;

    nbTrans = AppLoraWanGetNbTrans();
    AppAtOutputResultDec(nbTrans, APP_AT_MAX_NBTRANS_DEC_SIZE, '\0');

    return AT_RC_OK;
}

/*!
 * @fn
 * at+txpower: set TxPower
 */
AtResultCode_t AppAtTxPowerSet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_ERR;     /* result code          */
    int16_t len, txPower;
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);

        // check argv
        if (len > APP_AT_MAX_TXPOWER_DEC_SIZE) {
            return AT_RC_ERR;       // parameter too long
        }

        txPower = AppAtStr2Dec(argv, len);
        if ((txPower >= APP_AT_MIN_TXPOWER)&& (txPower <= APP_AT_MAX_TXPOWER)) {
            status = AppLoraWanSetTxPower((int8_t)txPower);
            if (status == LORAMAC_STATUS_OK) {
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
                AppLoraWanNvmDataMgmtSave(LORAMAC_NVM_MIBFLG_CHANNELS_TXPOWER);
#endif
                ret = AT_RC_OK;
            }
            else {
                AppAtMacStatusResult(status);
            }
        }
    }

    return ret;
}

/*!
 * @fn
 * at+txpower: read TxPower
 */
AtResultCode_t AppAtTxPowerRead(void *p)
{
    int8_t txPower;

    txPower = AppLoraWanGetTxPower();
    AppAtOutputResultDec(txPower, APP_AT_MAX_TXPOWER_DEC_SIZE, '\0');

    return AT_RC_OK;
}

/*!
 * @fn
 * at+maxdcycle: set MaxDutyCycle
 */
AtResultCode_t AppAtMaxDCycleSet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_ERR;     /* result code          */
    int16_t len, i, maxDCycle;
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);

        // check argv
        if (len > APP_AT_MAX_MAXDCYCLE_DEC_SIZE) {
            return AT_RC_ERR;       // parameter too long
        }

        for (i = 0 ; i < len ; i++) {
            if ((argv[i] < '0') || (argv[i] > '9')) {
                return AT_RC_ERR;   // parameter isn't decimal
            }
        }

        maxDCycle = AppAtStr2Dec(argv, len);
        if ((maxDCycle >= APP_AT_MIN_MAXDCYCLE) && (maxDCycle <= APP_AT_MAX_MAXDCYCLE)) {
            status = AppLoraWanSetMaxDutyCycle((uint8_t)(maxDCycle & 0xff));
            if (status == LORAMAC_STATUS_OK) {
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
                AppLoraWanNvmDataMgmtSave(LORAMAC_NVM_MIBFLG_MAX_DCYCLE);
#endif
                ret = AT_RC_OK;
            }
            else {
                AppAtMacStatusResult(status);
            }
        }
    }

    return ret;
}

/*!
 * @fn
 * at+maxdcycle: read MaxDutyCycle
 */
AtResultCode_t AppAtMaxDCycleRead(void *p)
{
    uint8_t maxDCycle;

    maxDCycle = AppLoraWanGetMaxDutyCycle();
    AppAtOutputResultDec(maxDCycle, APP_AT_MAX_MAXDCYCLE_DEC_SIZE, '\0');

    return AT_RC_OK;
}

/*!
 * @fn
 * at+rx1droffset set Rx1DrOffset
 */
AtResultCode_t AppAtRx1DrOffsetSet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_ERR;     /* result code          */
    int16_t len, i, rx1DrOffset;
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);

        // check argv
        if (len > APP_AT_MAX_RX1DROFFSET_DEC_SIZE) {
            return AT_RC_ERR;       // parameter too long
        }

        for (i = 0 ; i < len ; i++) {
            if ((argv[i] < '0') || (argv[i] > '9')) {
                return AT_RC_ERR;   // parameter isn't decimal
            }
        }

        rx1DrOffset = AppAtStr2Dec(argv, len);
        if ((rx1DrOffset >= APP_AT_MIN_RX1DROFFSET) && (rx1DrOffset <= APP_AT_MAX_RX1DROFFSET)) {
            status = AppLoraWanSetRx1DrOffset((uint8_t)(rx1DrOffset & 0xff));
            if (status == LORAMAC_STATUS_OK) {
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
                AppLoraWanNvmDataMgmtSave(LORAMAC_NVM_MIBFLG_RX1_DROFFSET);
#endif
                ret = AT_RC_OK;
            }
            else {
                AppAtMacStatusResult(status);
            }
        }
    }

    return ret;
}

/*!
 * @fn
 * at+rx1droffset: read Rx1DrOffset
 */
AtResultCode_t AppAtRx1DrOffsetRead(void *p)
{
    uint8_t rx1DrOffset;

    rx1DrOffset = AppLoraWanGetRx1DrOffset();
    AppAtOutputResultDec(rx1DrOffset, APP_AT_MAX_RX1DROFFSET_DEC_SIZE, '\0');

    return AT_RC_OK;
}

/*!
 * @fn
 * at+rx2freq set Rx2Freq
 */
AtResultCode_t AppAtRx2FreqSet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_ERR;     /* result code          */
    int16_t len, i;
    uint32_t rx2Freq;
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);

        // check argv
        if (len > APP_AT_MAX_RX2FREQ_DEC_SIZE) {
            return AT_RC_ERR;       // parameter too long
        }

        for (i = 0 ; i < len ; i++) {
            if ((argv[i] < '0') || (argv[i] > '9')) {
                return AT_RC_ERR;   // parameter isn't decimal
            }
        }

        rx2Freq = AppAtStr2Dec(argv, len);
        status = AppLoraWanSetRX2Freq(rx2Freq);
        if (status == LORAMAC_STATUS_OK) {
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
            AppLoraWanNvmDataMgmtSave(LORAMAC_NVM_MIBFLG_RX2_FREQUENCY);
#endif
            ret = AT_RC_OK;
        }
        else {
            AppAtMacStatusResult(status);
        }
    }

    return ret;
}

/*!
 * @fn
 * at+rx2freq: read Rx2Freq
 */
AtResultCode_t AppAtRx2FreqRead(void *p)
{
    uint32_t rx2Freq;

    rx2Freq = AppLoraWanGetRX2Freq();
    AppAtOutputResultDec(rx2Freq, APP_AT_MAX_RX2FREQ_DEC_SIZE, '\0');

    return AT_RC_OK;
}

/*!
 * @fn
 * at+rx2dr set Rx2DataRate
 */
AtResultCode_t AppAtRx2DrSet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_ERR;     /* result code          */
    int16_t len;
    uint16_t rx2Dr;
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);

        // check argv
        if (len > APP_AT_MAX_DR_DEC_SIZE) {
            return AT_RC_ERR;       // parameter too long
        }

        rx2Dr = AppAtStr2Dec(argv, len);
        if (rx2Dr > APP_AT_MAX_DR) {
            return AT_RC_ERR;       // unexpected duration
        }

        status = AppLoraWanSetRX2DataRate((uint8_t)rx2Dr);
        if (status == LORAMAC_STATUS_OK) {
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
            AppLoraWanNvmDataMgmtSave(LORAMAC_NVM_MIBFLG_RX2_DATARATE);
#endif
            ret = AT_RC_OK;
        }
        else {
            AppAtMacStatusResult(status);
        }
    }

    return ret;
}

/*!
 * @fn
 * at+rx2dr: read Rx2DataRate
 */
AtResultCode_t AppAtRx2DrRead(void *p)
{
    uint16_t rx2Dr;

    rx2Dr = AppLoraWanGetRX2DataRate();
    AppAtOutputResultDec(rx2Dr, APP_AT_MAX_DR_DEC_SIZE, '\0');

    return AT_RC_OK;
}

/*!
 * @fn
 * at+maxeirp set MaxEIRP
 */
AtResultCode_t AppAtMaxEirpSet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_ERR;     /* result code          */
    int16_t len;
    uint8_t maxEirp;
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);

        // check argv
        if (len > APP_AT_MAX_MAXEIRP_DEC_SIZE) {
            return AT_RC_ERR;       // parameter too long
        }

        maxEirp = AppAtStr2Dec(argv, len);

        status = AppLoraWanSetMaxEIRP(maxEirp);
        if (status == LORAMAC_STATUS_OK) {
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
            AppLoraWanNvmDataMgmtSave(LORAMAC_NVM_MIBFLG_MAX_EIRP);
#endif
            ret = AT_RC_OK;
        }
        else {
            AppAtMacStatusResult(status);
        }
    }

    return ret;
}

/*!
 * @fn
 * at+maxeirp: read MaxEIRP
 */
AtResultCode_t AppAtMaxEirpRead(void *p)
{
    uint8_t maxEirp;

    maxEirp = AppLoraWanGetMaxEIRP();
    AppAtOutputResultDec(maxEirp, APP_AT_MAX_MAXEIRP_DEC_SIZE, '\0');

    return AT_RC_OK;
}

/*!
 * @fn
 * at+downdwell set DownlinkDwellTime
 */
AtResultCode_t AppAtDownlinkDwellTimeSet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_ERR;     /* result code          */
    int16_t len;
    uint8_t downlinkDwellTime;
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);
        if (len == 1) {
            if ((argv[0] == '0') || (argv[0] == '1')) {
                downlinkDwellTime = argv[0] - '0';
                ret = AT_RC_OK;
            }

            if (ret == AT_RC_OK) {
                status = AppLoraWanSetDownlinkDwellTime(downlinkDwellTime);
                if (status == LORAMAC_STATUS_OK) {
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
                    AppLoraWanNvmDataMgmtSave(LORAMAC_NVM_MIBFLG_DOWNLINK_DWELLTIME);
#endif
                }
                else {
                    AppAtMacStatusResult(status);
                    ret = AT_RC_ERR;
                }
            }
        }
    }

    return ret;
}

/*!
 * @fn
 * at+downdwell: read DownlinkDwellTime
 */
AtResultCode_t AppAtDownlinkDwellTimeRead(void *p)
{
    uint8_t downlinkDwellTime;

    downlinkDwellTime = AppLoraWanGetDownlinkDwellTime();
    AppAtOutputResultDec(downlinkDwellTime, 1, '\0');

    return AT_RC_OK;
}

/*!
 * @fn
 * at+updwell set UplinkDwellTime
 */
AtResultCode_t AppAtUplinkDwellTimeSet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_ERR;     /* result code          */
    int16_t len;
    uint8_t uplinkDwellTime;
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);
        if (len == 1) {
            if ((argv[0] == '0') || (argv[0] == '1')) {
                uplinkDwellTime = argv[0] - '0';
                ret = AT_RC_OK;
            }

            if (ret == AT_RC_OK) {
                status = AppLoraWanSetUplinkDwellTime(uplinkDwellTime);
                if (status == LORAMAC_STATUS_OK) {
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
                    AppLoraWanNvmDataMgmtSave(LORAMAC_NVM_MIBFLG_UPLINK_DWELLTIME);
#endif
                }
                else {
                    AppAtMacStatusResult(status);
                    ret = AT_RC_ERR;
                }
            }
        }
    }

    return ret;
}

/*!
 * @fn
 * at+updwell: read UplinkDwellTime
 */
AtResultCode_t AppAtUplinkDwellTimeRead(void *p)
{
    uint8_t uplinkDwellTime;

    uplinkDwellTime = AppLoraWanGetUplinkDwellTime();
    AppAtOutputResultDec(uplinkDwellTime, 1, '\0');

    return AT_RC_OK;
}

/*!
 * @fn
 * at+pngsldr set PingSlotDataRate
 */
AtResultCode_t AppAtPngSlDrSet(void *p)
{
    uint8_t argc;                       /* number of argument   */
    int8_t *argv;                       /* argument             */
    AtResultCode_t ret = AT_RC_ERR;     /* result code          */
    int16_t len;
    uint16_t pngSlDr;
    LoRaMacStatus_t status;

    argc = AtParseListLen();
    if (argc == 1) {
        AtParsePopOne(&argv, &len);

        // check argv
        if (len > APP_AT_MAX_DR_DEC_SIZE) {
            return AT_RC_ERR;       // parameter too long
        }

        pngSlDr = AppAtStr2Dec(argv, len);
        if (pngSlDr > APP_AT_MAX_DR) {
            return AT_RC_ERR;       // unexpected duration
        }

        status = AppLoraWanSetPingSlotDataRate((uint8_t)pngSlDr);
        if (status == LORAMAC_STATUS_OK) {
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
            AppLoraWanNvmDataMgmtSave(LORAMAC_NVM_MIBFLG_PING_SLOT_DATARATE);
#endif
            ret = AT_RC_OK;
        }
        else {
            AppAtMacStatusResult(status);
        }
    }

    return ret;
}

/*!
 * @fn
 * at+pngsldr: read PingSlotDataRate
 */
AtResultCode_t AppAtPngSlDrRead(void *p)
{
    uint16_t pngSlDr;

    pngSlDr = AppLoraWanGetPingSlotDataRate();
    AppAtOutputResultDec(pngSlDr, APP_AT_MAX_DR_DEC_SIZE, '\0');

    return AT_RC_OK;
}
#endif

/*!
 * AT+LORAMODE; Set (switch) LoRa mode
 */
AtResultCode_t AppAtLoRaModeSet( void *p )
{
    AtResultCode_t      ret;     /* result code */
    uint8_t             argc;    /* number of argument */
    int8_t              *argv;   /* argument */
    int16_t             argvLen; /* length of an argument */
    uint8_t             status;
    uint8_t             loraMode;

    // init
    ret    = AT_RC_ERR;
    status = 0;

    argc = AtParseListLen();
    if( argc == 1 )
    {
        AtParsePopOne( &argv, &argvLen );
        loraMode = AppAtStr2Dec( argv, argvLen );

        if( ( loraMode == APP_LORA_MODE_LORAWAN ) ||
            ( loraMode == APP_LORA_MODE_PRIVATELORA ) )
        {
            status = AppSetLoRaMode( loraMode );
        }
    }

    if( status == 1 )
    {
        ret = AT_RC_OK;
    }
    else
    {
        AppAtOutputResultCode( "ERROR" );
    }

    return ret;
}

/*!
 * AT+LORAMODE; Read LoRa mode
 */
AtResultCode_t AppAtLoRaModeRead( void *p )
{
    uint8_t             loraMode;

    loraMode = AppGetLoRaMode();

    switch( loraMode )
    {
        case APP_LORA_MODE_LORAWAN:
            AppAtOutputResultCode( "0:LoRaWAN" );
            break;

        case APP_LORA_MODE_PRIVATELORA:
            AppAtOutputResultCode( "1:PrivateLoRa" );
            break;

        default:
            AppAtOutputResultCode( "?:(none)" );
            break;
    }

    return AT_RC_OK;
}
