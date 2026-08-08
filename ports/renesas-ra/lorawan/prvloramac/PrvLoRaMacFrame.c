/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    PrvLoRaMacFrame.c
  * @author  Renesas Electronics Corporation
  * @brief   
**/


#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "board.h"

#include "PrivateLoRa.h"
#include "PrvLoRaMacFrame.h"
#include "PrvLoRaMacCrypto.h"
#include "PrvLoRaMacRemoteDev.h"

/*--------*/
/* define */
#define PRVLORA_FRAME_SIZE_MHDR_NOSEC   ( sizeof(PrvLoRaFrameMhdr_t) )
#define PRVLORA_FRAME_SIZE_MHDR_SEC     ( sizeof(PrvLoRaFrameMhdrSec_t) )

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


//--------------------------------------------------------------------------------------------------
// Tx (make frame)

PrvLoRaStatus_t PrivateLoRaFrameGetTxFrameSize( uint8_t            *p_txFrameSize,
                                                uint8_t            txPayloadSize, 
                                                PrvLoRaTxOptions_t txOptions )
{
    PrvLoRaStatus_t     ret;
    uint16_t            frameSize;

    // initial check
    if( p_txFrameSize == NULL )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    // init
    ret       = PRVLORA_STATUS_PARAMETER_INVALID;
    frameSize = 0;

    if( txOptions.options.SecEnable == 1 )
    {
        frameSize = (uint16_t)PRVLORA_FRAME_SIZE_MHDR_SEC + 
                    (uint16_t)txPayloadSize + 
                    (uint16_t)PRVLORA_FRAME_SIZE_MIC;
    }
    else
    {
        frameSize = (uint16_t)PRVLORA_FRAME_SIZE_MHDR_NOSEC + 
                    (uint16_t)txPayloadSize;
    }
    if( frameSize <= (uint16_t)PRVLORA_FRAME_MAXSIZE )
    {
        (*p_txFrameSize) = (uint8_t)frameSize;
        ret              = PRVLORA_STATUS_OK;
    }

    return ret;
}

/*!
 * Make Tx frame
 */
PrvLoRaStatus_t PrivateLoRaFrameMakeTx( PrvLoRaFrame_t     *p_txFrame,
                                        uint8_t            frameType,
                                        bool               isAck,
                                        uint8_t            *p_dstAddr,
                                        uint8_t            *p_srcAddr,
                                        uint8_t            *p_txPayload,
                                        uint8_t            txPayloadSize,
                                        PrvLoRaTxOptions_t txOptions )
{
    PrvLoRaStatus_t         ret;
    PrvLoRaFrameMhdr_t      *p_frameMhdr;
    PrvLoRaFrameMhdrSec_t   *p_frameMhdrSec;
    uint8_t                 *p_framePayload;
    uint8_t                 *p_frameMic;
    uint16_t                frameSize;
    uint8_t                 *p_payloadSec;
    uint8_t                 payloadSecSize;
    PrvLoRaMacCryptoKeyId_t cryptoKeyId;
    uint32_t                frameCounter;
    uint32_t                valueMIC;
    bool                    isKeyRes;

    // initial check
    if( p_txFrame == NULL )
    {
        return PRVLORA_STATUS_ERROR;
    }
    if( ( p_dstAddr == NULL ) || ( p_srcAddr == NULL ) )
    {
        return PRVLORA_STATUS_ERROR;
    }
    if( ( p_txPayload == NULL ) && ( txPayloadSize > 0 ) )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    // init
    frameCounter   = (uint32_t)0;
    cryptoKeyId    = PRVLORA_CRYPTO_SESSION_KEY;
    p_payloadSec   = NULL;
    payloadSecSize = 0;
    isKeyRes       = false;

    //------------------
    // check frame size
    ret = PrivateLoRaFrameGetTxFrameSize( (uint8_t *)&frameSize, txPayloadSize, txOptions );

    //------------------
    // make header
    if( ret == PRVLORA_STATUS_OK )
    {
        switch( frameType )
        {
            case PRVLORA_FRAME_TYPE_MACCMD:
                if( txPayloadSize == 0 )
                {
                    ret = PRVLORA_STATUS_PARAMETER_INVALID;
                    break;
                }
                /* no break */

            case PRVLORA_FRAME_TYPE_DATA:
                // init
                p_frameMhdr    = &( p_txFrame->frame.frameMhdr );
                p_frameMhdrSec = &( p_txFrame->frame.frameMhdrSec );

                // format frame buffer
                memset1( p_txFrame->frame.frameBuffer, 0x00, PRVLORA_FRAME_MAXSIZE );

                // set frame control
                p_frameMhdr->frameControl.frameType = frameType;
                p_frameMhdr->frameControl.secEnabled = txOptions.options.SecEnable;
                p_frameMhdr->frameControl.ackRequest = txOptions.options.AckRequest;
                if( isAck == true )
                {
                    p_frameMhdr->frameControl.ack = 1;
                }
                else
                {
                    p_frameMhdr->frameControl.ack = 0;
                }

                // set src/dst (change endian to little)
                memcpyr( p_frameMhdr->dstAddrLE, p_dstAddr, PRVLORA_MACADDR_SIZE );
                memcpyr( p_frameMhdr->srcAddrLE, p_srcAddr, PRVLORA_MACADDR_SIZE );

                // set frame counter
                if( txOptions.options.SecEnable == 1 )
                {
                    // frame counter (security)
                    ret = PrivateLoRaRemoteDevGetFrameCounterTx( p_dstAddr, &frameCounter );
                    if( ret == PRVLORA_STATUS_OK )
                    {
                        memcpy1( p_frameMhdrSec->frameCounterLE, 
                                 (const uint8_t*)&frameCounter, 
                                 PRVLORA_FRAME_SIZE_FRAMECOUNTER );
                    }
                }
                break;

            default:
                ret = PRVLORA_STATUS_PARAMETER_INVALID;
                break;
        }
    }

    //------------------
    // make payload
    if( ret == PRVLORA_STATUS_OK )
    {
        if( txOptions.options.SecEnable == 1 )
        {
            p_framePayload = &( p_txFrame->frame.frameBuffer[ PRVLORA_FRAME_SIZE_MHDR_SEC ] );

            switch( frameType )
            {
                case PRVLORA_FRAME_TYPE_DATA:
                    p_payloadSec   = p_framePayload;
                    payloadSecSize = txPayloadSize;
                    break;

                case PRVLORA_FRAME_TYPE_MACCMD:
                    p_payloadSec   = &( p_framePayload[ 1 ] );  // CID is not encrypted
                    payloadSecSize = txPayloadSize - 1;

                   switch( p_txPayload[ 0 ] )
                    {
                        // if command is KeyReq; non-secured.
                        case PRVLORA_FRAME_MACCMD_CID_KEYREQ:
                            p_payloadSec   = NULL;
                            payloadSecSize = 0;
                            break;

                        // if command is KeyRes: use PSK
                        case PRVLORA_FRAME_MACCMD_CID_KEYRES:
                            isKeyRes    = true;
                            cryptoKeyId = PRVLORA_CRYPTO_PSK;
                            break;

                        default:
                            break;  // (nothing to do)
                    }
                    break;

                default:
                    // (never comes here)
                    break;
            }
        }
        else
        {
            p_framePayload = &( p_txFrame->frame.frameBuffer[ PRVLORA_FRAME_SIZE_MHDR_NOSEC ] );
        }

        if( txPayloadSize > 0 )
        {
            memcpy1( p_framePayload, p_txPayload, txPayloadSize );
            if( ( p_payloadSec != NULL ) && ( payloadSecSize > 0 ) )
            {
                ret = PrivateLoRaCryptoSecureMessage( cryptoKeyId, 
                                                      p_dstAddr,
                                                      p_srcAddr, 
                                                      p_payloadSec, 
                                                      payloadSecSize,
                                                      frameCounter );
            }
        }
    }

    //------------------
    // make MIC (security)
    if( ret == PRVLORA_STATUS_OK )
    {
        if( txOptions.options.SecEnable == 1 )
        {
            if( isKeyRes == true )
            {
                ret = PrivateLoRaCryptoGetKeyMIC( cryptoKeyId,
                                                  p_dstAddr,
                                                  p_txFrame->frame.frameBuffer,
                                                  (uint8_t)( frameSize - (uint16_t)PRVLORA_FRAME_SIZE_MIC ),
                                                  &valueMIC );
            }
            else
            {
                ret = PrivateLoRaCryptoGetMIC( cryptoKeyId,
                                               p_dstAddr,
                                               p_srcAddr,
                                               p_txFrame->frame.frameBuffer,
                                               (uint8_t)( frameSize - (uint16_t)PRVLORA_FRAME_SIZE_MIC ),
                                               frameCounter, 
                                               &valueMIC );
            }

            if( ret == PRVLORA_STATUS_OK )
            {
                p_frameMic = &( p_framePayload[ txPayloadSize ] );
                memcpy1( p_frameMic, (const uint8_t *)&valueMIC, PRVLORA_FRAME_SIZE_MIC );
            }
        }
    }

    if( ret == PRVLORA_STATUS_OK )
    {
        // update frame counter
        if( txOptions.options.SecEnable == 1 )
        {
            frameCounter += 1;
            PrivateLoRaRemoteDevSetFrameCounterTx( p_dstAddr, frameCounter );
        }

        p_txFrame->frameLength = (uint8_t)frameSize;
    }

    return ret;
}


/*!
 * Make MAC command frame - KeyReq
 */
PrvLoRaStatus_t PrivateLoRaFrameMakeKeyReq( PrvLoRaFrame_t  *p_txFrame,
                                            uint8_t         *p_dstAddr,
                                            uint8_t         *p_srcAddr,
                                            uint8_t         *p_initiatorNonce,
                                            bool            isAck )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaPayloadCmd_t         frmPayload;
    PrvLoRaPayloadCmdKeyReq_t   *p_payloadKeyReq;
    uint8_t                     payloadSize;
    PrvLoRaTxOptions_t          txOptions;
    PrvLoRaMacCryptoKeyId_t     cryptoKeyId;
    uint32_t                    valueMIC;
    uint8_t                     *p_frameMic;

    // initial check
    if( p_initiatorNonce == NULL )
    {
        return PRVLORA_STATUS_ERROR;
    }

    // init
    p_payloadKeyReq      = &( frmPayload.cmdPayload.keyReq );
    txOptions.txOptValue = 0x00;
    cryptoKeyId          = PRVLORA_CRYPTO_PSK;

    // make payload
    frmPayload.cid = PRVLORA_FRAME_MACCMD_CID_KEYREQ;
    memcpy1( p_payloadKeyReq->initiatorNonce, p_initiatorNonce, PRVLORA_CRYPTO_NONCE_SIZE );

    payloadSize = 1 + sizeof( PrvLoRaPayloadCmdKeyReq_t );  // 1 = cid size

    // TxOptions
    txOptions.options.SecEnable = 0;  // KeyReq message is non-secure

    // make frame (non-secure message)
    ret = PrivateLoRaFrameMakeTx( p_txFrame, 
                                  PRVLORA_FRAME_TYPE_MACCMD,
                                  isAck,
                                  p_dstAddr,
                                  p_srcAddr,
                                  (uint8_t *)&frmPayload, 
                                  payloadSize,
                                  txOptions );

    // add MIC for KeyReq (message is non-secure but need MIC)
    if( ret == PRVLORA_STATUS_OK )
    {
        ret = PrivateLoRaCryptoGetKeyMIC( cryptoKeyId,
                                          p_dstAddr,
                                          p_txFrame->frame.frameBuffer,
                                          p_txFrame->frameLength,
                                          &valueMIC );
        if( ret == PRVLORA_STATUS_OK )
        {
            p_frameMic = &( p_txFrame->frame.frameBuffer[ p_txFrame->frameLength ] );
            memcpy1( p_frameMic, (const uint8_t *)&valueMIC, PRVLORA_FRAME_SIZE_MIC );
            p_txFrame->frameLength += PRVLORA_FRAME_SIZE_MIC;
        }
    }

    return ret;
}

/*!
 * Make MAC command frame - KeyRes
 */
PrvLoRaStatus_t PrivateLoRaFrameMakeKeyRes( PrvLoRaFrame_t  *p_txFrame,
                                            uint8_t         *p_dstAddr,
                                            uint8_t         *p_srcAddr,
                                            uint8_t         *p_responderNonce )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaPayloadCmd_t         frmPayload;
    PrvLoRaPayloadCmdKeyRes_t   *p_payloadKeyRes;
    uint8_t                     payloadSize;
    PrvLoRaTxOptions_t          txOptions;

    // initial check
    if( p_responderNonce == NULL )
    {
        return PRVLORA_STATUS_ERROR;
    }

    // init
    p_payloadKeyRes      = &( frmPayload.cmdPayload.keyRes );
    txOptions.txOptValue = 0x00;

    // make payload
    frmPayload.cid = PRVLORA_FRAME_MACCMD_CID_KEYRES;
    memcpy1( p_payloadKeyRes->responderNonce, p_responderNonce, PRVLORA_CRYPTO_NONCE_SIZE );

    payloadSize = 1 + sizeof( PrvLoRaPayloadCmdKeyRes_t );  // 1 = cid size

    // TxOptions
    txOptions.options.SecEnable = 1;

    // make frame
    ret = PrivateLoRaFrameMakeTx( p_txFrame, 
                                  PRVLORA_FRAME_TYPE_MACCMD,
                                  false,
                                  p_dstAddr,
                                  p_srcAddr,
                                  (uint8_t *)&frmPayload, 
                                  payloadSize,
                                  txOptions );

    return ret;
}

/*!
 * Make MAC command frame - DevInfoReq
 */
PrvLoRaStatus_t PrivateLoRaFrameMakeDevInfoReq( PrvLoRaFrame_t     *p_txFrame,
                                                uint8_t            *p_dstAddr,
                                                uint8_t            *p_srcAddr,
                                                bool               isAck,
                                                PrvLoRaTxOptions_t *p_txOptions )
{
    PrvLoRaStatus_t             ret;
    PrvLoRaPayloadCmd_t         frmPayload;
    uint8_t                     payloadSize;
    PrvLoRaTxOptions_t          txOptions;

    // initial check
    if( p_txOptions == NULL )
    {
        return PRVLORA_STATUS_ERROR;
    }

    // init
    txOptions.txOptValue = 0x00;

    // make payload
    frmPayload.cid = PRVLORA_FRAME_MACCMD_CID_DEVINFOREQ;

    payloadSize = 1;  // 1 = cid size, no command parameter

    // TxOptions
    txOptions.options.SecEnable = p_txOptions->options.SecEnable;

    // make frame
    ret = PrivateLoRaFrameMakeTx( p_txFrame, 
                                  PRVLORA_FRAME_TYPE_MACCMD,
                                  isAck,
                                  p_dstAddr,
                                  p_srcAddr,
                                  (uint8_t *)&frmPayload, 
                                  payloadSize,
                                  txOptions );

    return ret;
}

/*!
 * Make MAC command frame - DevInfoRes
 */
PrvLoRaStatus_t PrivateLoRaFrameMakeDevInfoRes( PrvLoRaFrame_t     *p_txFrame,
                                                uint8_t            *p_dstAddr,
                                                uint8_t            *p_srcAddr,
                                                int8_t             snr,
                                                int8_t             txPower,
                                                uint32_t           txCycleTime,
                                                PrvLoRaTxOptions_t *p_txOptions )
{
    PrvLoRaStatus_t                 ret;
    PrvLoRaPayloadCmd_t             frmPayload;
    PrvLoRaPayloadCmdDevInfoRes_t   *p_payloadDevInfoRes;
    uint8_t                         payloadSize;
    PrvLoRaTxOptions_t              txOptions;
    uint8_t                         i;
    bool                            isAck;

    // initial check
    if( p_txOptions == NULL )
    {
        return PRVLORA_STATUS_ERROR;
    }

    // init
    p_payloadDevInfoRes  = &( frmPayload.cmdPayload.devInfoRes );
    txOptions.txOptValue = 0x00;
    isAck                = false;

    // prepare befor make payload
    if( snr > PRVLORA_FRAME_PRMMAX_SNR )
    {
        snr = PRVLORA_FRAME_PRMMAX_SNR;
    }
    else if( snr < PRVLORA_FRAME_PRMMIN_SNR )
    {
        snr = PRVLORA_FRAME_PRMMIN_SNR;
    }

    if( p_txOptions->options.ack == 1 )
    {
        isAck = true;
    }

    // make payload
    frmPayload.cid               = PRVLORA_FRAME_MACCMD_CID_DEVINFORES;
    p_payloadDevInfoRes->snr     = snr & PRVLORA_FRAME_PRMFILTER_SNR;
    p_payloadDevInfoRes->txPower = txPower & PRVLORA_FRAME_PRMFILTER_TXPOWER;
    for( i = 0; i < PRVLORA_FRAME_PRMSIZE_TXCYCLETIME; i++ )
    {
        p_payloadDevInfoRes->txCycleTime[ i ] = (uint8_t)( txCycleTime & 0x000000FF );
        txCycleTime = txCycleTime >> 8;
    }

    payloadSize = 1 + sizeof( PrvLoRaPayloadCmdDevInfoRes_t );  // 1 = cid size

    // TxOptions
    txOptions.options.SecEnable = p_txOptions->options.SecEnable;

    // make frame
    ret = PrivateLoRaFrameMakeTx( p_txFrame, 
                                  PRVLORA_FRAME_TYPE_MACCMD,
                                  isAck,
                                  p_dstAddr,
                                  p_srcAddr,
                                  (uint8_t *)&frmPayload, 
                                  payloadSize,
                                  txOptions );

    return ret;
}

/*!
 * Make MAC command frame - TxCycleReq
 */
PrvLoRaStatus_t PrivateLoRaFrameMakeTxCycleReq( PrvLoRaFrame_t     *p_txFrame,
                                                uint8_t            *p_dstAddr,
                                                uint8_t            *p_srcAddr,
                                                uint32_t           txCycleTime,
                                                bool               isAck,
                                                PrvLoRaTxOptions_t *p_txOptions )
{
    PrvLoRaStatus_t                 ret;
    PrvLoRaPayloadCmd_t             frmPayload;
    PrvLoRaPayloadCmdTxCycleReq_t   *p_payloadTxCycleReq;
    uint8_t                         payloadSize;
    PrvLoRaTxOptions_t              txOptions;
    uint8_t                         i;

    // initial check
    if( p_txOptions == NULL )
    {
        return PRVLORA_STATUS_ERROR;
    }

    // init
    p_payloadTxCycleReq  = &( frmPayload.cmdPayload.txCycleReq );
    txOptions.txOptValue = 0x00;

    // make payload
    frmPayload.cid = PRVLORA_FRAME_MACCMD_CID_TXCYCLEREQ;
    for( i = 0; i < PRVLORA_FRAME_PRMSIZE_TXCYCLETIME; i++ )
    {
        p_payloadTxCycleReq->txCycleTime[ i ] = (uint8_t)( txCycleTime & 0x000000FF );
        txCycleTime = txCycleTime >> 8;
    }

    payloadSize = 1 + sizeof( PrvLoRaPayloadCmdTxCycleReq_t );  // 1 = cid size

    // TxOptions
    txOptions.options.SecEnable = p_txOptions->options.SecEnable;

    // make frame
    ret = PrivateLoRaFrameMakeTx( p_txFrame, 
                                  PRVLORA_FRAME_TYPE_MACCMD,
                                  isAck,
                                  p_dstAddr,
                                  p_srcAddr,
                                  (uint8_t *)&frmPayload, 
                                  payloadSize,
                                  txOptions );

    return ret;
}

/*!
 * Make MAC command frame - TxCycleRes
 */
PrvLoRaStatus_t PrivateLoRaFrameMakeTxCycleRes( PrvLoRaFrame_t     *p_txFrame,
                                                uint8_t            *p_dstAddr,
                                                uint8_t            *p_srcAddr,
                                                PrvLoRaTxOptions_t *p_txOptions )
{
    PrvLoRaStatus_t                 ret;
    PrvLoRaPayloadCmd_t             frmPayload;
    uint8_t                         payloadSize;
    PrvLoRaTxOptions_t              txOptions;

    // initial check
    if( p_txOptions == NULL )
    {
        return PRVLORA_STATUS_ERROR;
    }

    // init
    txOptions.txOptValue = 0x00;

    // make payload
    frmPayload.cid = PRVLORA_FRAME_MACCMD_CID_TXCYCLERES;

    payloadSize = 1;  // 1 = cid size, no command parameter  // no parameter

    // TxOptions
    txOptions.options.SecEnable = p_txOptions->options.SecEnable;

    // make frame
    ret = PrivateLoRaFrameMakeTx( p_txFrame, 
                                  PRVLORA_FRAME_TYPE_MACCMD,
                                  false,
                                  p_dstAddr,
                                  p_srcAddr,
                                  (uint8_t *)&frmPayload, 
                                  payloadSize,
                                  txOptions );

    return ret;
}

//--------------------------------------------------------------------------------------------------
// Rx (parse frame)

/*!
 * Parse Rx frame
 */
PrvLoRaStatus_t PrivateLoRaFramePerserRx( uint8_t                   maxFrameSize,
                                          uint8_t                   *p_rxFrame,
                                          uint8_t                   rxFrameSize,
                                          PrvLoRaFrameMhdrFrmCtrl_t *p_frameCtrl,
                                          uint8_t                   *p_srcAddr,
                                          uint8_t                   *p_dstAddr,
                                          uint8_t                   *p_payload,
                                          uint8_t                   *p_payloadSize )
{
    PrvLoRaStatus_t         ret;
    PrvLoRaFrame_t          tmpRxFrame;
    PrvLoRaFrameMhdr_t      *p_frameMhdr;
    PrvLoRaFrameMhdrSec_t   *p_frameMhdrSec;
    uint8_t                 *p_framePayload;
    uint8_t                 minFrameSize;
    uint8_t                 payloadSize;
    uint8_t                 *p_payloadSec;
    uint8_t                 payloadSecSize;
    PrvLoRaMacCryptoKeyId_t cryptoKeyId;
    uint32_t                rcvdFrameCnt, frameCounterRx;
    int32_t                 tmpCnt;
    uint8_t                 *p_frameMIC;
    uint32_t                valueMIC;
    int                     compare;
    bool                    isCheckMic;
    bool                    isKeyReqRes;

    // initial check
    if( p_rxFrame == NULL )
    {
        return PRVLORA_STATUS_ERROR;
    }
    if( ( rxFrameSize == 0 ) || ( rxFrameSize > maxFrameSize ) )
    {
        return PRVLORA_STATUS_LENGTH_ERROR;
    }
    if( ( p_frameCtrl == NULL ) || 
        ( p_srcAddr == NULL ) || ( p_dstAddr == NULL ) || 
        ( p_payload == NULL ) || ( p_payloadSize == NULL ) )
    {
        return PRVLORA_STATUS_PARAMETER_INVALID;
    }

    // init
    ret            = PRVLORA_STATUS_OK;
    p_frameMhdr    = &( tmpRxFrame.frame.frameMhdr );
    p_frameMhdrSec = &( tmpRxFrame.frame.frameMhdrSec );
    rcvdFrameCnt   = (uint32_t)0;
    cryptoKeyId    = PRVLORA_CRYPTO_SESSION_KEY;
    isCheckMic     = false;
    isKeyReqRes    = false;
    p_payloadSec   = NULL;
    payloadSecSize = 0;

    //------------------
    // import rx frame
    tmpRxFrame.frameLength = rxFrameSize;
    memcpy1( tmpRxFrame.frame.frameBuffer, p_rxFrame, rxFrameSize );

    //----------------------------------
    // check frame control - frame type
    switch( p_frameMhdr->frameControl.frameType )
    {
        case PRVLORA_FRAME_TYPE_DATA:
        case PRVLORA_FRAME_TYPE_MACCMD:
            // OK - known frame type
            break;

        default:
            // Error - unknown frame type
            ret = PRVLORA_STATUS_PARAMETER_INVALID;
            break;
    }

    //---------------------
    // check frame length
    if( ret == PRVLORA_STATUS_OK )
    {
        if( p_frameMhdr->frameControl.secEnabled == 1 )
        {
            p_framePayload = &( tmpRxFrame.frame.frameBuffer[ PRVLORA_FRAME_SIZE_MHDR_SEC ] );
            minFrameSize   = (uint16_t)PRVLORA_FRAME_SIZE_MHDR_SEC + 
                             (uint16_t)PRVLORA_FRAME_SIZE_MIC;
        }
        else
        {
            p_framePayload = &( tmpRxFrame.frame.frameBuffer[ PRVLORA_FRAME_SIZE_MHDR_NOSEC ] );
            minFrameSize   = (uint16_t)PRVLORA_FRAME_SIZE_MHDR_NOSEC;
        }

        // get payload size
        if( rxFrameSize >= minFrameSize )
        {
            payloadSize = rxFrameSize - minFrameSize;
        }
        else
        {
            ret = PRVLORA_STATUS_LENGTH_ERROR;
        }
    }

    // get/output MHDR information
    if( ret == PRVLORA_STATUS_OK )
    {
        // FrameControl
        memcpy1( (uint8_t *)p_frameCtrl, 
                 (const uint8_t *)&(p_frameMhdr->frameControl), 
                 sizeof(PrvLoRaFrameMhdrFrmCtrl_t) );

        // Src/Dst (change endian to big)
        memcpyr( p_srcAddr, p_frameMhdr->srcAddrLE, PRVLORA_MACADDR_SIZE );
        memcpyr( p_dstAddr, p_frameMhdr->dstAddrLE, PRVLORA_MACADDR_SIZE );

        // check source device
        ret = PrivateLoRaRemoteDevSearchDevice( p_srcAddr );
    }

   //-------------------------------
    // security: check Frame counter
    if( ret == PRVLORA_STATUS_OK )
    {
        if( p_frameMhdrSec->frameControl.secEnabled == 1 )
        {
            // frame counter in rx frame
            memcpy1( (uint8_t *)&rcvdFrameCnt, p_frameMhdrSec->frameCounterLE, PRVLORA_FRAME_SIZE_FRAMECOUNTER );

            ret = PrivateLoRaRemoteDevGetFrameCounterRx( p_srcAddr, &frameCounterRx );
            if( ret == PRVLORA_STATUS_OK )
            {
                tmpCnt = (int32_t)rcvdFrameCnt - (int32_t)frameCounterRx;
                if( tmpCnt <= 0 )
                {
                    ret = PRVLORA_STATUS_CRYPTO_ERROR;  // received old frame counter
                }
            }
        }
    }

    //--------------------------
    // security: prepare
    if( ret == PRVLORA_STATUS_OK )
    {
        if( p_frameMhdrSec->frameControl.secEnabled == 1 )
        {
            isCheckMic = true;  // need to check MIC

            switch( p_frameMhdr->frameControl.frameType )
            {
                case PRVLORA_FRAME_TYPE_DATA:
                    p_payloadSec   = p_framePayload;
                    payloadSecSize = payloadSize;
                    break;

                case PRVLORA_FRAME_TYPE_MACCMD:
                    p_payloadSec   = &( p_framePayload[ 1 ] );  // CID is not encrypted
                    payloadSecSize = payloadSize - 1;

                    switch( p_framePayload[ 0 ] )
                    {
                        // if command is KeyReq : discard because it is non-secured
                        case PRVLORA_FRAME_MACCMD_CID_KEYREQ:
                            ret = PRVLORA_STATUS_PARAMETER_INVALID;
                            break;

                        // if command is KeyRes : use PSK
                        case PRVLORA_FRAME_MACCMD_CID_KEYRES:
                            isKeyReqRes = true;
                            cryptoKeyId = PRVLORA_CRYPTO_PSK;
                            break;

                        default:
                            break;  // (nothing to do)
                    }
                    break;

                default:
                    break;  // (never comes here)
            }
        }
        else
        {
            if( p_frameMhdr->frameControl.frameType == PRVLORA_FRAME_TYPE_MACCMD )
            {
                if( p_framePayload[ 0 ] == PRVLORA_FRAME_MACCMD_CID_KEYREQ )
                {
                    // KeyReq command is not secure but need to check MIC.
                    isCheckMic  = true;
                    isKeyReqRes = true;
                    cryptoKeyId = PRVLORA_CRYPTO_PSK;                    // use PSK
                    payloadSize = payloadSize - PRVLORA_FRAME_SIZE_MIC;  // last 4byte is MIC
                }
            }
        }
    }

    //--------------------------
    // security: check MIC
    if( ret == PRVLORA_STATUS_OK )
    {
        if( isCheckMic == true )
        {
            if( isKeyReqRes == true )
            {
                ret = PrivateLoRaCryptoGetKeyMIC( cryptoKeyId,
                                                  p_srcAddr,
                                                  tmpRxFrame.frame.frameBuffer,
                                                  ( rxFrameSize - (uint8_t)PRVLORA_FRAME_SIZE_MIC ),
                                                  &valueMIC );
            }
            else
            {
                ret = PrivateLoRaCryptoGetMIC( cryptoKeyId,
                                               p_srcAddr,
                                               p_srcAddr,
                                               tmpRxFrame.frame.frameBuffer,
                                               ( rxFrameSize - (uint8_t)PRVLORA_FRAME_SIZE_MIC ),
                                               rcvdFrameCnt,
                                               &valueMIC );
            }

            if( ret == PRVLORA_STATUS_OK )
            {
                p_frameMIC = &( p_framePayload[ payloadSize ] );
                compare = memcmp( &valueMIC, p_frameMIC, PRVLORA_FRAME_SIZE_MIC );
                if( compare != 0 )
                {
                    ret = PRVLORA_STATUS_CRYPTO_ERROR;
                }
            }
        }
    }

    //--------------------------
    // security: decrpto frame
    if( ret == PRVLORA_STATUS_OK )
    {
        if( p_frameMhdrSec->frameControl.secEnabled == 1 )
        {
            ret = PrivateLoRaCryptoUnsecureMessage( cryptoKeyId,
                                                    p_srcAddr,
                                                    p_srcAddr,
                                                    p_payloadSec,
                                                    payloadSecSize,
                                                    rcvdFrameCnt );
        }

        if( ret == PRVLORA_STATUS_OK )
        {
            memcpy1( p_payload, p_framePayload, payloadSize );
            (*p_payloadSize) = payloadSize;

            // update frame counter
            if( p_frameMhdrSec->frameControl.secEnabled == 1 )
            {
                PrivateLoRaRemoteDevSetFrameCounterRx( p_srcAddr, rcvdFrameCnt );
            }
        }
    }

    return ret;
}
