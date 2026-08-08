/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    PrvLoRaMacIndirectTx.c
  * @author  Renesas Electronics Corporation
  * @brief   
**/


#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "board.h"

#include "PrivateLoRa.h"
#include "PrvLoRaMacIndirectTx.h"

/*--------*/
/* define */
#define PRVLORA_INDTX_NUMQUEUE          PRVLORA_CONFIG_INDIRECT_TX_QUEUE_MAXNUM
#define PRVLORA_INDTX_NUMREMOTEDEV      PRVLORA_CONFIG_REMOTE_DEVICE_MAXNUM

/*----------------*/
/* typedef (enum) */
typedef enum _PrvLoRaIndTxMessageType_t
{
    PRVLORA_INDTX_MSGTYPE_NONE = 0,
    PRVLORA_INDTX_MSGTYPE_MCPS,
    PRVLORA_INDTX_MSGTYPE_MLME_KEY,
    PRVLORA_INDTX_MSGTYPE_MLME_DEVINFO,
    PRVLORA_INDTX_MSGTYPE_MLME_TXCYCLE,
} PrvLoRaIndTxMessageType_t;

/*------------------------*/
/* typedef (struct/union) */

typedef struct _PrvLoRaIndTxQElmMcps_t
{
    uint8_t             txData[ PRVLORA_TXDATA_MAXSIZE ];
    uint8_t             txDataSize;
    uint16_t            txHandle;
    PrvLoRaTxOptions_t  txOptions;
} PrvLoRaIndTxQElmMcps_t;

typedef struct _PrvLoRaIndTxQElmMlmeKey_t
{
    PrvLoRaTxOptions_t          txOptions;
} PrvLoRaIndTxQElmMlmeKey_t;

typedef struct _PrvLoRaIndTxQElmMlmeDevInfo_t
{
    PrvLoRaTxOptions_t      txOptions;
} PrvLoRaIndTxQElmMlmeDevInfo_t;

typedef struct _PrvLoRaIndTxQElmMlmeTxCycle_t
{
    PrvLoRaTxOptions_t      txOptions;
    uint32_t                txCycleTime;
} PrvLoRaIndTxQElmMlmeTxCycle_t;

typedef struct _PrvLoRaIndTxQueue_t
{
    PrvLoRaIndTxMessageType_t   msgType;
    union
    {
        PrvLoRaIndTxQElmMcps_t          mcps;
        PrvLoRaIndTxQElmMlmeKey_t       mlmeKey;
        PrvLoRaIndTxQElmMlmeDevInfo_t   mlmeDevInfo;
        PrvLoRaIndTxQElmMlmeTxCycle_t   mlmeTxCycle;
    } msg;
    /*---*/
    struct _PrvLoRaIndTxQueue_t *p_indTxQChainNext;
} PrvLoRaIndTxQueue_t;

typedef struct _PrvLoRaIndTxRemoteDev_t
{
    bool                    isRegistered;
    uint8_t                 devAddress[ PRVLORA_MACADDR_SIZE ];
    PrvLoRaIndTxQueue_t     *p_indTxQ;
} PrvLoRaIndTxRemoteDev_t;

/*-------------------------*/
/* global variable (const) */

/*-----------------*/
/* global variable */

PrvLoRaIndTxQueue_t     PrvLoRaIndTxQueue[ PRVLORA_INDTX_NUMQUEUE ];
PrvLoRaIndTxRemoteDev_t PrvLoraIndTxRemoteDev[ PRVLORA_INDTX_NUMREMOTEDEV ];

/*--------------------*/
/* function prototype */
static PrvLoRaStatus_t PrivateLoRaIndTxGetRemoteDevElement( uint8_t                 *p_remoteAddr, 
                                                            PrvLoRaIndTxRemoteDev_t **pp_foundRmtDevElm );

static void PrivateLoRaIndTxEnqueue( PrvLoRaIndTxRemoteDev_t *p_indTxRemoteDev, 
                                     PrvLoRaIndTxQueue_t     *p_indTxQueueElm );
static PrvLoRaStatus_t PrivateLoRaIndTxGetFreeQueue( PrvLoRaIndTxQueue_t  **pp_indTxFreeQElm );
static void PrivateLoRaIndTxDisconnectQueueChain( PrvLoRaIndTxRemoteDev_t *p_indTxRemoteDev );

//--------------------------------------------------------------------------------------------------
// 

/*!
 * 
 */
PrvLoRaStatus_t PrivateLoRaIndTxInit( void )
{
    memset1( (uint8_t *)&PrvLoRaIndTxQueue, 0x00, sizeof(PrvLoRaIndTxQueue) );
    memset1( (uint8_t *)&PrvLoraIndTxRemoteDev, 0x00, sizeof(PrvLoraIndTxRemoteDev) );

    return PRVLORA_STATUS_OK;
}

/*!
 * 
 */
PrvLoRaStatus_t PrivateLoRaIndTxRegisterRemoteDevice( uint8_t *p_remoteAddr )
{
    PrvLoRaStatus_t         ret;
    PrvLoRaIndTxRemoteDev_t *p_indTxRemoteDev, *p_search;
    uint8_t                 i;
    int                     compare;

    // initial check
    if( p_remoteAddr == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    // init
    ret              = PRVLORA_STATUS_ERROR;
    p_indTxRemoteDev = NULL;
    p_search         = &PrvLoraIndTxRemoteDev[ 0 ];

    for( i = 0; i < PRVLORA_INDTX_NUMREMOTEDEV; i++ )
    {
        if( p_search->isRegistered == true )
        {
            compare = memcmp( p_remoteAddr, p_search->devAddress, PRVLORA_MACADDR_SIZE );
            if( compare == 0 )
            {
                // found (overwrite it)
                p_indTxRemoteDev = p_search;
                break;  // exit from for(i) loop
            }
        }
        else
        {
            if( p_indTxRemoteDev == NULL )
            {
                p_indTxRemoteDev = p_search;  // candidate element
            }
        }

        // next
        p_search++;
    }

    if( p_indTxRemoteDev != NULL )
    {
        // remote device
        memcpy1( p_indTxRemoteDev->devAddress, p_remoteAddr, PRVLORA_MACADDR_SIZE );
        PrivateLoRaIndTxDisconnectQueueChain( p_indTxRemoteDev );
        p_indTxRemoteDev->isRegistered = true;

        ret = PRVLORA_STATUS_OK;
    }

    return ret;
}

/*!
 * 
 */
PrvLoRaStatus_t PrivateLoRaIndTxUnregisterRemoteDevice( uint8_t *p_remoteAddr )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaIndTxRemoteDev_t     *p_indTxRemoteDev;

    // initial check
    if( p_remoteAddr == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    ret = PrivateLoRaIndTxGetRemoteDevElement( p_remoteAddr, &p_indTxRemoteDev );
    if( ret == PRVLORA_STATUS_OK )
    {
        PrivateLoRaIndTxDisconnectQueueChain( p_indTxRemoteDev );
        p_indTxRemoteDev->isRegistered = false;
    }

    return ret;
}

/*!
 * (Subfunction; called from PrivateLoRaIndTxEnqueue****Req only)
 */
static void PrivateLoRaIndTxEnqueue( PrvLoRaIndTxRemoteDev_t *p_indTxRemoteDev, 
                                     PrvLoRaIndTxQueue_t     *p_indTxQueueElm )
{
    PrvLoRaIndTxQueue_t     *p_indTxQueueChain;

    if( p_indTxRemoteDev->p_indTxQ == NULL )
    {
        p_indTxRemoteDev->p_indTxQ = p_indTxQueueElm;
    }
    else
    {
        p_indTxQueueChain = p_indTxRemoteDev->p_indTxQ;
        while( p_indTxQueueChain->p_indTxQChainNext != NULL )
        {
            p_indTxQueueChain = p_indTxQueueChain->p_indTxQChainNext;
        }
        p_indTxQueueChain->p_indTxQChainNext = p_indTxQueueElm;
    }
}

/*!
 * 
 */
PrvLoRaStatus_t PrivateLoRaIndTxEnqueueMcpsReq( PrvLoRaMcpsReq_t *p_mcpsReq )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaIndTxRemoteDev_t     *p_indTxRemoteDev;
    PrvLoRaIndTxQueue_t         *p_indTxQueueElm;
    PrvLoRaIndTxQElmMcps_t      *p_indTxMcps;

    // initial check
    if( p_mcpsReq == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    // init
    p_indTxRemoteDev  = NULL;
    p_indTxQueueElm   = NULL;
    p_indTxMcps       = NULL;

    ret = PrivateLoRaIndTxGetRemoteDevElement( p_mcpsReq->dstMacAddr, &p_indTxRemoteDev );
    if( ret == PRVLORA_STATUS_OK )
    {
        ret = PrivateLoRaIndTxGetFreeQueue( &p_indTxQueueElm );
        if( ret == PRVLORA_STATUS_OK )
        {
            p_indTxMcps = &( p_indTxQueueElm->msg.mcps );  // re-init

            memcpy1( p_indTxMcps->txData, p_mcpsReq->p_txData, p_mcpsReq->txDataSize );
            p_indTxMcps->txDataSize                   = p_mcpsReq->txDataSize;
            p_indTxMcps->txHandle                     = p_mcpsReq->txHandle;
            p_indTxMcps->txOptions.txOptValue         = p_mcpsReq->txOptions.txOptValue;
            p_indTxMcps->txOptions.options.IndirectTx = 0;

            p_indTxQueueElm->p_indTxQChainNext = NULL;
        }
    }

    if( ret == PRVLORA_STATUS_OK )
    {
        PrivateLoRaIndTxEnqueue( p_indTxRemoteDev, p_indTxQueueElm );
        p_indTxQueueElm->msgType = PRVLORA_INDTX_MSGTYPE_MCPS;
    }

    return ret;
}

/*!
 * 
 */
PrvLoRaStatus_t PrivateLoRaIndTxEnqueueMlmeReq( PrvLoRaMlmeReq_t *p_mlmeReq )
{
    PrvLoRaStatus_t                 ret;
    PrvLoRaIndTxRemoteDev_t         *p_indTxRemoteDev;
    PrvLoRaIndTxQueue_t             *p_indTxQueueElm;
    PrvLoRaIndTxMessageType_t       indTxMsgType;
    // (MLME-KEY)
    PrvLoRaMlmeKeyReq_t             *p_mlmeKey;
    PrvLoRaIndTxQElmMlmeKey_t       *p_indTxMlmeKey;
    // (MLME-DEVINFO)
    PrvLoRaMlmeDevInfoReq_t         *p_mlmeDevInfo;
    PrvLoRaIndTxQElmMlmeDevInfo_t   *p_indTxMlmeDevInfo;
    // (MLME-TXCYCLE)
    PrvLoRaMlmeTxCycleReq_t         *p_mlmeTxCycle;
    PrvLoRaIndTxQElmMlmeTxCycle_t   *p_indTxMlmeTxCycle;

    // initial check
    if( p_mlmeReq == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    // init
    p_indTxRemoteDev  = NULL;
    p_indTxQueueElm   = NULL;
    indTxMsgType      = PRVLORA_INDTX_MSGTYPE_NONE;

    ret = PrivateLoRaIndTxGetFreeQueue( &p_indTxQueueElm );
    if( ret == PRVLORA_STATUS_OK )
    {
        switch( p_mlmeReq->mlmeType )
        {
            case PRVLORA_MLME_KEY:
                p_mlmeKey      = &( p_mlmeReq->req.keyReq );
                p_indTxMlmeKey = &( p_indTxQueueElm->msg.mlmeKey );

                ret = PrivateLoRaIndTxGetRemoteDevElement( p_mlmeKey->dstMacAddr, &p_indTxRemoteDev );
                if( ret == PRVLORA_STATUS_OK )
                {
                    p_indTxMlmeKey->txOptions.txOptValue         = p_mlmeKey->txOptions.txOptValue;
                    p_indTxMlmeKey->txOptions.options.IndirectTx = 0;
                    indTxMsgType = PRVLORA_INDTX_MSGTYPE_MLME_KEY;
                }
                break;

            case PRVLORA_MLME_DEVINFO:
                p_mlmeDevInfo      = &( p_mlmeReq->req.devInfoReq );
                p_indTxMlmeDevInfo = &( p_indTxQueueElm->msg.mlmeDevInfo );

                ret = PrivateLoRaIndTxGetRemoteDevElement( p_mlmeDevInfo->dstMacAddr, &p_indTxRemoteDev );
                if( ret == PRVLORA_STATUS_OK )
                {
                    p_indTxMlmeDevInfo->txOptions.txOptValue         = p_mlmeDevInfo->txOptions.txOptValue;
                    p_indTxMlmeDevInfo->txOptions.options.IndirectTx = 0;
                    indTxMsgType = PRVLORA_INDTX_MSGTYPE_MLME_DEVINFO;
                }
                break;

            case PRVLORA_MLME_TXCYCLE:
                p_mlmeTxCycle      = &( p_mlmeReq->req.txCycleReq );
                p_indTxMlmeTxCycle = &( p_indTxQueueElm->msg.mlmeTxCycle );

                ret = PrivateLoRaIndTxGetRemoteDevElement( p_mlmeTxCycle->dstMacAddr, &p_indTxRemoteDev );
                if( ret == PRVLORA_STATUS_OK )
                {
                    p_indTxMlmeTxCycle->txOptions.txOptValue         = p_mlmeTxCycle->txOptions.txOptValue;
                    p_indTxMlmeTxCycle->txOptions.options.IndirectTx = 0;
                    p_indTxMlmeTxCycle->txCycleTime                  = p_mlmeTxCycle->txCycleTime;
                    indTxMsgType = PRVLORA_INDTX_MSGTYPE_MLME_TXCYCLE;
                }
                break;

            default:
                ret = PRVLORA_STATUS_REQUSET_INVALID;
                break;
        }

        p_indTxQueueElm->p_indTxQChainNext = NULL;
    }

    if( ret == PRVLORA_STATUS_OK )
    {
        PrivateLoRaIndTxEnqueue( p_indTxRemoteDev, p_indTxQueueElm );
        p_indTxQueueElm->msgType = indTxMsgType;
    }

    return ret;
}

/*!
 * 
 */
PrvLoRaStatus_t PrivateLoRaIndTxDequeueAndSend( uint8_t *p_remoteAddr )
{
    PrvLoRaStatus_t                 ret;
    PrvLoRaIndTxRemoteDev_t         *p_indTxRemoteDev;
    PrvLoRaIndTxQueue_t             *p_indTxQueueElm;
    PrvLoRaMcpsReq_t                mcpsReq;
    PrvLoRaMlmeReq_t                mlmeReq;
    // (MLME-KEY)
    PrvLoRaMlmeKeyReq_t             *p_mlmeKey;
    PrvLoRaIndTxQElmMlmeKey_t       *p_indTxMlmeKey;
    // (MLME-DEVINFO)
    PrvLoRaMlmeDevInfoReq_t         *p_mlmeDevInfo;
    PrvLoRaIndTxQElmMlmeDevInfo_t   *p_indTxMlmeDevInfo;
    // (MLME-TXCYCLE)
    PrvLoRaMlmeTxCycleReq_t         *p_mlmeTxCycle;
    PrvLoRaIndTxQElmMlmeTxCycle_t   *p_indTxMlmeTxCycle;

    // initial check
    if( p_remoteAddr == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    // init
    p_indTxRemoteDev  = NULL;
    p_indTxQueueElm   = NULL;
    memset1( (uint8_t *)&mcpsReq, 0x00, sizeof(PrvLoRaMcpsReq_t) );
    memset1( (uint8_t *)&mlmeReq, 0x00, sizeof(PrvLoRaMlmeReq_t) );

    ret = PrivateLoRaIndTxGetRemoteDevElement( p_remoteAddr, &p_indTxRemoteDev );
    if( ret == PRVLORA_STATUS_OK )
    {
        if( p_indTxRemoteDev->p_indTxQ != NULL )
        {
            p_indTxQueueElm = p_indTxRemoteDev->p_indTxQ;  // re-init

            switch( p_indTxQueueElm->msgType )
            {
                case PRVLORA_INDTX_MSGTYPE_MCPS:
                    memcpy1( &(mcpsReq.dstMacAddr[0]), p_remoteAddr, PRVLORA_MACADDR_SIZE );
                    mcpsReq.p_txData             = p_indTxQueueElm->msg.mcps.txData;
                    mcpsReq.txDataSize           = p_indTxQueueElm->msg.mcps.txDataSize;
                    mcpsReq.txHandle             = p_indTxQueueElm->msg.mcps.txHandle;
                    mcpsReq.txOptions.txOptValue = p_indTxQueueElm->msg.mcps.txOptions.txOptValue;

                    ret = PrivateLoRaMcpsRequest( &mcpsReq );
                    break;

                case PRVLORA_INDTX_MSGTYPE_MLME_KEY:
                    p_mlmeKey      = &( mlmeReq.req.keyReq );
                    p_indTxMlmeKey = &( p_indTxQueueElm->msg.mlmeKey );

                    mlmeReq.mlmeType = PRVLORA_MLME_KEY;
                    memcpy1( &(p_mlmeKey->dstMacAddr[0]), p_remoteAddr, PRVLORA_MACADDR_SIZE );
                    p_mlmeKey->txOptions.txOptValue = p_indTxMlmeKey->txOptions.txOptValue;

                    ret = PrivateLoRaMlmeRequest( &mlmeReq );
                    break;

                case PRVLORA_INDTX_MSGTYPE_MLME_DEVINFO:
                    p_mlmeDevInfo      = &( mlmeReq.req.devInfoReq );
                    p_indTxMlmeDevInfo = &( p_indTxQueueElm->msg.mlmeDevInfo );

                    mlmeReq.mlmeType = PRVLORA_MLME_DEVINFO;
                    memcpy1( &(p_mlmeDevInfo->dstMacAddr[0]), p_remoteAddr, PRVLORA_MACADDR_SIZE );
                    p_mlmeDevInfo->txOptions.txOptValue = p_indTxMlmeDevInfo->txOptions.txOptValue;

                    ret = PrivateLoRaMlmeRequest( &mlmeReq );
                    break;

                case PRVLORA_INDTX_MSGTYPE_MLME_TXCYCLE:
                    p_mlmeTxCycle      = &( mlmeReq.req.txCycleReq );
                    p_indTxMlmeTxCycle = &( p_indTxQueueElm->msg.mlmeTxCycle );

                    mlmeReq.mlmeType = PRVLORA_MLME_TXCYCLE;
                    memcpy( &(p_mlmeTxCycle->dstMacAddr[0]), p_remoteAddr, PRVLORA_MACADDR_SIZE );
                    p_mlmeTxCycle->txOptions.txOptValue = p_indTxMlmeTxCycle->txOptions.txOptValue;
                    p_mlmeTxCycle->txCycleTime          = p_indTxMlmeTxCycle->txCycleTime;

                    ret = PrivateLoRaMlmeRequest( &mlmeReq );
                    break;

                default:
                    ret = PRVLORA_STATUS_ERROR;
                    break;
            }

            // Dequeue
            p_indTxRemoteDev->p_indTxQ = p_indTxQueueElm->p_indTxQChainNext;
            memset1( (uint8_t *)p_indTxQueueElm, 0x00, sizeof(PrvLoRaIndTxQueue_t) );
        }
    }

    return ret;
}


//--------------------------------------------------------------------------------------------------
// static functions

/*!
 * 
 */
static PrvLoRaStatus_t PrivateLoRaIndTxGetRemoteDevElement( uint8_t                 *p_remoteAddr, 
                                                            PrvLoRaIndTxRemoteDev_t **pp_foundRmtDevElm )
{
    PrvLoRaStatus_t         ret;
    PrvLoRaIndTxRemoteDev_t *p_indRemoteDev;
    uint8_t                 i;
    int                     compare;

    // init
    ret            = PRVLORA_STATUS_NO_REMOTE_DEVICE_ENTRY;
    p_indRemoteDev = &PrvLoraIndTxRemoteDev[ 0 ];

    for( i = 0; i < PRVLORA_INDTX_NUMREMOTEDEV; i++ )
    {
        if( p_indRemoteDev->isRegistered == true )
        {
            compare = memcmp( p_remoteAddr, p_indRemoteDev->devAddress, PRVLORA_MACADDR_SIZE );
            if( compare == 0 )
            {
                // found
                (*pp_foundRmtDevElm) = p_indRemoteDev;

                ret = PRVLORA_STATUS_OK;
                break;      // exit from for(i) loop
            }
        }

        // next
        p_indRemoteDev++;
    }

    return ret;
}

static PrvLoRaStatus_t PrivateLoRaIndTxGetFreeQueue( PrvLoRaIndTxQueue_t **pp_indTxFreeQElm )
{
    PrvLoRaStatus_t         ret;
    PrvLoRaIndTxQueue_t     *p_indTxQueue;
    uint8_t                 i;

    // init
    ret          = PRVLORA_STATUS_INSUFFICIENT_MEMORY;
    p_indTxQueue = &PrvLoRaIndTxQueue[ 0 ];

    for( i = 0; i < PRVLORA_INDTX_NUMQUEUE; i++ )
    {
        if( p_indTxQueue->msgType == PRVLORA_INDTX_MSGTYPE_NONE )
        {
            // found
            (*pp_indTxFreeQElm) = p_indTxQueue;

            ret = PRVLORA_STATUS_OK;
            break;      // exit from for(i) loop
        }

        // next
        p_indTxQueue++;
    }

    return ret;
}

static void PrivateLoRaIndTxDisconnectQueueChain( PrvLoRaIndTxRemoteDev_t *p_indTxRemoteDev )
{
    PrvLoRaIndTxQueue_t     *p_indTxQueue, *p_indTxQueueNext;

    p_indTxQueue = p_indTxRemoteDev->p_indTxQ;
    while( p_indTxQueue != NULL )
    {
        p_indTxQueueNext = p_indTxQueue->p_indTxQChainNext;
        memset1( (uint8_t *)p_indTxQueue, 0x00, sizeof(PrvLoRaIndTxQueue_t) );
        p_indTxQueue = p_indTxQueueNext;
    }
}
