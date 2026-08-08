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

#include "privatelora_sample.h"

#include "at_proc.h"
#include "privatelora_at_proc.h"

// string buffer
#ifndef APP_AT_OCT_BUFF_SIZE
#define APP_AT_OCT_BUFF_SIZE    512
#endif
uint8_t appAtOctBuff[ APP_AT_OCT_BUFF_SIZE ];


/*--------------------*/
/* function prototype */
int8_t AtRespFormat(void);

//--------------------------------------------------------------------------------------------------
// Init

/*!
 * AT command initialization
 */
void AppAtInit( void )
{
    uint8_t     loraMode;

    /* disable UART interrupt    */
    AtDeInitUart();


    loraMode = AppGetLoRaMode();
    switch( loraMode )
    {
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


//--------------------------------------------------------------------------------------------------
// Tools for input

/*!
 * @fn
 * get the top address for string data
 * @param str   string argument
 * @return  top address for the string data, NULL if some error, *len == 0 if no string
 */
int8_t *AppAtGetString( int8_t *str, int16_t *len )
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
uint16_t AppAtHexStr2Hex( int8_t *hexStr )
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
void AppAtHexArray2HexStr( uint8_t *hexStr, uint8_t *hexArray, uint16_t len )
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
AtResultCode_t AppAtHexStr2HexDataArrayWithPadding( uint8_t *hexDataArray,
                                                    uint16_t hexDataArrayLen,
                                                    int8_t *hexStr,
                                                    uint16_t hexStrLen )
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
AtResultCode_t AppAtHexStr2HexDataArray( uint8_t *hexDataArray, int8_t *hexStr, uint16_t hexStrLen )
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
int32_t AppAtStr2Dec( int8_t *str, uint8_t len )
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

//--------------------------------------------------------------------------------------------------
// Output

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
