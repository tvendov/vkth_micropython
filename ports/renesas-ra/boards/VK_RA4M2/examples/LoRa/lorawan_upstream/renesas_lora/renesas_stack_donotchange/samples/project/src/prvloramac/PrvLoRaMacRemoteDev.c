/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    PrvLoRaMacRemoteDev.c
  * @author  Renesas Electronics Corporation
  * @brief   
**/


#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "board.h"

#include "PrivateLoRa.h"
#include "PrvLoRaMacRemoteDev.h"
#include "PrvLoRaMacCrypto.h"

/*--------*/
/* define */
#define PRVLORA_RMTDEV_MAXNUM       PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM


/*----------------*/
/* typedef (enum) */

/*------------------------*/
/* typedef (struct/union) */

typedef struct _PrvLoRaRemoteDevElement_t
{
    bool        isRegistered;
    uint16_t    updatedParams;
    uint8_t     devAddress[ PRVLORA_MACADDR_SIZE ];
    uint8_t     secPsk[ PRVLORA_CRYPTKEY_SIZE ];
    uint8_t     secSessionKey[ PRVLORA_CRYPTKEY_SIZE ];
    uint32_t    frameCounterTx;
    uint32_t    frameCounterRx;
    uint8_t     initiatorNonce[ PRVLORA_CRYPTO_NONCE_SIZE ];
    uint8_t     responderNonce[ PRVLORA_CRYPTO_NONCE_SIZE ];
} PrvLoRaRemoteDevElement_t;

typedef struct _PrvLoRaRemoteDevMng_t
{
    uint8_t                     numRemoteDev;
    PrvLoRaRemoteDevElement_t   remoteDevice[ PRVLORA_RMTDEV_MAXNUM ];
} PrvLoRaRemoteDevMng_t;

/*-------------------------*/
/* global variable (const) */

/*-----------------*/
/* global variable */

PrvLoRaRemoteDevMng_t   PrvLoRaDeviceMng = { .numRemoteDev = 0 };

/*--------------------*/
/* function prototype */
static PrvLoRaStatus_t PrivateLoRaRemoteDevGetDevElement( uint8_t                   *p_remoteAddr, 
                                                          PrvLoRaRemoteDevElement_t **pp_foundDevElm );


//--------------------------------------------------------------------------------------------------
// 

/*!
 * Initialization
 */
void PrivateLoRaRemoteDevInit( void )
{
    PrivateLoRaRemoteDevUnregisterAll();
}

/*!
 * Register remote device information
 */
PrvLoRaStatus_t PrivateLoRaRemoteDevRegister( uint8_t  *p_remoteAddr, 
                                              uint8_t  *p_psk,
                                              uint8_t  *p_sessionKey,
                                              uint32_t initialFrameCounterTx,
                                              uint32_t initialFrameCounterRx )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaRemoteDevElement_t   *p_remoteDevElm, *p_search;
    uint8_t                     i;
    int                         compare;

    // initial check
    if( p_remoteAddr == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    // init
    ret            = PRVLORA_STATUS_ERROR;
    p_remoteDevElm = NULL;
    p_search       = &( PrvLoRaDeviceMng.remoteDevice[ 0 ] );

    for( i = 0; i < PRVLORA_RMTDEV_MAXNUM; i++ )
    {
        if( p_search->isRegistered == true )
        {
            compare = memcmp( p_remoteAddr, p_search->devAddress, PRVLORA_MACADDR_SIZE );
            if( compare == 0 )
            {
                // found (overwrite it)
                p_remoteDevElm = p_search;
                break;  // exit from for(i) loop
            }
        }
        else
        {
            if( p_remoteDevElm == NULL )
            {
                p_remoteDevElm = p_search;  // candidate element
            }
        }

        // next
        p_search++;
    }

    if( p_remoteDevElm != NULL )
    {
        // init element
        memset1( (uint8_t *)p_remoteDevElm, 0x00, sizeof(PrvLoRaRemoteDevElement_t) );

        // remote device
        memcpy1( p_remoteDevElm->devAddress, p_remoteAddr, PRVLORA_MACADDR_SIZE );
        // PSK
        if( p_psk != NULL )
        {
            memcpy1( p_remoteDevElm->secPsk,  p_psk, PRVLORA_CRYPTKEY_SIZE );
        }
        // Session key
        if( p_sessionKey != NULL )
        {
            memcpy1( p_remoteDevElm->secSessionKey, p_sessionKey, PRVLORA_CRYPTKEY_SIZE );
        }
        else
        {
            memcpy1( p_remoteDevElm->secSessionKey, p_psk, PRVLORA_CRYPTKEY_SIZE );  // set PSK to SessionKey
        }
        // frame counter
        p_remoteDevElm->frameCounterTx = initialFrameCounterTx;
        p_remoteDevElm->frameCounterRx = initialFrameCounterRx;

        p_remoteDevElm->isRegistered = true;

        if( p_remoteDevElm != p_search )
        {
            PrvLoRaDeviceMng.numRemoteDev++;
        }

        ret = PRVLORA_STATUS_OK;
    }

    return ret;
}

/*!
 * Unregister remote device information
 */
PrvLoRaStatus_t PrivateLoRaRemoteDevUnregister( uint8_t *p_remoteAddr )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaRemoteDevElement_t   *p_remoteDevElm;

    // initial check
    if( p_remoteAddr == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    ret = PrivateLoRaRemoteDevGetDevElement( p_remoteAddr, &p_remoteDevElm );
    if( ret == PRVLORA_STATUS_OK )
    {
        p_remoteDevElm->isRegistered = false;
        PrvLoRaDeviceMng.numRemoteDev--;
    }

    return ret;
}

/*!
 * Unregister all remote device information
 */
void PrivateLoRaRemoteDevUnregisterAll( void )
{
    memset1( (uint8_t *)&PrvLoRaDeviceMng, 0x00, sizeof(PrvLoRaDeviceMng) );
}

/*!
 * Search remode device
 */
PrvLoRaStatus_t PrivateLoRaRemoteDevSearchDevice( uint8_t *p_remoteAddr )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaRemoteDevElement_t   *p_remoteDevElm;  // dummy

    // initial check
    if( p_remoteAddr == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    ret = PrivateLoRaRemoteDevGetDevElement( p_remoteAddr, &p_remoteDevElm );

    return ret;
}

/*!
 * 
 */
uint8_t PrivateLoRaRemoteDevGetNumRegistered( void )
{
    return PrvLoRaDeviceMng.numRemoteDev;
}

/*!
 * 
 */
uint8_t PrivateLoRaRemoteDevGetRegisteredDeviceList( uint8_t *p_remoteList[] )
{
    PrvLoRaRemoteDevElement_t   *p_remoteDevElm;
    uint8_t                     i, numReg;

    // init
    p_remoteDevElm = &( PrvLoRaDeviceMng.remoteDevice[ 0 ] );

    numReg = 0;
    for( i = 0; i < PRVLORA_RMTDEV_MAXNUM; i++ )
    {
        if( p_remoteDevElm->isRegistered == true )
        {
            p_remoteList[ numReg ] = p_remoteDevElm->devAddress;
            numReg++;
        }

        // next
        p_remoteDevElm++;
    }

    return PrvLoRaDeviceMng.numRemoteDev;
}

//--------------------------------------------------------------------------------------------------
// Set/Get

/*!
 * 
 */
PrvLoRaStatus_t PrivateLoRaRemoteDevGetPSK( uint8_t *p_remoteAddr, uint8_t *p_psk )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaRemoteDevElement_t   *p_remoteDevElm;

    // initial check
    if( ( p_remoteAddr == NULL ) || ( p_psk == NULL ) )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    ret = PrivateLoRaRemoteDevGetDevElement( p_remoteAddr, &p_remoteDevElm );
    if( ret == PRVLORA_STATUS_OK )
    {
        memcpy1( p_psk, p_remoteDevElm->secPsk, PRVLORA_CRYPTKEY_SIZE );
    }

    return ret;
}

/*!
 * 
 */
PrvLoRaStatus_t PrivateLoRaRemoteDevGetSessionKey( uint8_t *p_remoteAddr, uint8_t *p_sessionKey )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaRemoteDevElement_t   *p_remoteDevElm;

    // initial check
    if( ( p_remoteAddr == NULL ) || ( p_sessionKey == NULL ) )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    ret = PrivateLoRaRemoteDevGetDevElement( p_remoteAddr, &p_remoteDevElm );
    if( ret == PRVLORA_STATUS_OK )
    {
        memcpy1( p_sessionKey, p_remoteDevElm->secSessionKey, PRVLORA_CRYPTKEY_SIZE );
    }

    return ret;
}

/*!
 * 
 */
PrvLoRaStatus_t PrivateLoRaRemoteDevSetSessionKey( uint8_t *p_remoteAddr, uint8_t *p_sessionKey )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaRemoteDevElement_t   *p_remoteDevElm;
    int                         compare;

    // initial check
    if( ( p_remoteAddr == NULL ) || ( p_sessionKey == NULL ) )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    ret = PrivateLoRaRemoteDevGetDevElement( p_remoteAddr, &p_remoteDevElm );
    if( ret == PRVLORA_STATUS_OK )
    {
        compare = memcmp( p_remoteDevElm->secSessionKey, p_sessionKey, PRVLORA_CRYPTKEY_SIZE );
        if( compare != 0 )
        {
            memcpy1( p_remoteDevElm->secSessionKey, p_sessionKey, PRVLORA_CRYPTKEY_SIZE );
            p_remoteDevElm->updatedParams |= PRVLORA_REMOTEDEV_UPDATED_SESSION_KEY;
        }
    }

    return ret;
}

/*!
 * 
 */
PrvLoRaStatus_t PrivateLoRaRemoteDevGetFrameCounterTx( uint8_t *p_remoteAddr, uint32_t *p_frameCounterTx )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaRemoteDevElement_t   *p_remoteDevElm;

    // initial check
    if( ( p_remoteAddr == NULL ) || ( p_frameCounterTx == NULL ) )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    ret = PrivateLoRaRemoteDevGetDevElement( p_remoteAddr, &p_remoteDevElm );
    if( ret == PRVLORA_STATUS_OK )
    {
        (*p_frameCounterTx) = p_remoteDevElm->frameCounterTx;
    }

    return ret;
}

/*!
 * 
 */
PrvLoRaStatus_t PrivateLoRaRemoteDevSetFrameCounterTx( uint8_t *p_remoteAddr, uint32_t frameCounterTx )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaRemoteDevElement_t   *p_remoteDevElm;

    // initial check
    if( p_remoteAddr == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    ret = PrivateLoRaRemoteDevGetDevElement( p_remoteAddr, &p_remoteDevElm );
    if( ret == PRVLORA_STATUS_OK )
    {
        if( p_remoteDevElm->frameCounterTx != frameCounterTx )
        {
            p_remoteDevElm->frameCounterTx = frameCounterTx;
            p_remoteDevElm->updatedParams |= PRVLORA_REMOTEDEV_UPDATED_FCNT_TX;
        }
    }

    return ret;
}

/*!
 * 
 */
PrvLoRaStatus_t PrivateLoRaRemoteDevGetFrameCounterRx( uint8_t *p_remoteAddr, uint32_t *p_frameCounterRx )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaRemoteDevElement_t   *p_remoteDevElm;

    // initial check
    if( ( p_remoteAddr == NULL ) || ( p_frameCounterRx == NULL ) )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    ret = PrivateLoRaRemoteDevGetDevElement( p_remoteAddr, &p_remoteDevElm );
    if( ret == PRVLORA_STATUS_OK )
    {
        (*p_frameCounterRx) = p_remoteDevElm->frameCounterRx;
    }

    return ret;
}

/*!
 * 
 */
PrvLoRaStatus_t PrivateLoRaRemoteDevSetFrameCounterRx( uint8_t *p_remoteAddr, uint32_t frameCounterRx )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaRemoteDevElement_t   *p_remoteDevElm;

    // initial check
    if( p_remoteAddr == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    ret = PrivateLoRaRemoteDevGetDevElement( p_remoteAddr, &p_remoteDevElm );
    if( ret == PRVLORA_STATUS_OK )
    {
        if( p_remoteDevElm->frameCounterRx != frameCounterRx )
        {
            p_remoteDevElm->frameCounterRx = frameCounterRx;
            p_remoteDevElm->updatedParams |= PRVLORA_REMOTEDEV_UPDATED_FCNT_RX;
        }
    }

    return ret;
}

/*!
 * 
 */
PrvLoRaStatus_t PrivateLoRaRemoteDevGetInitiatorNonce( uint8_t *p_remoteAddr, uint8_t *p_initiatorNonce )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaRemoteDevElement_t   *p_remoteDevElm;

    // initial check
    if( ( p_remoteAddr == NULL ) || ( p_initiatorNonce == NULL ) )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    ret = PrivateLoRaRemoteDevGetDevElement( p_remoteAddr, &p_remoteDevElm );
    if( ret == PRVLORA_STATUS_OK )
    {
        memcpy1( p_initiatorNonce, p_remoteDevElm->initiatorNonce, PRVLORA_CRYPTO_NONCE_SIZE );
    }

    return ret;
}

/*!
 * 
 */
PrvLoRaStatus_t PrivateLoRaRemoteDevSetInitiatorNonce( uint8_t *p_remoteAddr, uint8_t *p_initiatorNonce )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaRemoteDevElement_t   *p_remoteDevElm;

    // initial check
    if( ( p_remoteAddr == NULL ) || ( p_initiatorNonce == NULL ) )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    ret = PrivateLoRaRemoteDevGetDevElement( p_remoteAddr, &p_remoteDevElm );
    if( ret == PRVLORA_STATUS_OK )
    {
        memcpy1( p_remoteDevElm->initiatorNonce, p_initiatorNonce, PRVLORA_CRYPTO_NONCE_SIZE );
    }

    return ret;
}

/*!
 * 
 */
PrvLoRaStatus_t PrivateLoRaRemoteDevGetResponderNonce( uint8_t *p_remoteAddr, uint8_t *p_responderNonce )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaRemoteDevElement_t   *p_remoteDevElm;

    // initial check
    if( ( p_remoteAddr == NULL ) || ( p_responderNonce == NULL ) )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    ret = PrivateLoRaRemoteDevGetDevElement( p_remoteAddr, &p_remoteDevElm );
    if( ret == PRVLORA_STATUS_OK )
    {
        memcpy1( p_responderNonce, p_remoteDevElm->responderNonce, PRVLORA_CRYPTO_NONCE_SIZE );
    }

    return ret;
}

/*!
 * 
 */
PrvLoRaStatus_t PrivateLoRaRemoteDevSetResponderNonce( uint8_t *p_remoteAddr, uint8_t *p_responderNonce )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaRemoteDevElement_t   *p_remoteDevElm;

    // initial check
    if( ( p_remoteAddr == NULL ) || ( p_responderNonce == NULL ) )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    ret = PrivateLoRaRemoteDevGetDevElement( p_remoteAddr, &p_remoteDevElm );
    if( ret == PRVLORA_STATUS_OK )
    {
        memcpy1( p_remoteDevElm->responderNonce, p_responderNonce, PRVLORA_CRYPTO_NONCE_SIZE );
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------
// 
PrvLoRaStatus_t PrivateLoRaRemoteDevGetUpdatedElement( PrvLoRaRemoteDevUpdated_t *p_updtRemoteDev )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaRemoteDevElement_t   *p_remoteDevElm;
    uint8_t                     i;

    // initial check
    if( p_updtRemoteDev == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    // init
    ret            = PRVLORA_STATUS_NO_REMOTE_DEVICE_ENTRY;
    p_remoteDevElm = &( PrvLoRaDeviceMng.remoteDevice[ 0 ] );

    for( i = 0; i < PRVLORA_RMTDEV_MAXNUM; i++ )
    {
        if( p_remoteDevElm->isRegistered == true )
        {
            if( p_remoteDevElm->updatedParams != (uint16_t)0 )
            {
                memcpy1( p_updtRemoteDev->devAddress, p_remoteDevElm->devAddress, PRVLORA_MACADDR_SIZE  );
                memcpy1( p_updtRemoteDev->secPsk,  p_remoteDevElm->secPsk,  PRVLORA_CRYPTKEY_SIZE );
                memcpy1( p_updtRemoteDev->secSessionKey, p_remoteDevElm->secSessionKey, PRVLORA_CRYPTKEY_SIZE );
                p_updtRemoteDev->frameCounterTx = p_remoteDevElm->frameCounterTx;
                p_updtRemoteDev->frameCounterRx = p_remoteDevElm->frameCounterRx;

                p_updtRemoteDev->updatedParams = p_remoteDevElm->updatedParams;
                p_remoteDevElm->updatedParams  = (uint16_t)0;

                ret = PRVLORA_STATUS_OK;
                break;      // exit from for(i) loop
            }
        }

        // next
        p_remoteDevElm++;
    }

    return ret;
}


//--------------------------------------------------------------------------------------------------
// static functions

/*!
 * 
 */
static PrvLoRaStatus_t PrivateLoRaRemoteDevGetDevElement( uint8_t                   *p_remoteAddr, 
                                                          PrvLoRaRemoteDevElement_t **pp_foundRmtDevElm )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaRemoteDevElement_t   *p_remoteDevElm;
    uint8_t                     i;
    int                         compare;

    // init
    ret            = PRVLORA_STATUS_NO_REMOTE_DEVICE_ENTRY;
    p_remoteDevElm = &( PrvLoRaDeviceMng.remoteDevice[ 0 ] );

    for( i = 0; i < PRVLORA_RMTDEV_MAXNUM; i++ )
    {
        if( p_remoteDevElm->isRegistered == true )
        {
            compare = memcmp( p_remoteAddr, p_remoteDevElm->devAddress, PRVLORA_MACADDR_SIZE );
            if( compare == 0 )
            {
                // found
                (*pp_foundRmtDevElm) = p_remoteDevElm;
                ret = PRVLORA_STATUS_OK;
                break;      // exit from for(i) loop
            }
        }

        // next
        p_remoteDevElm++;
    }

    return ret;
}
