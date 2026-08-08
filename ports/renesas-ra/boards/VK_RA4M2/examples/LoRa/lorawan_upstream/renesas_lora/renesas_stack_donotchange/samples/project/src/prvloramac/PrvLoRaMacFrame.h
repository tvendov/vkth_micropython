/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    PrvLoRaMacFrame.h
  * @author  Renesas Electronics Corporation
  * @brief   
**/

#ifndef __PRVLORAMACFRAME_H__
#define __PRVLORAMACFRAME_H__

#include "PrivateLoRa.h"
#include "PrvLoRaMacCrypto.h"

/*--------*/
/* define */
#define PRVLORA_FRAME_MAXSIZE               255     // (uint8_t)

#define PRVLORA_FRAME_SIZE_ACKONLY          17
#define PRVLORA_FRAME_SIZE_FRAMECOUNTER     4
#define PRVLORA_FRAME_SIZE_MIC              4

#define PRVLORA_FRAME_TYPE_DATA             0x01
#define PRVLORA_FRAME_TYPE_MACCMD           0x03

// MAC command ID
#define PRVLORA_FRAME_MACCMD_CID_KEYREQ         0x02
#define PRVLORA_FRAME_MACCMD_CID_KEYRES         0x03
#define PRVLORA_FRAME_MACCMD_CID_DEVINFOREQ     0x04
#define PRVLORA_FRAME_MACCMD_CID_DEVINFORES     0x05
#define PRVLORA_FRAME_MACCMD_CID_TXCYCLEREQ     0x06
#define PRVLORA_FRAME_MACCMD_CID_TXCYCLERES     0x07

// Parameters
// - SNR : lower 6bit of 1Byte(8bit) are valid. it is signed value.
#define PRVLORA_FRAME_PRMMIN_SNR                -32
#define PRVLORA_FRAME_PRMMAX_SNR                31
#define PRVLORA_FRAME_PRMFILTER_SNR             0x3F
#define PRVLORA_FRAME_PRMGET_SNR( snr )         ( (int8_t)( (snr) << 2 ) >> 2 )

// - TxPower : all 1Byte(8bit) are valid.
#define PRVLORA_FRAME_PRMFILTER_TXPOWER         0xFF

// - TxCycleTime : lower 17bit of 3Byte(24bit) are valid.
#define PRVLORA_FRAME_PRMSIZE_TXCYCLETIME       3
#define PRVLORA_FRAME_PRMMIN_TXCYCLETIME        0x0000000A
#define PRVLORA_FRAME_PRMMAX_TXCYCLETIME        0x0001FFFF
#define PRVLORA_FRAME_PRMFILTER_TXCYCLETIME     0x0001FFFF

/*----------------*/
/* typedef (enum) */


/*------------------------*/
/* typedef (struct/union) */

// MHDR - FrameControl
typedef struct _PrvLoRaFrameMhdrFrmCtrl_t
{
    uint8_t     frameType   : 3;
    uint8_t     secEnabled  : 1;
    uint8_t     _reserved1  : 1;
    uint8_t     ackRequest  : 1;
    uint8_t     _reserved2  : 1;
    uint8_t     ack         : 1;
} PrvLoRaFrameMhdrFrmCtrl_t;

// MHDR (no security)
typedef struct _PrvLoRaFrameMhdr_t
{
    PrvLoRaFrameMhdrFrmCtrl_t   frameControl;
    uint8_t                     dstAddrLE[ PRVLORA_MACADDR_SIZE ];
    uint8_t                     srcAddrLE[ PRVLORA_MACADDR_SIZE ];
} PrvLoRaFrameMhdr_t;

// MHDR (security)
typedef struct _PrvLoRaFrameMhdrSec_t
{
    PrvLoRaFrameMhdrFrmCtrl_t   frameControl;                       // same with PrvLoRaFrameMhdr_t
    uint8_t                     dstAddrLE[ PRVLORA_MACADDR_SIZE ];  // same with PrvLoRaFrameMhdr_t
    uint8_t                     srcAddrLE[ PRVLORA_MACADDR_SIZE ];  // same with PrvLoRaFrameMhdr_t
    uint8_t                     frameCounterLE[ PRVLORA_FRAME_SIZE_FRAMECOUNTER ];  // (not uint32_t to avoid additional padding)
} PrvLoRaFrameMhdrSec_t;

// Frame
typedef struct _PrvLoRaFrame_t
{
    uint8_t     frameLength;
    uint8_t     _reserved;
    union
    {
        uint8_t                 frameBuffer[ PRVLORA_FRAME_MAXSIZE ];
        // MHDR
        PrvLoRaFrameMhdr_t      frameMhdr;
        PrvLoRaFrameMhdrSec_t   frameMhdrSec;
    } frame;
} PrvLoRaFrame_t;


// Payload - MAC commnad (KeyReq)
typedef struct _PrvLoRaPayloadCmdKeyReq_t
{
    uint8_t         initiatorNonce[ PRVLORA_CRYPTO_NONCE_SIZE ];
} PrvLoRaPayloadCmdKeyReq_t;

// Payload - MAC commnad (KeyRes)
typedef struct _PrvLoRaPayloadCmdKeyRes_t
{
    uint8_t         responderNonce[ PRVLORA_CRYPTO_NONCE_SIZE ];
} PrvLoRaPayloadCmdKeyRes_t;

// Payload - MAC commnad (DevInfoReq)
// typedef struct PrvLoRaPayloadCmdDevInfoReq_t
// {
//     /* no payload */
// } PrvLoRaPayloadCmdDevInfoReq_t;

// Payload - MAC commnad (DevInfoRes)
typedef struct _PrvLoRaPayloadCmdDevInfoRes_t
{
    int8_t      snr;                                                // 6-bit  (upper 2bit is RFU)
    int8_t      txPower;                                            // 8-bit  (signed)
    uint8_t     txCycleTime[ PRVLORA_FRAME_PRMSIZE_TXCYCLETIME ];   // 24-bit (upper 8bit is RFU)
} PrvLoRaPayloadCmdDevInfoRes_t;

// Payload - MAC commnad (TxCycleReq)
typedef struct _PrvLoRaPayloadCmdTxCycleReq_t
{
    uint8_t     txCycleTime[ PRVLORA_FRAME_PRMSIZE_TXCYCLETIME ];   // 24-bit (upper 8bit is RFU)
} PrvLoRaPayloadCmdTxCycleReq_t;

// Payload - MAC commnad (TxCycleRes)
// typedef struct _PrvLoRaPayloadCmdTxCycleRes_t
// {
//     /* no payload */
// } PrvLoRaPayloadCmdTxCycleRes_t;

// Payload - MAC commnad
typedef struct _PrvLoRaPayloadCmd_t
{
    uint8_t     cid;

    union
    {
        PrvLoRaPayloadCmdKeyReq_t           keyReq;
        PrvLoRaPayloadCmdKeyRes_t           keyRes;
        //PrvLoRaPayloadCmdDevInfoReq_t       devInfoReq;  // no payload
        PrvLoRaPayloadCmdDevInfoRes_t       devInfoRes;
        PrvLoRaPayloadCmdTxCycleReq_t       txCycleReq;
        //PrvLoRaPayloadCmdTxCycleRes_t       txCycleRes;  // no payload
    } cmdPayload;
} PrvLoRaPayloadCmd_t;


/*-----------*/
/* Functions */

extern PrvLoRaStatus_t PrivateLoRaFrameGetTxFrameSize( uint8_t            *p_txFrameSize,
                                                       uint8_t            txPayloadSize, 
                                                       PrvLoRaTxOptions_t txOptions );
extern PrvLoRaStatus_t PrivateLoRaFrameMakeTx( PrvLoRaFrame_t     *p_txFrame,
                                               uint8_t            frameType,
                                               bool               isAck,
                                               uint8_t            *p_dstAddr,
                                               uint8_t            *p_srcAddr,
                                               uint8_t            *p_txPayload,
                                               uint8_t            txPayloadSize,
                                               PrvLoRaTxOptions_t txOptions );

extern PrvLoRaStatus_t PrivateLoRaFrameMakeKeyReq( PrvLoRaFrame_t     *p_txFrame,
                                                   uint8_t            *p_dstAddr,
                                                   uint8_t            *p_srcAddr,
                                                   uint8_t            *p_initiatorNonce,
                                                   bool               isAck );
extern PrvLoRaStatus_t PrivateLoRaFrameMakeKeyRes( PrvLoRaFrame_t     *p_txFrame,
                                                   uint8_t            *p_dstAddr,
                                                   uint8_t            *p_srcAddr,
                                                   uint8_t            *p_responderNonce );
extern PrvLoRaStatus_t PrivateLoRaFrameMakeDevInfoReq( PrvLoRaFrame_t     *p_txFrame,
                                                       uint8_t            *p_dstAddr,
                                                       uint8_t            *p_srcAddr,
                                                       bool               isAck,
                                                       PrvLoRaTxOptions_t *p_txOptions );
extern PrvLoRaStatus_t PrivateLoRaFrameMakeDevInfoRes( PrvLoRaFrame_t     *p_txFrame,
                                                       uint8_t            *p_dstAddr,
                                                       uint8_t            *p_srcAddr,
                                                       int8_t             snr,
                                                       int8_t             txPower,
                                                       uint32_t           txCycleTime,
                                                       PrvLoRaTxOptions_t *p_txOptions );
extern PrvLoRaStatus_t PrivateLoRaFrameMakeTxCycleReq( PrvLoRaFrame_t     *p_txFrame,
                                                       uint8_t            *p_dstAddr,
                                                       uint8_t            *p_srcAddr,
                                                       uint32_t           txCycleTime,
                                                       bool               isAck,
                                                       PrvLoRaTxOptions_t *p_txOptions );
extern PrvLoRaStatus_t PrivateLoRaFrameMakeTxCycleRes( PrvLoRaFrame_t     *p_txFrame,
                                                       uint8_t            *p_dstAddr,
                                                       uint8_t            *p_srcAddr,
                                                       PrvLoRaTxOptions_t *p_txOptions );

extern PrvLoRaStatus_t PrivateLoRaFramePerserRx( uint8_t                   maxFrameSize,
                                                 uint8_t                   *p_rxFrame,
                                                 uint8_t                   rxFrameSize,
                                                 PrvLoRaFrameMhdrFrmCtrl_t *p_frameCtrl,
                                                 uint8_t                   *p_srcAddr,
                                                 uint8_t                   *p_dstAddr,
                                                 uint8_t                   *p_payload,
                                                 uint8_t                   *p_payloadSize );

#endif  // __PRVLORAMACFRAME_H__
