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

#include "LoRaMultiPackageAccessProcess.h"

#if (FUOTA_VERSION >= FUOTA_VERSION_2_0_0)  // Only V2.0.0 is available

#ifndef FUOTA_UPLINK_BUFFER
#define FUOTA_UPLINK_BUFFER                     128
#endif
#define MLPKG_DOWNLINK_INDBUFFER_SIZE           256
#define MLPKG_UPLINK_ANSBUFFER_SIZE             FUOTA_UPLINK_BUFFER


/*** Remote multicast setup command ***/
/* macros */
#define MLPKG_CID_PACKAGE_VERSION_REQ           0x00
#define MLPKG_CID_PACKAGE_VERSION_ANS           0x00
#define MLPKG_CID_DEV_PACKAGE_REQ               0x01
#define MLPKG_CID_DEV_PACKAGE_ANS               0x01
#define MLPKG_CID_MULTI_PACK_BUFFER_REQ         0x02
#define MLPKG_CID_MULTI_PACK_BUFFER_FRAG        0x02

#define MLPKG_PLEN_PACKAGE_VERSION_REQ          (0)
#define MLPKG_PLEN_PACKAGE_VERSION_ANS          (2)
#define MLPKG_PLEN_DEV_PACKAGE_REQ              (0)
#define MLPKG_PLEN_DEV_PACKAGE_ANS(x)           (1 + 3 * (x))  // 1 + 2*(NbTotalPackages)
#define MLPKG_PLEN_MULTI_PACK_BUFFER_REQ        (2)
#define MLPKG_PLEN_MULTI_PACK_BUFFER_FRAG(x)    (2 + (x))      // 2 + sizeof(ANSbuffer)

#define MLPKG_MASK_PACKAGEID_MSB                (0x80)
#define MLPKG_MASK_PACKAGEID_MASK               (0x7F)

#if defined(APP_COMPLIANCE)
#define MLPKG_RXCMD_QUEUENUM                    (31)
#else
#define MLPKG_RXCMD_QUEUENUM                    (31)//(2)
#endif

/* typedef */
typedef struct {
    uint8_t     startByte;
    uint8_t     stopByte;
} MlPkg_MultiPackBufferReq_t;

typedef struct
{
    uint8_t     cid;
    union {
        MlPkg_MultiPackBufferReq_t  mlPkgBufReq;
    } reqCmdParam;
} MlPkg_RxCmd_t;

typedef struct
{
    MlPkg_RxCmd_t   rxCmdQueue[ MLPKG_RXCMD_QUEUENUM ];
    uint8_t         numCmd;
} MlPkg_RxCmdQueue_t;

/* global variable */
MlPkg_RxCmdQueue_t MlPkgCmdQueue;

/* function prototype */
static MlPkgStatus_t LoRaMlPkgHandlePackageVersionReq( uint8_t *p_buffer, size_t length );
static MlPkgStatus_t LoRaMlPkgHandleDevPackageReq( uint8_t *p_buffer, size_t length );
static MlPkgStatus_t LoRaMlPkgHandleMultpPackBufferReq( uint8_t *p_buffer, size_t length );

static MlPkgStatus_t LoRaMlPkgProcessPackageVersionReq( uint8_t        *p_buffer, 
                                                        uint8_t        *p_payloadLen, 
                                                        uint8_t        bufferMaxSize, 
                                                        uint32_t       *p_txDelayMs,
                                                        MlPkg_RxCmd_t  *p_rcCmd/*nouse*/ );
static MlPkgStatus_t LoRaMlPkgProcessDevPackageReq( uint8_t       *p_buffer, 
                                                    uint8_t       *p_payloadLen, 
                                                    uint8_t       bufferMaxSize,
                                                    uint32_t      *p_txDelayMs,
                                                    MlPkg_RxCmd_t *p_rcCmd );
static MlPkgStatus_t LoRaMlPkgProcessMultpPackBufferReq( uint8_t         *p_buffer, 
                                                         uint8_t         *p_payloadLen, 
                                                         uint8_t         bufferMaxSize,
                                                         uint32_t        *p_txDelayMs,
                                                         MlPkg_RxCmd_t   *p_rcCmd );

/*** Event; notify to upper ***/
LoRaMlPkgEventCb_t  MlPkgEventCbFuncs = {0};

/*** IB ***/
typedef struct
{
    uint32_t    __reserved;
} MlPkg_IB_params_t;

MlPkg_IB_params_t   MlPkgIBParams;

/*** MultiPackage state / management ***/
#define MLPKG_STATE_NONE                    0x00
#define MLPKG_STATE_INITIALIZED             0x01
#define MLPKG_STATE_IDLE                    0x02
#define MLPKG_STATE_MCPSIND_HANDLING        0x03
#define MLPKG_STATE_MCPSIND_HANDLING_END    0x04
#define MLPKG_STATE_UPLINK_PREPARING        0x10
#define MLPKG_STATE_UPLINK_START            0x20
#define MLPKG_STATE_UPLINK_REMAINED         0x40
uint8_t MlPkgState = MLPKG_STATE_NONE;

/*** Manage ***/
typedef struct
{
    uint8_t                     state;
    //-----
    McpsIndication_t            mcpsIndStored;
    uint8_t                     mcpsIndBuffer[ MLPKG_DOWNLINK_INDBUFFER_SIZE ];
    uint8_t                     mcpsIndBufOffset;
    uint16_t                    cmdToken;   // CommandToken is 1Byte, and 0xFFFF means no CommandToken 
    bool                        isMultiPackBufferReq;
    //-----
    uint8_t                     ansBuffer[ MLPKG_UPLINK_ANSBUFFER_SIZE ];
    uint8_t                     ansLength;
    uint8_t                     *p_ansSendNext;
    MlPkg_MultiPackBufferReq_t  ansBuffFrag;
} MlpkgMcpsIndHandleMng_t;

MlpkgMcpsIndHandleMng_t MlPkgMcpsIndMng = { .state = MLPKG_STATE_NONE };

static void  LoRaMlPkgResetMcpsIndMng( void );

/*** else ***/
// package list
typedef struct
{
    uint8_t                             numPkg;
    MlPkg_DevPackageElement_t *p_pkgList;
} MlPkgPackageList_t;
MlPkgPackageList_t  MlPkgPackageList;

static MlPkgStatus_t LoRaMlPkgSearchFportFromPackageId( uint8_t packageId,
                                                        uint8_t *p_fport );
static MlPkgStatus_t LoRaMlPkgSearchPackageIdFromFport( uint8_t fport,
                                                        uint8_t *p_packageId );


/*!
 * MultiPackage initialization
 */
MlPkgStatus_t LoRaMultiPackageAccessInit( LoRaMlPkgEventCb_t *p_mlPkgEventCb )
{
    MlPkgStatus_t   res;

    // check
    if( p_mlPkgEventCb == NULL )
    {
        return MLPKG_STATUS_PARAMETER_INVALID;
    }
    if( ( p_mlPkgEventCb->LoRaMlPkgPackageCmdPayloadLenReqCb == NULL ) ||
        ( p_mlPkgEventCb->LoRaMlPkgPackageListReqCb == NULL ) )
    {
        return MLPKG_STATUS_PARAMETER_INVALID;
    }

    /* get information of supported package from upper */
    res = MLPKG_STATUS_OK;
    if( MlPkgMcpsIndMng.state == MLPKG_STATE_NONE )
    {
        (p_mlPkgEventCb->LoRaMlPkgPackageListReqCb)( &(MlPkgPackageList.numPkg),
                                                     &(MlPkgPackageList.p_pkgList) );
        res = LoRaMlPkgSearchFportFromPackageId( MLPKG_PACKAGE_IDENTIFIER, NULL );  // NULL = no need fport
    }

    if( res == MLPKG_STATUS_OK )
    {
        /* IB */
        memset1( (uint8_t *)&MlPkgIBParams, 0x00, sizeof(MlPkg_IB_params_t) );
        
        /* for command */
        memset1( (uint8_t *)&MlPkgCmdQueue, 0x00, sizeof(MlPkg_RxCmdQueue_t) );
        
        /* event */
        if( p_mlPkgEventCb != &MlPkgEventCbFuncs )  // means upper-layer requests initialization
        {
            memcpy1( (uint8_t *)&MlPkgEventCbFuncs, (uint8_t *)p_mlPkgEventCb, sizeof(LoRaMlPkgEventCb_t) );
        }

        /* init MlPkgMcpsIndMng */
        LoRaMlPkgResetMcpsIndMng();
        
        MlPkgMcpsIndMng.state = MLPKG_STATE_INITIALIZED;
    }
    else
    {
        // MultiPackageAccess is not used
        // (nothing to do here)
    }

    return res;
}

/*!
 * MultiPackage start
 */
MlPkgStatus_t LoRaMultiPackageAccessStart( void )
{
    MlPkgStatus_t  res;

    // init
    res = MLPKG_STATUS_ERROR;

    if( MlPkgMcpsIndMng.state == MLPKG_STATE_INITIALIZED )
    {
        MlPkgMcpsIndMng.state = MLPKG_STATE_IDLE;
        res                   = MLPKG_STATUS_OK;
    }

    return res;
}

/*!
 * MultiPackage stop
 */
void LoRaMultiPackageAccessStop( void )
{
    MlPkg_IB_params_t  ibParams;

    if( MlPkgMcpsIndMng.state != MLPKG_STATE_NONE )
    {
        memcpy1( (uint8_t *)&ibParams, (uint8_t *)&MlPkgIBParams, sizeof(MlPkg_IB_params_t) );
        LoRaMultiPackageAccessInit( &MlPkgEventCbFuncs );
        memcpy1( (uint8_t *)&MlPkgIBParams, (uint8_t *)&ibParams, sizeof(MlPkg_IB_params_t) );
    }
}

/*!
 * MCPS-Indication event function for MultiPackage
 */
MlPkgStatus_t LoRaMultiPackageAccessMcpsIndication( McpsIndication_t *p_mcpsIndication )
{
    MlPkgStatus_t   status, funcRet;
    uint8_t         *p_buffer;
    uint8_t         bufferSize;

    // MultiPackage is not running
    if( ( MlPkgMcpsIndMng.state != MLPKG_STATE_IDLE ) && 
        ( MlPkgMcpsIndMng.state != MLPKG_STATE_MCPSIND_HANDLING ) && 
        ( MlPkgMcpsIndMng.state != MLPKG_STATE_MCPSIND_HANDLING_END ) )
    {
        return MLPKG_STATUS_ERROR;
    }

    // Reject error indication
    if ( p_mcpsIndication->Status != LORAMAC_EVENT_INFO_STATUS_OK )
    {
        return MLPKG_STATUS_ERROR;
    }

    // MultiPackage messages MUST NOT be sent using multicast.
    if( (p_mcpsIndication->McpsIndication != MCPS_UNCONFIRMED) &&
        (p_mcpsIndication->McpsIndication != MCPS_CONFIRMED) )
    {
        LoRaMultiPackageAccesResetState();
        return MLPKG_STATUS_COMMAND_ERROR;
    }

    // init
    status     = MLPKG_STATUS_OK;
    p_buffer   = p_mcpsIndication->Buffer;
    bufferSize = p_mcpsIndication->BufferSize;

    memset1( (uint8_t *)&MlPkgCmdQueue, 0x00, sizeof(MlPkg_RxCmdQueue_t) );

    // Get MultiPackage commands
    if( *p_buffer == MLPKG_CID_MULTI_PACK_BUFFER_REQ )
    {
        if( bufferSize == (MLPKG_PLEN_MULTI_PACK_BUFFER_REQ + 1) )  // +1 = CID length
        {
            LoRaMlPkgHandleMultpPackBufferReq( &( p_buffer[1] ), (bufferSize - 1) );
        }
    }
    else
    {
        while( bufferSize > 0 )
        {
            switch( *p_buffer )  //= CID
            {
                case MLPKG_CID_PACKAGE_VERSION_REQ:
                    funcRet = LoRaMlPkgHandlePackageVersionReq( &( p_buffer[1] ), (bufferSize - 1) );
                    if( funcRet == MLPKG_STATUS_OK )
                    {
                        bufferSize -= (MLPKG_PLEN_PACKAGE_VERSION_REQ + 1);  // +1 = CID length
                        p_buffer   += (MLPKG_PLEN_PACKAGE_VERSION_REQ + 1);
                    }
                    else
                    {
                        bufferSize = 0;  // exit from while() loop
                    }
                    break;

                case MLPKG_CID_DEV_PACKAGE_REQ:
                    funcRet = LoRaMlPkgHandleDevPackageReq( &( p_buffer[1] ), (bufferSize - 1) );
                    if( funcRet == MLPKG_STATUS_OK )
                    {
                        bufferSize -= (MLPKG_PLEN_DEV_PACKAGE_REQ + 1);  // +1 = CID length
                        p_buffer   += (MLPKG_PLEN_DEV_PACKAGE_REQ + 1);
                    }
                    else
                    {
                        bufferSize = 0;  // exit from while() loop
                    }
                    break;

                default:
                    bufferSize = 0;  // exit from while() loop
                    break;
            }
        }
    }

    if( MlPkgMcpsIndMng.state == MLPKG_STATE_MCPSIND_HANDLING_END )
    {
        // (special case) all packages have been handled
        MlPkgMcpsIndMng.state = MLPKG_STATE_UPLINK_PREPARING;
    }
    else
    {
        if( MlPkgCmdQueue.numCmd == 0 )
        {
            status = MLPKG_STATUS_COMMAND_ERROR;
        }
    }

    return status;
}

/*!
 * Process event of FUOTA
 */
void LoRaMultiPackageAccessProcessCommand( uint8_t  *p_buffer, 
                                           uint8_t  *p_payloadLen, 
                                           uint8_t  bufferMaxSize, 
                                           uint32_t *p_txDelayMs,
                                           bool     *p_isRetransEn )
{
    MlPkgStatus_t   funcRet;
    MlPkg_RxCmd_t   *p_rxCmd;
    uint8_t         payloadLen, payloadLenTotal;
    uint32_t        txDelayMs, tmpTxDelayMs;
    uint8_t         i;

    // MultiPackage is not running
    if( MlPkgMcpsIndMng.state < MLPKG_STATE_IDLE )
    {
        return;
    }

    // init
    payloadLenTotal = 0;
    txDelayMs       = 0;

    for( i = 0; i < MlPkgCmdQueue.numCmd; i++ )
    {
        // init (loop)
        p_rxCmd     = &( MlPkgCmdQueue.rxCmdQueue[ i ] );
        tmpTxDelayMs = 0;

        switch( p_rxCmd->cid )
        {
            case MLPKG_CID_PACKAGE_VERSION_REQ:
                funcRet = LoRaMlPkgProcessPackageVersionReq( p_buffer, 
                                                             &payloadLen, 
                                                             bufferMaxSize, 
                                                             &tmpTxDelayMs,
                                                             p_rxCmd );
                break;

            case MLPKG_CID_DEV_PACKAGE_REQ:
                funcRet = LoRaMlPkgProcessDevPackageReq( p_buffer, 
                                                         &payloadLen, 
                                                         bufferMaxSize,
                                                         &tmpTxDelayMs,
                                                         p_rxCmd );
                break;

            case MLPKG_CID_MULTI_PACK_BUFFER_REQ:
                funcRet = LoRaMlPkgProcessMultpPackBufferReq( p_buffer, 
                                                              &payloadLen, 
                                                              bufferMaxSize, 
                                                              &tmpTxDelayMs,
                                                              p_rxCmd );
                break;

            default:
                funcRet = MLPKG_STATUS_COMMAND_ERROR;
                break;
        }

        if( funcRet == MLPKG_STATUS_OK )
        {
            p_buffer        += payloadLen;
            payloadLenTotal += payloadLen;
            bufferMaxSize   -= payloadLen;

            if( txDelayMs < tmpTxDelayMs )
            {
                txDelayMs = tmpTxDelayMs;
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
}

/*!
 * Process timer interrupt of FUOTA
 */
void LoRaMultiPackageAccessProcessEvent( void )
{
    // nothing to do
}

/*!
 * Returns whether MultiPackageAccess is idle
 * If MultiPackageAccess is not idle (return false), LoRaMultiPackageAccessProcessEvent() call is required
 * ... always idle (return true)
 */
bool LoRaMultiPackageAccessIsIdle( void )
{
    bool    bRet;

    // init 
    bRet = true;  // (init) idle - function call is not required

#if 0
    if( MlPkgMcpsIndMng.state >= MLPKG_STATE_IDLE )
    {
        // Currently nothing to do.
        // Add your code and return false if LoRaMultiPackageAccessProcessEvent() call is required.
        //bRet = false;
    }
#endif

    return bRet;
}

/*!
 * Uplink result notification from upper layer
 */
void LoRaMultiPackageAccessSendCompCommand( bool isSuccess )
{
    // nothing to do
}

/*!
 * IB Get Request
 */
MlPkgStatus_t LoRaMultiPackageAccessIbGetRequest( uint8_t ib, void *vpVal )
{
#if 0  // Currently nothing to do. If you want to add operations, please check the 'FwMngState'.
    if( MlPkgMcpsIndMng.state == FWMNG_STATE_NONE )
    {
        return MLPKG_STATUS_ERROR;
    }
    
    if( vpVal == NULL )
    {
        return MLPKG_STATUS_PARAMETER_INVALID;
    }
#endif

    // no IB
    return MLPKG_STATUS_SERVICE_UNKNOWN;
}

/*!
 * IB Set Request
 */
MlPkgStatus_t LoRaMultiPackageAccessIbSetRequest( uint8_t ib, void *vpVal )
{
#if 0  // Currently nothing to do. If you want to add operations, please check the 'FwMngState'.
    if( MlPkgMcpsIndMng.state == FWMNG_STATE_NONE )
    {
        return MLPKG_STATUS_ERROR;
    }
    
    if( vpVal == NULL )
    {
        return MLPKG_STATUS_PARAMETER_INVALID;
    }
#endif

    // no IB
    return MLPKG_STATUS_SERVICE_UNKNOWN;
}

/*!
 * Get MultiPackage command payload size
 */
MlPkgStatus_t LoRaMultiPackageAccesGetRcvdCmdPayloadLen( uint8_t cid, uint8_t *p_cmdPayloadLen )
{
    MlPkgStatus_t   ret;

    // init
    ret = MLPKG_STATUS_OK;

    switch( cid )
    {
        case MLPKG_CID_PACKAGE_VERSION_REQ:
            (*p_cmdPayloadLen) = MLPKG_PLEN_PACKAGE_VERSION_REQ;
            break;

        case MLPKG_CID_DEV_PACKAGE_REQ:
            (*p_cmdPayloadLen) = MLPKG_PLEN_DEV_PACKAGE_REQ;
            break;

        case MLPKG_CID_MULTI_PACK_BUFFER_REQ:
            (*p_cmdPayloadLen) = MLPKG_PLEN_MULTI_PACK_BUFFER_REQ;
            break;

        default:
            ret = MLPKG_STATUS_COMMAND_ERROR;
            break;
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------

/*!
 *
 */
bool LoRaMultiPackageAccessIsMultiPackBufferReqCommand( McpsIndication_t *p_mcpsInd )
{
    bool    bRet;

    // init
    bRet = false;

    if( ( p_mcpsInd->Port == MLPKG_FPORT ) &&
        ( p_mcpsInd->McpsIndication != MCPS_MULTICAST ) &&
        ( p_mcpsInd->Buffer[0] == MLPKG_CID_MULTI_PACK_BUFFER_REQ) )
    {
        bRet = true;
    }

    return bRet;
}

/*!
 * Split McpsIndication per package
 */
MlPkgStatus_t LoRaMultiPackageAccessSplitMcpsIndication( McpsIndication_t *p_srcMcpsInd, 
                                                         McpsIndication_t *p_dstMcpsInd )
{
    MlPkgStatus_t   ret, funcRet;
    uint8_t         *p_buffer;
    uint8_t         bufferSize;
    uint8_t         cmdPayloadLen, cmdTotalLen;
    uint8_t         totalLength, tmpLen;
    uint8_t         packageId, fPorts;
    uint8_t         numCmd;

    //--------------
    // check state
    if( MlPkgMcpsIndMng.state == MLPKG_STATE_MCPSIND_HANDLING_END )
    {
        // no more package in McpsIndication
        // make p_dstMcpsInd;
        //   FPort is MultiPackage and no payload
        memcpy1( (uint8_t *)p_dstMcpsInd, (uint8_t *)p_srcMcpsInd, sizeof(McpsIndication_t) );
        p_dstMcpsInd->McpsIndication = MCPS_UNCONFIRMED;
        p_dstMcpsInd->Port           = MLPKG_FPORT;
        p_dstMcpsInd->BufferSize     = 0;

        return MLPKG_STATUS_OK;
    }
    else if( ( MlPkgMcpsIndMng.state != MLPKG_STATE_IDLE ) && 
             ( MlPkgMcpsIndMng.state != MLPKG_STATE_MCPSIND_HANDLING ) )
    {
        // MultiPackage is not running or cannot (no more) handle
        return MLPKG_STATUS_ERROR;
    }
    else
    {
        // no payload
        if( p_srcMcpsInd->BufferSize == 0 )
        {
            return MLPKG_STATUS_LENGTH_ERROR;
        }
    }

    //---------------------------------------------------------------------------
    // special case: MultpPackBufferReq command
    //   The "Command Token" field MUST be appended to all downlink command sets 
    //   except if the downlink only contains a MultiPackBufferReq command.
    if( ( MlPkgMcpsIndMng.state == MLPKG_STATE_IDLE ) &&
        ( p_srcMcpsInd->Port == MLPKG_FPORT ) &&
        ( p_srcMcpsInd->Buffer[0] == MLPKG_CID_MULTI_PACK_BUFFER_REQ ) )
    {
        // init MlPkgMcpsIndMng (NOT use  LoRaMlPkgResetMcpsIndMng() )
        memset1( (uint8_t *)&(MlPkgMcpsIndMng.mcpsIndStored ), 0x00, sizeof(McpsIndication_t) );
        MlPkgMcpsIndMng.mcpsIndBufOffset     = 0;
        MlPkgMcpsIndMng.isMultiPackBufferReq = true;
        
        // copy mcpsIndication
        memcpy1( (uint8_t *)p_dstMcpsInd, (uint8_t *)p_srcMcpsInd, sizeof(McpsIndication_t) );

        return MLPKG_STATUS_OK;
    }

    // init
    ret         = MLPKG_STATUS_OK;
    p_buffer    = p_srcMcpsInd->Buffer;
    totalLength = 0;
    numCmd      = 0;
    cmdTotalLen = 0;
    fPorts      = 0;
    packageId   = 0;

    if( p_srcMcpsInd->Port == MLPKG_FPORT )
    {
        //---------------------------------
        // MultiPackageAccess frame

        if( MlPkgMcpsIndMng.state == MLPKG_STATE_IDLE )
        {
            //--------------------------------------------
            // Start the McpsIndication splitting process

            // init MlPkgMcpsIndMng
            memset1( (uint8_t *)&(MlPkgMcpsIndMng.mcpsIndStored ), 0x00, sizeof(McpsIndication_t) );
            MlPkgMcpsIndMng.mcpsIndBufOffset     = 0;
            MlPkgMcpsIndMng.cmdToken             = 0xFFFF;
            MlPkgMcpsIndMng.isMultiPackBufferReq = false;
            MlPkgMcpsIndMng.ansLength            = 0;

            // get CommandToken
            MlPkgMcpsIndMng.cmdToken = (uint16_t)p_buffer[ p_srcMcpsInd->BufferSize - 1 ];
            bufferSize               = p_srcMcpsInd->BufferSize - 1;

            // copy payload (except CommandToken)
            memcpy1( MlPkgMcpsIndMng.mcpsIndBuffer, p_srcMcpsInd->Buffer, bufferSize );
            MlPkgMcpsIndMng.mcpsIndBufOffset = 0;
        }
        else
        {
            bufferSize = p_srcMcpsInd->BufferSize;
        }

        //------------------------------
        // get packageID and FPorts
        if( ( MlPkgMcpsIndMng.state == MLPKG_STATE_IDLE ) && 
            ( (p_buffer[ 0 ] & MLPKG_MASK_PACKAGEID_MSB) == 0x00 ) )
        {
            // MultiPackageAccess (1st package)
            packageId = MLPKG_PACKAGE_IDENTIFIER;
            fPorts    = MLPKG_FPORT;
        }
        else
        {
            // Another package or MultiPackageAccess (not 1st)
            if( ( p_buffer[ 0 ] & MLPKG_MASK_PACKAGEID_MSB ) == MLPKG_MASK_PACKAGEID_MSB )
            {
                packageId = p_buffer[ 0 ] & MLPKG_MASK_PACKAGEID_MASK;  // [0] = 0x80 | packageID
                funcRet   = LoRaMlPkgSearchFportFromPackageId( packageId, &fPorts );
                if( funcRet == MLPKG_STATUS_OK )
                {
                    p_buffer++;
                    totalLength++;
                }
                else
                {
                    // non-supported pakage: cannot process MultiPackage frame
                    if( MlPkgMcpsIndMng.state == MLPKG_STATE_IDLE )
                    {
                        // error if the package is 1st
                        ret = MLPKG_STATUS_SERVICE_UNKNOWN;
                    }
                    else
                    {
                        // no more process MultiPackage frame. go to next state.
                        MlPkgMcpsIndMng.state = MLPKG_STATE_MCPSIND_HANDLING_END;
                    }
                }
            }
            else
            {
                // no more process MultiPackage frame (not 1st package). go to next state.
                MlPkgMcpsIndMng.state = MLPKG_STATE_MCPSIND_HANDLING_END;
            }
        }

        //--------------------------------------------------------
        // get package command set and split mcps-indication
        if( ret == MLPKG_STATUS_OK )
        {
            if( MlPkgMcpsIndMng.state != MLPKG_STATE_MCPSIND_HANDLING_END )
            {
                // get length of package command set
                funcRet = MLPKG_STATUS_OK;
                while( (funcRet == MLPKG_STATUS_OK) && (totalLength < bufferSize) )
                {
                    if( packageId == MLPKG_PACKAGE_IDENTIFIER )
                    {
                        // get MultiPackageAccess command length
                        funcRet = LoRaMultiPackageAccesGetRcvdCmdPayloadLen( p_buffer[ cmdTotalLen ],  //= CID
                                                                             &cmdPayloadLen );
                    }
                    else
                    {
                        // ask command length
                        funcRet = (*MlPkgEventCbFuncs.LoRaMlPkgPackageCmdPayloadLenReqCb)( packageId,
                                                                                           p_buffer[ cmdTotalLen ],  //= CID
                                                                                           &cmdPayloadLen );
                    }
                
                    if( funcRet == MLPKG_STATUS_OK )
                    {
                        tmpLen = totalLength + cmdPayloadLen + 1;  //+1 = CID
                        if( tmpLen <= bufferSize )
                        {
                            totalLength  = tmpLen;
                            cmdTotalLen += cmdPayloadLen + 1;  //+1 = CID
                            numCmd++;
                        }
                        else
                        {
                            break;  // exit from while() loop
                        }
                    }
                }

                if( numCmd > 0 )
                {
                    if( totalLength < bufferSize )
                    {
                        // update state
                        MlPkgMcpsIndMng.state = MLPKG_STATE_MCPSIND_HANDLING;
                    }
                    else
                    {
                        // all packages have been handeled. go to next state
                        MlPkgMcpsIndMng.state = MLPKG_STATE_MCPSIND_HANDLING_END;
                    }
                }
                else  // if( numCmd == 0 )
                {
                    if( MlPkgMcpsIndMng.state == MLPKG_STATE_MCPSIND_HANDLING )
                    {
                        // all packages have been handeled. go to next state
                        MlPkgMcpsIndMng.state = MLPKG_STATE_MCPSIND_HANDLING_END;
                    }
                    else
                    {
                        ret = MLPKG_STATUS_COMMAND_ERROR;
                    }
                }
            }
        }

        //----------------------------------------------------
        // split and make new McpsIndication for one package
        if( ret == MLPKG_STATUS_OK )
        {
            if( ( numCmd > 0 ) || ( MlPkgMcpsIndMng.state == MLPKG_STATE_MCPSIND_HANDLING )  )
            {
                memcpy1( (uint8_t *)p_dstMcpsInd, (uint8_t *)p_srcMcpsInd, sizeof(McpsIndication_t) );
                p_dstMcpsInd->Port       = fPorts;
                p_dstMcpsInd->Buffer     = p_buffer;
                p_dstMcpsInd->BufferSize = cmdTotalLen;

                // keep remained data
                if( MlPkgMcpsIndMng.state == MLPKG_STATE_MCPSIND_HANDLING )
                {
                    memcpy1( (uint8_t *)&(MlPkgMcpsIndMng.mcpsIndStored), (uint8_t *)p_srcMcpsInd, sizeof(McpsIndication_t) );
                    MlPkgMcpsIndMng.mcpsIndBufOffset         += totalLength;
                    MlPkgMcpsIndMng.mcpsIndStored.Buffer      = &( MlPkgMcpsIndMng.mcpsIndBuffer[ MlPkgMcpsIndMng.mcpsIndBufOffset ] );
                    MlPkgMcpsIndMng.mcpsIndStored.BufferSize  = bufferSize - totalLength;
                }
            }
            else
            {
                // make p_dstMcpsInd;
                //   FPort is MultiPackage and no payload
                memcpy1( (uint8_t *)p_dstMcpsInd, (uint8_t *)p_srcMcpsInd, sizeof(McpsIndication_t) );
                p_dstMcpsInd->McpsIndication = MCPS_UNCONFIRMED;
                p_dstMcpsInd->Port           = MLPKG_FPORT;
                p_dstMcpsInd->BufferSize     = 0;
            }
        }
    }
    else
    {
        //---------------------------------
        // Not MultiPackageAccess frame
        if( MlPkgMcpsIndMng.state == MLPKG_STATE_IDLE )
        {
            memcpy1( (uint8_t *)p_dstMcpsInd, (uint8_t *)p_srcMcpsInd, sizeof(McpsIndication_t) );
        }
        else
        {
            // MultiPackage process is not finished yet.
            ret = MLPKG_STATUS_BUSY;
        }
    }

    return ret;
}

/*!
 * Create an answer in MultiPackage frame format
 */
MlPkgStatus_t LoRaMultiPackageAccessCreateAnsUplink( uint8_t srcFport,    uint8_t *p_srcPayload, uint8_t srcLength, 
                                                     uint8_t *p_dstFport, uint8_t *p_dstPayload, uint8_t *p_dstLength,
                                                     uint8_t dstLenMax )
{
    MlPkgStatus_t   ret, funcRet;
    uint8_t         packageId;
    uint8_t         *p_ansBuf, ansLen;

    // init
    ret    = MLPKG_STATUS_OK;
    ansLen = 0;

    //----------------------------------------------------------------------
    // handle MultiPackBufferFreq command or not handle MultiPackageAccess 
    if( ( MlPkgMcpsIndMng.state == MLPKG_STATE_IDLE ) ||
        ( MlPkgMcpsIndMng.isMultiPackBufferReq == true ) )
    {
        (*p_dstFport)  = srcFport;
        (*p_dstLength) = srcLength;
        memcpy1( (uint8_t *)p_dstPayload, (uint8_t *)p_srcPayload, srcLength );

        MlPkgMcpsIndMng.isMultiPackBufferReq = false;  // clear

        return MLPKG_STATUS_OK;
    }

    //--------------------------------------------------------
    // handle MultiPackageAccess
    //   create MultiPackage frame and store to ANS buffer
    if( ( MlPkgMcpsIndMng.state == MLPKG_STATE_MCPSIND_HANDLING ) ||
        ( MlPkgMcpsIndMng.state == MLPKG_STATE_MCPSIND_HANDLING_END ) ||
        ( MlPkgMcpsIndMng.state == MLPKG_STATE_UPLINK_PREPARING ) )
    {
        //------------
        // get FPort
        funcRet = LoRaMlPkgSearchPackageIdFromFport( srcFport, &packageId );
        if( funcRet == MLPKG_STATUS_OK )
        {
            //----------------------
            // store to ANS buffer
            if( srcLength > 0 )
            {
                p_ansBuf = &( MlPkgMcpsIndMng.ansBuffer[ MlPkgMcpsIndMng.ansLength ] );
                
                if( ( MlPkgMcpsIndMng.ansLength != 0 ) || ( srcFport != MLPKG_FPORT ) )
                {
                    p_ansBuf[0] = ( MLPKG_MASK_PACKAGEID_MSB | packageId );
                
                    p_ansBuf++;
                    MlPkgMcpsIndMng.ansLength++;
                }
                
                ansLen = MlPkgMcpsIndMng.ansLength + srcLength;
                if( ansLen <= MLPKG_UPLINK_ANSBUFFER_SIZE )
                {
                    memcpy1( (uint8_t *)p_ansBuf, (uint8_t *)p_srcPayload, srcLength );
                    MlPkgMcpsIndMng.ansLength = ansLen;
                }
            }
        }

        if( MlPkgMcpsIndMng.state == MLPKG_STATE_UPLINK_PREPARING )
        {
            if( MlPkgMcpsIndMng.ansLength > 0 )
            {
                MlPkgMcpsIndMng.state = MLPKG_STATE_UPLINK_START;
            }
            else
            {
                // error; no more process.
                ret                   = MLPKG_STATUS_ERROR;
                MlPkgMcpsIndMng.state = MLPKG_STATE_IDLE;
            }
        }
    }

    //-------------------------------------------------------------
    // Creating MultiPackage frame is complete; prepare to uplink
    if( ret == MLPKG_STATUS_OK )
    {
        if( ( MlPkgMcpsIndMng.state == MLPKG_STATE_UPLINK_START ) ||
            ( MlPkgMcpsIndMng.state == MLPKG_STATE_UPLINK_REMAINED ) )
        {
            if( ( MlPkgMcpsIndMng.state == MLPKG_STATE_UPLINK_START ) &&
                ( ( MlPkgMcpsIndMng.ansLength + 1 ) <= dstLenMax ) )   // +1 = CommandToken 
            {
                //---------------------------------------------
                // Answer frame can be set in a uplink payload
                memcpy1( p_dstPayload, MlPkgMcpsIndMng.ansBuffer, MlPkgMcpsIndMng.ansLength );
                p_dstPayload[ MlPkgMcpsIndMng.ansLength ] = (uint8_t)( MlPkgMcpsIndMng.cmdToken );
                (*p_dstLength) = MlPkgMcpsIndMng.ansLength + 1;  // +1 = CommandToken
                (*p_dstFport)  = MLPKG_FPORT;

                MlPkgMcpsIndMng.state = MLPKG_STATE_IDLE;
            }
            else
            {
                //------------------------------------------------
                // Answer frame cannot be set in a uplink payload
                //   divide answer, create MultiPackBufferFrag, and set it to destination
                if( MlPkgMcpsIndMng.state == MLPKG_STATE_UPLINK_START )
                {
                    MlPkgMcpsIndMng.ansBuffFrag.startByte = 0;
                    MlPkgMcpsIndMng.ansBuffFrag.stopByte  = MlPkgMcpsIndMng.ansLength - 1;
                    MlPkgMcpsIndMng.p_ansSendNext         = &( MlPkgMcpsIndMng.ansBuffer[ 0 ] );
                }
                
                ret = LoRaMlPkgProcessMultpPackBufferReq( p_dstPayload, p_dstLength, dstLenMax,
                                                          NULL, NULL );
                if( ret == MLPKG_STATUS_OK )
                {
                    (*p_dstFport) = MLPKG_FPORT;
                }
                else
                {
                    // error; no more process.
                    MlPkgMcpsIndMng.state = MLPKG_STATE_IDLE;
                }
            }
        }
        else
        {
            //----------------------------------------------
            // Unprocessed McpsIndication (package) is left.
            ret = MLPKG_STATUS_PENDING;
        }
    }

    return ret;
}

/*!
 * Pass the unprocessed McpsIndication
 */
MlPkgStatus_t LoRaMultiPackageAccessGetNextMcpsInd( McpsIndication_t **pp_nextMcpsInd )
{
    MlPkgStatus_t   ret;

    // init 
    ret = MLPKG_STATUS_ERROR;

    if( ( MlPkgMcpsIndMng.state == MLPKG_STATE_MCPSIND_HANDLING ) ||
        ( MlPkgMcpsIndMng.state == MLPKG_STATE_MCPSIND_HANDLING_END ) )
    {
        (*pp_nextMcpsInd) = &( MlPkgMcpsIndMng.mcpsIndStored );
        ret = MLPKG_STATUS_OK;
    }

    return ret;
}

/*!
 * Pass the remained ANS buffer size
 */
MlPkgStatus_t LoRaMultiPackageAccessGetRemainedAnsBufferSize( uint8_t *p_remainedSize )
{
    MlPkgStatus_t   ret;

    // init 
    ret = MLPKG_STATUS_OK;

    if( MlPkgMcpsIndMng.state > MLPKG_STATE_IDLE )
    {
        if( p_remainedSize != NULL )
        {
            (*p_remainedSize) = MLPKG_UPLINK_ANSBUFFER_SIZE - MlPkgMcpsIndMng.ansLength;
        }
    }
    else
    {
        // error; not active
        ret = MLPKG_STATUS_ERROR;

        if( p_remainedSize != NULL )
        {
            (*p_remainedSize) = MLPKG_UPLINK_ANSBUFFER_SIZE;
        }
    }

    return ret;
}

/*!
 * Check if there is any unsent ANS buffer data
 */
bool LoRaMultiPackageAccessIsRemainedBufferFrag( void )
{
    bool    bRet;

    // init
    bRet = false;
    if( MlPkgMcpsIndMng.state == MLPKG_STATE_UPLINK_REMAINED )
    {
        bRet = true;
    }

    return bRet;
}

/*!
 * Reset MultiPackage state (NOT flush ANS buffer)
 */
void LoRaMultiPackageAccesResetState( void )
{
    MlPkgMcpsIndMng.state                = MLPKG_STATE_IDLE;
    MlPkgMcpsIndMng.isMultiPackBufferReq = false;
}

//--------------------------------------------------------------------------------------------------

/*!
 * check received command; PackageVersionReq
 */
static MlPkgStatus_t LoRaMlPkgHandlePackageVersionReq( uint8_t *p_buffer, size_t length )
{
    MlPkg_RxCmd_t    *p_rxCmd;

    // payload length check
    //  ... it has no payload

    // command queue check
    if( MlPkgCmdQueue.numCmd >= MLPKG_RXCMD_QUEUENUM )
    {
        return MLPKG_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( MlPkgCmdQueue.rxCmdQueue[ MlPkgCmdQueue.numCmd ] );

    // get parameter
    p_rxCmd->cid = MLPKG_CID_PACKAGE_VERSION_REQ;

    MlPkgCmdQueue.numCmd++;

#ifdef DEBUG_MLPKG
    print( "*MLPKG:PackageVersionReq" );
    print_newline();
#endif

    return MLPKG_STATUS_OK;
}

/*!
 * check received command; DevPackageReq
 */
static MlPkgStatus_t LoRaMlPkgHandleDevPackageReq( uint8_t *p_buffer, size_t length )
{
    MlPkg_RxCmd_t    *p_rxCmd;

    // payload length check
    //  ... it has no payload

    // command queue check
    if( MlPkgCmdQueue.numCmd >= MLPKG_RXCMD_QUEUENUM )
    {
        return MLPKG_STATUS_ERROR;
    }

    // init
    p_rxCmd = &( MlPkgCmdQueue.rxCmdQueue[ MlPkgCmdQueue.numCmd ] );

    // get parameter
    p_rxCmd->cid = MLPKG_CID_DEV_PACKAGE_REQ;

    MlPkgCmdQueue.numCmd++;

#ifdef DEBUG_MLPKG
    print( "*MLPKG:DevPackageReq" );
    print_newline();
#endif

    return MLPKG_STATUS_OK;
}


/*!
 * check received command; MultpPackBufferReq
 */
static MlPkgStatus_t LoRaMlPkgHandleMultpPackBufferReq( uint8_t *p_buffer, size_t length )
{
    MlPkg_RxCmd_t               *p_rxCmd;
    MlPkg_MultiPackBufferReq_t  *p_cmdMultiPackBufReq;

    // payload length check
    if( length < MLPKG_PLEN_MULTI_PACK_BUFFER_REQ )
    {
        return MLPKG_STATUS_LENGTH_ERROR;
    }
    // command queue check
    if( MlPkgCmdQueue.numCmd >= MLPKG_RXCMD_QUEUENUM )
    {
        return MLPKG_STATUS_ERROR;
    }
    // MultpPackBufferReq must be the single command
    if( MlPkgMcpsIndMng.isMultiPackBufferReq == false )
    {
        return MLPKG_STATUS_ERROR;
    }

    // init
    p_rxCmd              = &( MlPkgCmdQueue.rxCmdQueue[ MlPkgCmdQueue.numCmd ] );
    p_cmdMultiPackBufReq = (MlPkg_MultiPackBufferReq_t *)&( p_rxCmd->reqCmdParam );

    // get parameter
    p_rxCmd->cid = MLPKG_CID_MULTI_PACK_BUFFER_REQ;

    p_cmdMultiPackBufReq->startByte = (*p_buffer);
    p_buffer++;
    p_cmdMultiPackBufReq->stopByte = (*p_buffer);
    //p_buffer++;

    MlPkgCmdQueue.numCmd++;
    
#ifdef DEBUG_MLPKG
    print( "*MLPKG:MultiPackBufferReq" );
    print_newline();
    print( "*MLPKG:  StartByte=" );
    print_dec( p_cmdMultiPackBufReq->startByte, 3, '\0' );
    print( ", StopByte=" );
    print_dec( p_cmdMultiPackBufReq->stopByte, 3, '\0' );
    print_newline();
#endif

    return MLPKG_STATUS_OK;
}


//--------------------------------------------------------------------------------------------------

/*!
 * Process received command; PackageVersionReq
 */
static MlPkgStatus_t LoRaMlPkgProcessPackageVersionReq( uint8_t        *p_buffer, 
                                                        uint8_t        *p_payloadLen, 
                                                        uint8_t        bufferMaxSize, 
                                                        uint32_t       *p_txDelayMs,
                                                        MlPkg_RxCmd_t  *p_rcCmd/*nouse*/ )
{
    // length check
    if( bufferMaxSize < (MLPKG_PLEN_PACKAGE_VERSION_ANS + 1) )  // +1 = CID length
    {
        return MLPKG_STATUS_LENGTH_ERROR;
    }

    // make PackageVersionAns
    p_buffer[0] = MLPKG_CID_PACKAGE_VERSION_ANS;
    p_buffer[1] = MLPKG_PACKAGE_IDENTIFIER;
    p_buffer[2] = MLPKG_PACKAGE_VERSION;

    (*p_payloadLen) = MLPKG_PLEN_PACKAGE_VERSION_ANS + 1;  // +1 = CID length

#ifdef DEBUG_MLPKG
    print( "*MLPKG:PackageVersionAns" );
    print_newline();
    print( "*MLPKG:  PackageIdentifier=" );
    print_dec( p_buffer[1], 3, '\0' );
    print( ", PackageVersion=" );
    print_dec( p_buffer[2], 3, '\0' );
    print_newline();
#endif

    return MLPKG_STATUS_OK;
}

/*!
 * Process received command; DevPackageReq
 */
static MlPkgStatus_t LoRaMlPkgProcessDevPackageReq( uint8_t       *p_buffer, 
                                                    uint8_t       *p_payloadLen, 
                                                    uint8_t       bufferMaxSize,
                                                    uint32_t      *p_txDelayMs,
                                                    MlPkg_RxCmd_t *p_rcCmd )
{
    uint8_t                         i, nbPackages;
    MlPkg_DevPackageElement_t *p_pkgList;

    // get package list
    nbPackages = MlPkgPackageList.numPkg;
    p_pkgList  = MlPkgPackageList.p_pkgList;

    // length check
    if( bufferMaxSize < (MLPKG_PLEN_DEV_PACKAGE_ANS(nbPackages) + 1) )  // +1 = CID length
    {
        return MLPKG_STATUS_LENGTH_ERROR;
    }

    // make DevPackageAns
    p_buffer[0] = MLPKG_CID_DEV_PACKAGE_ANS;
    p_buffer[1] = nbPackages;
    for( i = 0; i < nbPackages; i++ )
    {
        p_buffer[ (i * 3) + 2 ] = p_pkgList->packageId;
        p_buffer[ (i * 3) + 3 ] = p_pkgList->packageVersion;
        p_buffer[ (i * 3) + 4 ] = p_pkgList->fport;

        p_pkgList++;
    }

    (*p_payloadLen) = MLPKG_PLEN_DEV_PACKAGE_ANS(nbPackages) + 1;  // +1 = CID length

#ifdef DEBUG_MLPKG
    print( "*MLPKG:DevPackageAns" );
    print_newline();
    print( "*MLPKG:  NbPackages=" );
    print_dec( p_buffer[1], 3, '\0' );
    print_newline();
    for( i = 0; i < nbPackages; i++ )
    {
        print( "*MLPKG:  [" );
        print_dec( i, 2, '\0' );
        print( "]PackageId=" );
        print_dec( p_buffer[(i*3)+2], 3, '\0' );
        print( ", PackageVersion=" );
        print_dec( p_buffer[(i*3)+3], 3, '\0' );
        print( ", FPort=" );
        print_dec( p_buffer[(i*3)+4], 3, '\0' );
        print_newline(); 
    }
#endif

    return MLPKG_STATUS_OK;
}

/*!
 * Process received command; MultpPackBufferReq
 */
static MlPkgStatus_t LoRaMlPkgProcessMultpPackBufferReq( uint8_t         *p_buffer, 
                                                         uint8_t         *p_payloadLen, 
                                                         uint8_t         bufferMaxSize,
                                                         uint32_t        *p_txDelayMs,
                                                         MlPkg_RxCmd_t   *p_rcCmd )
{
    MlPkgStatus_t               ret;
    MlPkg_MultiPackBufferReq_t  *p_cmdMultiPackBufReq;
    uint8_t                     writeSize;

    // init
    ret = MLPKG_STATUS_OK;

    //------------------------------------------
    // AS sends (requests) MultiPackBufferReq
    if( MlPkgMcpsIndMng.state == MLPKG_STATE_IDLE )
    {
        // init
        p_cmdMultiPackBufReq = (MlPkg_MultiPackBufferReq_t *)&( p_rcCmd->reqCmdParam );

        if( ( MlPkgMcpsIndMng.ansLength == 0 ) ||
            ( p_cmdMultiPackBufReq->startByte > p_cmdMultiPackBufReq->stopByte ) ||
            ( p_cmdMultiPackBufReq->startByte >= MlPkgMcpsIndMng.ansLength) )
        {
            //--------------------------------------
            // MultiPackBufferReq is invalid
            // send MultiPackBufferFrag with 0xFF
            if( bufferMaxSize >= 2 )
            {
                p_buffer[0]     = MLPKG_CID_MULTI_PACK_BUFFER_FRAG;
                p_buffer[1]     = 0xFF;
                (*p_payloadLen) = 2;

#ifdef DEBUG_MLPKG
                print( "*MLPKG:MultiPackBufferFrag" );
                print_newline();
                print( "*MLPKG:  ErrorStatus(0x" );
                print_hex( p_buffer[1], 2 );
                print( ")" );
                print_newline();
#endif
            }
            else
            {
                return MLPKG_STATUS_LENGTH_ERROR;
            }

            return MLPKG_STATUS_OK;
        }

        //--------------------------------------
        // MultiPackBufferReq is valid
        MlPkgMcpsIndMng.ansBuffFrag.startByte = p_cmdMultiPackBufReq->startByte;
        if( p_cmdMultiPackBufReq->stopByte >= MlPkgMcpsIndMng.ansLength )
        {
            MlPkgMcpsIndMng.ansBuffFrag.stopByte = MlPkgMcpsIndMng.ansLength - 1;
        }
        else
        {
            MlPkgMcpsIndMng.ansBuffFrag.stopByte = p_cmdMultiPackBufReq->stopByte;
        }

        MlPkgMcpsIndMng.p_ansSendNext = &( MlPkgMcpsIndMng.ansBuffer[ MlPkgMcpsIndMng.ansBuffFrag.startByte ] );
    }


    if( ( MlPkgMcpsIndMng.state == MLPKG_STATE_IDLE ) ||
        ( MlPkgMcpsIndMng.state == MLPKG_STATE_UPLINK_START ) ||
        ( MlPkgMcpsIndMng.state == MLPKG_STATE_UPLINK_REMAINED ) )
    {
        if( bufferMaxSize >= 4 )
        {
            p_buffer[0] = MLPKG_CID_MULTI_PACK_BUFFER_FRAG;
            p_buffer[1] = MlPkgMcpsIndMng.ansBuffFrag.startByte;
            writeSize      = 2;
            bufferMaxSize -= 2;

#ifdef DEBUG_MLPKG
            print( "*MLPKG:MultpPackBufferFrag" );
            print_newline();
            print( "*MLPKG:  BaseByte=" );
            print_dec( p_buffer[1], 3, '\0' );
            print_newline();
            print( "*MLPKG:  ANSbuffer=" );
#endif
            while( bufferMaxSize > 1 )
            {
                p_buffer[ writeSize ] = *(MlPkgMcpsIndMng.p_ansSendNext);
#ifdef DEBUG_MLPKG
                print_hex( p_buffer[ writeSize ], 2 );
#endif
                writeSize++;
                bufferMaxSize--;
                MlPkgMcpsIndMng.ansBuffFrag.startByte++;

                if( MlPkgMcpsIndMng.p_ansSendNext < &(MlPkgMcpsIndMng.ansBuffer[ MlPkgMcpsIndMng.ansBuffFrag.stopByte ]) )
                {
                    MlPkgMcpsIndMng.p_ansSendNext++;
                }
                else
                {
                    break;  // exit from while() loop
                }
            }
            p_buffer[ writeSize ] = (uint8_t)( MlPkgMcpsIndMng.cmdToken );
#ifdef DEBUG_MLPKG
            print_newline();
            print( "*MLPKG:  CommandToken=0x" );
            print_hex( p_buffer[ writeSize ], 2 );
            print_newline();
#endif
            (*p_payloadLen) = writeSize + 1;  // +1 = token

            // check if all ANS buffer is sent
            if( MlPkgMcpsIndMng.p_ansSendNext == &(MlPkgMcpsIndMng.ansBuffer[ MlPkgMcpsIndMng.ansBuffFrag.stopByte ]) )
            {
                MlPkgMcpsIndMng.state = MLPKG_STATE_IDLE;
            }
            else
            {
                MlPkgMcpsIndMng.state = MLPKG_STATE_UPLINK_REMAINED;
            }
        }
        else
        {
            ret = MLPKG_STATUS_LENGTH_ERROR;
        }
    }
    else
    {
        ret = MLPKG_STATUS_ERROR;
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------
static void LoRaMlPkgResetMcpsIndMng( void )
{
    memset1( (uint8_t *)&MlPkgMcpsIndMng, 0x00, sizeof(MlpkgMcpsIndHandleMng_t) );
    MlPkgMcpsIndMng.cmdToken = (uint16_t)(-1);
}

static MlPkgStatus_t LoRaMlPkgSearchFportFromPackageId( uint8_t packageId,
                                                        uint8_t *p_fport )
{
    MlPkgStatus_t   res;
    uint8_t         i;

    // init
    res = MLPKG_STATUS_SERVICE_UNKNOWN;

    for( i = 0; i < MlPkgPackageList.numPkg; i++ )
    {
        if( packageId == MlPkgPackageList.p_pkgList[i].packageId )
        {
            // found
            if( p_fport != NULL )
            {
                (*p_fport) = MlPkgPackageList.p_pkgList[i].fport;
            }
            res = MLPKG_STATUS_OK;
            break;  // exit from for() loop
        }
    }

    return res;
}

static MlPkgStatus_t LoRaMlPkgSearchPackageIdFromFport( uint8_t fport,
                                                        uint8_t *p_packageId )
{
    MlPkgStatus_t   res;
    uint8_t         i;

    // init
    res = MLPKG_STATUS_SERVICE_UNKNOWN;

    for( i = 0; i < MlPkgPackageList.numPkg; i++ )
    {
        if( fport == MlPkgPackageList.p_pkgList[i].fport )
        {
            // found
            if( p_packageId != NULL )
            {
                (*p_packageId) = MlPkgPackageList.p_pkgList[i].packageId;
            }
            res = MLPKG_STATUS_OK;
            break;  // exit from for() loop
        }
    }

    return res;
}

//--------------------------------------------------------------------------------------------------

#endif  // FUOTA_VERSION
