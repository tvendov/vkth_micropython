/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "board.h"
#include "timer.h"
#include "LoRaMac.h"

#include "LoRaFragmentProcess.h"
#include "LoRaFragmentFec.h"

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
#include "secure-element.h"
#endif

/*** Fragment data block command ***/
/* macros */
#define FRGMNT_CID_PACKAGE_VERSION_REQ              0x00
#define FRGMNT_CID_PACKAGE_VERSION_ANS              0x00
#define FRGMNT_CID_FRAG_SESSION_STATUS_REQ          0x01
#define FRGMNT_CID_FRAG_SESSION_STATUS_ANS          0x01
#define FRGMNT_CID_FRAG_SESSION_SETUP_REQ           0x02
#define FRGMNT_CID_FRAG_SESSION_SETUP_ANS           0x02
#define FRGMNT_CID_FRAG_SESSION_DELETE_REQ          0x03
#define FRGMNT_CID_FRAG_SESSION_DELETE_ANS          0x03
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
#define FRGMNT_CID_FRAG_DATA_BLOCK_RECEIVED_REQ     0x04
#define FRGMNT_CID_FRAG_DATA_BLOCK_RECEIVED_ANS     0x04
#endif
#define FRGMNT_CID_DATA_FRAGMENT                    0x08

#define FRGMNT_PLEN_PACKAGE_VERSION_REQ             (0)
#define FRGMNT_PLEN_PACKAGE_VERSION_ANS             (2)
#define FRGMNT_PLEN_FRAG_SESSION_STATUS_REQ         (1)
#define FRGMNT_PLEN_FRAG_SESSION_STATUS_ANS         (4)
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
#define FRGMNT_PLEN_FRAG_SESSION_SETUP_REQ          (16)
#else
#define FRGMNT_PLEN_FRAG_SESSION_SETUP_REQ          (10)
#endif
#define FRGMNT_PLEN_FRAG_SESSION_SETUP_ANS          (1)
#define FRGMNT_PLEN_FRAG_SESSION_DELETE_REQ         (1)
#define FRGMNT_PLEN_FRAG_SESSION_DELETE_ANS         (1)
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
#define FRGMNT_PLEN_FRAG_DATA_BLOCK_RECEIVED_REQ    (1)
#define FRGMNT_PLEN_FRAG_DATA_BLOCK_RECEIVED_ANS    (1)
#endif
#define FRGMNT_PLEN_DATA_FRAGMENT                   (2)  // + size of Pm_n

#define FRGMNT_RXCMD_QUEUENUM                       (4)

/* typedef */
typedef struct {
    uint8_t     fragIndex;     // bit2-1 of FragStatusReqParams
    uint8_t     participants;  // bit0   of FragStatusReqParams
} Frgmnt_StatusReq_t;

typedef struct {
    uint8_t     fragIndex;       // bit5-4 of FragSession
    uint8_t     mcGroupBitMask;  // bit3-0 of FragSession
    uint16_t    nbFrag;
    uint8_t     fragSize;
    uint8_t     control;
    uint8_t     padding;
    uint32_t    descriptor;
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    uint16_t    sessionCnt;
    uint32_t    mic;
#endif
} Frgmnt_SetupReq_t;

typedef struct {
    uint8_t     fragIndex;  // bit1-0 of Param
} Frgmnt_DeleteReq_t;

typedef struct {
    uint8_t     fragIndex;
    uint16_t    n_th;           // bit13-0;  N=1...16383
    uint8_t     pmnLen;
    uint8_t     *p_pmn;
} Frgmnt_DataFragmentMsg_t; 

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
typedef struct {
    uint8_t     fragIndex;  // bit1-0 of Param
} Frgmnt_DataBlockRcvdAns_t;
#endif

typedef struct
{
    uint8_t     cid;
    union {
        Frgmnt_StatusReq_t          statusReq;
        Frgmnt_SetupReq_t           setupReq;
        Frgmnt_DeleteReq_t          deleteReq;
        Frgmnt_DataFragmentMsg_t    dataFragMsg;
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        Frgmnt_DataBlockRcvdAns_t   dataBlkRcvdAns;
#endif
    } reqCmdParam;
    uint32_t    devAddress;
} Frgmnt_RxCmd_t;

typedef struct
{
    Frgmnt_RxCmd_t  rxCmdQueue[ FRGMNT_RXCMD_QUEUENUM ];
    uint8_t         numCmd;
    uint8_t         isBroadcast;
} Frgmnt_RxCmdQueue_t;

/* global variable */
Frgmnt_RxCmdQueue_t FrgmntCmdQueue;
uint8_t FrgmntRxDataBuff[ 256 ];

/* function prototype */
static FrgmntStatus_t LoRaFrgmntHandlePackageVersionReq( uint32_t devAddress, uint8_t *p_buffer, size_t length );
static FrgmntStatus_t LoRaFrgmntHandleFragSessionStatusReq( uint32_t devAddress, uint8_t *p_buffer, size_t length );
static FrgmntStatus_t LoRaFrgmntHandleFragSessionSetupReq( uint32_t devAddress, uint8_t *p_buffer, size_t length );
static FrgmntStatus_t LoRaFrgmntHandleFragSessionDeleteReq( uint32_t devAddress, uint8_t *p_buffer, size_t length );
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
static FrgmntStatus_t LoRaFrgmntHandleFragDataBlockRcvdAns( uint32_t devAddress, uint8_t *p_buffer, size_t length );
#endif
static FrgmntStatus_t LoRaFrgmntHandleDataFragmentMsg( uint32_t devAddress, uint8_t *p_buffer, size_t length );

static FrgmntStatus_t LoRaFrgmntProcessPackageVersionReq( uint8_t        *p_buffer, 
                                                          uint8_t        *p_payloadLen, 
                                                          uint8_t        bufferMaxSize, 
                                                          uint32_t       *p_txDelayMs,
                                                          Frgmnt_RxCmd_t *p_rcCmd/*nouse*/ );
static FrgmntStatus_t LoRaFrgmntProcessFragSessionStatusReq( uint8_t        *p_buffer, 
                                                             uint8_t        *p_payloadLen, 
                                                             uint8_t        bufferMaxSize, 
                                                             uint32_t       *p_txDelayMs,
                                                             Frgmnt_RxCmd_t *p_rcCmd );
static FrgmntStatus_t LoRaFrgmntProcessFragSessionSetupReq( uint8_t        *p_buffer, 
                                                            uint8_t        *p_payloadLen, 
                                                            uint8_t        bufferMaxSize, 
                                                            uint32_t       *p_txDelayMs,
                                                            Frgmnt_RxCmd_t *p_rcCmd );
static FrgmntStatus_t LoRaFrgmntProcessFragSessionDeleteReq( uint8_t        *p_buffer, 
                                                             uint8_t        *p_payloadLen, 
                                                             uint8_t        bufferMaxSize, 
                                                             uint32_t       *p_txDelayMs,
                                                             Frgmnt_RxCmd_t *p_rcCmd );
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
static FrgmntStatus_t LoRaFrgmntProcessFragDataBlockRcvdAns( uint8_t        *p_buffer, 
                                                             uint8_t        *p_payloadLen, 
                                                             uint8_t        bufferMaxSize, 
                                                             uint32_t       *p_txDelayMs,
                                                             Frgmnt_RxCmd_t *p_rcCmd );
#endif
static FrgmntStatus_t LoRaFrgmntProcessDataFragmentMsg( uint8_t        *p_buffer, 
                                                        uint8_t        *p_payloadLen, 
                                                        uint8_t        bufferMaxSize, 
                                                        uint32_t       *p_txDelayMs,
                                                        Frgmnt_RxCmd_t *p_rcCmd );

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
static FrgmntStatus_t LoRaFrgmntSendFragDataBlockReceivedReq( uint8_t  *p_buffer, 
                                                              uint8_t  *p_payloadLen, 
                                                              uint8_t  bufferMaxSize, 
                                                              uint32_t *p_txDelayMs,
                                                              uint8_t  fragIndex );
#endif

static uint32_t LoRaFrgmntGetUplinkDelayMs( uint8_t fragIndex );

/*** Manage fragment session ***/
#define FRGMNT_DATAFRAG_STATUS_NONE                 0x00
#define FRGMNT_DATAFRAG_STATUS_MEMORYERROR          0x01
#define FRGMNT_DATAFRAG_STATUS_MICERROR             0x02
#define FRGMNT_DATAFRAG_STATUS_RUNNING              0x40  // local status
#define FRGMNT_DATAFRAG_STATUS_COMPLETE_SUCCESS     0x80  // local status

typedef struct {
    uint8_t     fragIndex_session; // fragmentation session index
    uint8_t     mcGroupBitMask;
    uint16_t    nbFrag;
    uint8_t     fragSize;
    uint8_t     padding;
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    bool        ackReception;
#endif
    //uint8_t   fragAlgo;         // reserved for the future
    uint8_t     blockAckDelay;
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    uint16_t    sessionCntPrev;     // it is updated when the 1st fragment is received
    uint16_t    sessionCntPrevSet;  //   <- keep the sessionCnt
    uint32_t    mic;
    uint8_t     b0Data[16];       // for MIC calculation (v2.00)
#endif
    /*---*/
    uint16_t    prevIndex;
    uint16_t    numFragReceived;
    uint16_t    numMissingUncodedFrag;
    uint8_t     dataFragStatus;
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    bool       isRcvDataBlkRcvdAns;
#endif
    /*---*/
    uint8_t     *p_dataBlockBuff;
} Frgmnt_SessionMng_t;

Frgmnt_SessionMng_t FrgmntSessionMng[ FRGMNT_CONFIG_MAX_FRAG_INDEX ];  // array number = fragIndex
uint8_t FrgmntDataBlockBuffer[ FRGMNT_CONFIG_MAX_FRAG_INDEX ][ FRGMNT_CONFIG_MAX_DATABLK_SIZE ];

static Frgmnt_SessionMng_t* LoRaFrgmntSearchSessionMng( uint8_t fragIndex );
static Frgmnt_SessionMng_t* LoRaFrgmntGetSessionMng( uint8_t fragIndex );
static void LoRaFrgmntReleaseSessionMng( Frgmnt_SessionMng_t *p_sessionMng );
static FrgmntStatus_t LoRaFrgmntDataBlockWrite( uint8_t fragIndex, uint16_t n_th, uint8_t *p_pmn );

/*** Event; notify to upper ***/
LoRaFrgmntEventCb_t FrgmntEventCbFuncs = {0};

/*** IB ***/
typedef struct
{
    uint32_t    __reserved;
} Frgmnt_IB_params_t;

Frgmnt_IB_params_t  FrgmntIBParams;

/*** Fragment state ***/
#define FRGMNT_STATE_NONE           0x00
#define FRGMNT_STATE_INITIALIZED    0x01
#define FRGMNT_STATE_RUNNING        0x02
uint8_t FrgmntState = FRGMNT_STATE_NONE;

/*** Fragmentation session index ***/
#define FRGMNT_SESSIONINDEX_NOUSE   0xFF
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
uint16_t FrgmntsessionCntPrev[ 4 ] = { (uint16_t)(-1), (uint16_t)(-1), (uint16_t)(-1), (uint16_t)(-1) };
#endif

/*!
 * FragmentDataBlock initialization
 */
FrgmntStatus_t LoRaFragmentInit( LoRaFrgmntEventCb_t *p_frgmntEventCb )
{
    Frgmnt_SessionMng_t *p_sessionMng;
    uint8_t             i;

    if( p_frgmntEventCb == NULL )
    {
        return FRGMNT_STATUS_PARAMETER_INVALID;
    }
    if( ( p_frgmntEventCb->LoRaFrgmntSessionSetupIndication == NULL ) || 
        ( p_frgmntEventCb->LoRaFrgmntDataBlockIndication == NULL ) || 
        ( p_frgmntEventCb->LoRaFrgmntSessionEndIndication == NULL ) )
    {
        return FRGMNT_STATUS_PARAMETER_INVALID;
    }

    /* IB */
    memset1( (uint8_t *)&FrgmntIBParams, 0x00, sizeof(Frgmnt_IB_params_t) );

    /* for command */
    memset1( (uint8_t *)&FrgmntCmdQueue, 0x00, sizeof(Frgmnt_RxCmdQueue_t) );

    /* session mng */
    for( i = 0; i < FRGMNT_CONFIG_MAX_FRAG_INDEX; i++ )
    {
        p_sessionMng  = &( FrgmntSessionMng[ i ] );

        p_sessionMng->p_dataBlockBuff = &( FrgmntDataBlockBuffer[ i ][ 0 ] );  // need to link for initialization
        LoRaFrgmntReleaseSessionMng( p_sessionMng );
    }

    /* event */
    if( p_frgmntEventCb != &FrgmntEventCbFuncs )  // means upper-layer requests initialization
    {
        memcpy1( (uint8_t *)&FrgmntEventCbFuncs, (uint8_t *)p_frgmntEventCb, sizeof(LoRaFrgmntEventCb_t) );
    }

    /* FEC */
    LoRaFragmentFecInit();

    FrgmntState = FRGMNT_STATE_INITIALIZED;
    return FRGMNT_STATUS_OK;
}

/*!
 * Fragment start
 */
FrgmntStatus_t LoRaFragmentStart( void )
{
    FrgmntStatus_t  res;

    // init
    res = FRGMNT_STATUS_ERROR;

    if( FrgmntState == FRGMNT_STATE_INITIALIZED )
    {
        FrgmntState = FRGMNT_STATE_RUNNING;
        res = FRGMNT_STATUS_OK;
    }

    return res;
}

/*!
 * Fragment stop
 */
void LoRaFragmentStop( void )
{
    if( FrgmntState != FRGMNT_STATE_NONE )
    {
        LoRaFragmentInit( &FrgmntEventCbFuncs );
    }
}

/*!
 * MCPS-Indication event function for FragmentDataBlock
 */
FrgmntStatus_t LoRaFragmentMcpsIndication( McpsIndication_t *p_mcpsIndication )
{
    FrgmntStatus_t  funcRet;
    uint8_t         *p_buffer;
    uint8_t         bufferSize;

    // Fragment is not running
    if( FrgmntState != FRGMNT_STATE_RUNNING )
    {
        return FRGMNT_STATUS_ERROR;
    }

    // Reject error indication
    if ( p_mcpsIndication->Status != LORAMAC_EVENT_INFO_STATUS_OK )
    {
        return FRGMNT_STATUS_ERROR;
    }

    // init
    p_buffer   = p_mcpsIndication->Buffer;
    bufferSize = p_mcpsIndication->BufferSize;

    memset1( (uint8_t *)&FrgmntCmdQueue, 0x00, sizeof(Frgmnt_RxCmdQueue_t) );
    if( p_mcpsIndication->McpsIndication == MCPS_MULTICAST )
    {
        FrgmntCmdQueue.isBroadcast = 1;
    }

    // Get FragmentDataBlock commands
    //   DataFragment command SHALL be the only command in a message's payload. 
    if( (bufferSize > 0) && (*p_buffer == FRGMNT_CID_DATA_FRAGMENT) )  //= CID = DataFragmnet
    {
        LoRaFrgmntHandleDataFragmentMsg( p_mcpsIndication->DevAddress, &( p_buffer[1] ), (bufferSize - 1) );
    }
    else
    {
        while( bufferSize > 0 )
        {
            switch( *p_buffer )  //= CID
            {
                case FRGMNT_CID_PACKAGE_VERSION_REQ:
                    // Unicast only
                    if( (p_mcpsIndication->McpsIndication == MCPS_UNCONFIRMED) ||
                        (p_mcpsIndication->McpsIndication == MCPS_CONFIRMED) )
                    {
                        funcRet = LoRaFrgmntHandlePackageVersionReq( p_mcpsIndication->DevAddress, &( p_buffer[1] ), (bufferSize - 1) );
                    }
                    else
                    {
                        funcRet = FRGMNT_STATUS_COMMAND_ERROR;
                    }

                    if( funcRet == FRGMNT_STATUS_OK )
                    {
                        bufferSize -= (FRGMNT_PLEN_PACKAGE_VERSION_REQ + 1);  // +1 = CID length
                        p_buffer   += (FRGMNT_PLEN_PACKAGE_VERSION_REQ + 1);
                    }
                    else
                    {
                        bufferSize = 0;  // exit from while() loop
                    }
                    break;

                case FRGMNT_CID_FRAG_SESSION_STATUS_REQ:
                    // Unicast & multicast
                    if( (p_mcpsIndication->McpsIndication == MCPS_UNCONFIRMED) ||
                        (p_mcpsIndication->McpsIndication == MCPS_CONFIRMED)   ||
                        (p_mcpsIndication->McpsIndication == MCPS_MULTICAST) )
                    {
                        funcRet = LoRaFrgmntHandleFragSessionStatusReq( p_mcpsIndication->DevAddress, &( p_buffer[1] ), (bufferSize - 1) );
                    }
                    else
                    {
                        funcRet = FRGMNT_STATUS_COMMAND_ERROR;
                    }

                    if( funcRet == FRGMNT_STATUS_OK )
                    {
                        bufferSize -= (FRGMNT_PLEN_FRAG_SESSION_STATUS_REQ + 1);  // +1 = CID length
                        p_buffer   += (FRGMNT_PLEN_FRAG_SESSION_STATUS_REQ + 1);
                    }
                    else
                    {
                        bufferSize = 0;  // exit from while() loop
                    }
                    break;

                case FRGMNT_CID_FRAG_SESSION_SETUP_REQ:
                    // Unicast only
                    if( (p_mcpsIndication->McpsIndication == MCPS_UNCONFIRMED) ||
                        (p_mcpsIndication->McpsIndication == MCPS_CONFIRMED) )
                    {
                        funcRet = LoRaFrgmntHandleFragSessionSetupReq( p_mcpsIndication->DevAddress, &( p_buffer[1] ), (bufferSize - 1) );
                    }
                    else
                    {
                        funcRet = FRGMNT_STATUS_COMMAND_ERROR;
                    }

                    if( funcRet == FRGMNT_STATUS_OK )
                    {
                        bufferSize -= (FRGMNT_PLEN_FRAG_SESSION_SETUP_REQ + 1);  // +1 = CID length
                        p_buffer   += (FRGMNT_PLEN_FRAG_SESSION_SETUP_REQ + 1);
                    }
                    else
                    {
                        bufferSize = 0;  // exit from while() loop
                    }
                    break;

                case FRGMNT_CID_FRAG_SESSION_DELETE_REQ:
                    // Unicast only
                    if( (p_mcpsIndication->McpsIndication == MCPS_UNCONFIRMED) ||
                        (p_mcpsIndication->McpsIndication == MCPS_CONFIRMED) )
                    {
                        funcRet = LoRaFrgmntHandleFragSessionDeleteReq( p_mcpsIndication->DevAddress, &( p_buffer[1] ), (bufferSize - 1) );
                    }
                    else
                    {
                        funcRet = FRGMNT_STATUS_COMMAND_ERROR;
                    }

                    if( funcRet == FRGMNT_STATUS_OK )
                    {
                        bufferSize -= (FRGMNT_PLEN_FRAG_SESSION_DELETE_REQ + 1);  // +1 = CID length
                        p_buffer   += (FRGMNT_PLEN_FRAG_SESSION_DELETE_REQ + 1);
                    }
                    else
                    {
                        bufferSize = 0;  // exit from while() loop
                    }
                    break;

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
                case FRGMNT_CID_FRAG_DATA_BLOCK_RECEIVED_ANS:
                    // Unicast only
                    if( (p_mcpsIndication->McpsIndication == MCPS_UNCONFIRMED) ||
                        (p_mcpsIndication->McpsIndication == MCPS_CONFIRMED) )
                    {
                        funcRet = LoRaFrgmntHandleFragDataBlockRcvdAns( p_mcpsIndication->DevAddress, &( p_buffer[1] ), (bufferSize - 1) );
                    }
                    else
                    {
                        funcRet = FRGMNT_STATUS_COMMAND_ERROR;
                    }

                    if( funcRet == FRGMNT_STATUS_OK )
                    {
                        bufferSize -= (FRGMNT_PLEN_FRAG_DATA_BLOCK_RECEIVED_ANS + 1);  // +1 = CID length
                        p_buffer   += (FRGMNT_PLEN_FRAG_DATA_BLOCK_RECEIVED_ANS + 1);
                    }
                    else
                    {
                        bufferSize = 0;  // exit from while() loop
                    }
                    break;
#endif
                default:
                    bufferSize = 0;  // exit from while() loop
                    break;
            }
        }
    }

    if( FrgmntCmdQueue.numCmd == 0 )
    {
        return FRGMNT_STATUS_COMMAND_ERROR;
    }

    return FRGMNT_STATUS_OK;
}

/*!
 * Process command of FragmenteDataBlock
 */
void LoRaFragmentProcessCommand( uint8_t  *p_buffer, 
                                 uint8_t  *p_payloadLen, 
                                 uint8_t  bufferMaxSize, 
                                 uint32_t *p_txDelayMs,
                                 bool     *p_isRetransEn )
{
    FrgmntStatus_t  funcRet;
    Frgmnt_RxCmd_t  *p_rxCmd;
    uint8_t         payloadLen, payloadLenTotal;
    uint32_t        txDelayMs, tmpTxDelayMs;
    uint8_t         i;

    // Fragment is not running
    if( FrgmntState != FRGMNT_STATE_RUNNING )
    {
        return;
    }

    // init
    payloadLenTotal = 0;
    txDelayMs       = 0;

    for( i = 0; i < FrgmntCmdQueue.numCmd; i++ )
    {
        // init (loop)
        p_rxCmd      = &( FrgmntCmdQueue.rxCmdQueue[ i ] );
        tmpTxDelayMs = 0;

        switch( p_rxCmd->cid )
        {
            case FRGMNT_CID_PACKAGE_VERSION_REQ:
                funcRet = LoRaFrgmntProcessPackageVersionReq( p_buffer, 
                                                              &payloadLen, 
                                                              bufferMaxSize, 
                                                              &tmpTxDelayMs, 
                                                              p_rxCmd );
                break;

            case FRGMNT_CID_FRAG_SESSION_STATUS_REQ:
                funcRet = LoRaFrgmntProcessFragSessionStatusReq( p_buffer, 
                                                                 &payloadLen, 
                                                                 bufferMaxSize, 
                                                                 &tmpTxDelayMs, 
                                                                 p_rxCmd );
                break;

            case FRGMNT_CID_FRAG_SESSION_SETUP_REQ:
                funcRet = LoRaFrgmntProcessFragSessionSetupReq( p_buffer, 
                                                                &payloadLen, 
                                                                bufferMaxSize, 
                                                                &tmpTxDelayMs, 
                                                                p_rxCmd );
                break;

            case FRGMNT_CID_FRAG_SESSION_DELETE_REQ:
                funcRet = LoRaFrgmntProcessFragSessionDeleteReq( p_buffer, 
                                                                 &payloadLen, 
                                                                 bufferMaxSize, 
                                                                 &tmpTxDelayMs, 
                                                                 p_rxCmd );
                break;

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
            case FRGMNT_CID_FRAG_DATA_BLOCK_RECEIVED_ANS:
                funcRet = LoRaFrgmntProcessFragDataBlockRcvdAns( p_buffer, 
                                                                 &payloadLen, 
                                                                 bufferMaxSize, 
                                                                 &tmpTxDelayMs, 
                                                                 p_rxCmd );
                break;
#endif
            case FRGMNT_CID_DATA_FRAGMENT:
                funcRet = LoRaFrgmntProcessDataFragmentMsg( p_buffer, 
                                                            &payloadLen, 
                                                            bufferMaxSize, 
                                                            &tmpTxDelayMs, 
                                                            p_rxCmd );
                break;

            default:
                funcRet = FRGMNT_STATUS_COMMAND_ERROR;
                break;
        }

        if( funcRet == FRGMNT_STATUS_OK )
        {
            p_buffer        += payloadLen;
            payloadLenTotal += payloadLen;
            bufferMaxSize   -= payloadLen;

            if( FrgmntCmdQueue.isBroadcast != 0 )
            {
                if( txDelayMs < tmpTxDelayMs )
                {
                    txDelayMs = tmpTxDelayMs;
                }
            }
        }
        else
        {
            break;  // exit for(i) loop
        }
    }

    (*p_payloadLen)  = payloadLenTotal;
    (*p_txDelayMs)   = txDelayMs;
    (*p_isRetransEn) = true;

#ifdef DEBUG_FRGMNT
    if( (*p_payloadLen) > 0 )
    {
        if( FrgmntCmdQueue.isBroadcast != 0 )
        {
            print( "*FRGMNT:(apply BlockAckDelay) " );
        }
        else
        {
            print( "*FRGMNT:(not apply BlockAckDelay) " );
        }
        print( "delay=" );
        print_dec( (*p_txDelayMs), 10, '\0' );
        print( " msec" );
        print_newline();
    }
#endif

}

/*!
 * Process timer interrupt of FragmentDataBlock
 */
void LoRaFragmentProcessEvent( void )
{
#if 0  // Currently nothing to do. If you want to add operations, please check the 'FrgmntState'.
    if( FrgmntState != FRGMNT_STATE_RUNNING )
    {
        // Fragment is not running
        return;
    }
#endif
}

/*!
 * Returns whether Fragment is idle
 * If Fragment is not idle (return false), LoRaFragmentProcessEvent() call is required
 * ... always idle (return true)
 */
bool LoRaFragmentIsIdle( void )
{
    bool    bRet;

    // init 
    bRet = true;  // (init) idle - function call is not required

#if 0
    if( FrgmntState == FRGMNT_STATE_RUNNING )
    {
        // Currently nothing to do.
        // Add your code and return false if LoRaFragmentProcessEvent() call is required.
        //bRet = false;
    }
#endif

    return bRet;
}

/*!
 * Uplink result notification from upper layer
 */
void LoRaFragmentSendCompCommand( bool isSuccess )
{
    // nothing to do
}

/*!
 * IB Get Request
 */
FrgmntStatus_t LoRaFragmentIbGetRequest( uint8_t ib, void *vpVal )
{
#if 0  // Currently nothing to do. If you want to add operations, please check the 'FrgmntState'.
    if( FrgmntState == FRGMNT_STATE_NONE )
    {
        // Fragment is not initialized
        return FRGMNT_STATUS_ERROR;
    }
#endif

    // no IB
    return FRGMNT_STATUS_SERVICE_UNKNOWN;
}

/*!
 * IB Set Request
 */
FrgmntStatus_t LoRaFragmentIbSetRequest( uint8_t ib, void *vpVal )
{
#if 0  // Currently nothing to do. If you want to add operations, please check the 'FrgmntState'.
    if( FrgmntState == FRGMNT_STATE_NONE )
    {
        // Fragment is not initialized
        return FRGMNT_STATUS_ERROR;
    }
#endif

    // no IB
    return FRGMNT_STATUS_SERVICE_UNKNOWN;
}

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
/*!
 * Get Fragment command payload size
 */
FrgmntStatus_t LoRaFragmentGetRcvdCmdPayloadLen( uint8_t cid, uint8_t *p_cmdPayloadLen )
{
    FrgmntStatus_t   ret;

    // init
    ret = FRGMNT_STATUS_OK;

    switch( cid )
    {
        case FRGMNT_CID_PACKAGE_VERSION_REQ:
            (*p_cmdPayloadLen) = FRGMNT_PLEN_PACKAGE_VERSION_REQ;
            break;

        case FRGMNT_CID_FRAG_SESSION_STATUS_REQ:
            (*p_cmdPayloadLen) = FRGMNT_PLEN_FRAG_SESSION_STATUS_REQ;
            break;

        case FRGMNT_CID_FRAG_SESSION_SETUP_REQ:
            (*p_cmdPayloadLen) = FRGMNT_PLEN_FRAG_SESSION_SETUP_REQ;
            break;

        case FRGMNT_CID_FRAG_SESSION_DELETE_REQ:
            (*p_cmdPayloadLen) = FRGMNT_PLEN_FRAG_SESSION_DELETE_REQ;
            break;

//#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        case FRGMNT_CID_FRAG_DATA_BLOCK_RECEIVED_ANS:
            (*p_cmdPayloadLen) = FRGMNT_PLEN_FRAG_DATA_BLOCK_RECEIVED_ANS;
            break;
//#endif
        case FRGMNT_CID_DATA_FRAGMENT:
        default:
            ret = FRGMNT_STATUS_COMMAND_ERROR;
            break;
    }

    return ret;
}
#endif

//--------------------------------------------------------------------------------------------------

/*!
 * check received command; PackageVersionReq
 */
static FrgmntStatus_t LoRaFrgmntHandlePackageVersionReq( uint32_t devAddress, uint8_t *p_buffer, size_t length )
{
    Frgmnt_RxCmd_t  *p_rxCmd;

    // payload length check
    //  ... it has no payload

    // command queue check
    if( FrgmntCmdQueue.numCmd >= FRGMNT_RXCMD_QUEUENUM )
    {
        return FRGMNT_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( FrgmntCmdQueue.rxCmdQueue[ FrgmntCmdQueue.numCmd ] );

    // get parameter
    p_rxCmd->cid        = FRGMNT_CID_PACKAGE_VERSION_REQ;
    p_rxCmd->devAddress = devAddress;

    FrgmntCmdQueue.numCmd++;

#ifdef DEBUG_FRGMNT
    print( "*FRGMNT:PackageVersionReq" );
    print_newline();
#endif

    return FRGMNT_STATUS_OK;
}

/*!
 * check received command; FragSessionStatusReq
 */
static FrgmntStatus_t LoRaFrgmntHandleFragSessionStatusReq( uint32_t devAddress, uint8_t *p_buffer, size_t length )
{
    Frgmnt_RxCmd_t      *p_rxCmd;
    Frgmnt_StatusReq_t  *p_cmdStatusReq;

    // payload length check
    if( length < FRGMNT_PLEN_FRAG_SESSION_STATUS_REQ )
    {
        return FRGMNT_STATUS_LENGTH_ERROR;
    }
    // command queue check
    if( FrgmntCmdQueue.numCmd >= FRGMNT_RXCMD_QUEUENUM )
    {
        return FRGMNT_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( FrgmntCmdQueue.rxCmdQueue[ FrgmntCmdQueue.numCmd ] );
    p_cmdStatusReq = (Frgmnt_StatusReq_t *)&( p_rxCmd->reqCmdParam );

    // get parameter
    p_rxCmd->cid        = FRGMNT_CID_FRAG_SESSION_STATUS_REQ;
    p_rxCmd->devAddress = devAddress;
    p_cmdStatusReq->fragIndex    = ( (*p_buffer) & 0x06 ) >> 1;  // bit2-1 of FragStatusReqParams
    p_cmdStatusReq->participants = (*p_buffer) & 0x01;           // bit0   of FragStatusReqParams
    //p_buffer++;

    FrgmntCmdQueue.numCmd++;

#ifdef DEBUG_FRGMNT
    print( "*FRGMNT:FragSessionStatusReq" );
    print_newline();
    print( "*FRGMNT:  FragIndex=" );
    print_dec( p_cmdStatusReq->fragIndex, 3, '\0' );
    print( ", Participants=" );
    print_dec( p_cmdStatusReq->participants, 3, '\0' );
    print_newline();
#endif

    return FRGMNT_STATUS_OK;
}

/*!
 * check received command; FragSessionSetupReq
 */
static FrgmntStatus_t LoRaFrgmntHandleFragSessionSetupReq( uint32_t devAddress, uint8_t *p_buffer, size_t length )
{
    Frgmnt_RxCmd_t      *p_rxCmd;
    Frgmnt_SetupReq_t   *p_cmdSetupReq;

    // payload length check
    if( length < FRGMNT_PLEN_FRAG_SESSION_SETUP_REQ )
    {
        return FRGMNT_STATUS_LENGTH_ERROR;
    }
    // command queue check
    if( FrgmntCmdQueue.numCmd >= FRGMNT_RXCMD_QUEUENUM )
    {
        return FRGMNT_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( FrgmntCmdQueue.rxCmdQueue[ FrgmntCmdQueue.numCmd ] );
    p_cmdSetupReq = (Frgmnt_SetupReq_t *)&( p_rxCmd->reqCmdParam );

    // get parameter
    p_rxCmd->cid        = FRGMNT_CID_FRAG_SESSION_SETUP_REQ;
    p_rxCmd->devAddress = devAddress;

    p_cmdSetupReq->fragIndex      = ( (*p_buffer) & 0x30 ) >> 4;  // bit5-4 of FragSession
    p_cmdSetupReq->mcGroupBitMask = (*p_buffer) & 0x0F;           // bit3-0 of FragSession
    p_buffer++;

    p_cmdSetupReq->nbFrag = ( (uint16_t)p_buffer[1] << 8 ) | (uint16_t)p_buffer[0];
    p_buffer += 2;

    p_cmdSetupReq->fragSize = (*p_buffer);
    p_buffer++;

    p_cmdSetupReq->control = (*p_buffer);
    p_buffer++;

    p_cmdSetupReq->padding = (*p_buffer);
    p_buffer++;

    p_cmdSetupReq->descriptor = ( (uint32_t)p_buffer[3] << 24 ) | ( (uint32_t)p_buffer[2] << 16 ) |
                                ( (uint32_t)p_buffer[1] << 8 )  |   (uint32_t)p_buffer[0];
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    p_buffer += 4;

    p_cmdSetupReq->sessionCnt = ( (uint16_t)p_buffer[1] << 8 ) | (uint16_t)p_buffer[0];
    p_buffer += 2;

    p_cmdSetupReq->mic = ( (uint32_t)p_buffer[3] << 24 ) | ( (uint32_t)p_buffer[2] << 16 ) |
                         ( (uint32_t)p_buffer[1] << 8 )  |   (uint32_t)p_buffer[0];
    //p_buffer += 4;
#endif

    FrgmntCmdQueue.numCmd++;
    
#ifdef DEBUG_FRGMNT
    print( "*FRGMNT:FragSessionSetupReq" );
    print_newline();
    print( "*FRGMNT:  FragIndex=" );
    print_dec( p_cmdSetupReq->fragIndex, 3, '\0' );
    print( ", McGroupBitMask=0x" );
    print_hex( p_cmdSetupReq->mcGroupBitMask, 1 );
    print_newline();
    print( "*FRGMNT:  NbFrag=" );
    print_dec( p_cmdSetupReq->nbFrag, 3, '\0' );
    print( ", FragSize=" );
    print_dec( p_cmdSetupReq->fragSize, 3, '\0' );
    print( ", Padding=" );
    print_dec( p_cmdSetupReq->padding, 3, '\0' );
    print_newline();
    print( "*FRGMNT:  Ctrl.FragAlgo=" );
    print_dec( ((p_cmdSetupReq->control & 0x38) >> 3), 3, '\0' );
    print( ", Ctrl.BlockAckDelay=" );
    print_dec( (p_cmdSetupReq->control & 0x07), 3, '\0' );
    print_newline();
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    print( "*FRGMNT:  Ctrl.AckReception=" );
    print_dec( ((p_cmdSetupReq->control & 0x40) >> 6), 3, '\0' );
    print_newline();
#endif
    print( "*FRGMNT:  Descriptor=0x" );
    print_hex( p_cmdSetupReq->descriptor, 8 );
    print_newline();
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    print( "*FRGMNT:  SessonCnt=0x" );
    print_hex( p_cmdSetupReq->sessionCnt, 4 );
    print( ", MIC=0x" );
    print_hex( p_cmdSetupReq->mic, 8 );
    print_newline();
#endif
#endif

    return FRGMNT_STATUS_OK;
}

/*!
 * check received command; FragSessionDeleteReq
 */
static FrgmntStatus_t LoRaFrgmntHandleFragSessionDeleteReq( uint32_t devAddress, uint8_t *p_buffer, size_t length )
{
    Frgmnt_RxCmd_t      *p_rxCmd;
    Frgmnt_DeleteReq_t  *p_cmdDeleteReq;

    // payload length check
    if( length < FRGMNT_PLEN_FRAG_SESSION_DELETE_REQ )
    {
        return FRGMNT_STATUS_LENGTH_ERROR;
    }
    // command queue check
    if( FrgmntCmdQueue.numCmd >= FRGMNT_RXCMD_QUEUENUM )
    {
        return FRGMNT_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( FrgmntCmdQueue.rxCmdQueue[ FrgmntCmdQueue.numCmd ] );
    p_cmdDeleteReq = (Frgmnt_DeleteReq_t *)&( p_rxCmd->reqCmdParam );

    // get parameter
    p_rxCmd->cid        = FRGMNT_CID_FRAG_SESSION_DELETE_REQ;
    p_rxCmd->devAddress = devAddress;

    p_cmdDeleteReq->fragIndex = (*p_buffer) & 0x03;  // bit1-0 of Param
    //p_buffer++;

    FrgmntCmdQueue.numCmd++;
    
#ifdef DEBUG_FRGMNT
    print( "*FRGMNT:FragSessionDeleteReq" );
    print_newline();
    print( "*FRGMNT:  FragIndex=" );
    print_dec( p_cmdDeleteReq->fragIndex, 3, '\0' );
    print_newline();
#endif

    return FRGMNT_STATUS_OK;
}

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
/*!
 * check received command; FragDataBlockReceivedAns
 */
static FrgmntStatus_t LoRaFrgmntHandleFragDataBlockRcvdAns( uint32_t devAddress, uint8_t *p_buffer, size_t length )
{
    Frgmnt_RxCmd_t              *p_rxCmd;
    Frgmnt_DataBlockRcvdAns_t   *p_cmdDataBlkRcvdAns;

    // payload length check
    if( length < FRGMNT_PLEN_FRAG_DATA_BLOCK_RECEIVED_ANS )
    {
        return FRGMNT_STATUS_LENGTH_ERROR;
    }
    // command queue check
    if( FrgmntCmdQueue.numCmd >= FRGMNT_RXCMD_QUEUENUM )
    {
        return FRGMNT_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( FrgmntCmdQueue.rxCmdQueue[ FrgmntCmdQueue.numCmd ] );
    p_cmdDataBlkRcvdAns = (Frgmnt_DataBlockRcvdAns_t *)&( p_rxCmd->reqCmdParam );

    // get parameter
    p_rxCmd->cid        = FRGMNT_CID_FRAG_DATA_BLOCK_RECEIVED_ANS;
    p_rxCmd->devAddress = devAddress;

    p_cmdDataBlkRcvdAns->fragIndex = (*p_buffer) & 0x03;  // bit1-0 of Param
    //p_buffer++;

    FrgmntCmdQueue.numCmd++;
    
#ifdef DEBUG_FRGMNT
    print( "*FRGMNT:FragDataBlockReceivedAns" );
    print_newline();
    print( "*FRGMNT  Status.FragIndex = 0x" );
    print_hex( p_cmdDataBlkRcvdAns->fragIndex, 2 );
    print_newline();
#endif

    return FRGMNT_STATUS_OK;
}
#endif

/*!
 * check received command; DataFragment
 */
static FrgmntStatus_t LoRaFrgmntHandleDataFragmentMsg( uint32_t devAddress, uint8_t *p_buffer, size_t length )
{
    FrgmntStatus_t              res;
    Frgmnt_RxCmd_t              *p_rxCmd;
    Frgmnt_DataFragmentMsg_t    *p_cmdDataFragMsg;
    Frgmnt_SessionMng_t         *p_sessionMng;
    uint8_t                     fragIndex;
    uint16_t                    n_th;

    // payload length check (minimum)
    if( length < FRGMNT_PLEN_DATA_FRAGMENT )
    {
        return FRGMNT_STATUS_LENGTH_ERROR;
    }
    // command queue check
    if( FrgmntCmdQueue.numCmd >= FRGMNT_RXCMD_QUEUENUM )
    {
        return FRGMNT_STATUS_ERROR;  // never comes here
    }

    // init
    res              = FRGMNT_STATUS_ERROR;
    p_rxCmd          = &( FrgmntCmdQueue.rxCmdQueue[ FrgmntCmdQueue.numCmd ] );
    p_cmdDataFragMsg = (Frgmnt_DataFragmentMsg_t *)&( p_rxCmd->reqCmdParam );

    // get parameter
    fragIndex = ( p_buffer[1] & 0xC0 ) >> 6;                                               // bit15-14 of Inxex&N
    n_th = ( ( (uint16_t)p_buffer[1] << 8 ) | (uint16_t)p_buffer[0] ) & (uint16_t)0x3FFF;  // bit13-0  of Inxex&N
    p_buffer += 2;

    // get session mng
    p_sessionMng = LoRaFrgmntSearchSessionMng( fragIndex );
    if( p_sessionMng != NULL )
    {
        if( ( p_sessionMng->fragSize > 0 ) &&
            ( p_sessionMng->fragSize == (length - FRGMNT_PLEN_DATA_FRAGMENT) ) )  // less than 256
        {
            memcpy1( (uint8_t *)&(FrgmntRxDataBuff[0]), p_buffer, p_sessionMng->fragSize );

            p_rxCmd->cid        = FRGMNT_CID_DATA_FRAGMENT;
            p_rxCmd->devAddress = devAddress;

            p_cmdDataFragMsg->fragIndex = fragIndex;
            p_cmdDataFragMsg->n_th      = n_th;
            p_cmdDataFragMsg->pmnLen    = p_sessionMng->fragSize;
            p_cmdDataFragMsg->p_pmn     = &(FrgmntRxDataBuff[0]);

            FrgmntCmdQueue.numCmd++;
            res = FRGMNT_STATUS_OK;

#ifdef DEBUG_FRGMNT
            print( "*FRGMNT:DataFragment" );
            print_newline();
            print( "*FRGMNT:  FragIndex=" );
            print_dec( p_cmdDataFragMsg->fragIndex, 3, '\0' );
            print_newline();
            print( "*FRGMNT:  n_th = " );
            print_dec( p_cmdDataFragMsg->n_th, 5, '\0' );
            print( " (nbFrag=" );
            print_dec( p_sessionMng->nbFrag, 5, '\0' );
            print( ")" );
            if( p_cmdDataFragMsg->n_th > p_sessionMng->nbFrag )
            {
                print( "  <Redundancy fragment>" );
            }
            print_newline();
            print( "*FRGMNT:  " );
            for( uint8_t _i = 0; _i < p_cmdDataFragMsg->pmnLen; _i++ )
            {
                print_hex( p_cmdDataFragMsg->p_pmn[ _i ], 2 );
                print( " " );
            }
            print_newline();
            print( "*FRGMNT:  (fragSize=" );
            print_dec( p_sessionMng->fragSize, 3, '\0' );
            print( ")" );
            print_newline();
#endif
        }
    }

    return res;
}


//--------------------------------------------------------------------------------------------------

/*!
 * Process received command; PackageVersionReq
 */
static FrgmntStatus_t LoRaFrgmntProcessPackageVersionReq( uint8_t        *p_buffer, 
                                                          uint8_t        *p_payloadLen, 
                                                          uint8_t        bufferMaxSize, 
                                                          uint32_t       *p_txDelayMs,
                                                          Frgmnt_RxCmd_t *p_rcCmd/*nouse*/ )
{
    // length check
    if( bufferMaxSize < (FRGMNT_PLEN_PACKAGE_VERSION_ANS + 1) )  // +1 = CID length
    {
        return FRGMNT_STATUS_LENGTH_ERROR;
    }

    // make PackageVersionAns
    p_buffer[0] = FRGMNT_CID_PACKAGE_VERSION_ANS;
    p_buffer[1] = FRGMNT_PACKAGE_IDENTIFIER;
    p_buffer[2] = FRGMNT_PACKAGE_VERSION;

    (*p_payloadLen) = FRGMNT_PLEN_PACKAGE_VERSION_ANS + 1;  // +1 = CID length

#ifdef DEBUG_FRGMNT
    print( "*FRGMNT:PackageVersionAns" );
    print_newline();
    print( "*FRGMNT:  PackageIdentifier=" );
    print_dec( p_buffer[1], 3, '\0' );
    print( ", PackageVersion=" );
    print_dec( p_buffer[2], 3, '\0' );
    print_newline();
#endif

    return FRGMNT_STATUS_OK;
}

/*!
 * Process received command; FragSessionStatusReq
 */
static FrgmntStatus_t LoRaFrgmntProcessFragSessionStatusReq( uint8_t        *p_buffer, 
                                                             uint8_t        *p_payloadLen, 
                                                             uint8_t        bufferMaxSize, 
                                                             uint32_t       *p_txDelayMs,
                                                             Frgmnt_RxCmd_t *p_rcCmd )
{
    Frgmnt_StatusReq_t      *p_cmdStatusReq;
    uint8_t                 status;
    uint16_t                received_index;
    Frgmnt_SessionMng_t     *p_sessionMng;
    uint8_t                 payloadLen;
    bool                    isNeedAns;

    // length check
    //   ... later

    // init
    p_cmdStatusReq = (Frgmnt_StatusReq_t *)&( p_rcCmd->reqCmdParam );
    status         = 0x00;
    isNeedAns      = true;

    // search session mng
    p_sessionMng = LoRaFrgmntSearchSessionMng( p_cmdStatusReq->fragIndex );

    // check
    if( p_sessionMng != NULL )
    {
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        if( ( p_sessionMng->dataFragStatus == FRGMNT_DATAFRAG_STATUS_MEMORYERROR ) ||
            ( p_sessionMng->dataFragStatus == FRGMNT_DATAFRAG_STATUS_MICERROR ) )
        {
            status |= p_sessionMng->dataFragStatus;  // bit1 = MIC error, bit0 = Memory error
        }
#else  // FUOTA V1.0.0
        if( p_sessionMng->dataFragStatus == FRGMNT_DATAFRAG_STATUS_MEMORYERROR )
        {
            status |= FRGMNT_DATAFRAG_STATUS_MEMORYERROR;  // bit0 = Memory error
        }
#endif

        // check - need to send answer?
        if( ( p_cmdStatusReq->participants == 0 ) &&  // participants = 0 ... Only missing receiver SHALL answer
            ( status == 0x00 ) )
        {
            if( p_sessionMng->dataFragStatus == FRGMNT_DATAFRAG_STATUS_COMPLETE_SUCCESS )
            {
                isNeedAns = false;
            }
        }
        else  // participants = 1 ... All receivers SHALL answer
        {
            // isNeedAns = true;
        }
    }
    else
    {
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        status |= 0x04;  // bit2 = Session does not exist
#else  // FUOTA V1.0.0
        return FRGMNT_STATUS_PARAMETER_INVALID;  // invalid session - cannot reply.
#endif
    }

    // make FragSessionStatusAns
    payloadLen = 0;
    if( isNeedAns == true )
    {
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        p_buffer[ 0 ] = FRGMNT_CID_FRAG_SESSION_STATUS_ANS;
        p_buffer[ 1 ] = status;
        payloadLen = 2;

        if( p_sessionMng != NULL )
        {
            received_index = ( (uint16_t)p_cmdStatusReq->fragIndex << 14 ) |  // bit15-14 = fragIndex
                             ( p_sessionMng->numFragReceived & 0x3FFF );      // bit13-0  = nbFragReceived
            p_buffer[ 2 ] = (uint8_t)(  received_index & 0x00FF );
            p_buffer[ 3 ] = (uint8_t)( (received_index & 0xFF00) >> 8 );

            if( p_sessionMng->numMissingUncodedFrag <= (uint16_t)255 )
            {
                p_buffer[ 4 ] = (uint8_t)( p_sessionMng->numMissingUncodedFrag );
            }
            else
            {
                p_buffer[ 4 ] = (uint16_t)255;
            }

            payloadLen = 5;
        }

#else  // FUOTA V1.0.0
        p_buffer[ 0 ] = FRGMNT_CID_FRAG_SESSION_STATUS_ANS;
        p_buffer[ 4 ] = status;
        payloadLen = 5;

        // (p_sessionMng is not NULL)
        received_index = ( (uint16_t)p_cmdStatusReq->fragIndex << 14 ) |  // bit15-14 = fragIndex
                         ( p_sessionMng->numFragReceived & 0x3FFF );      // bit13-0  = nbFragReceived
        p_buffer[ 1 ] = (uint8_t)(  received_index & 0x00FF );
        p_buffer[ 2 ] = (uint8_t)( (received_index & 0xFF00) >> 8 );
        
        if( p_sessionMng->numMissingUncodedFrag <= (uint16_t)255 )
        {
            p_buffer[ 3 ] = (uint8_t)( p_sessionMng->numMissingUncodedFrag );
        }
        else
        {
            p_buffer[ 3 ] = (uint16_t)255;
        }

#endif
        // length check
        if( payloadLen > bufferMaxSize )
        {
            return FRGMNT_STATUS_LENGTH_ERROR;
        }
    }

    (*p_payloadLen) = payloadLen;
    if( p_sessionMng != NULL )
    {
        (*p_txDelayMs) = LoRaFrgmntGetUplinkDelayMs( p_cmdStatusReq->fragIndex );
    }

#ifdef DEBUG_FRGMNT
    if( isNeedAns == true )
    {
        print( "*FRGMNT:FragSessionStatusAns" );
        print_newline();
        print( "*FRGMNT:  FragIndex=" );
        print_dec( p_cmdStatusReq->fragIndex, 3, '\0' );
        print( ", Status=0x" );
        print_hex( status, 2 );
        print_newline();
        if( p_sessionMng != NULL )
        {
            print( "*FRGMNT:  NbFragReceived=" );
            print_dec( p_sessionMng->numFragReceived, 5, '\0' );
            print( ", MissingFrag=" );
            print_dec( p_sessionMng->numMissingUncodedFrag, 5, '\0' );
            print_newline();
        }
    }
#endif

    return FRGMNT_STATUS_OK;
}

/*!
 * Process received command; FragSessionSetupReq
 */
static FrgmntStatus_t LoRaFrgmntProcessFragSessionSetupReq( uint8_t        *p_buffer, 
                                                            uint8_t        *p_payloadLen, 
                                                            uint8_t        bufferMaxSize, 
                                                            uint32_t       *p_txDelayMs,
                                                            Frgmnt_RxCmd_t *p_rcCmd )
{
    Frgmnt_SetupReq_t       *p_cmdSetupReq;
    Frgmnt_SessionMng_t     *p_sessionMng;
    uint8_t                 status;
    uint32_t                sizeDataBlk;
    FrgmntStatus_t          cbFuncRet;

    // length check
    if( bufferMaxSize < (FRGMNT_PLEN_FRAG_SESSION_SETUP_ANS + 1) )  // +1 = CID length
    {
        return FRGMNT_STATUS_LENGTH_ERROR;
    }

    // init
    (*p_payloadLen) = 0;

    p_cmdSetupReq = (Frgmnt_SetupReq_t *)&( p_rcCmd->reqCmdParam );
    status        = 0x00;
    sizeDataBlk   = 0;

    // check
    // check - fragIndex
    p_sessionMng = LoRaFrgmntSearchSessionMng( p_cmdSetupReq->fragIndex );
    if( p_sessionMng != NULL )
    {
        // Notify to upper which current session is stoped before new session will be started.
        (*FrgmntEventCbFuncs.LoRaFrgmntSessionEndIndication)( p_cmdSetupReq->fragIndex );

        // release session
        LoRaFrgmntReleaseSessionMng( p_sessionMng );
    }
    // Get new session
    p_sessionMng = LoRaFrgmntGetSessionMng( p_cmdSetupReq->fragIndex );
    if( p_sessionMng == NULL )
    {
        status |= 0x04;  // bit2 : FragIndex unsupported
    }

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    // check - SessionCnt replay
    if( status == 0x00 )  //= p_sessionMng is not NULL
    {
        if( (int16_t)( p_cmdSetupReq->sessionCnt - p_sessionMng->sessionCntPrev ) <= 0 )
        {
            status |= 0x10;  // bit4 : SessionCnt replay
        }
    }
#endif

    // check - NbFrag
    if( status == 0x00 )  //= p_sessionMng is not NULL
    {
        if( p_cmdSetupReq->nbFrag > FRGMNT_CONFIG_MAX_NBFRAG )
        {
            status |= 0x02;  // bit1 : Not enough memory
        }
    }
    // check - NbFrag, FragSize, and Padding  (memory size)
    if( status == 0x00 )  //= p_sessionMng is not NULL
    {
        sizeDataBlk  = (uint32_t)( p_cmdSetupReq->nbFrag ) * (uint32_t)( p_cmdSetupReq->fragSize );
        sizeDataBlk -= (uint32_t)( p_cmdSetupReq->padding );
        if( sizeDataBlk > FRGMNT_CONFIG_MAX_DATABLK_SIZE )
        {
            status |= 0x02;  // bit1 : Not enough memory
        }

        if( sizeDataBlk == 0 )
        {
            // None of the status bits match. Discard request.
            LoRaFrgmntReleaseSessionMng( p_sessionMng );
            return FRGMNT_STATUS_OK;
        }
    }

    // check - Control (FragAlgo)
    if( status == 0x00 )  //= p_sessionMng is not NULL
    {
        if( ( p_cmdSetupReq->control & 0x38 ) != 0x00 )  // bit5-3: FragAlgo  // 0 = FEC
        {
            status |= 0x01;  // bit0 : FragAlgo unsupported
        }
    }

    // check - descriptor
    if( status == 0x00 )  //= p_sessionMng is not NULL
    {
        // ask to upper whether descriptor is acceptable.
        cbFuncRet = (*FrgmntEventCbFuncs.LoRaFrgmntSessionSetupIndication)( p_cmdSetupReq->fragIndex, 
                                                                            p_cmdSetupReq->descriptor );
        if( cbFuncRet != FRGMNT_STATUS_OK )
        {
            status |= 0x08;  // bit3 : Wrong Descriptor
        }
    }

    // make FragSessionSetupAns
    p_buffer[0] = FRGMNT_CID_FRAG_SESSION_SETUP_ANS;
    p_buffer[1] = status | ( p_cmdSetupReq->fragIndex << 6 );  // bit7-6 : FragIndex

    // activate sesion
    if( status == 0x00 )  //= p_sessionMng is not NULL
    {
        p_sessionMng->dataFragStatus    = FRGMNT_DATAFRAG_STATUS_RUNNING;

        p_sessionMng->mcGroupBitMask    = p_cmdSetupReq->mcGroupBitMask;
        p_sessionMng->nbFrag            = p_cmdSetupReq->nbFrag;
        p_sessionMng->fragSize          = p_cmdSetupReq->fragSize;
        p_sessionMng->padding           = p_cmdSetupReq->padding;
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        p_sessionMng->ackReception      = ( p_cmdSetupReq->control & 0x40 ) >> 6;  // bit6  : AckReception
#endif
        p_sessionMng->blockAckDelay     = p_cmdSetupReq->control & 0x07;           // bit2-0: BlockAckDelay
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        p_sessionMng->sessionCntPrevSet = p_cmdSetupReq->sessionCnt;  // not set SessionCntPrev at this timing
        p_sessionMng->mic               = p_cmdSetupReq->mic;
#endif

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        // make B0 for MIC calculation
        p_sessionMng->b0Data[ 0] = 0x49;  // fixed value
        p_sessionMng->b0Data[ 1] = (uint8_t)( p_cmdSetupReq->sessionCnt & 0x00FF );
        p_sessionMng->b0Data[ 2] = (uint8_t)( ( p_cmdSetupReq->sessionCnt & 0xFF00 ) >> 8 );
        p_sessionMng->b0Data[ 3] = p_cmdSetupReq->fragIndex;
        p_sessionMng->b0Data[ 4] = (uint8_t)( p_cmdSetupReq->descriptor & 0x000000FF );
        p_sessionMng->b0Data[ 5] = (uint8_t)( ( p_cmdSetupReq->descriptor & 0x0000FF00 ) >> 8 );
        p_sessionMng->b0Data[ 6] = (uint8_t)( ( p_cmdSetupReq->descriptor & 0x00FF0000 ) >> 16 );
        p_sessionMng->b0Data[ 7] = (uint8_t)( ( p_cmdSetupReq->descriptor & 0xFF000000 ) >> 24 );
        p_sessionMng->b0Data[ 8] = 0;
        p_sessionMng->b0Data[ 9] = 0;
        p_sessionMng->b0Data[10] = 0;
        p_sessionMng->b0Data[11] = 0;
        p_sessionMng->b0Data[12] = (uint8_t)( sizeDataBlk & 0x000000FF );
        p_sessionMng->b0Data[13] = (uint8_t)( ( sizeDataBlk & 0x0000FF00 ) >> 8 );
        p_sessionMng->b0Data[14] = (uint8_t)( ( sizeDataBlk & 0x00FF0000 ) >> 16 );
        p_sessionMng->b0Data[15] = (uint8_t)( ( sizeDataBlk & 0xFF000000 ) >> 24 );
#endif

        LoRaFragmentFecSetup( p_cmdSetupReq->fragIndex, 
                              p_sessionMng->nbFrag, p_sessionMng->fragSize );
    }

    (*p_payloadLen) = FRGMNT_PLEN_FRAG_SESSION_SETUP_ANS + 1;  // +1 = CID length

    if( status != 0x00 )
    {
        // session is not established. release sessionMng.
        LoRaFrgmntReleaseSessionMng( p_sessionMng );
    }

#ifdef DEBUG_FRGMNT
    print( "*FRGMNT:FragSessionSetupAns" );
    print_newline();
    print( "*FRGMNT:  FragIndex=" );
    print_dec( p_cmdSetupReq->fragIndex, 3, '\0' );
    print( ", StatusBits=0x" );
    print_hex( (status & 0x3F), 2 );
    print_newline();
#endif

    return FRGMNT_STATUS_OK;
}

/*!
 * Process received command; FragSessionDeleteReq
 */
static FrgmntStatus_t LoRaFrgmntProcessFragSessionDeleteReq( uint8_t        *p_buffer, 
                                                             uint8_t        *p_payloadLen, 
                                                             uint8_t        bufferMaxSize, 
                                                             uint32_t       *p_txDelayMs,
                                                             Frgmnt_RxCmd_t *p_rcCmd )
{
    uint8_t                 fragIndex;
    Frgmnt_SessionMng_t     *p_sessionMng;
    uint8_t                 status;

    // length check
    if( bufferMaxSize < (FRGMNT_PLEN_FRAG_SESSION_DELETE_ANS + 1) )  // +1 = CID length
    {
        return FRGMNT_STATUS_LENGTH_ERROR;
    }

    // init
    fragIndex    = p_rcCmd->reqCmdParam.deleteReq.fragIndex;
    status       = 0x00;

    // check
    // search session mng & check if session is active.
    p_sessionMng = LoRaFrgmntSearchSessionMng( fragIndex );
    if( p_sessionMng == NULL )
    {
        status |= 0x04;  // bit2: session does not exist 
    }

    // make FragSessionDeleteAns
    p_buffer[0] = FRGMNT_CID_FRAG_SESSION_DELETE_ANS;
    p_buffer[1] = status | ( fragIndex & 0x03 );  // bit1-0; FragIndex

    // delete session
    if( p_sessionMng != NULL )
    {
        LoRaFrgmntReleaseSessionMng( p_sessionMng );

        // Notify to upper which current session is stoped.
        (*FrgmntEventCbFuncs.LoRaFrgmntSessionEndIndication)( fragIndex );
    }

    (*p_payloadLen) = FRGMNT_PLEN_FRAG_SESSION_DELETE_ANS + 1;  // +1 = CID length

#ifdef DEBUG_FRGMNT
    print( "*FRGMNT:FragSessionDeleteAns" );
    print_newline();
    print( "*FRGMNT:  FragIndex=" );
    print_dec( fragIndex, 3, '\0' );
    print( ", StatusBits=0x" );
    print_hex( (status & 0xFC), 2 );
    print_newline();
#endif

    return FRGMNT_STATUS_OK;
}

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
/*!
 * Process received command; FragDataBlockReceivedAns
 */
static FrgmntStatus_t LoRaFrgmntProcessFragDataBlockRcvdAns( uint8_t        *p_buffer, 
                                                             uint8_t        *p_payloadLen, 
                                                             uint8_t        bufferMaxSize, 
                                                             uint32_t       *p_txDelayMs,
                                                             Frgmnt_RxCmd_t *p_rcCmd )
{
    Frgmnt_DataBlockRcvdAns_t   *p_cmdDataBlkRcvdAns;
    Frgmnt_SessionMng_t         *p_sessionMng;

    // init
    (*p_payloadLen)  = 0;  // not used
    p_cmdDataBlkRcvdAns = (Frgmnt_DataBlockRcvdAns_t *)&( p_rcCmd->reqCmdParam );

    p_sessionMng = LoRaFrgmntSearchSessionMng( p_cmdDataBlkRcvdAns->fragIndex );
    if( p_sessionMng != NULL )
    {
        if( ( p_sessionMng->dataFragStatus == FRGMNT_DATAFRAG_STATUS_COMPLETE_SUCCESS ) ||
            ( p_sessionMng->dataFragStatus == FRGMNT_DATAFRAG_STATUS_MICERROR ) )
        {
            p_sessionMng->isRcvDataBlkRcvdAns = true;
        }
    }

    return FRGMNT_STATUS_OK;
}
#endif

/*!
 * Process received command; DataFragment
 */
static FrgmntStatus_t LoRaFrgmntProcessDataFragmentMsg( uint8_t        *p_buffer, 
                                                        uint8_t        *p_payloadLen, 
                                                        uint8_t        bufferMaxSize, 
                                                        uint32_t       *p_txDelayMs,
                                                        Frgmnt_RxCmd_t *p_rcCmd )
{
    Frgmnt_DataFragmentMsg_t    *p_cmdDataFragMsg;
    Frgmnt_SessionMng_t         *p_sessionMng;
    uint8_t                     mcGroupId;
    MibRequestConfirm_t         mibGet;
    uint32_t                    sizeDataBlk;
    uint16_t                    fecResult;
    FrgmntStatus_t              funcRes;
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    uint32_t                    calcMic;
    SecureElementStatus_t       secRes;
#endif

    // init
    (*p_payloadLen)  = 0;
    p_cmdDataFragMsg = (Frgmnt_DataFragmentMsg_t *)&( p_rcCmd->reqCmdParam );
    p_sessionMng     = NULL;
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
    calcMic          = 0;
#endif

    // check
    if( p_cmdDataFragMsg->n_th == 0 )
    {
#ifdef DEBUG_FRGMNT
        print( "*FRGMNT:  <= Discard it. (Nth=0)" );
        print_newline();
#endif
        return FRGMNT_STATUS_ERROR;  // N is starting from 1
    }

    // search session mng
    p_sessionMng = LoRaFrgmntSearchSessionMng( p_cmdDataFragMsg->fragIndex );

    // check DevAddress
    if( p_sessionMng != NULL )
    {
        mcGroupId = LoRaMacMcChannelGetGroupId( p_rcCmd->devAddress );
        if( (mcGroupId == 0xFF) || ((p_sessionMng->mcGroupBitMask & (1 << mcGroupId)) == 0x00) )
        {
            mibGet.Type = MIB_DEV_ADDR;
            LoRaMacMibGetRequestConfirm( &mibGet );
            if( p_rcCmd->devAddress != mibGet.Param.DevAddr )
            {
                p_sessionMng = NULL;  // neither unicast nor multicast which the session can use.
#ifdef DEBUG_FRGMNT
                print( "*FRGMNT:  <= Discard it. (Unknown DevAddress)" );
                print_newline();
#endif
            }
        }
    }

    // check n-th
    if( p_sessionMng != NULL )
    {
        if( p_sessionMng->prevIndex >= p_cmdDataFragMsg->n_th )
        {
            p_sessionMng = NULL;   // old fragment index
#ifdef DEBUG_FRGMNT
            print( "*FRGMNT:  <= Discard it. (old fragment index)" );
            print_newline();
#endif
        }
    }

    // process
    if( p_sessionMng != NULL )
    {
        // count the fragment rx
        // (V200) NbFragReceived is the total number of fragments received, including coded, uncoded, and repeated
        p_sessionMng->numFragReceived++;

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
        // update SessionCntPrev when 1st fragment is received
        if( p_sessionMng->numFragReceived == 1 )
        {
            p_sessionMng->sessionCntPrev                        = p_sessionMng->sessionCntPrevSet;
            FrgmntsessionCntPrev[ p_cmdDataFragMsg->fragIndex ] = p_sessionMng->sessionCntPrevSet;
        }
#endif

        if( p_sessionMng->dataFragStatus != FRGMNT_DATAFRAG_STATUS_RUNNING )
        {
#ifdef DEBUG_FRGMNT
            print( "*FRGMNT:  <= Discard it. (already finished or error occurred)" );
            print_newline();
#endif
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
            // send FragDataBlockReceivedReq (retry; Ans is not received)
            LoRaFrgmntSendFragDataBlockReceivedReq( p_buffer, p_payloadLen, bufferMaxSize, 
                                                    p_txDelayMs, p_cmdDataFragMsg->fragIndex );
#endif
#if (FUOTA_VERSION == FUOTA_VERSION_1_0_0)
            // cancel counted the fragment rx
            p_sessionMng->numFragReceived--;
#endif
            return FRGMNT_STATUS_OK;
        }

        // check lost
        while( (p_sessionMng->prevIndex + 1 ) < p_cmdDataFragMsg->n_th )
        {
            p_sessionMng->prevIndex++;
            if( p_sessionMng->prevIndex <= p_sessionMng->nbFrag )
            {
                p_sessionMng->numMissingUncodedFrag++;

                funcRes = LoRaFragmentFecMissedUncoded( p_cmdDataFragMsg->fragIndex, p_sessionMng->prevIndex );
                if( funcRes != FRGMNT_STATUS_OK )
                {
                    p_sessionMng->dataFragStatus = FRGMNT_DATAFRAG_STATUS_MEMORYERROR;
#ifdef DEBUG_FRGMNT
                    print( "*FRGMNT:  <= Discard it. (Memory error)" );
                    print_newline();
#endif
                    return FRGMNT_STATUS_ERROR;
                }
            }
        }
        
        // uncoded fragment
        if( p_cmdDataFragMsg->n_th <= p_sessionMng->nbFrag )
        {
            // store fragment data
            LoRaFrgmntDataBlockWrite( p_cmdDataFragMsg->fragIndex, 
                                      p_cmdDataFragMsg->n_th, 
                                      p_cmdDataFragMsg->p_pmn );
#ifdef DEBUG_FRGMNT
            print( "*FRGMNT:  <= Stored." );
            print_newline();
#endif
        }
        // redundancy fragment
        else
        {
            // recover by FEC
            fecResult = LoRaFragmentFecProcessRedundancy( p_cmdDataFragMsg->fragIndex, 
                                                          p_cmdDataFragMsg->n_th, 
                                                          p_cmdDataFragMsg->p_pmn,
                                                          p_sessionMng->p_dataBlockBuff );
            if( fecResult != FRGMNT_FEC_STATUS_ONGOING )
            {
                p_sessionMng->numMissingUncodedFrag = 0;  // complete
            }
        }

        // check if it is finished.
        if( p_cmdDataFragMsg->n_th >= p_sessionMng->nbFrag )
        {
            if( p_sessionMng->numMissingUncodedFrag == 0 )
            {
                p_sessionMng->dataFragStatus = FRGMNT_DATAFRAG_STATUS_COMPLETE_SUCCESS;
                sizeDataBlk  = (uint32_t)( p_sessionMng->nbFrag ) * (uint32_t)( p_sessionMng->fragSize );
                sizeDataBlk -= (uint32_t)( p_sessionMng->padding );

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
                // Calculate and verify MIC
                secRes = SecureElementComputeAesCmac( p_sessionMng->b0Data, 
                                                      p_sessionMng->p_dataBlockBuff, 
                                                      sizeDataBlk, 
                                                      DATA_BLK_INT_KEY, 
                                                      &calcMic );
  #ifdef DEBUG_FRGMNT
                {
                    uint8_t _i;
                    print( "*FRGMNT: [MIC] rcvd = 0x" );
                    print_hex( p_sessionMng->mic, 8 );
                    print( " / calc = 0x" );
                    print_hex( calcMic, 8 );
                    print_newline();
                    print( "*FRGMNT: [MIC] B0 = " );
                    for( _i = 0; _i < 16; _i++ )
                    {
                        print_hex( p_sessionMng->b0Data[_i], 2 );
                    }
                    print_newline();
                }
  #endif
                if( ( secRes != SECURE_ELEMENT_SUCCESS ) || ( calcMic != p_sessionMng->mic )  )
                {
                    p_sessionMng->dataFragStatus = FRGMNT_DATAFRAG_STATUS_MICERROR;
                }

                // send FragDataBlockReceivedReq
                LoRaFrgmntSendFragDataBlockReceivedReq( p_buffer, p_payloadLen, bufferMaxSize,
                                                        p_txDelayMs, p_cmdDataFragMsg->fragIndex );
#endif

                // Indicate data block to upper
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
                if( p_sessionMng->dataFragStatus == FRGMNT_DATAFRAG_STATUS_COMPLETE_SUCCESS )
#endif
                {
                    (*FrgmntEventCbFuncs.LoRaFrgmntDataBlockIndication)( p_cmdDataFragMsg->fragIndex,
                                                                         p_sessionMng->p_dataBlockBuff,
                                                                         sizeDataBlk );
                }
            }
        }

        // for next
        p_sessionMng->prevIndex = p_cmdDataFragMsg->n_th;
    }

    return FRGMNT_STATUS_OK;
}

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
static FrgmntStatus_t LoRaFrgmntSendFragDataBlockReceivedReq( uint8_t  *p_buffer, 
                                                              uint8_t  *p_payloadLen, 
                                                              uint8_t  bufferMaxSize, 
                                                              uint32_t *p_txDelayMs,
                                                              uint8_t  fragIndex )
{
    Frgmnt_SessionMng_t *p_sessionMng;
    uint8_t             status;
    
    // length check
    if( bufferMaxSize < (FRGMNT_PLEN_FRAG_DATA_BLOCK_RECEIVED_REQ + 1) )  // +1 = CID length
    {
        return FRGMNT_STATUS_LENGTH_ERROR;
    }

    // init
    (*p_payloadLen) = 0;

    p_sessionMng = LoRaFrgmntSearchSessionMng( fragIndex );
    if( p_sessionMng == NULL )
    {
        return FRGMNT_STATUS_ERROR;  // (fail-safe; never comes here)
    }

    // check
    // FragDataBlockReceivedReq can be sent 
    // only if the AckReception bit was set in the FragSessionSetupReq command
    if( p_sessionMng->ackReception != 0 )
    {
        if( ( p_sessionMng->isRcvDataBlkRcvdAns == true ) ||
            ( ( p_sessionMng->dataFragStatus != FRGMNT_DATAFRAG_STATUS_COMPLETE_SUCCESS ) && 
              ( p_sessionMng->dataFragStatus != FRGMNT_DATAFRAG_STATUS_MICERROR ) ) )
        {
            // Fragment session is not finished yet
            // or FragDataBlockReceivedAns has already been received
        }
        else
        {
            // make FragDataBlockReceivedReq
            status = fragIndex & 0x03;  // bit1-0: FragIndex
            if( p_sessionMng->dataFragStatus == FRGMNT_DATAFRAG_STATUS_MICERROR )
            {
                status = status | 0x04;  // bit2: MICError
            }
            
            p_buffer[0] = FRGMNT_CID_FRAG_DATA_BLOCK_RECEIVED_REQ;
            p_buffer[1] = status;
        
            (*p_payloadLen) = FRGMNT_PLEN_FRAG_DATA_BLOCK_RECEIVED_REQ + 1;  // +1 = CID length
            (*p_txDelayMs) = LoRaFrgmntGetUplinkDelayMs( fragIndex );
        
#ifdef DEBUG_FRGMNT
            print( "*FRGMNT:FragDataBlockReceivedReq" );
            print_newline();
            print( "*FRGMNT:  FragIndex=" );
            print_dec( fragIndex, 3, '\0' );
            print( ", MICError=" );
            print_dec( (status > 2), 3, '\0' );
            print_newline();
#endif
        }
    }

    return FRGMNT_STATUS_OK;
}
#endif

static uint32_t LoRaFrgmntGetUplinkDelayMs( uint8_t fragIndex )
{
    uint32_t                retDelayMs;
    Frgmnt_SessionMng_t     *p_sessionMng;
    
    // init
    retDelayMs = 0;

    p_sessionMng = LoRaFrgmntSearchSessionMng( fragIndex );
    if( p_sessionMng == NULL )
    {
        return (uint32_t)0;  // (fail-safe; never comes here)
    }

    // The actual delay SHALL be rand() * 2^(BlockAckDelay+4) seconds 
    //   where rand() is a random real number in the [0:1] interval. 
    retDelayMs  = (uint32_t)1 << (p_sessionMng->blockAckDelay + 4);
    retDelayMs *= randr( 1, 1000 );

    return( retDelayMs );
}

//--------------------------------------------------------------------------------------------------

static FrgmntStatus_t LoRaFrgmntDataBlockWrite( uint8_t fragIndex, uint16_t n_th, uint8_t *p_pmn )
{
    Frgmnt_SessionMng_t *p_sessionMng;
    uint16_t            writePos;
    uint8_t             writeSize;

    if( p_pmn == NULL )
    {
        return FRGMNT_STATUS_ERROR;
    }

    // init
    p_sessionMng = LoRaFrgmntSearchSessionMng( fragIndex );
    if( p_sessionMng == NULL )
    {
        return FRGMNT_STATUS_ERROR;  // (fail-safe; never comes here)
    }

    // check
    if( ( n_th == 0 ) || ( n_th > p_sessionMng->nbFrag ) )
    {
        return FRGMNT_STATUS_ERROR;
    }

    // write to buffer
    writePos  = (uint16_t)( p_sessionMng->fragSize ) * (n_th - 1);
    writeSize = p_sessionMng->fragSize;
    if( n_th == p_sessionMng->nbFrag )
    {
        writeSize -= p_sessionMng->padding;
    }

    memcpy1( &( p_sessionMng->p_dataBlockBuff[ writePos ] ), p_pmn, writeSize );

    return FRGMNT_STATUS_OK;
}

//--------------------------------------------------------------------------------------------------

static Frgmnt_SessionMng_t* LoRaFrgmntSearchSessionMng( uint8_t fragIndex )
{
    uint8_t             i;
    Frgmnt_SessionMng_t *p_sessionMng, *p_searchMng;

    // init
    p_sessionMng = NULL;

    for( i = 0; i < FRGMNT_CONFIG_MAX_FRAG_INDEX; i++ )
    {
        p_searchMng = &( FrgmntSessionMng[ i ] );

        if( p_searchMng->fragIndex_session == fragIndex )
        {
            p_sessionMng = p_searchMng;
            break;  // exit from for(i) loop
        }
    }

    return p_sessionMng;
}

static Frgmnt_SessionMng_t* LoRaFrgmntGetSessionMng( uint8_t fragIndex )
{
    uint8_t             i;
    Frgmnt_SessionMng_t *p_sessionMng, *p_searchMng;

    // init
    p_sessionMng = NULL;

    // search session
    p_sessionMng = LoRaFrgmntSearchSessionMng( fragIndex );
    if( p_sessionMng == NULL )
    {
        // search empty session
        for( i = 0; i < FRGMNT_CONFIG_MAX_FRAG_INDEX; i++ )
        {
            p_searchMng = &( FrgmntSessionMng[ i ] );
            if( p_searchMng->fragIndex_session == FRGMNT_SESSIONINDEX_NOUSE )
            {
                p_sessionMng = p_searchMng;

                p_sessionMng->fragIndex_session = fragIndex;
#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)
                p_sessionMng->sessionCntPrev = FrgmntsessionCntPrev[ fragIndex ];
#endif
                break;  // exit from for(i) loop
            }
        }
    }

    return p_sessionMng;
}

static void LoRaFrgmntReleaseSessionMng( Frgmnt_SessionMng_t *p_sessionMng )
{
    uint8_t     *p_dataBlkBuff;

    if( p_sessionMng != NULL )
    {
        p_dataBlkBuff = p_sessionMng->p_dataBlockBuff;
        memset1( p_dataBlkBuff, 0x00, FRGMNT_CONFIG_MAX_DATABLK_SIZE );

        memset1( (uint8_t *)p_sessionMng, 0x00, sizeof(Frgmnt_SessionMng_t) );
        p_sessionMng->fragIndex_session = FRGMNT_SESSIONINDEX_NOUSE;
        p_sessionMng->p_dataBlockBuff   = p_dataBlkBuff;
    }
}
