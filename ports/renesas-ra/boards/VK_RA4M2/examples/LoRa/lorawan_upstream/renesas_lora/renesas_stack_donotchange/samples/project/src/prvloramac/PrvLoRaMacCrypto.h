/*
    (C) 2023 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
/**
  * @file    PrvLoRaMacCrypto.h
  * @author  Renesas Electronics Corporation
  * @brief   
**/

#ifndef __PRVLORAMACCRYPTO_H__
#define __PRVLORAMACCRYPTO_H__

#include "PrivateLoRa.h"

/*--------*/
/* define */

#define PRVLORA_CRYPTO_NONCE_SIZE     16

/*----------------*/
/* typedef (enum) */
/*!
 * PrivateLoRa Key identifier
 */
typedef enum _PrvLoRaMacCryptoKeyId_t
{
    PRVLORA_CRYPTO_PSK = 0,
    PRVLORA_CRYPTO_SESSION_KEY,
    /*---*/
    MAXNUM_PRVLORA_CRYPTO
} PrvLoRaMacCryptoKeyId_t;

/*------------------------*/
/* typedef (struct/union) */


/*-----------*/
/* Functions */

// init
extern PrvLoRaStatus_t PrivateLoRaCryptoInit( void );

// Frame
extern PrvLoRaStatus_t PrivateLoRaCryptoSecureMessage( PrvLoRaMacCryptoKeyId_t keyId,
                                                       uint8_t                 *p_remoteAddr,
                                                       uint8_t                 *p_macAddr,
                                                       uint8_t                 *p_message,
                                                       uint8_t                 messageSize,
                                                       uint32_t                frameCounter );
extern PrvLoRaStatus_t PrivateLoRaCryptoUnsecureMessage( PrvLoRaMacCryptoKeyId_t keyId,
                                                         uint8_t                 *p_remoteAddr,
                                                         uint8_t                 *p_macAddr,
                                                         uint8_t                 *p_message,
                                                         uint8_t                 messageSize,
                                                         uint32_t                frameCounter );

// MIC
extern PrvLoRaStatus_t PrivateLoRaCryptoGetMIC( PrvLoRaMacCryptoKeyId_t keyId,
                                                uint8_t                 *p_remoteAddr,
                                                uint8_t                 *p_macAddr,
                                                uint8_t                 *p_message,
                                                uint8_t                 messageSize,
                                                uint32_t                frameCounter,
                                                uint32_t                *p_calcMic );
extern PrvLoRaStatus_t PrivateLoRaCryptoGetKeyMIC( PrvLoRaMacCryptoKeyId_t keyId,
                                                   uint8_t                 *p_remoteAddr,
                                                   uint8_t                 *p_message,
                                                   uint8_t                 messageSize,
                                                   uint32_t                *p_calcMic );

// Nonce
extern void PrivateLoRaCryptoMakeNonce( uint8_t *p_nonce, uint8_t nonceLen );

// Security key
extern PrvLoRaStatus_t PrivateLoRaCryptoMakeSessionKey( uint8_t *p_remoteAddr,
                                                        uint8_t *p_devAddr,
                                                        uint8_t *p_responderNonce,
                                                        uint8_t *p_initiatorNonce,
                                                        bool    isKeyReqInitiate );

#endif  /* __PRVLORAMACCRYPTO_H__ */
