/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    PrvLoRaMacCrypto.c
  * @author  Renesas Electronics Corporation
  * @brief   
**/


#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "aes.h"
#include "cmac.h"
#ifdef RM_TINYCRYPT
#include <tinycrypt/aes.h>
#endif

#include "board.h"

#include "radio.h"

#include "PrivateLoRa.h"
#include "PrvLoRaMacCrypto.h"
#include "PrvLoRaMacRemoteDev.h"

#include "includes.h"
#include "sha1.h"

/*--------*/
/* define */
#define PRVLORA_SEC_BLOCKSIZE       16

/*----------------*/
/* typedef (enum) */

/*------------------------*/
/* typedef (struct/union) */

/*-------------------------*/
/* global variable (const) */

/*-----------------*/
/* global variable */

/*--------------------*/
/* function prototype */
static PrvLoRaStatus_t PrivateLoRaCryptoGetKey( PrvLoRaMacCryptoKeyId_t keyId,
                                                uint8_t                 *p_dstAddr,
                                                uint8_t                 *p_secKey );

static PrvLoRaStatus_t PrivateLoRaCryptoAesEncrypt( uint8_t  *p_buffer, 
                                                    uint16_t buffSize, 
                                                    uint8_t  *p_secKey, 
                                                    uint8_t  *p_encBuffer );
static PrvLoRaStatus_t PrivateLoRaCryptoComputeAesCmac( uint8_t  *p_micBxBuffer, 
                                                        uint8_t  *p_buffer, 
                                                        uint16_t buffSize, 
                                                        uint8_t  *p_secKey, 
                                                        uint32_t *p_cmacVal );
static PrvLoRaStatus_t PrivateLoRaCryptoDeriveSessionKey( uint8_t *p_key, 
                                                          uint8_t *p_remoteAddr, 
                                                          uint8_t *p_devAddr, 
                                                          uint8_t *p_responderNonce, 
                                                          uint8_t *p_initiatorNonce,
                                                          uint8_t *p_mac );

//--------------------------------------------------------------------------------------------------
// Init / setup

/*!
 * Initialization
 */
PrvLoRaStatus_t PrivateLoRaCryptoInit( void )
{
    // nothing to do
    return PRVLORA_STATUS_OK;
}

/*!
 * Get PSK/SessionKey
 */
static PrvLoRaStatus_t PrivateLoRaCryptoGetKey( PrvLoRaMacCryptoKeyId_t keyId,
                                                uint8_t                 *p_dstAddr,
                                                uint8_t                 *p_secKey )
{
    PrvLoRaStatus_t ret;

    // init
    ret = PRVLORA_STATUS_CRYPTO_ERROR;

    // get security key
    switch( keyId )
    {
        case PRVLORA_CRYPTO_PSK:
            ret = PrivateLoRaRemoteDevGetPSK( p_dstAddr, p_secKey );
            break;

        case PRVLORA_CRYPTO_SESSION_KEY:
            ret = PrivateLoRaRemoteDevGetSessionKey( p_dstAddr, p_secKey );
            break;

        default:
            break;
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------
// AES128 / CMAC

/*!
 *
 */
static PrvLoRaStatus_t PrivateLoRaCryptoAesEncrypt( uint8_t  *p_buffer, 
                                                    uint16_t buffSize, 
                                                    uint8_t  *p_secKey, 
                                                    uint8_t  *p_encBuffer )
{
    aes_context aesContext;
    uint8_t     block;

    // initial check
    if( ( p_secKey == NULL) || ( p_buffer == NULL ) || ( p_encBuffer == NULL ) )
    {
        return PRVLORA_STATUS_CRYPTO_ERROR;
    }
    // Check if the size is divisible by 16,
    if( ( buffSize % 16 ) != 0 )
    {
        return PRVLORA_STATUS_CRYPTO_ERROR;
    }

    // init
    block = 0;
    memset1( aesContext.ksch, '\0', sizeof(aesContext.ksch) );

#ifdef RM_TINYCRYPT
    tc_aes128_set_encrypt_key( (TCAesKeySched_t)&aesContext.ksch, p_secKey );
#else
    aes_set_key( p_secKey, 16, &aesContext );
#endif

    while( buffSize != 0 )
    {
#ifdef RM_TINYCRYPT
        HW_SCE_PowerOn();
        tc_aes_encrypt( &p_encBuffer[ block ], &p_buffer[ block ], (TCAesKeySched_t)&aesContext.ksch );
        HW_SCE_PowerOff();
#else
        aes_encrypt( &p_buffer[ block ], &p_encBuffer[ block ], &aesContext );
#endif
        block    = block + 16;
        buffSize = buffSize - 16;
    }

    return PRVLORA_STATUS_OK;
}

/*!
 *
 */
static PrvLoRaStatus_t PrivateLoRaCryptoComputeAesCmac( uint8_t  *p_micBxBuffer, 
                                                        uint8_t  *p_buffer, 
                                                        uint16_t buffSize, 
                                                        uint8_t  *p_secKey, 
                                                        uint32_t *p_cmacVal )
{
    uint8_t         cmac[ 16 ];
    uint32_t        cmacVal;
    AES_CMAC_CTX    aesCmacCtx;

    // initial check
    if( ( p_secKey == NULL ) || ( p_cmacVal == NULL ) )
    {
        return PRVLORA_STATUS_CRYPTO_ERROR;
    }
    if( ( p_buffer == NULL ) && ( buffSize > 0 ) )
    {
        return PRVLORA_STATUS_CRYPTO_ERROR;
    }

    // calc CMAC
    AES_CMAC_Init( &aesCmacCtx );
    AES_CMAC_SetKey( &aesCmacCtx, p_secKey );
    if( p_micBxBuffer != NULL )
    {
        AES_CMAC_Update( &aesCmacCtx, p_micBxBuffer, 16 );
    }
    AES_CMAC_Update( &aesCmacCtx, p_buffer, buffSize );
    AES_CMAC_Final( cmac, &aesCmacCtx );

    // Bring into the required format
    cmacVal  = ( uint32_t )cmac[ 3 ] << 24;
    cmacVal |= ( uint32_t )cmac[ 2 ] << 16;
    cmacVal |= ( uint32_t )cmac[ 1 ] <<  8;
    cmacVal |= ( uint32_t )cmac[ 0 ];
    (*p_cmacVal) = cmacVal;

    return PRVLORA_STATUS_OK;
}

//--------------------------------------------------------------------------------------------------
// encrypto / decrypto

/*!
 * Secure message (= Tx)
 */
PrvLoRaStatus_t PrivateLoRaCryptoSecureMessage( PrvLoRaMacCryptoKeyId_t keyId,
                                                uint8_t                 *p_remoteAddr,
                                                uint8_t                 *p_macAddr,
                                                uint8_t                 *p_message,
                                                uint8_t                 messageSize,
                                                uint32_t                frameCounter )
{
    PrvLoRaStatus_t         ret;
    uint8_t                 secKey[ PRVLORA_CRYPTKEY_SIZE ];
    uint8_t                 blockAi[ PRVLORA_SEC_BLOCKSIZE ];
    uint8_t                 blockSi[ PRVLORA_SEC_BLOCKSIZE ];
    uint8_t                 cnt, i;

    // initial check
    if( p_macAddr == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }
    if( messageSize > 0 )
    {
        if( p_message == NULL )
        {
            return PRVLORA_STATUS_PARAMETER_INVALID;
        }
    }
    else
    {
        return PRVLORA_STATUS_OK;  // nothing to do
    }

    // get security key
    ret = PrivateLoRaCryptoGetKey( keyId, p_remoteAddr, secKey );

    // encrypto message
    if( ret == PRVLORA_STATUS_OK )
    {
        // make block Ai : 0x01 | DevEUI | FCntUp | 0x00 | i(cnt)
        blockAi[ 0 ] = 0x01;
        memcpyr( &blockAi[ 1 ], p_macAddr, PRVLORA_MACADDR_SIZE );  // need to reverse endian
        memcpy1( &blockAi[ 9 ], (const uint8_t *)&frameCounter, sizeof(uint32_t) );
        blockAi[ 13 ] = 0x00;
        blockAi[ 14 ] = 0x00;

        cnt = 1;  // init
        while( messageSize > 0 )
        {
            // update block Ai
            blockAi[ 15 ] = cnt;

            // AES128 encryption
            memset1( blockSi, 0x00, PRVLORA_SEC_BLOCKSIZE );
            ret = PrivateLoRaCryptoAesEncrypt( blockAi, PRVLORA_SEC_BLOCKSIZE, secKey, blockSi );
            if( ret == PRVLORA_STATUS_OK )
            {
                for( i = 0; i < PRVLORA_SEC_BLOCKSIZE; i++ )
                {
                    (*p_message) = (*p_message) ^ blockSi[ i ];

                    p_message++;
                    messageSize--;
                    if( messageSize == 0 )
                    {
                        break;  // exit from for(i) loop
                    }
                }

                // next block
                cnt++;
            }
            else
            {
                break;  // exit from while(messageSize) loop
            }
        }
    }

    return ret;
}

/*!
 * Unsecure message (= Rx)
 */
PrvLoRaStatus_t PrivateLoRaCryptoUnsecureMessage( PrvLoRaMacCryptoKeyId_t keyId,
                                                  uint8_t                 *p_remoteAddr,
                                                  uint8_t                 *p_macAddr,
                                                  uint8_t                 *p_message,
                                                  uint8_t                 messageSize,
                                                  uint32_t                frameCounter )
{
    PrvLoRaStatus_t     ret;

    ret = PrivateLoRaCryptoSecureMessage( keyId,
                                          p_remoteAddr,
                                          p_macAddr,
                                          p_message,
                                          messageSize,
                                          frameCounter );

    return ret;
}

//--------------------------------------------------------------------------------------------------
// MIC

/*!
 * calculate MIC
 */
PrvLoRaStatus_t PrivateLoRaCryptoGetMIC( PrvLoRaMacCryptoKeyId_t keyId,
                                         uint8_t                 *p_remoteAddr,
                                         uint8_t                 *p_macAddr,
                                         uint8_t                 *p_message,
                                         uint8_t                 messageSize,
                                         uint32_t                frameCounter,
                                         uint32_t                *p_calcMic )
{
    PrvLoRaStatus_t     ret;
    uint8_t             secKey[ PRVLORA_CRYPTKEY_SIZE ];
    uint8_t             blockB0[ PRVLORA_SEC_BLOCKSIZE ];
    uint32_t            calcMic;

    // get security key
    ret = PrivateLoRaCryptoGetKey( keyId, p_remoteAddr, secKey );
    if( ret == PRVLORA_STATUS_OK )
    {
        // make block B0 : 0x49 | DevEUI | FCntUp | 0x00 | len(msg)
        blockB0[ 0 ] = 0x49;
        memcpyr( &blockB0[ 1 ], p_macAddr, PRVLORA_MACADDR_SIZE );  // need to reverse endian
        memcpy1( &blockB0[ 9 ], (const uint8_t *)&frameCounter, sizeof(uint32_t) );
        blockB0[ 13 ] = 0x00;
        blockB0[ 14 ] = 0x00;
        blockB0[ 15 ] = messageSize;

        // calculate MIC
        ret = PrivateLoRaCryptoComputeAesCmac( blockB0, 
                                               p_message, 
                                               (uint16_t)messageSize, 
                                               secKey, 
                                               &calcMic );
        if( ret == PRVLORA_STATUS_OK )
        {
            if( p_calcMic != NULL )
            {
                (*p_calcMic) = calcMic;
            }
        }
    }

    return ret;
}

/*!
 * calculate MIC for KeyReq/Res
 */
PrvLoRaStatus_t PrivateLoRaCryptoGetKeyMIC( PrvLoRaMacCryptoKeyId_t keyId,
                                            uint8_t                 *p_remoteAddr,
                                            uint8_t                 *p_message,
                                            uint8_t                 messageSize,
                                            uint32_t                *p_calcMic )
{
    PrvLoRaStatus_t     ret;
    uint8_t             secKey[ PRVLORA_CRYPTKEY_SIZE ];
    uint32_t            calcMic;

    // get security key
    ret = PrivateLoRaCryptoGetKey( keyId, p_remoteAddr, secKey );
    if( ret == PRVLORA_STATUS_OK )
    {
        // calculate MIC
        ret = PrivateLoRaCryptoComputeAesCmac( NULL, 
                                               p_message, 
                                               (uint16_t)messageSize, 
                                               secKey,
                                               &calcMic );
        if( ret == PRVLORA_STATUS_OK )
        {
            if( p_calcMic != NULL )
            {
                (*p_calcMic) = calcMic;
            }
        }
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------
// Security key

/*!
 * Make SessionKey
 */
PrvLoRaStatus_t PrivateLoRaCryptoMakeSessionKey( uint8_t *p_remoteAddr,
                                           uint8_t *p_devAddr,
                                           uint8_t *p_responderNonce,
                                           uint8_t *p_initiatorNonce,
                                           bool    isKeyReqInitiate )
{
    PrvLoRaStatus_t     ret;
    uint8_t             psk[ PRVLORA_CRYPTKEY_SIZE ];
    uint8_t             sessionKey[ PRVLORA_CRYPTKEY_SIZE ];
    uint8_t             *p_keyreqDevAddr, *p_keyresDevAddr;

    // initial check
    if( ( p_remoteAddr == NULL ) || ( p_responderNonce == NULL ) || ( p_initiatorNonce == NULL ) )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    // get security key
    ret = PrivateLoRaCryptoGetKey( PRVLORA_CRYPTO_PSK, p_remoteAddr, psk );

    // make Session Key
    if( ret == PRVLORA_STATUS_OK )
    {
        if( isKeyReqInitiate == true )
        {
            p_keyreqDevAddr = p_devAddr;
            p_keyresDevAddr = p_remoteAddr;
        }
        else
        {
            p_keyreqDevAddr = p_remoteAddr;
            p_keyresDevAddr = p_devAddr;
        }

        ret = PrivateLoRaCryptoDeriveSessionKey( psk, 
                                                 p_keyresDevAddr, 
                                                 p_keyreqDevAddr, 
                                                 p_responderNonce, 
                                                 p_initiatorNonce, 
                                                 sessionKey );

        if( ret == PRVLORA_STATUS_OK )
        {
            PrivateLoRaRemoteDevSetInitiatorNonce( p_remoteAddr, p_initiatorNonce );
            PrivateLoRaRemoteDevSetResponderNonce( p_remoteAddr, p_responderNonce );

            PrivateLoRaRemoteDevSetSessionKey( p_remoteAddr, sessionKey );
        }
    }

    return ret;
}

static PrvLoRaStatus_t PrivateLoRaCryptoDeriveSessionKey( uint8_t *p_key, 
                                                          uint8_t *p_remoteAddr, 
                                                          uint8_t *p_devAddr, 
                                                          uint8_t *p_responderNonce, 
                                                          uint8_t *p_initiatorNonce,
                                                          uint8_t *p_mac )
{
    const uint8_t label[] = "Private LoRa Session Key";
    uint8_t     data[ (PRVLORA_CRYPTO_NONCE_SIZE * 2) + (PRVLORA_MACADDR_SIZE * 2) ], *p_data;
    uint16_t    data_len;

    p_data = data;
    memcpy1( p_data, p_remoteAddr, PRVLORA_MACADDR_SIZE );
    p_data += PRVLORA_MACADDR_SIZE;
    memcpy1( p_data, p_devAddr, PRVLORA_MACADDR_SIZE );
    p_data += PRVLORA_MACADDR_SIZE;
    memcpy1( p_data, p_responderNonce, PRVLORA_CRYPTO_NONCE_SIZE );
    p_data += PRVLORA_CRYPTO_NONCE_SIZE;
    memcpy1( p_data, p_initiatorNonce, PRVLORA_CRYPTO_NONCE_SIZE );
    p_data += PRVLORA_CRYPTO_NONCE_SIZE;
    data_len = (uint16_t)( p_data - data );

    sha1_prf( p_key, PRVLORA_CRYPTKEY_SIZE, (const char *)label, data, data_len, p_mac, PRVLORA_CRYPTKEY_SIZE );

    return PRVLORA_STATUS_OK;
}

//--------------------------------------------------------------------------------------------------
// Nonce

/*!
 * Make nonce (random)
 */
void PrivateLoRaCryptoMakeNonce( uint8_t *p_nonce, uint8_t nonceLen )
{
    uint32_t    randVal;
    uint8_t     i;

    while( nonceLen > 0 )
    {
        randVal = Radio.Random();

        for( i = 0; i < sizeof(uint32_t); i++ )
        {
            (*p_nonce) = (uint8_t)randVal;

            // next
            nonceLen--;
            if( nonceLen == 0 )
            {
                break;  // exit from for(i) loop
            }

            p_nonce++;
            randVal >>= 8;
        }
    }
}
