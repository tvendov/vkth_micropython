/*!
 * \file      LoRaMac.c
 *
 * \brief     LoRa MAC layer implementation
 *
 * \copyright Revised BSD License, see section \ref LICENSE.
 *
 * \code
 *                ______                              _
 *               / _____)             _              | |
 *              ( (____  _____ ____ _| |_ _____  ____| |__
 *               \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 *               _____) ) ____| | | || |_| ____( (___| | | |
 *              (______/|_____)_|_|_| \__)_____)\____)_| |_|
 *              (C)2013-2017 Semtech
 *
 *               ___ _____ _   ___ _  _____ ___  ___  ___ ___
 *              / __|_   _/_\ / __| |/ / __/ _ \| _ \/ __| __|
 *              \__ \ | |/ _ \ (__| ' <| _| (_) |   / (__| _|
 *              |___/ |_/_/ \_\___|_|\_\_| \___/|_|_\\___|___|
 *              embedded.connectivity.solutions===============
 *
 * \endcode
 *
 * \author    Miguel Luis ( Semtech )
 *
 * \author    Gregory Cristian ( Semtech )
 *
 * \author    Daniel Jaeckle ( STACKFORCE )
 *
 * \author    Johannes Bruder ( STACKFORCE )
 */
/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
#include "stdio.h"

#include "board.h"

#include "region/Region.h"
#include "LoRaMacClassB.h"
#include "LoRaMacCrypto.h"
#include "secure-element.h"
#include "LoRaMacTest.h"
#include "LoRaMacTypes.h"
#include "LoRaMacConfirmQueue.h"
#include "LoRaMacHeaderTypes.h"
#include "LoRaMacMessageTypes.h"
#include "LoRaMacParser.h"
#include "LoRaMacCommands.h"
#include "LoRaMacAdr.h"
#include "utilities.h"

#include "LoRaMac.h"
#include "LoRaMacConfig.h"

/* mac.send() breadcrumb macro SBC() — see mod_lorawan.h. Foreground
 * call-path only (LoRaMacMcpsRequest → Send → ScheduleTx → SendFrameOnChannel). */
#include "mod_lorawan.h"

/* T-V3.1 — Class C RX probe instrumentation. Captures decision points
 * inside OpenContinuousRxCWindow and the three radio-event handlers. */
#include "glue/lorawan_rxc_diag.h"

#if defined( LORACOMBO_ENABLED )
    #include "RadioWrapper.h"
    #define LORAMAC_RADIO_INIT( p_radioEvt )    RadioWrapperInit( RADIOWRAP_LORAMODE_LORAWAN, (p_radioEvt) )
    #define LORAMAC_RADIO_SET_LORAMODE()        RadioWrapperSetLoRaMode( RADIOWRAP_LORAMODE_LORAWAN )
#else
    #define LORAMAC_RADIO_INIT( p_radioEvt )    Radio.Init( (p_radioEvt) )
    #define LORAMAC_RADIO_SET_LORAMODE()        /* nothing to do */
#endif

/*!
 * Maximum PHY layer payload size
 */
#define LORAMAC_PHY_MAXPAYLOAD                      255

/*!
 * Maximum MAC commands buffer size
 */
#define LORA_MAC_COMMAND_MAX_LENGTH                 (NUM_OF_MAC_COMMANDS * LORAMAC_COMMAND_MAXSIZE)

/*!
 * Maximum length of the fOpts field
 */
#define LORA_MAC_COMMAND_MAX_FOPTS_LENGTH           15

/*!
 * LoRaMac duty cycle for the back-off procedure during the first hour.
 */
#define BACKOFF_DC_1_HOUR                           100

/*!
 * LoRaMac duty cycle for the back-off procedure during the next 10 hours.
 */
#define BACKOFF_DC_10_HOURS                         1000

/*!
 * LoRaMac duty cycle for the back-off procedure during the next 24 hours.
 */
#define BACKOFF_DC_24_HOURS                         10000

/*!
 * MAC Command length
 */
#define LORAMAC_COMMAND_LEN_LINK_CHECK_ANS          3
#define LORAMAC_COMMAND_LEN_LINK_ADR_REQ            5
#define LORAMAC_COMMAND_LEN_DUTY_CYCLE_REQ          2
#define LORAMAC_COMMAND_LEN_RX_PARAM_SETUP_REQ      5
#define LORAMAC_COMMAND_LEN_DEV_STATUS_REQ          1
#define LORAMAC_COMMAND_LEN_NEW_CHANNEL_REQ         6
#define LORAMAC_COMMAND_LEN_RX_TIMING_SETUP_REQ     2
#define LORAMAC_COMMAND_LEN_TX_PARAM_SETUP_REQ      2
#define LORAMAC_COMMAND_LEN_DL_CHANNEL_REQ          5
#define LORAMAC_COMMAND_LEN_DEVICE_TIME_ANS         6
#define LORAMAC_COMMAND_LEN_PING_SLOT_INFO_ANS      1
#define LORAMAC_COMMAND_LEN_PING_SLOT_CHANNEL_REQ   5
#define LORAMAC_COMMAND_LEN_BEACON_TIMING_ANS       4
#define LORAMAC_COMMAND_LEN_BEACON_FREQ_REQ         4

/*!
 * LoRaMac internal states
 */
enum eLoRaMacState
{
    LORAMAC_IDLE          = 0x00000000,
    LORAMAC_STOPPED       = 0x00000001,
    LORAMAC_TX_RUNNING    = 0x00000002,
    LORAMAC_RX            = 0x00000004,
    LORAMAC_ACK_RETRY     = 0x00000010,
    LORAMAC_TX_DELAYED    = 0x00000020,
    LORAMAC_TX_CONFIG     = 0x00000040,
    LORAMAC_RX_ABORT      = 0x00000080,
};

/*
 * Request permission state
 */
typedef enum eLoRaMacRequestHandling
{
    LORAMAC_REQUEST_HANDLING_OFF = 0,
    LORAMAC_REQUEST_HANDLING_ON = !LORAMAC_REQUEST_HANDLING_OFF
}LoRaMacRequestHandling_t;

typedef struct sLoRaMacNvmCtx
{
    /*
     * LoRaMac region.
     */
    LoRaMacRegion_t Region;
    /*
     * LoRaMac default parameters
     */
    LoRaMacParams_t MacParamsDefaults;
    /*
     * Network ID ( 3 bytes )
     */
    uint32_t NetID;
    /*
     * Mote Address
     */
    uint32_t DevAddr;
#if (LORAMAC_MAX_MC_CTX > 0)
    /*!
    * Multicast channel list
    */
    MulticastCtx_t MulticastChannelList[LORAMAC_MAX_MC_CTX];
#endif
    /*
     * Actual device class
     */
    DeviceClass_t DeviceClass;
    /*
     * Indicates if the node is connected to
     * a private or public network
     */
    bool PublicNetwork;
    /*
     * LoRaMac ADR control status
     */
    bool AdrCtrlOn;
    /*
     * Counts the number of missed ADR acknowledgements
     */
    uint32_t AdrAckCounter;

    /*
     * LoRaMac parameters
     */
    LoRaMacParams_t MacParams;
    /*
     * Maximum duty cycle
     * \remark Possibility to shutdown the device.
     */
    uint8_t MaxDCycle;
    /*
    * Enables/Disables duty cycle management (Test only)
    */
    bool DutyCycleOn;
    /*
     * Current channel index
     */
    uint8_t LastTxChannel;
    /*
     * Buffer containing the MAC layer commands
     */
    uint8_t MacCommandsBuffer[LORA_MAC_COMMAND_MAX_LENGTH];
    /*
     * If the server has sent a FRAME_TYPE_DATA_CONFIRMED_DOWN this variable indicates
     * if the ACK bit must be set for the next transmission
     */
    bool SrvAckRequested;
    /*
     * Aggregated duty cycle management
     */
    uint16_t AggregatedDCycle;
    /*
    * Aggregated duty cycle management
    */
    TimerTime_t LastTxDoneTime;
    TimerTime_t AggregatedTimeOff;
    /*
    * Stores the time at LoRaMac initialization.
    *
    * \remark Used for the BACKOFF_DC computation.
    */
    SysTime_t InitializationTime;
    /*
     * Current LoRaWAN Version
     */
    Version_t Version;
    /*
     * End-Device network activation
     */
    ActivationType_t NetworkActivation;
    /*!
     * Last received Message integrity Code (MIC)
     */
    uint32_t LastRxMic;
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
    /*
     * Set to true, if the datarate was increased
     * with a link adr request.
     */
    bool ChannelsDatarateChangedLinkAdrReq;
#endif
    /*
     * Enables/disable FPort 224 processing (certification port)
     */
    bool IsCertPortOn;
}LoRaMacNvmCtx_t;

typedef struct sLoRaMacCtx
{
    /*
    * Length of packet in PktBuffer
    */
    uint16_t PktBufferLen;
    /*
    * Buffer containing the data to be sent or received.
    */
    uint8_t PktBuffer[LORAMAC_PHY_MAXPAYLOAD];
    /*!
    * Current processed transmit message
    */
    LoRaMacMessage_t TxMsg;
#if (LORAMAC_VERSION >= 0x01010000)  // no need following in case of LW10x
    /*!
    * Buffer containing the data received by the application.
    */
    uint8_t AppData[LORAMAC_PHY_MAXPAYLOAD];
#endif
    /*
    * Size of buffer containing the application data.
    */
    uint8_t AppDataSize;
#ifdef LORAMAC_RXC_CONTINUOUS_ENABLED
    /*
    * Buffer containing the upper layer data.
    */
    uint8_t RxPayload[LORAMAC_PHY_MAXPAYLOAD];
#endif
    SysTime_t LastTxSysTime;
    /*
    * LoRaMac internal state
    */
    uint32_t MacState;
    /*
    * LoRaMac upper layer event functions
    */
    LoRaMacPrimitives_t* MacPrimitives;
    /*
    * LoRaMac upper layer callback functions
    */
    LoRaMacCallback_t* MacCallbacks;
    /*
    * Radio events function pointer
    */
    RadioEvents_t RadioEvents;
    /*
    * LoRaMac duty cycle delayed Tx timer
    */
    TimerEvent_t TxDelayedTimer;
    /*
    * LoRaMac reception windows timers
    */
    TimerEvent_t RxWindowTimer1;
    TimerEvent_t RxWindowTimer2;
    /*
    * LoRaMac reception windows delay
    * \remark normal frame: RxWindowXDelay = ReceiveDelayX - RADIO_WAKEUP_TIME
    *         join frame  : RxWindowXDelay = JoinAcceptDelayX - RADIO_WAKEUP_TIME
    */
    uint32_t RxWindow1Delay;
    uint32_t RxWindow2Delay;
    /*
    * LoRaMac Rx windows configuration
    */
    RxConfigParams_t RxWindow1Config;
    RxConfigParams_t RxWindow2Config;
    RxConfigParams_t RxWindowCConfig;
    /*
     * Limit of uplinks without any donwlink response before the ADRACKReq bit will be set.
     */
    uint16_t AdrAckLimit;
    /*
     * Limit of uplinks without any donwlink response after a the first frame with set ADRACKReq bit
     * before the trying to regain the connectivity.
     */
    uint16_t AdrAckDelay;
    /*
    * Acknowledge timeout timer. Used for packet retransmissions.
    */
    TimerEvent_t AckTimeoutTimer;
    /*
     * Uplink messages repetitions counter
     */
    uint8_t ChannelsNbTransCounter;
    /*
     * Number of trials to get a frame acknowledged
     */
    uint8_t AckTimeoutRetries;
    /*
     * Number of trials to get a frame acknowledged
     */
    uint8_t AckTimeoutRetriesCounter;
    /*
     * Indicates if the AckTimeout timer has expired or not
     */
    bool AckTimeoutRetry;
    /*
     * If the node has sent a FRAME_TYPE_DATA_CONFIRMED_UP this variable indicates
     * if the nodes needs to manage the server acknowledgement.
     */
    bool NodeAckRequested;
    /*
     * Current channel index
     */
    uint8_t Channel;
    /*
    * Last transmission time on air
    */
    TimerTime_t TxTimeOnAir;
    /*
    * Structure to hold an MCPS indication data.
    */
    McpsIndication_t McpsIndication;
    /*
    * Structure to hold MCPS confirm data.
    */
    McpsConfirm_t McpsConfirm;
    /*
    * Structure to hold MLME confirm data.
    */
    MlmeConfirm_t MlmeConfirm;
#ifdef LORAMAC_CLASSB_ENABLED
    /*
    * Structure to hold MLME indication data.
    */
    MlmeIndication_t MlmeIndication;
#endif
    /*
    * Holds the current rx window slot
    */
    LoRaMacRxSlot_t RxSlot;
    /*
    * LoRaMac tx/rx operation state
    */
    LoRaMacFlags_t MacFlags;
    /*
    * Data structure indicating if a request is allowed or not.
    */
    LoRaMacRequestHandling_t AllowRequests;
    /*
    * Non-volatile module context structure
    */
    LoRaMacNvmCtx_t* NvmCtx;
    /*
    * Duty cycle wait time
    */
    TimerTime_t DutyCycleWaitTime;
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
    /*
     * Start time of the response timeout
     */
    TimerTime_t ResponseTimeoutStartTime;
#endif
    uint32_t notifyMibFlag;
}LoRaMacCtx_t;

/*
 * Module context.
 */
static LoRaMacCtx_t MacCtx;

/*
 * Non-volatile module context.
 */
static LoRaMacNvmCtx_t NvmMacCtx;


#ifdef LORAMAC_CTX_SAVERESTORE_ENALED
/*
 * List of module contexts.
 */
LoRaMacCtxs_t Contexts;
#endif

/*!
 * Defines the LoRaMac radio events status
 */
typedef union uLoRaMacRadioEvents
{
    uint32_t Value;
    struct sEvents
    {
        uint32_t RxProcessPending : 1;
        uint32_t RxTimeout        : 1;
        uint32_t RxError          : 1;
        uint32_t TxTimeout        : 1;
        uint32_t RxDone           : 1;
        uint32_t TxDone           : 1;
    }Events;
}LoRaMacRadioEvents_t;

/*!
 * LoRaMac radio events status
 */
LoRaMacRadioEvents_t LoRaMacRadioEvents = { .Value = 0 };

/*!
 * Defines the LoRaMac timer events status
 */
typedef union uLoRaMacTimerEvents
{
    uint32_t Value;
    struct sTimerEvents
    {
        uint32_t RxWin1Timer    : 1;
        uint32_t RxWin2Timer    : 1;
        uint32_t TxDelayedTimer : 1;
    }Events;
}LoRaMacTimerEvents_t;

/*!
 * LoRaMac radio events status
 */
LoRaMacTimerEvents_t LoRaMacTimerEvents = { .Value = 0 };

static void LoRaMacHandleTimerEvents( void );
static void ProcessTimerRxOnWindow1( void );
static void ProcessTimerRxOnWindow2( void );
static void ProcessTimerTxDelayed( void );

/*!
 * \brief Function to be executed on Radio Tx Done event
 */
static void OnRadioTxDone( void );

/*!
 * \brief This function prepares the MAC to abort the execution of function
 *        OnRadioRxDone in case of a reception error.
 */
static void PrepareRxDoneAbort( void );

/*!
 * \brief Function to be executed on Radio Rx Done event
 */
static void OnRadioRxDone( uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr );

/*!
 * \brief Function executed on Radio Tx Timeout event
 */
static void OnRadioTxTimeout( void );

/*!
 * \brief Function executed on Radio Rx error event
 */
void OnRadioRxError( void );

/*!
 * \brief Function executed on Radio Rx Timeout event
 */
static void OnRadioRxTimeout( void );

/*!
 * \brief Function executed on duty cycle delayed Tx  timer event
 */
static void OnTxDelayedTimerEvent( void );

/*!
 * \brief Function executed on first Rx window timer event
 */
static void OnRxWindow1TimerEvent( void );

/*!
 * \brief Function executed on second Rx window timer event
 */
static void OnRxWindow2TimerEvent( void );

/*!
 * \brief Function executed on AckTimeout timer event
 */
static void OnAckTimeoutTimerEvent( void );

/*!
 * \brief Configures the events to trigger an MLME-Indication with
 *        a MLME type of MLME_SCHEDULE_UPLINK.
 */
static void SetMlmeScheduleUplinkIndication( void );

/*!
 * Computes next 32 bit downlink counter value and determines the frame counter ID.
 *
 * \param[IN]     addrID                - Address identifier
 * \param[IN]     fType                 - Frame type
 * \param[IN]     macMsg                - Data message object, holding the current 16 bit transmitted frame counter
 * \param[IN]     lrWanVersion          - LoRaWAN version
 * \param[IN]     maxFCntGap            - Maximum allowed frame counter difference (only for 1.0.X necessary)
 * \param[OUT]    fCntID                - Frame counter identifier
 * \param[OUT]    currentDown           - Current downlink counter value
 *
 * \retval                              - Status of the operation
 */
static LoRaMacCryptoStatus_t GetFCntDown( AddressIdentifier_t addrID, FType_t fType, LoRaMacMessageData_t* macMsg,
                                          uint16_t maxFCntGap, FCntIdentifier_t* fCntID, uint32_t* currentDown );

/*!
 * \brief Switches the device class
 *
 * \param [IN] deviceClass Device class to switch to
 */
static LoRaMacStatus_t SwitchClass( DeviceClass_t deviceClass );

/*!
 * \brief Gets the maximum application payload length in the absence of the optional FOpt field.
 *
 * \param [IN] datarate        Current datarate
 *
 * \retval                    Max length
 */
static uint8_t GetMaxAppPayloadWithoutFOptsLength( int8_t datarate );

/*!
 * \brief Validates if the payload fits into the frame, taking the datarate
 *        into account.
 *
 * \details Refer to chapter 4.3.2 of the LoRaWAN specification, v1.0
 *
 * \param lenN Length of the application payload. The length depends on the
 *             datarate and is region specific
 *
 * \param datarate Current datarate
 *
 * \param fOptsLen Length of the fOpts field
 *
 * \retval [false: payload does not fit into the frame, true: payload fits into
 *          the frame]
 */
static bool ValidatePayloadLength( uint8_t lenN, int8_t datarate, uint8_t fOptsLen );

/*!
 * \brief Decodes MAC commands in the fOpts field and in the payload
 *
 * \param [IN] payload      A pointer to the payload
 * \param [IN] macIndex     The index of the payload where the MAC commands start
 * \param [IN] commandsSize The size of the MAC commands
 * \param [IN] snr          The SNR value  of the frame
 * \param [IN] rxSlot       The RX slot where the frame was received
 */
static void ProcessMacCommands( uint8_t* payload, uint8_t macIndex, uint8_t commandsSize, int8_t snr, LoRaMacRxSlot_t rxSlot );

/*!
 * \brief LoRaMAC layer generic send frame
 *
 * \param [IN] macHdr      MAC header field
 * \param [IN] fPort       MAC payload port
 * \param [IN] fBuffer     MAC data buffer to be sent
 * \param [IN] fBufferSize MAC data buffer size
 * \retval status          Status of the operation.
 */
LoRaMacStatus_t Send( LoRaMacHeader_t* macHdr, uint8_t fPort, void* fBuffer, uint16_t fBufferSize );

/*!
 * \brief LoRaMAC layer send join/rejoin request
 *
 * \param [IN] joinReqType Type of join-request or rejoin
 *
 * \retval status          Status of the operation.
 */
LoRaMacStatus_t SendReJoinReq( JoinReqIdentifier_t joinReqType );

/*!
 * \brief LoRaMAC layer frame buffer initialization
 *
 * \param [IN] macHdr      MAC header field
 * \param [IN] fCtrl       MAC frame control field
 * \param [IN] fOpts       MAC commands buffer
 * \param [IN] fPort       MAC payload port
 * \param [IN] fBuffer     MAC data buffer to be sent
 * \param [IN] fBufferSize MAC data buffer size
 * \retval status          Status of the operation.
 */
LoRaMacStatus_t PrepareFrame( LoRaMacHeader_t* macHdr, LoRaMacFrameCtrl_t* fCtrl, uint8_t fPort, void* fBuffer, uint16_t fBufferSize );

/*
 * \brief Schedules the frame according to the duty cycle
 *
 * \param [IN] allowDelayedTx When set to true, the a frame will be delayed,
 *                            the duty cycle restriction is active
 * \retval Status of the operation
 */
static LoRaMacStatus_t ScheduleTx( bool allowDelayedTx );

/*
 * \brief Secures the current processed frame ( TxMsg )
 * \param[IN]     txDr      Data rate used for the transmission
 * \param[IN]     txCh      Index of the channel used for the transmission
 * \retval status           Status of the operation
 */
static LoRaMacStatus_t SecureFrame( uint8_t txDr, uint8_t txCh );

/*
 * \brief Calculates the back-off time for the band of a channel.
 *
 * \param [IN] channel     The last Tx channel index
 */
static void CalculateBackOff( uint8_t channel );

/*
 * \brief Function to remove pending MAC commands
 *
 * \param [IN] rxSlot     The RX slot on which the frame was received
 * \param [IN] fCtrl      The frame control field of the received frame
 * \param [IN] request    The request type
 */
static void RemoveMacCommands( LoRaMacRxSlot_t rxSlot, LoRaMacFrameCtrl_t fCtrl, Mcps_t request );

/*!
 * \brief LoRaMAC layer prepared frame buffer transmission with channel specification
 *
 * \remark PrepareFrame must be called at least once before calling this
 *         function.
 *
 * \param [IN] channel     Channel to transmit on
 * \retval status          Status of the operation.
 */
LoRaMacStatus_t SendFrameOnChannel( uint8_t channel );

/*!
 * \brief Sets the radio in continuous transmission mode
 *
 * \remark Uses the radio parameters set on the previous transmission.
 *
 * \param [IN] timeout     Time in seconds while the radio is kept in continuous wave mode
 * \param [IN] frequency   RF frequency to be set.
 * \param [IN] power       RF output power to be set.
 * \retval status          Status of the operation.
 */
LoRaMacStatus_t SetTxContinuousWave( uint16_t timeout, uint32_t frequency, int8_t power );

/*!
 * \brief Resets MAC specific parameters to default
 */
static void ResetMacParameters( void );

/*!
 * \brief Initializes and opens the reception window
 *
 * \param [IN] rxTimer  Window timer to be topped.
 * \param [IN] rxConfig Window parameters to be setup
 */
static void RxWindowSetup( TimerEvent_t* rxTimer, RxConfigParams_t* rxConfig );

/*!
 * \brief Opens up a continuous RX C window. This is used for
 *        class c devices.
 */
static void OpenContinuousRxCWindow( void );

#ifdef LORAMAC_CTX_SAVERESTORE_ENALED
/*!
 * \brief   Returns a pointer to the internal contexts structure.
 *
 * \retval  void Points to a structure containing all contexts
 */
LoRaMacCtxs_t* GetCtxs( void );

/*!
 * \brief   Restoring of internal module contexts
 *
 * \details This function allows to restore module contexts by a given pointer.
 *
 *
 * \retval  LoRaMacStatus_t Status of the operation. Possible returns are:
 *          returns are:
 *          \ref LORAMAC_STATUS_OK,
 *          \ref LORAMAC_STATUS_PARAMETER_INVALID,
 */
LoRaMacStatus_t RestoreCtxs( LoRaMacCtxs_t* contexts );
#endif

/*!
 * \brief   Determines the frame type
 *
 * \param [IN] macMsg Data message object
 *
 * \param [OUT] fType Frame type
 *
 * \retval  LoRaMacStatus_t Status of the operation. Possible returns are:
 *          returns are:
 *          \ref LORAMAC_STATUS_OK,
 *          \ref LORAMAC_STATUS_PARAMETER_INVALID,
 */
LoRaMacStatus_t DetermineFrameType( LoRaMacMessageData_t* macMsg, FType_t* fType );

/*!
 * \brief Checks if the retransmission should be stopped in case of a unconfirmed uplink
 *
 * \retval Returns true if it should be stopped.
 */
static bool CheckRetransUnconfirmedUplink( void );

/*!
 * \brief Checks if the retransmission should be stopped in case of a confirmed uplink
 *
 * \retval Returns true it should be stopped.
 */
static bool CheckRetransConfirmedUplink( void );

/*!
 * \brief Stops the uplink retransmission
 *
 * \retval Returns true if successful.
 */
static bool StopRetransmission( void );

/*!
 * \brief Calls the MacProcessNotify callback to indicate that a LoRaMacProcess call is pending
 */
static void OnMacProcessNotify( void );

/*!
 * \brief Handles the ACK retries algorithm.
 *        Increments the re-tries counter up until the specified number of
 *        trials or the allowed maximum. Decrease the uplink datarate every 2
 *        trials.
 */
static void AckTimeoutRetriesProcess( void );

/*!
 * \brief Finalizes the ACK retries algorithm.
 *        If no ACK is received restores the default channels
 */
static void AckTimeoutRetriesFinalize( void );

/*!
 * \brief Calls the callback to indicate that a context changed
 */
static void CallNvmCtxCallback( void );

#ifdef LORAMAC_CLASSB_ENABLED
/*!
 * \brief Verifies if a request is pending currently
 *
 *\retval 1: Request pending, 0: request not pending
 */
static uint8_t IsRequestPending( void );
#endif

/*!
 * \brief Enabled the possibility to perform requests
 *
 * \param [IN] requestState Request permission state
 */
static void LoRaMacEnableRequests( LoRaMacRequestHandling_t requestState );

/*!
 * \brief This function verifies if a RX abort occurred
 */
static void LoRaMacCheckForRxAbort( void );

#ifdef LORAMAC_CLASSB_ENABLED
/*!
 * \brief This function verifies if a beacon acquisition MLME
 *        request was pending
 *
 * \retval 1: Request pending, 0: no request pending
 */
static uint8_t LoRaMacCheckForBeaconAcquisition( void );
#endif

/*!
 * \brief This function handles join request
 */
static void LoRaMacHandleMlmeRequest( void );

/*!
 * \brief This function handles mcps request
 */
static void LoRaMacHandleMcpsRequest( void );

/*!
 * \brief This function handles callback events for requests
 */
static void LoRaMacHandleRequestEvents( void );

/*!
 * \brief This function handles callback events for indications
 */
static void LoRaMacHandleIndicationEvents( void );

static void LoRaMacRemoveConfirmQueue( void );
DeviceClass_t LoRaMacGetDeviceClass( void );

static void LoRaMacResetNvmEvtMibFlag( void );

/*!
 * Get processiong time for downlink
 */
enum eLoRaMacProcTimeKind
{
    LORAMAC_STACK_PROCTIME_SEL_RX1_ON = 0,
    LORAMAC_STACK_PROCTIME_SEL_RX2_ON
};
static int32_t LoRaMacGetStackProcessTime( uint8_t kind );

#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
/*!
 * \brief This function verifies if the response timeout has been elapsed. If
 *        this is the case, the status of Nvm.MacGroup1.SrvAckRequested will be
 *        reset.
 *
 * \param [IN] timeoutInMs Timeout [ms] to be compared.
 *
 * \param [IN] startTimeInMs Start time [ms] used as a base. If set to 0,
 *                           no comparison will be done.
 *
 * \retval true: Response timeout has been elapsed, false: Response timeout
 *         has not been elapsed or startTimeInMs is 0.
 */
static bool LoRaMacHandleResponseTimeout( TimerTime_t timeoutInMs, TimerTime_t startTimeInMs );
#endif

/*!
 * Structure used to store the radio Tx event data
 */
struct
{
    TimerTime_t CurTime;
}TxDoneParams;

/*!
 * Structure used to store the radio Rx event data
 */
struct
{
    TimerTime_t LastRxDone;
    uint8_t *Payload;
    uint16_t Size;
    int16_t Rssi;
    int8_t Snr;
}RxDoneParams;

static void OnRadioTxDone( void )
{
    TxDoneParams.CurTime = TimerGetCurrentTime( );
    MacCtx.LastTxSysTime = SysTimeFromMs( TxDoneParams.CurTime );

    LoRaMacRadioEvents.Events.TxDone = 1;

    OnMacProcessNotify();
}

static void OnRadioRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr )
{
    RxDoneParams.LastRxDone = TimerGetCurrentTime( );
    RxDoneParams.Payload = payload;
    RxDoneParams.Size = size;
    RxDoneParams.Rssi = rssi;
    RxDoneParams.Snr = snr;

    LoRaMacRadioEvents.Events.RxDone = 1;
    LoRaMacRadioEvents.Events.RxProcessPending = 1;

    OnMacProcessNotify();
}

static void OnRadioTxTimeout( void )
{
    LoRaMacRadioEvents.Events.TxTimeout = 1;

    OnMacProcessNotify();
}

void OnRadioRxError( void )
{
    LoRaMacRadioEvents.Events.RxError = 1;

    OnMacProcessNotify();
}

static void OnRadioRxTimeout( void )
{
    LoRaMacRadioEvents.Events.RxTimeout = 1;

    OnMacProcessNotify();
}

static void UpdateRxSlotIdleState( void )
{
    if( MacCtx.NvmCtx->DeviceClass != CLASS_C )
    {
        MacCtx.RxSlot = RX_SLOT_NONE;
    }
    else
    {
        MacCtx.RxSlot = RX_SLOT_WIN_CLASS_C;
    }
}

static void ProcessRadioTxDone( void )
{
    GetPhyParams_t getPhy;
    PhyParam_t phyParam;
    SetBandTxDoneParams_t txDone;
    TimerTime_t offset;
    uint32_t rxWin1TimeVal, rxWin2TimeVal;

    LORAMAC_RADIOSLEEP_TXDONE( 0 );

    // Setup timers
    offset  = TimerGetCurrentTime();
    offset -= TxDoneParams.CurTime;
    if( MacCtx.RxWindow1Delay > (uint32_t)offset )
    {
        rxWin1TimeVal = MacCtx.RxWindow1Delay - (uint32_t)offset;
        rxWin2TimeVal = MacCtx.RxWindow2Delay - (uint32_t)offset;
    }
    else
    {
        // fail-safe to avoid waiting an huge amount of time
        rxWin1TimeVal = 0;
        rxWin2TimeVal = 0;
    }
    TimerSetValue( &MacCtx.RxWindowTimer1, rxWin1TimeVal );
    TimerStart( &MacCtx.RxWindowTimer1 );
    TimerSetValue( &MacCtx.RxWindowTimer2, rxWin2TimeVal );
    TimerStart( &MacCtx.RxWindowTimer2 );

    if( ( MacCtx.NvmCtx->DeviceClass == CLASS_C ) || ( MacCtx.NodeAckRequested == true ) )
    {
        getPhy.Attribute = PHY_ACK_TIMEOUT;
        phyParam = RegionGetPhyParam( MacCtx.NvmCtx->Region, &getPhy );
        TimerSetValue( &MacCtx.AckTimeoutTimer, MacCtx.RxWindow2Delay + phyParam.Value );
        TimerStart( &MacCtx.AckTimeoutTimer );
    }

#if defined(DEBUG_LORAMAC)
    LoRaMacDebugOnRadioTxDone( TxDoneParams.CurTime );
#endif

    // Store last Tx channel
    MacCtx.NvmCtx->LastTxChannel = MacCtx.Channel;
    // Update last tx done time for the current channel
    txDone.Channel = MacCtx.Channel;
    if( MacCtx.NvmCtx->NetworkActivation == ACTIVATION_TYPE_NONE )
    {
        txDone.Joined  = false;
    }
    else
    {
        txDone.Joined  = true;
    }
    txDone.LastTxDoneTime = TxDoneParams.CurTime;
    RegionSetBandTxDone( MacCtx.NvmCtx->Region, &txDone );
    // Update Aggregated last tx done time
    MacCtx.NvmCtx->LastTxDoneTime = TxDoneParams.CurTime;

    if( MacCtx.NodeAckRequested == false )
    {
        MacCtx.McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_OK;
    }
}

static void PrepareRxDoneAbort( void )
{
    MacCtx.MacState |= LORAMAC_RX_ABORT;

    if( MacCtx.NodeAckRequested == true )
    {
        OnAckTimeoutTimerEvent();
    }

    MacCtx.MacFlags.Bits.McpsInd = 1;
    MacCtx.MacFlags.Bits.MacDone = 1;

    UpdateRxSlotIdleState( );
}

static void ProcessRadioRxDone( void )
{
    LoRaMacHeader_t macHdr;
    ApplyCFListParams_t applyCFList;
    GetPhyParams_t getPhy;
    PhyParam_t phyParam;
    LoRaMacCryptoStatus_t macCryptoStatus = LORAMAC_CRYPTO_ERROR;
#ifdef LORAMAC_CLASSB_ENABLED
    bool isDoneBeaconRx;
    bool isPingMCframe;
#endif

    LoRaMacMessageData_t macMsgData = { 0 };
    LoRaMacMessageJoinAccept_t macMsgJoinAccept = { 0 };
    uint8_t *payload = RxDoneParams.Payload;
    uint16_t size = RxDoneParams.Size;
    int16_t rssi = RxDoneParams.Rssi;
    int8_t snr = RxDoneParams.Snr;

    uint8_t pktHeaderLen = 0;

    uint32_t downLinkCounter = 0;
    uint32_t address = MacCtx.NvmCtx->DevAddr;
    AddressIdentifier_t addrID = UNICAST_DEV_ADDR;
    FCntIdentifier_t fCntID;
    uint32_t rxDelay;
#if (LORAMAC_MAX_MC_CTX > 0)
    uint8_t multicast = 0;
#endif

    LoRaMacRadioEvents.Events.RxProcessPending = 0;

    MacCtx.McpsConfirm.AckReceived = false;
    MacCtx.McpsIndication.Rssi = rssi;
    MacCtx.McpsIndication.Snr = snr;
    MacCtx.McpsIndication.RxSlot = MacCtx.RxSlot;
    LORAWAN_RXC_STORE_U8(last_rx_done_slot, (uint8_t)MacCtx.RxSlot);  /* T-V3.1 #12 */
    /* r13 Phase 1 — slot-id snapshot reset on MLME_JOIN_REQ + close-timestamp. */
    __atomic_store_n(&lorawan_rxc_diag.last_rx_done_slot_id,
                     (uint8_t)MacCtx.RxSlot, __ATOMIC_RELAXED);
    {
        uint32_t cyc = *(volatile uint32_t *)0xE0001004UL;
        if( MacCtx.RxSlot == RX_SLOT_WIN_1 )
        {
            __atomic_store_n((uint32_t *)&lorawan_rxc_diag.last_rx1_close_cyc,
                             cyc, __ATOMIC_RELAXED);
        }
        else if( MacCtx.RxSlot == RX_SLOT_WIN_2 )
        {
            __atomic_store_n((uint32_t *)&lorawan_rxc_diag.last_rx2_close_cyc,
                             cyc, __ATOMIC_RELAXED);
        }
    }
    MacCtx.McpsIndication.Port = 0;
    MacCtx.McpsIndication.Multicast = 0;
    MacCtx.McpsIndication.FramePending = 0;
    MacCtx.McpsIndication.Buffer = NULL;
    MacCtx.McpsIndication.BufferSize = 0;
    MacCtx.McpsIndication.RxData = false;
    MacCtx.McpsIndication.AckReceived = false;
    MacCtx.McpsIndication.DownLinkCounter = 0;
    MacCtx.McpsIndication.McpsIndication = MCPS_UNCONFIRMED;
    MacCtx.McpsIndication.DevAddress = 0;
    MacCtx.McpsIndication.DeviceTimeAnsReceived = false;
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
    MacCtx.McpsIndication.ResponseTimeout = 0;
#endif

    LORAMAC_RADIOSLEEP_RXDONE( 0 );

    if( ( MacCtx.McpsIndication.RxSlot == RX_SLOT_WIN_1 ) ||
        ( MacCtx.NvmCtx->DeviceClass == CLASS_C ) )
    {
        TimerStop( &MacCtx.RxWindowTimer2 );
    }

#if defined(DEBUG_LORAMAC)
    LoRaMacDebugOnRadioRxDone( payload, size, rssi, snr, (uint8_t)MacCtx.RxSlot, RxDoneParams.LastRxDone );
#endif

#ifdef LORAMAC_CLASSB_ENABLED
    // This function must be called even if we are not in class b mode yet.
    if( LoRaMacClassBRxBeacon( payload, size, &isDoneBeaconRx ) == true )
    {
        if( isDoneBeaconRx == true )
        {
            LORAMAC_RADIOSLEEP_RXDONE_BEACON( 0 );
            MacCtx.MlmeIndication.BeaconInfo.Rssi = rssi;
            MacCtx.MlmeIndication.BeaconInfo.Snr = snr;
        }
        return;
    }
    // Check if we expect a ping or a multicast slot.
    isPingMCframe = false;
    if( MacCtx.NvmCtx->DeviceClass == CLASS_B )
    {
        if( LoRaMacClassBIsPingExpected( ) == true )
        {
            LoRaMacClassBSetPingSlotState( PINGSLOT_STATE_CALC_PING_OFFSET );
            LoRaMacClassBPingSlotTimerEvent();
            MacCtx.McpsIndication.RxSlot = RX_SLOT_WIN_CLASS_B_PING_SLOT;
            isPingMCframe = true;
        }
        else if( LoRaMacClassBIsMulticastExpected( ) == true )
        {
            LoRaMacClassBSetMulticastSlotState( PINGSLOT_STATE_CALC_PING_OFFSET );
            LoRaMacClassBMulticastSlotTimerEvent();
            MacCtx.McpsIndication.RxSlot = RX_SLOT_WIN_CLASS_B_MULTICAST_SLOT;
            isPingMCframe = true;
        }
    }
#endif

    // MLME-Cfm may not be notified unless this function is called.
    LoRaMacConfirmQueueSetStatusCmn( LORAMAC_EVENT_INFO_STATUS_ERROR );

    if( size == 0 )
    {
        MacCtx.McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
        PrepareRxDoneAbort( );
        return;
    }
    macHdr.Value = payload[pktHeaderLen++];

    switch( macHdr.Bits.MType )
    {
        case FRAME_TYPE_JOIN_ACCEPT:
        {
            if( (size != LORAMAC_JOIN_ACCEPT_M_MSG_SIZE) &&
                (size != (LORAMAC_JOIN_ACCEPT_M_MSG_SIZE + LORAMAC_CF_LIST_FIELD_SIZE)) )
            {
                MacCtx.McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
                PrepareRxDoneAbort( );
                return;
            }

            macMsgJoinAccept.Buffer = payload;
            macMsgJoinAccept.BufSize = size;

            // Abort in case if the device isn't joined yet and no rejoin request is ongoing.
            if( MacCtx.NvmCtx->NetworkActivation != ACTIVATION_TYPE_NONE )
            {
                MacCtx.McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
                PrepareRxDoneAbort( );
                return;
            }
            macCryptoStatus = LoRaMacCryptoHandleJoinAccept( JOIN_REQ, SecureElementGetJoinEui( ), &macMsgJoinAccept );

            VerifyParams_t verifyRxDr;
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
            bool rxDrValid = true;
            if( macMsgJoinAccept.DLSettings.Bits.RX2DataRate != 0x0F )
#else
            bool rxDrValid = false;
#endif
            {
                verifyRxDr.DatarateParams.Datarate = macMsgJoinAccept.DLSettings.Bits.RX2DataRate;
                verifyRxDr.DatarateParams.DownlinkDwellTime = MacCtx.NvmCtx->MacParams.DownlinkDwellTime;
                rxDrValid = RegionVerify( MacCtx.NvmCtx->Region, &verifyRxDr, PHY_RX_DR );
            }

            if( ( LORAMAC_CRYPTO_SUCCESS == macCryptoStatus ) && ( rxDrValid == true ) )
            {
                // Network ID
                LoRaMacCommonCopyArrayToUint32( &(MacCtx.NvmCtx->NetID), &(macMsgJoinAccept.NetID[0]), 3 );

                // Device Address
                MacCtx.NvmCtx->DevAddr = macMsgJoinAccept.DevAddr;

                // DLSettings
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
                if( MacCtx.NvmCtx->MacParams.Rx1DrOffset != macMsgJoinAccept.DLSettings.Bits.RX1DRoffset )
                {
                    MacCtx.NvmCtx->MacParams.Rx1DrOffset = macMsgJoinAccept.DLSettings.Bits.RX1DRoffset;
                    LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_RX1_DROFFSET );
                }
#else
                MacCtx.NvmCtx->MacParams.Rx1DrOffset = macMsgJoinAccept.DLSettings.Bits.RX1DRoffset;
#endif

                // Verify if we shall assign the new datarate
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
                if( macMsgJoinAccept.DLSettings.Bits.RX2DataRate != 0x0F )
#endif
                {
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
                    if( MacCtx.NvmCtx->MacParams.Rx2Channel.Datarate != macMsgJoinAccept.DLSettings.Bits.RX2DataRate )
                    {
                        MacCtx.NvmCtx->MacParams.Rx2Channel.Datarate = macMsgJoinAccept.DLSettings.Bits.RX2DataRate;
                        LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_RX2_DATARATE );
                    }
#else
                    MacCtx.NvmCtx->MacParams.Rx2Channel.Datarate = macMsgJoinAccept.DLSettings.Bits.RX2DataRate;
#endif
                    MacCtx.NvmCtx->MacParams.RxCChannel.Datarate = macMsgJoinAccept.DLSettings.Bits.RX2DataRate;
                }

                // RxDelay
                rxDelay = macMsgJoinAccept.RxDelay;
                if( rxDelay == 0 )
                {
                    rxDelay = 1;
                }
                rxDelay *= (uint32_t)1000;
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
                if( MacCtx.NvmCtx->MacParams.ReceiveDelay1 != rxDelay )
                {
                    MacCtx.NvmCtx->MacParams.ReceiveDelay1 = rxDelay;
                    MacCtx.NvmCtx->MacParams.ReceiveDelay2 = MacCtx.NvmCtx->MacParams.ReceiveDelay1 + 1000;
                    LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_RECEIVE_DELAY_1 );
                }
#else
                MacCtx.NvmCtx->MacParams.ReceiveDelay1 = rxDelay;
                MacCtx.NvmCtx->MacParams.ReceiveDelay2 = MacCtx.NvmCtx->MacParams.ReceiveDelay1 + 1000;
#endif

                // Reset NbTrans to default value
                MacCtx.NvmCtx->MacParams.ChannelsNbTrans = 1;

                MacCtx.NvmCtx->Version.Fields.Minor = 0;

                // Apply CF list
                applyCFList.Payload = macMsgJoinAccept.CFList;
                applyCFList.Size = size - LORAMAC_JOIN_ACCEPT_M_MSG_SIZE;
                applyCFList.JoinChannel = MacCtx.Channel;   // Apply the last tx channel
                RegionApplyCFList( MacCtx.NvmCtx->Region, &applyCFList );

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
#if defined(REGION_AS923) || defined(REGION_EU868)
                if( (MacCtx.NvmCtx->Region == LORAMAC_REGION_AS923) ||
                    (MacCtx.NvmCtx->Region == LORAMAC_REGION_EU868) )
                {
                    LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_CHANNELS );
                }
#endif
                LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_CHANNELS_MASK );
#endif

                MacCtx.NvmCtx->NetworkActivation = ACTIVATION_TYPE_OTAA;

                // MLME handling
                if( LoRaMacConfirmQueueIsCmdActive( MLME_JOIN ) == true )
                {
                    LoRaMacConfirmQueueSetStatus( LORAMAC_EVENT_INFO_STATUS_OK, MLME_JOIN );
                }
            }
            else
            {
                // MLME handling
                if( LoRaMacConfirmQueueIsCmdActive( MLME_JOIN ) == true )
                {
                    if( macCryptoStatus == LORAMAC_CRYPTO_FAIL_JOIN_NONCE )
                    {
                        // JoinNonce is invalid
                        LoRaMacConfirmQueueSetStatus( LORAMAC_EVENT_INFO_STATUS_JOIN_NONCE_FAIL, MLME_JOIN );
                    }
                    else
                    {
                        LoRaMacConfirmQueueSetStatus( LORAMAC_EVENT_INFO_STATUS_JOIN_FAIL, MLME_JOIN );
                    }
                }
            }
            break;
        }
        case FRAME_TYPE_DATA_CONFIRMED_DOWN:
            MacCtx.McpsIndication.McpsIndication = MCPS_CONFIRMED;
            // Intentional fall through
            // no break
        case FRAME_TYPE_DATA_UNCONFIRMED_DOWN:
            // Check if the received payload size is valid
            getPhy.UplinkDwellTime = MacCtx.NvmCtx->MacParams.DownlinkDwellTime;
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
#if defined(REGION_AS923)
            if( MacCtx.NvmCtx->Region == LORAMAC_REGION_AS923 )
            {
                // (RP002)
                // The end-device SHALL only enforce the maximum Downlink MAC Payload Size
                // defined for DownlinkDwellTime = 0 (no dwell time enforced)
                // regardless of the actual setting.
                getPhy.UplinkDwellTime = 0;
            }
#endif
#endif
            getPhy.Datarate = MacCtx.McpsIndication.RxDatarate;
            getPhy.Attribute = PHY_MAX_PAYLOAD;
            phyParam = RegionGetPhyParam( MacCtx.NvmCtx->Region, &getPhy );
            if( ( R_MAX( 0, ( int16_t )( ( int16_t ) size - ( int16_t ) LORA_MAC_FRMPAYLOAD_OVERHEAD ) ) > ( int16_t )phyParam.Value ) ||
                ( size < LORAMAC_FRAME_PAYLOAD_MIN_SIZE ) )
            {
                MacCtx.McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
                PrepareRxDoneAbort( );
                return;
            }
            macMsgData.Buffer = payload;
            macMsgData.BufSize = size;
#ifdef LORAMAC_RXC_CONTINUOUS_ENABLED
            macMsgData.FRMPayload = MacCtx.RxPayload;
#endif
            macMsgData.FRMPayloadSize = LORAMAC_PHY_MAXPAYLOAD;

            if( LORAMAC_PARSER_SUCCESS != LoRaMacParserData( &macMsgData ) )
            {
                MacCtx.McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
                PrepareRxDoneAbort( );
                return;
            }

#ifdef LORAMAC_CLASSB_ENABLED
            // Check if we expect a ping or a multicast slot.
            if( isPingMCframe == true )
            {
                LoRaMacClassBSetFPendingBit( macMsgData.FHDR.DevAddr,
                                             ( uint8_t )macMsgData.FHDR.FCtrl.Bits.FPending );
            }
#endif

            // Store device address
            MacCtx.McpsIndication.DevAddress = macMsgData.FHDR.DevAddr;

            FType_t fType;
            if( LORAMAC_STATUS_OK != DetermineFrameType( &macMsgData, &fType ) )
            {
                MacCtx.McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
                PrepareRxDoneAbort( );
                return;
            }

            //Check if it is a multicast message
#if (LORAMAC_MAX_MC_CTX > 0)
            multicast = 0;
            downLinkCounter = 0;
#endif
            if( address != macMsgData.FHDR.DevAddr )
            {
#if (LORAMAC_MAX_MC_CTX > 0)
                uint8_t i;
                for( i = 0; i < LORAMAC_MAX_MC_CTX; i++ )
                {
                    if( ( MacCtx.NvmCtx->MulticastChannelList[i].ChannelParams.Address == macMsgData.FHDR.DevAddr ) &&
                        ( MacCtx.NvmCtx->MulticastChannelList[i].ChannelParams.IsEnabled == true ) )
                    {
                        multicast = 1;
                        addrID = MacCtx.NvmCtx->MulticastChannelList[i].ChannelParams.GroupID;
                        downLinkCounter = *( MacCtx.NvmCtx->MulticastChannelList[i].DownLinkCounter );
                        address = MacCtx.NvmCtx->MulticastChannelList[i].ChannelParams.Address;
                        if( MacCtx.NvmCtx->DeviceClass == CLASS_C )
                        {
                            MacCtx.McpsIndication.RxSlot = RX_SLOT_WIN_CLASS_C_MULTICAST;
                        }
                        else  // if( MacCtx.NvmCtx->DeviceClass == CLASS_B )
                        {
                            MacCtx.McpsIndication.RxSlot = RX_SLOT_WIN_CLASS_B_MULTICAST_SLOT;
                        }
                        break;
                    }
                }
                if( i == LORAMAC_MAX_MC_CTX )
#endif  // LORAMAC_MAX_MC_CTX
                {
                    // it is not our address
                    MacCtx.McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_ADDRESS_FAIL;
                    PrepareRxDoneAbort( );
                    return;
                }
            }

#if (LORAMAC_MAX_MC_CTX > 0)
            // Filter messages according to multicast downlink exceptions
            if( ( multicast == 1 ) && ( ( fType != FRAME_TYPE_D ) ||
                                        ( macMsgData.FHDR.FCtrl.Bits.Ack == true ) ||
                                        ( macMsgData.FHDR.FCtrl.Bits.AdrAckReq == true ) ) )
            {
                MacCtx.McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
                PrepareRxDoneAbort( );
                return;
            }
#endif

#if (LORAMAC_VERSION < LORAWAN_VERSION_1_0_4)  // LW1.0.3
            // Get maximum allowed counter difference
            getPhy.Attribute = PHY_MAX_FCNT_GAP;
            phyParam = RegionGetPhyParam( MacCtx.NvmCtx->Region, &getPhy );
#else
            // (LW1.0.4)
            // NOTE: phyParam.Value is not used in GetFCntDown() function.
#endif
            // Get downlink frame counter value
            macCryptoStatus = GetFCntDown( addrID, fType, &macMsgData, phyParam.Value, &fCntID, &downLinkCounter );
            if( macCryptoStatus != LORAMAC_CRYPTO_SUCCESS )
            {
                if( macCryptoStatus == LORAMAC_CRYPTO_FAIL_FCNT_DUPLICATED )
                {
                    // Catch the case of repeated downlink frame counter
                    MacCtx.McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_DOWNLINK_REPEATED;
#if (LORAMAC_VERSION < LORAWAN_VERSION_1_0_4)  // LW1.0.3
                    if( ( MacCtx.NvmCtx->Version.Fields.Minor == 0 ) && ( macHdr.Bits.MType == FRAME_TYPE_DATA_CONFIRMED_DOWN ) && ( MacCtx.NvmCtx->LastRxMic == macMsgData.MIC ) )
                    {
                        MacCtx.NvmCtx->SrvAckRequested = true;
                    }
#endif
                }
#if (LORAMAC_VERSION < LORAWAN_VERSION_1_0_4)  // LW1.0.3
                else if( macCryptoStatus == LORAMAC_CRYPTO_FAIL_MAX_GAP_FCNT )
                {
                    // Lost too many frames
                    MacCtx.McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_DOWNLINK_TOO_MANY_FRAMES_LOSS;
                }
#endif
                else
                {
                    // Other errors
                    MacCtx.McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
                }
                MacCtx.McpsIndication.DownLinkCounter = downLinkCounter;
                PrepareRxDoneAbort( );
                return;
            }

            macCryptoStatus = LoRaMacCryptoUnsecureMessage( addrID, address, fCntID, downLinkCounter, &macMsgData );
            if( macCryptoStatus != LORAMAC_CRYPTO_SUCCESS )
            {
                if( macCryptoStatus == LORAMAC_CRYPTO_FAIL_ADDRESS )
                {
                    // We are not the destination of this frame.
                    MacCtx.McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_ADDRESS_FAIL;
                }
                else
                {
                    // MIC calculation fail
                    MacCtx.McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_MIC_FAIL;
                }
                PrepareRxDoneAbort( );
                return;
            }

            // Frame is valid
            MacCtx.McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_OK;
#if (LORAMAC_MAX_MC_CTX > 0)
            MacCtx.McpsIndication.Multicast = multicast;
#else
            MacCtx.McpsIndication.Multicast = 0;
#endif
            MacCtx.McpsIndication.FramePending = macMsgData.FHDR.FCtrl.Bits.FPending;
            MacCtx.McpsIndication.Buffer = NULL;
            MacCtx.McpsIndication.BufferSize = 0;
            MacCtx.McpsIndication.DownLinkCounter = downLinkCounter;
            MacCtx.McpsIndication.AckReceived = macMsgData.FHDR.FCtrl.Bits.Ack;

            MacCtx.McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_OK;
            MacCtx.McpsConfirm.AckReceived = macMsgData.FHDR.FCtrl.Bits.Ack;

            // Reset ADR ACK Counter only, when RX1 or RX2 slot
            if( ( MacCtx.McpsIndication.RxSlot == RX_SLOT_WIN_1 ) ||
                ( MacCtx.McpsIndication.RxSlot == RX_SLOT_WIN_2 ) )
            {
                MacCtx.NvmCtx->AdrAckCounter = 0;
            }

            // MCPS Indication and ack requested handling
#if (LORAMAC_MAX_MC_CTX > 0)
            if( multicast == 1 )
            {
                MacCtx.McpsIndication.McpsIndication = MCPS_MULTICAST;
            }
            else
#endif
            {
                if( macHdr.Bits.MType == FRAME_TYPE_DATA_CONFIRMED_DOWN )
                {
                    MacCtx.NvmCtx->SrvAckRequested = true;
                    if( MacCtx.NvmCtx->Version.Fields.Minor == 0 )
                    {
                        MacCtx.NvmCtx->LastRxMic = macMsgData.MIC;
                    }
                    MacCtx.McpsIndication.McpsIndication = MCPS_CONFIRMED;

#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
                    // Handle response timeout for class c and class b downlinks
                    if( ( MacCtx.McpsIndication.RxSlot != RX_SLOT_WIN_1 ) &&
                        ( MacCtx.McpsIndication.RxSlot != RX_SLOT_WIN_2 ) )
                    {
                        // Calculate timeout
                        MacCtx.McpsIndication.ResponseTimeout = REGION_COMMON_CLASS_B_C_RESP_TIMEOUT;
                        MacCtx.ResponseTimeoutStartTime = RxDoneParams.LastRxDone;
                    }
#endif
                }
                else
                {
                    MacCtx.NvmCtx->SrvAckRequested = false;
                    MacCtx.McpsIndication.McpsIndication = MCPS_UNCONFIRMED;
                }
            }

            RemoveMacCommands( MacCtx.McpsIndication.RxSlot, macMsgData.FHDR.FCtrl, MacCtx.McpsConfirm.McpsRequest );

            switch( fType )
            {
                case FRAME_TYPE_A:
                {  /* +----------+------+-------+--------------+
                    * | FOptsLen | Fopt | FPort |  FRMPayload  |
                    * +----------+------+-------+--------------+
                    * |    > 0   |   X  |  > 0  |       X      |
                    * +----------+------+-------+--------------+
                    */

                    // Decode MAC commands in FOpts field
                    ProcessMacCommands( macMsgData.FHDR.FOpts, 0, macMsgData.FHDR.FCtrl.Bits.FOptsLen, snr, MacCtx.McpsIndication.RxSlot );
                    MacCtx.McpsIndication.Port = macMsgData.FPort;
                    MacCtx.McpsIndication.Buffer = macMsgData.FRMPayload;
                    MacCtx.McpsIndication.BufferSize = macMsgData.FRMPayloadSize;
                    MacCtx.McpsIndication.RxData = true;
                    break;
                }
                case FRAME_TYPE_B:
                {  /* +----------+------+-------+--------------+
                    * | FOptsLen | Fopt | FPort |  FRMPayload  |
                    * +----------+------+-------+--------------+
                    * |    > 0   |   X  |   -   |       -      |
                    * +----------+------+-------+--------------+
                    */

                    // Decode MAC commands in FOpts field
                    ProcessMacCommands( macMsgData.FHDR.FOpts, 0, macMsgData.FHDR.FCtrl.Bits.FOptsLen, snr, MacCtx.McpsIndication.RxSlot );
                    MacCtx.McpsIndication.Port = macMsgData.FPort;
                    break;
                }
                case FRAME_TYPE_C:
                {  /* +----------+------+-------+--------------+
                    * | FOptsLen | Fopt | FPort |  FRMPayload  |
                    * +----------+------+-------+--------------+
                    * |    = 0   |   -  |  = 0  | MAC commands |
                    * +----------+------+-------+--------------+
                    */

                    // Decode MAC commands in FRMPayload
                    ProcessMacCommands( macMsgData.FRMPayload, 0, macMsgData.FRMPayloadSize, snr, MacCtx.McpsIndication.RxSlot );
                    MacCtx.McpsIndication.Port = macMsgData.FPort;
                    break;
                }
                case FRAME_TYPE_D:
                {  /* +----------+------+-------+--------------+
                    * | FOptsLen | Fopt | FPort |  FRMPayload  |
                    * +----------+------+-------+--------------+
                    * |    = 0   |   -  |  > 0  |       X      |
                    * +----------+------+-------+--------------+
                    */

                    // No MAC commands just application payload
                    MacCtx.McpsIndication.Port = macMsgData.FPort;
                    MacCtx.McpsIndication.Buffer = macMsgData.FRMPayload;
                    MacCtx.McpsIndication.BufferSize = macMsgData.FRMPayloadSize;
                    MacCtx.McpsIndication.RxData = true;
                    break;
                }
                default:
                    MacCtx.McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
                    PrepareRxDoneAbort( );
                    break;
            }

            if( ( macMsgData.FPort == LORAMAC_CERT_FPORT ) && ( MacCtx.NvmCtx->IsCertPortOn == false ) )
            { // Do not notify the upper layer of data reception on FPort LORAMAC_CERT_FPORT if the port
              // handling is disabled.
                MacCtx.McpsIndication.Port = macMsgData.FPort;
                MacCtx.McpsIndication.Buffer = NULL;
                MacCtx.McpsIndication.BufferSize = 0;
                MacCtx.McpsIndication.RxData = false;
            }

            // Provide always an indication, skip the callback to the user application,
            // in case of a confirmed downlink retransmission.
            MacCtx.MacFlags.Bits.McpsInd = 1;

            break;
        default:
            MacCtx.McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
            PrepareRxDoneAbort( );
            break;
    }

    // Verify if we need to disable the AckTimeoutTimer
    // Only aplies if downlink is received on Rx1 or Rx2 windows.
    if( ( MacCtx.McpsIndication.RxSlot == RX_SLOT_WIN_1 ) ||
        ( MacCtx.McpsIndication.RxSlot == RX_SLOT_WIN_2 ) )
    {
        if( MacCtx.NodeAckRequested == true )
        {
            if( MacCtx.McpsConfirm.AckReceived == true )
            {
                OnAckTimeoutTimerEvent();
            }
        }
    }

    if( MacCtx.McpsIndication.RxSlot != RX_SLOT_WIN_CLASS_C )
    {
        MacCtx.MacFlags.Bits.MacDone = 1;
    }

    UpdateRxSlotIdleState( );
}

static void ProcessRadioTxTimeout( void )
{
    LORAMAC_RADIOSLEEP_TXTIMEOUT( 0 );
    UpdateRxSlotIdleState( );

    MacCtx.McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_TX_TIMEOUT;
    LoRaMacConfirmQueueSetStatusCmn( LORAMAC_EVENT_INFO_STATUS_TX_TIMEOUT );
    if( MacCtx.NodeAckRequested == true )
    {
        MacCtx.AckTimeoutRetry = true;
    }
    MacCtx.MacFlags.Bits.MacDone = 1;
}

static void HandleRadioRxErrorTimeout( LoRaMacEventInfoStatus_t rx1EventInfoStatus, LoRaMacEventInfoStatus_t rx2EventInfoStatus )
{
    bool isRx1ErrAbort = false;
    bool classBRx = false;

#ifdef LORAMAC_CLASSB_ENABLED
    if( LoRaMacClassBIsBeaconExpected( ) == true )
    {
        LoRaMacClassBSetBeaconState( BEACON_STATE_TIMEOUT );
        LoRaMacClassBBeaconTimerEvent();
        classBRx = true;
    }
    if( MacCtx.NvmCtx->DeviceClass == CLASS_B )
    {
        if( LoRaMacClassBIsPingExpected( ) == true )
        {
            LoRaMacClassBSetPingSlotState( PINGSLOT_STATE_CALC_PING_OFFSET );
            LoRaMacClassBPingSlotTimerEvent();
            classBRx = true;
        }
        if( LoRaMacClassBIsMulticastExpected( ) == true )
        {
            LoRaMacClassBSetMulticastSlotState( PINGSLOT_STATE_CALC_PING_OFFSET );
            LoRaMacClassBMulticastSlotTimerEvent();
            classBRx = true;
        }
    }
#endif

    // GitHub issue #757 "Stack stucks in LORAMAC_TX_RUNNING state if RX1 error occurs"
    CRITICAL_SECTION_BEGIN();

    if( classBRx == false )
    {
        if( MacCtx.RxSlot == RX_SLOT_WIN_1 )
        {
            if( MacCtx.NodeAckRequested == true )
            {
                MacCtx.McpsConfirm.Status = rx1EventInfoStatus;
            }
            LoRaMacConfirmQueueSetStatusCmn( rx1EventInfoStatus );
            // GitHub issue #757 "Stack stucks in LORAMAC_TX_RUNNING state if RX1 error occurs"
            if( !TimerExists( &MacCtx.RxWindowTimer2 ) )
            {
                isRx1ErrAbort = true;
                MacCtx.MacFlags.Bits.MacDone = 1;
            }
        }
        else
        {
            if( MacCtx.NodeAckRequested == true )
            {
                MacCtx.McpsConfirm.Status = rx2EventInfoStatus;
            }
            LoRaMacConfirmQueueSetStatusCmn( rx2EventInfoStatus );

            if( MacCtx.NvmCtx->DeviceClass != CLASS_C )
            {
                MacCtx.MacFlags.Bits.MacDone = 1;
            }
        }
    }
    // Radio low power; process it before updating MacCtx.RxSlot (calling UpdateRxSlotIdleState())
    if( isRx1ErrAbort == true )
    {
        LORAMAC_RADIOSLEEP_RXERRTO_RX1ERR_ABORT( 0 );
    }
    else
    {
        LORAMAC_RADIOSLEEP_RXERRTO( 0 );
    }

    UpdateRxSlotIdleState( );

    // GitHub issue #757 "Stack stucks in LORAMAC_TX_RUNNING state if RX1 error occurs"
    CRITICAL_SECTION_END();
}

static void ProcessRadioRxError( void )
{
    LORAWAN_RXC_STORE_U8(last_rx_error_slot, (uint8_t)MacCtx.RxSlot);  /* T-V3.1 #14 */
    /* r13 Phase 1 — close timestamp for RxError on the active slot. */
    {
        uint32_t cyc = *(volatile uint32_t *)0xE0001004UL;
        if( MacCtx.RxSlot == RX_SLOT_WIN_1 )
        {
            __atomic_store_n((uint32_t *)&lorawan_rxc_diag.last_rx1_close_cyc,
                             cyc, __ATOMIC_RELAXED);
        }
        else if( MacCtx.RxSlot == RX_SLOT_WIN_2 )
        {
            __atomic_store_n((uint32_t *)&lorawan_rxc_diag.last_rx2_close_cyc,
                             cyc, __ATOMIC_RELAXED);
        }
    }
    HandleRadioRxErrorTimeout( LORAMAC_EVENT_INFO_STATUS_RX1_ERROR, LORAMAC_EVENT_INFO_STATUS_RX2_ERROR );
}

static void ProcessRadioRxTimeout( void )
{
    LORAWAN_RXC_STORE_U8(last_rx_timeout_slot, (uint8_t)MacCtx.RxSlot);  /* T-V3.1 #13 */
    /* r13 Phase 1 — close timestamp for RxTimeout on the active slot. */
    {
        uint32_t cyc = *(volatile uint32_t *)0xE0001004UL;
        if( MacCtx.RxSlot == RX_SLOT_WIN_1 )
        {
            __atomic_store_n((uint32_t *)&lorawan_rxc_diag.last_rx1_close_cyc,
                             cyc, __ATOMIC_RELAXED);
        }
        else if( MacCtx.RxSlot == RX_SLOT_WIN_2 )
        {
            __atomic_store_n((uint32_t *)&lorawan_rxc_diag.last_rx2_close_cyc,
                             cyc, __ATOMIC_RELAXED);
        }
    }
    HandleRadioRxErrorTimeout( LORAMAC_EVENT_INFO_STATUS_RX1_TIMEOUT, LORAMAC_EVENT_INFO_STATUS_RX2_TIMEOUT );
}

static void LoRaMacHandleIrqEvents( void )
{
    LoRaMacRadioEvents_t events;

    CRITICAL_SECTION_BEGIN( );
    events = LoRaMacRadioEvents;
    LoRaMacRadioEvents.Value = 0;
    CRITICAL_SECTION_END( );

    if( events.Value != 0 )
    {
        if( events.Events.TxDone == 1 )
        {
            ProcessRadioTxDone( );
        }
        if( events.Events.RxDone == 1 )
        {
            ProcessRadioRxDone( );
        }
        if( events.Events.TxTimeout == 1 )
        {
            ProcessRadioTxTimeout( );
        }
        if( events.Events.RxError == 1 )
        {
            ProcessRadioRxError( );
        }
        if( events.Events.RxTimeout == 1 )
        {
            ProcessRadioRxTimeout( );
        }
    }
}

bool LoRaMacIsBusy( void )
{
    if( LoRaMacRadioEvents.Events.RxProcessPending == 1 )
    {
        return true;
    }

    if( ( MacCtx.MacState == LORAMAC_IDLE ) &&
        ( MacCtx.AllowRequests == LORAMAC_REQUEST_HANDLING_ON ) )
    {
        return false;
    }
    return true;
}

static void LoRaMacEnableRequests( LoRaMacRequestHandling_t requestState )
{
    MacCtx.AllowRequests = requestState;
}

static void LoRaMacHandleRequestEvents( void )
{
    // Handle events
    LoRaMacFlags_t reqEvents = MacCtx.MacFlags;

    // If beacon acquisition is stopped in LoRaMacStop(), event (MlmeReq) will set in LORAMAC_STOPPED state.
    if( ( MacCtx.MacState == LORAMAC_IDLE ) ||
        ( ( MacCtx.MacState == LORAMAC_STOPPED ) && ( MacCtx.MacFlags.Bits.MlmeReq == 1 ) ) )
    {
        // Update event bits
        MacCtx.MacFlags.Bits.McpsReq = 0;
        MacCtx.MacFlags.Bits.MlmeReq = 0;

        // Allow requests again
        LoRaMacEnableRequests( LORAMAC_REQUEST_HANDLING_ON );

        // Handle callbacks
        if( reqEvents.Bits.McpsReq == 1 )
        {
            MacCtx.MacPrimitives->MacMcpsConfirm( &MacCtx.McpsConfirm );
        }

        if( reqEvents.Bits.MlmeReq == 1 )
        {
            LoRaMacConfirmQueueHandleCb( &MacCtx.MlmeConfirm );
            if( LoRaMacConfirmQueueGetCnt( ) > 0 )
            {
                MacCtx.MacFlags.Bits.MlmeReq = 1;
            }
        }

#ifdef LORAMAC_CLASSB_ENABLED
        // Start beaconing again
        LoRaMacClassBResumeBeaconing( );
#endif
        // Procedure done. Reset variables.
        MacCtx.MacFlags.Bits.MacDone = 0;
    }
}

static void LoRaMacHandleScheduleUplinkEvent( void )
{
    // Handle events
    if( MacCtx.MacState == LORAMAC_IDLE )
    {
        // Verify if sticky MAC commands are pending or not
        bool isStickyMacCommandPending = false;
        LoRaMacCommandsStickyCmdsPending( &isStickyMacCommandPending );
        if( isStickyMacCommandPending == true )
        {// Setup MLME indication
            SetMlmeScheduleUplinkIndication( );
        }
    }
}

static void LoRaMacHandleIndicationEvents( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    // Handle MLME indication
    if( MacCtx.MacFlags.Bits.MlmeInd == 1 )
    {
        MacCtx.MacFlags.Bits.MlmeInd = 0;
        MacCtx.MacPrimitives->MacMlmeIndication( &MacCtx.MlmeIndication );
    }
#endif

    if( MacCtx.MacFlags.Bits.MlmeSchedUplinkInd == 1 )
    {
        MlmeIndication_t schduleUplinkIndication;
        schduleUplinkIndication.MlmeIndication = MLME_SCHEDULE_UPLINK;
        schduleUplinkIndication.Status = LORAMAC_EVENT_INFO_STATUS_OK;

        MacCtx.MacPrimitives->MacMlmeIndication( &schduleUplinkIndication );
        MacCtx.MacFlags.Bits.MlmeSchedUplinkInd = 0;
    }

    // Handle MCPS indication
    if( MacCtx.MacFlags.Bits.McpsInd == 1 )
    {
        MacCtx.MacFlags.Bits.McpsInd = 0;
        MacCtx.MacPrimitives->MacMcpsIndication( &MacCtx.McpsIndication );
    }
}

static void LoRaMacHandleMcpsRequest( void )
{
    // Handle MCPS uplinks
    if( MacCtx.MacFlags.Bits.McpsReq == 1 )
    {
        bool stopRetransmission = false;
        bool waitForRetransmission = false;

        if( MacCtx.McpsConfirm.McpsRequest == MCPS_UNCONFIRMED )
        {
            stopRetransmission = CheckRetransUnconfirmedUplink( );
        }
        else if( MacCtx.McpsConfirm.McpsRequest == MCPS_CONFIRMED )
        {
            if( MacCtx.AckTimeoutRetry == true )
            {
                stopRetransmission = CheckRetransConfirmedUplink( );

#if (LORAMAC_VERSION >= 0x01010000)
                if( MacCtx.NvmCtx->Version.Fields.Minor == 0 )
#endif
                {
                    if( stopRetransmission == false )
                    {
                        AckTimeoutRetriesProcess( );
                    }
                    else
                    {
                        AckTimeoutRetriesFinalize( );
                    }
                }
            }
            else
            {
                waitForRetransmission = true;
            }
        }

        if( stopRetransmission == true )
        {// Stop retransmission
            TimerStop( &MacCtx.TxDelayedTimer );
            MacCtx.MacState &= ~LORAMAC_TX_DELAYED;
            StopRetransmission( );
        }
        else if( waitForRetransmission == false )
        {// Arrange further retransmission
            MacCtx.MacFlags.Bits.MacDone = 0;
            // Reset the state of the AckTimeout
            MacCtx.AckTimeoutRetry = false;
            // Sends the same frame again
            ProcessTimerTxDelayed();
        }
    }
}

static void LoRaMacHandleMlmeRequest( void )
{
    // Handle join request
    if( MacCtx.MacFlags.Bits.MlmeReq == 1 )
    {
        if( LoRaMacConfirmQueueIsCmdActive( MLME_JOIN ) == true )
        {
            if( LoRaMacConfirmQueueGetStatus( MLME_JOIN ) == LORAMAC_EVENT_INFO_STATUS_OK )
            {// Node joined successfully
                MacCtx.ChannelsNbTransCounter = 0;
            }
            MacCtx.MacState &= ~LORAMAC_TX_RUNNING;
        }
        else if( LoRaMacConfirmQueueIsCmdActive( MLME_TXCW ) == true )
        {
            MacCtx.MacState &= ~LORAMAC_TX_RUNNING;
        }
    }
}

#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
static bool CheckForMinimumAbpDatarate( bool adr, ActivationType_t activation, bool datarateChanged )
{
    if( ( adr == true ) &&
        ( activation == ACTIVATION_TYPE_ABP ) &&
        ( datarateChanged == false ) )
    {
        return true;
    }
    return false;
}
#endif  // LORAMAC_ACTMODE_ABP_ENABLED
#endif

#ifdef LORAMAC_CLASSB_ENABLED
static uint8_t LoRaMacCheckForBeaconAcquisition( void )
{
    if( ( LoRaMacConfirmQueueIsCmdActive( MLME_BEACON_ACQUISITION ) == true ) &&
        ( MacCtx.MacFlags.Bits.McpsReq == 0 ) )
    {
        if( MacCtx.MacFlags.Bits.MlmeReq == 1 )
        {
            MacCtx.MacState &= ~LORAMAC_TX_RUNNING;
            return 0x01;
        }
    }
    return 0x00;
}
#endif

static void LoRaMacCheckForRxAbort( void )
{
    // A error occurs during receiving
    if( ( MacCtx.MacState & LORAMAC_RX_ABORT ) == LORAMAC_RX_ABORT )
    {
        MacCtx.MacState &= ~LORAMAC_RX_ABORT;
        MacCtx.MacState &= ~LORAMAC_TX_RUNNING;
    }
}

#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
static bool LoRaMacHandleResponseTimeout( TimerTime_t timeoutInMs, TimerTime_t startTimeInMs )
{
    if( startTimeInMs != 0 )
    {
        TimerTime_t elapsedTime = TimerGetElapsedTime( startTimeInMs );
        if( elapsedTime > timeoutInMs )
        {
            MacCtx.NvmCtx->SrvAckRequested = false;
            return true;
        }
    }
    return false;
}
#endif

void LoRaMacProcess( void )
{
#ifdef LORAMAC_CLASSB_ENABLED
    uint8_t noTx = 0x00;
#endif

    // RF IRQ
    if( Radio.IrqProcess != NULL )
    {
        Radio.IrqProcess( );
    }

    LoRaMacHandleIrqEvents( );
#ifdef LORAMAC_CLASSB_ENABLED
    LoRaMacClassBProcess( );
#endif
    LoRaMacHandleTimerEvents( );

    // MAC proceeded a state and is ready to check
    if( MacCtx.MacFlags.Bits.MacDone == 1 )
    {
        LoRaMacEnableRequests( LORAMAC_REQUEST_HANDLING_OFF );
        LoRaMacCheckForRxAbort( );

#ifdef LORAMAC_CLASSB_ENABLED
        // An error occurs during transmitting
        if( IsRequestPending( ) > 0 )
        {
            noTx = LoRaMacCheckForBeaconAcquisition( );
        }
#endif

#ifdef LORAMAC_CLASSB_ENABLED
        if( noTx == 0x00 )
#endif
        {
            LoRaMacHandleMlmeRequest( );
            LoRaMacHandleMcpsRequest( );
        }
        LoRaMacHandleRequestEvents( );
        LoRaMacHandleScheduleUplinkEvent( );
        LoRaMacEnableRequests( LORAMAC_REQUEST_HANDLING_ON );

        CallNvmCtxCallback();
    }
    LoRaMacHandleIndicationEvents( );

    if( MacCtx.MacState != LORAMAC_STOPPED )
    {
#if (LORAMAC_MAX_MC_CTX > 0)
        if( ( MacCtx.RxSlot == RX_SLOT_WIN_CLASS_C ) ||
            ( MacCtx.RxSlot == RX_SLOT_WIN_CLASS_C_MULTICAST) )
#else
        if( MacCtx.RxSlot == RX_SLOT_WIN_CLASS_C )
#endif
        {
            OpenContinuousRxCWindow( );
        }
    }
}

static void LoRaMacHandleTimerEvents( void )
{
    LoRaMacTimerEvents_t events;

    CRITICAL_SECTION_BEGIN( );
    events = LoRaMacTimerEvents;
    LoRaMacTimerEvents.Value = 0;
    CRITICAL_SECTION_END( );

    if( events.Value != 0 )
    {
        if( events.Events.RxWin1Timer == 1 )
        {
            ProcessTimerRxOnWindow1();
        }
        if( events.Events.RxWin2Timer == 1 )
        {
            ProcessTimerRxOnWindow2();
        }
        if( events.Events.TxDelayedTimer == 1 )
        {
            ProcessTimerTxDelayed();
        }
    }
}

static void ProcessTimerTxDelayed( void )
{
    LoRaMacStatus_t          status;
    LoRaMacEventInfoStatus_t eventStatus;

    MacCtx.MacState &= ~LORAMAC_TX_DELAYED;

#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
    if( LoRaMacHandleResponseTimeout( REGION_COMMON_CLASS_B_C_RESP_TIMEOUT,
                                      MacCtx.ResponseTimeoutStartTime ) == true )
    {
        // Skip retransmission
        return;
    }
#endif

    // Schedule frame, allow delayed frame transmissions
    status = ScheduleTx( true );
    switch( status )
    {
        case LORAMAC_STATUS_OK:
        {
            break;
        }
        default:
        {
            // Stop retransmission attempt
            //    Note: Retransmission is stopped in case of NO_FREE_CHANNEL
            //    Note: Need to convert LoRaMacStatus_t into LoRaMacEventInfoStatus_t
            if (status == LORAMAC_STATUS_LENGTH_ERROR)
            {
                // The MAC could not retransmit a frame since the MAC decreased the datarate.
                eventStatus = LORAMAC_EVENT_INFO_STATUS_TX_DR_PAYLOAD_SIZE_ERROR;
            }
            else
            {
                // Other error such as overwrap with ping slots, beacon slot, duty cycle restricted, etc
                eventStatus = LORAMAC_EVENT_INFO_STATUS_ERROR;
            }
            MacCtx.McpsConfirm.Datarate = MacCtx.NvmCtx->MacParams.ChannelsDatarate;
            MacCtx.McpsConfirm.NbRetries = MacCtx.AckTimeoutRetriesCounter;
            MacCtx.McpsConfirm.Status = eventStatus;
            LoRaMacConfirmQueueSetStatusCmn( eventStatus );
            StopRetransmission( );
            break;
        }
    }
}

static void OnTxDelayedTimerEvent( void )
{
    LoRaMacTimerEvents.Events.TxDelayedTimer = 1;
}


static void ProcessTimerRxOnWindow1( void )
{
    MacCtx.RxWindow1Config.Channel = MacCtx.Channel;
    MacCtx.RxWindow1Config.DrOffset = MacCtx.NvmCtx->MacParams.Rx1DrOffset;
    MacCtx.RxWindow1Config.DownlinkDwellTime = MacCtx.NvmCtx->MacParams.DownlinkDwellTime;
    MacCtx.RxWindow1Config.RxContinuous = false;
    MacCtx.RxWindow1Config.RxSlot = RX_SLOT_WIN_1;
    MacCtx.RxWindow1Config.NetworkActivation = MacCtx.NvmCtx->NetworkActivation;

    RxWindowSetup( &MacCtx.RxWindowTimer1, &MacCtx.RxWindow1Config );
}

static void OnRxWindow1TimerEvent( void )
{
    LoRaMacTimerEvents.Events.RxWin1Timer = 1;
}


static void ProcessTimerRxOnWindow2( void )
{
    MacCtx.RxWindow2Config.Channel = MacCtx.Channel;
    MacCtx.RxWindow2Config.Frequency = MacCtx.NvmCtx->MacParams.Rx2Channel.Frequency;
    MacCtx.RxWindow2Config.DownlinkDwellTime = MacCtx.NvmCtx->MacParams.DownlinkDwellTime;
    MacCtx.RxWindow2Config.RxContinuous = false;
    MacCtx.RxWindow2Config.RxSlot = RX_SLOT_WIN_2;
    MacCtx.RxWindow2Config.NetworkActivation = MacCtx.NvmCtx->NetworkActivation;

    RxWindowSetup( &MacCtx.RxWindowTimer2, &MacCtx.RxWindow2Config );
}

static void OnRxWindow2TimerEvent( void )
{
    // Check if we are processing Rx1 window.
    // If yes, we don't setup the Rx2 window.
    if( MacCtx.RxSlot == RX_SLOT_WIN_1 )
    {
        /* r13 Phase 1 — cumulative count of RX2 windows skipped because RX1
         * was still active when the RX2 timer fired. Never reset. */
        __atomic_fetch_add((uint32_t *)&lorawan_rxc_diag.rx2_skipped_total,
                           1u, __ATOMIC_RELAXED);
        return;
    }

    LoRaMacTimerEvents.Events.RxWin2Timer = 1;
}


static void OnAckTimeoutTimerEvent( void )
{
    TimerStop( &MacCtx.AckTimeoutTimer );

    if( MacCtx.NodeAckRequested == true )
    {
        MacCtx.AckTimeoutRetry = true;
    }
    if( MacCtx.NvmCtx->DeviceClass == CLASS_C )
    {
        MacCtx.MacFlags.Bits.MacDone = 1;
    }
    OnMacProcessNotify();
}

static LoRaMacCryptoStatus_t GetFCntDown( AddressIdentifier_t addrID, FType_t fType, LoRaMacMessageData_t* macMsg,
                                          uint16_t maxFCntGap, FCntIdentifier_t* fCntID, uint32_t* currentDown )
{
    LoRaMacCryptoStatus_t   ret;
#if LORAMAC_CHECK_MCFCNT_RANGE
    MulticastCtx_t          *pMcListEntry;
#endif

    if( ( macMsg == NULL ) || ( fCntID == NULL ) ||
        ( currentDown == NULL ) )
    {
        return LORAMAC_CRYPTO_ERROR_NPE;
    }

    // Determine the frame counter identifier and choose counter from FCntList
    switch( addrID )
    {
        case UNICAST_DEV_ADDR:
#if (LORAMAC_VERSION >= 0x01010000)
            if( MacCtx.NvmCtx->Version.Fields.Minor == 1 )
            {
                if( ( fType == FRAME_TYPE_A ) || ( fType == FRAME_TYPE_D ) )
                {
                    *fCntID = A_FCNT_DOWN;
                }
                else
                {
                    *fCntID = N_FCNT_DOWN;
                }
            }
            else
#endif
            { // For LoRaWAN 1.0.X
                *fCntID = FCNT_DOWN;
            }
            break;
#if (LORAMAC_MAX_MC_CTX > 0)
        case MULTICAST_0_ADDR:
            *fCntID = MC_FCNT_DOWN_0;
            break;
#endif
#if (LORAMAC_MAX_MC_CTX > 1)
        case MULTICAST_1_ADDR:
            *fCntID = MC_FCNT_DOWN_1;
            break;
#endif
#if (LORAMAC_MAX_MC_CTX > 2)
        case MULTICAST_2_ADDR:
            *fCntID = MC_FCNT_DOWN_2;
            break;
#endif
#if (LORAMAC_MAX_MC_CTX > 3)
        case MULTICAST_3_ADDR:
            *fCntID = MC_FCNT_DOWN_3;
            break;
#endif
        default:
            return LORAMAC_CRYPTO_FAIL_FCNT_ID;
    }

    ret = LoRaMacCryptoGetFCntDown( *fCntID, maxFCntGap, macMsg->FHDR.FCnt, currentDown );
#if (LORAMAC_MAX_MC_CTX > 0)
#if LORAMAC_CHECK_MCFCNT_RANGE
    if( ret == LORAMAC_CRYPTO_SUCCESS )
    {
        // in case of multicast, need to check if downlink frame counte is in min and max
        if( ( addrID == MULTICAST_0_ADDR ) || ( addrID == MULTICAST_1_ADDR ) ||
            ( addrID == MULTICAST_2_ADDR ) || ( addrID == MULTICAST_3_ADDR ) )
        {
            pMcListEntry = &(MacCtx.NvmCtx->MulticastChannelList[addrID]);  // addrID = groupID
            if( ( pMcListEntry->ChannelParams.FCountMin > (*currentDown) ) ||
                ( pMcListEntry->ChannelParams.FCountMax < (*currentDown)  ) )
            {
                ret = LORAMAC_CRYPTO_ERROR;
            }
        }
    }
#endif
#endif  // LORAMAC_MAX_MC_CTX

    return ret;
}

static LoRaMacStatus_t SwitchClass( DeviceClass_t deviceClass )
{
    LoRaMacStatus_t status = LORAMAC_STATUS_PARAMETER_INVALID;

    switch( MacCtx.NvmCtx->DeviceClass )
    {
        case CLASS_A:
        {
            if( deviceClass == CLASS_A )
            {
                // Revert back RxC parameters
                MacCtx.NvmCtx->MacParams.RxCChannel = MacCtx.NvmCtx->MacParams.Rx2Channel;
                status = LORAMAC_STATUS_OK;
            }
            if( deviceClass == CLASS_B )
            {
#ifdef LORAMAC_CLASSB_ENABLED
                status = LoRaMacClassBSwitchClass( deviceClass );
                if( status == LORAMAC_STATUS_OK )
                {
                    MacCtx.NvmCtx->DeviceClass = deviceClass;
                }
#else
                status = LORAMAC_STATUS_SERVICE_UNKNOWN;
#endif
            }

            if( deviceClass == CLASS_C )
            {
                // Stop beaconing
#ifdef LORAMAC_CLASSB_ENABLED
                LoRaMacClassBStopBeaconig();
                if( LoRaMacConfirmQueueIsCmdActive( MLME_BEACON_ACQUISITION ) == true )
                {
                    MacCtx.MacFlags.Bits.MacDone = 1;
                }
#endif
                MacCtx.NvmCtx->DeviceClass = deviceClass;

                MacCtx.RxWindowCConfig = MacCtx.RxWindow2Config;
                MacCtx.RxWindowCConfig.RxSlot = RX_SLOT_WIN_CLASS_C;

#if (LORAMAC_MAX_MC_CTX > 0)
                for( int8_t i = 0; i < LORAMAC_MAX_MC_CTX; i++ )
                {
                    if( ( MacCtx.NvmCtx->MulticastChannelList[i].ChannelParams.IsEnabled == true ) &&
                        ( MacCtx.NvmCtx->MulticastChannelList[i].ChannelParams.Class == CLASS_C) )
                    {
                        MacCtx.NvmCtx->MacParams.RxCChannel.Frequency = MacCtx.NvmCtx->MulticastChannelList[i].ChannelParams.RxParams.ClassC.Frequency;
                        MacCtx.NvmCtx->MacParams.RxCChannel.Datarate = MacCtx.NvmCtx->MulticastChannelList[i].ChannelParams.RxParams.ClassC.Datarate;

                        MacCtx.RxWindowCConfig.Channel = MacCtx.Channel;
                        MacCtx.RxWindowCConfig.Frequency = MacCtx.NvmCtx->MacParams.RxCChannel.Frequency;
                        MacCtx.RxWindowCConfig.DownlinkDwellTime = MacCtx.NvmCtx->MacParams.DownlinkDwellTime;
                        MacCtx.RxWindowCConfig.RxSlot = RX_SLOT_WIN_CLASS_C_MULTICAST;
#ifdef LORAMAC_RXC_CONTINUOUS_ENABLED
                        MacCtx.RxWindowCConfig.RxContinuous = true;
#else
                        MacCtx.RxWindowCConfig.RxContinuous = false;
#endif
                        break;
                    }
                }
#endif  // LORAMAC_MAX_MC_CTX

                // Set the NodeAckRequested indicator to default
                MacCtx.NodeAckRequested = false;
                // nop: do not sleep here
                OpenContinuousRxCWindow( );

                status = LORAMAC_STATUS_OK;
            }
            break;
        }
#ifdef LORAMAC_CLASSB_ENABLED
        case CLASS_B:
        {
            if( deviceClass == CLASS_B )
            {
                status = LORAMAC_STATUS_OK;
            }
            else
            {
                status = LoRaMacClassBSwitchClass( deviceClass );
            }
            if( status == LORAMAC_STATUS_OK )
            {
                MacCtx.NvmCtx->DeviceClass = deviceClass;
            }
            break;
        }
#endif
        case CLASS_C:
        {
            if( deviceClass == CLASS_A )
            {
                MacCtx.NvmCtx->DeviceClass = deviceClass;

                LORAMAC_RADIOSLEEP_SWITCHCLASS_C2A( 0 );
                // Update RxSlot to NONE not to re-open continuous Rx window
                UpdateRxSlotIdleState();

                status = LORAMAC_STATUS_OK;
            }
            else if( deviceClass == CLASS_B )
            {
                status = LORAMAC_STATUS_SERVICE_UNKNOWN;
            }
            else if( deviceClass == CLASS_C )
            {
                status = LORAMAC_STATUS_OK;
            }
            break;
        }
        default:
        {
            status = LORAMAC_STATUS_PARAMETER_INVALID;
            break;
        }
    }

    return status;
}

static uint8_t GetMaxAppPayloadWithoutFOptsLength( int8_t datarate )
{
    GetPhyParams_t getPhy;
    PhyParam_t phyParam;

    // Setup PHY request
    getPhy.UplinkDwellTime = MacCtx.NvmCtx->MacParams.UplinkDwellTime;
    getPhy.Datarate = datarate;
    getPhy.Attribute = PHY_MAX_PAYLOAD;
    phyParam = RegionGetPhyParam( MacCtx.NvmCtx->Region, &getPhy );

    return phyParam.Value;
}

static bool ValidatePayloadLength( uint8_t lenN, int8_t datarate, uint8_t fOptsLen )
{
    uint16_t maxN = 0;
    uint16_t payloadSize = 0;

    maxN = GetMaxAppPayloadWithoutFOptsLength( datarate );

    // Calculate the resulting payload size
    payloadSize = ( lenN + fOptsLen );

    // Validation of the application payload size
    if( ( payloadSize <= maxN ) && ( payloadSize <= LORAMAC_PHY_MAXPAYLOAD ) )
    {
        return true;
    }
    return false;
}

static void SetMlmeScheduleUplinkIndication( void )
{
    MacCtx.MacFlags.Bits.MlmeSchedUplinkInd = 1;
}

static void ProcessMacCommands( uint8_t *payload, uint8_t macIndex, uint8_t commandsSize, int8_t snr, LoRaMacRxSlot_t rxSlot )
{
    uint8_t status = 0;
    bool adrBlockFound = false;
    uint8_t macCmdPayload[2] = { 0x00, 0x00 };
    uint8_t payloadSizeLinkAdrReq;
    int8_t stmp8;

#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)  // It may be applicable to LW1.0.3
    if( ( rxSlot != RX_SLOT_WIN_1 ) && ( rxSlot != RX_SLOT_WIN_2 ) )
    {
        // Do only parse MAC commands for Class A RX windows
        return;
    }
#endif

    while( macIndex < commandsSize )
    {
        // Decode Frame MAC commands
        switch( payload[macIndex++] )
        {
            case SRV_MAC_LINK_CHECK_ANS:
            {
                // Check command length
                if( ( commandsSize - macIndex ) < (uint8_t)( LORAMAC_COMMAND_LEN_LINK_CHECK_ANS - 1U ) )
                {
                    macIndex = commandsSize;
                    break;
                }
                if( LoRaMacConfirmQueueIsCmdActive( MLME_LINK_CHECK ) == true )
                {
                    LoRaMacConfirmQueueSetStatus( LORAMAC_EVENT_INFO_STATUS_OK, MLME_LINK_CHECK );
                    MacCtx.MlmeConfirm.DemodMargin = payload[macIndex++];
                    MacCtx.MlmeConfirm.NbGateways = payload[macIndex++];
                }
                else
                {
                    // Skip command
                    macIndex += ( LORAMAC_COMMAND_LEN_LINK_CHECK_ANS - 1U );
                }
                break;
            }
            case SRV_MAC_LINK_ADR_REQ:
            {
                // Check command length
                uint8_t chkIndex = macIndex - 1;
                do
                {
                    if( ( commandsSize - chkIndex ) < LORAMAC_COMMAND_LEN_LINK_ADR_REQ )
                    {
                        macIndex = commandsSize;
                        break;  // exit from do-while loop
                    }
                    chkIndex += LORAMAC_COMMAND_LEN_LINK_ADR_REQ;
                } while( ( commandsSize > chkIndex ) && ( payload[chkIndex] == SRV_MAC_LINK_ADR_REQ ) );

                if( macIndex == commandsSize )
                {
                    break;
                }
                LinkAdrReqParams_t linkAdrReq;
                int8_t linkAdrDatarate = DR_0;
                int8_t linkAdrTxPower = TX_POWER_0;
                uint8_t linkAdrNbRep = 0;
                uint8_t linkAdrNbBytesParsed = 0;

#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
                // There is a fundamental difference in reporting the status
                // of the LinkAdrRequests when ADR is on or off. When ADR is on, every
                // LinkAdrAns contains the same value. This does not hold when ADR is off,
                // where every LinkAdrAns requires an individual status.
                if( MacCtx.NvmCtx->AdrCtrlOn == false )
                {
                    // When ADR is off, this function will loop over the individual LinkAdrRequests
                    // and will call RegionLinkAdrReq for each individually, as every request
                    // requires an individual answer.
                    // When ADR is off, the function RegionLinkAdrReq ignores the new values for
                    // ChannelsDatarate, ChannelsTxPower and ChannelsNbTrans.
                    payloadSizeLinkAdrReq = LORAMAC_COMMAND_LEN_LINK_ADR_REQ;
                    adrBlockFound = false;  // reset blocking
                }
                else
#endif
                {
                    // The function RegionLinkAdrReq will take care
                    // about the parsing and interpretation of the LinkAdrRequest block and
                    // it provides one status which shall be applied to every LinkAdrAns
                    payloadSizeLinkAdrReq = commandsSize - ( macIndex - 1 );
                }

                if( adrBlockFound == false )
                {
                    adrBlockFound = true;

                    // Fill parameter structure
                    linkAdrReq.Payload = &payload[macIndex - 1];
                    linkAdrReq.PayloadSize = payloadSizeLinkAdrReq;
                    linkAdrReq.AdrEnabled = MacCtx.NvmCtx->AdrCtrlOn;
                    linkAdrReq.UplinkDwellTime = MacCtx.NvmCtx->MacParams.UplinkDwellTime;
                    linkAdrReq.CurrentDatarate = MacCtx.NvmCtx->MacParams.ChannelsDatarate;
                    linkAdrReq.CurrentTxPower = MacCtx.NvmCtx->MacParams.ChannelsTxPower;
                    linkAdrReq.CurrentNbRep = MacCtx.NvmCtx->MacParams.ChannelsNbTrans;
                    linkAdrReq.Version = MacCtx.NvmCtx->Version;

                    // Process the ADR requests
                    status = RegionLinkAdrReq( MacCtx.NvmCtx->Region, &linkAdrReq, &linkAdrDatarate,
                                               &linkAdrTxPower, &linkAdrNbRep, &linkAdrNbBytesParsed );

                    if( ( status & 0x07 ) == 0x07 )
                    {
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
                        if( linkAdrDatarate > MacCtx.NvmCtx->MacParams.ChannelsDatarate )
                        {
                            MacCtx.NvmCtx->ChannelsDatarateChangedLinkAdrReq = true;
                        }
#endif
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
                        if( MacCtx.NvmCtx->MacParams.ChannelsDatarate != linkAdrDatarate )
                        {
                            MacCtx.NvmCtx->MacParams.ChannelsDatarate = linkAdrDatarate;
                            LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_CHANNELS_DATARATE );
                        }
                        if( MacCtx.NvmCtx->MacParams.ChannelsTxPower != linkAdrTxPower )
                        {
                            MacCtx.NvmCtx->MacParams.ChannelsTxPower = linkAdrTxPower;
                            LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_CHANNELS_TXPOWER );
                        }
                        if( MacCtx.NvmCtx->MacParams.ChannelsNbTrans != linkAdrNbRep )
                        {
                            MacCtx.NvmCtx->MacParams.ChannelsNbTrans = linkAdrNbRep;
                            LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_CHANNELS_NB_TRANS );
                        }
                        LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_CHANNELS_MASK );
#else
                        MacCtx.NvmCtx->MacParams.ChannelsDatarate = linkAdrDatarate;
                        MacCtx.NvmCtx->MacParams.ChannelsTxPower = linkAdrTxPower;
                        MacCtx.NvmCtx->MacParams.ChannelsNbTrans = linkAdrNbRep;
#endif
                    }

                    // Add the answers to the buffer
                    for( uint8_t i = 0; i < ( linkAdrNbBytesParsed / 5 ); i++ )
                    {
                        LoRaMacCommandsAddCmd( MOTE_MAC_LINK_ADR_ANS, &status, 1 );
                    }
                    // Update MAC index
                    macIndex += linkAdrNbBytesParsed - 1;
                }
                else
                {
                    macIndex = chkIndex;
                }
                break;
            }
            case SRV_MAC_DUTY_CYCLE_REQ:
            {
                // Check command length
                if( ( commandsSize - macIndex ) < (uint8_t)( LORAMAC_COMMAND_LEN_DUTY_CYCLE_REQ - 1U ) )
                {
                    macIndex = commandsSize;
                    break;
                }
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
                if( MacCtx.NvmCtx->MaxDCycle != (payload[macIndex] & 0x0F) )
                {
                    MacCtx.NvmCtx->MaxDCycle = payload[macIndex++] & 0x0F;
                    MacCtx.NvmCtx->AggregatedDCycle = 1 << MacCtx.NvmCtx->MaxDCycle;
                    LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_MAX_DCYCLE );
                }
#else
                MacCtx.NvmCtx->MaxDCycle = payload[macIndex++] & 0x0F;
                MacCtx.NvmCtx->AggregatedDCycle = 1 << MacCtx.NvmCtx->MaxDCycle;
#endif

                LoRaMacCommandsAddCmd( MOTE_MAC_DUTY_CYCLE_ANS, macCmdPayload, 0 );
                break;
            }
            case SRV_MAC_RX_PARAM_SETUP_REQ:
            {
                // Check command length
                if( ( commandsSize - macIndex ) < (uint8_t)( LORAMAC_COMMAND_LEN_RX_PARAM_SETUP_REQ - 1U ) )
                {
                    macIndex = commandsSize;
                    break;
                }
                RxParamSetupReqParams_t rxParamSetupReq;
                status = 0x07;

                rxParamSetupReq.DrOffset = ( payload[macIndex] >> 4 ) & 0x07;
                rxParamSetupReq.Datarate = payload[macIndex] & 0x0F;
                macIndex++;

#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
                if( rxParamSetupReq.Datarate == 0x0F )
                {
                    // Keep the current datarate
                    rxParamSetupReq.Datarate = MacCtx.NvmCtx->MacParams.Rx2Channel.Datarate;
                }
#endif

                LoRaMacCommonCopyArrayToUint32( &(rxParamSetupReq.Frequency), &(payload[macIndex]), 3 );
                rxParamSetupReq.Frequency *= 100;
                macIndex += 3;

                // Perform request on region
                status = RegionRxParamSetupReq( MacCtx.NvmCtx->Region, &rxParamSetupReq );

                if( ( status & 0x07 ) == 0x07 )
                {
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
                    if( MacCtx.NvmCtx->MacParams.Rx1DrOffset != rxParamSetupReq.DrOffset )
                    {
                        MacCtx.NvmCtx->MacParams.Rx1DrOffset = rxParamSetupReq.DrOffset;
                        LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_RX1_DROFFSET );
                    }
                    if( MacCtx.NvmCtx->MacParams.Rx2Channel.Frequency != rxParamSetupReq.Frequency )
                    {
                        MacCtx.NvmCtx->MacParams.Rx2Channel.Frequency = rxParamSetupReq.Frequency;
                        LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_RX2_FREQUENCY );
                    }
                    if( MacCtx.NvmCtx->MacParams.Rx2Channel.Datarate != rxParamSetupReq.Datarate )
                    {
                        MacCtx.NvmCtx->MacParams.Rx2Channel.Datarate = rxParamSetupReq.Datarate;
                        LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_RX2_DATARATE );
                    }
#else
                    MacCtx.NvmCtx->MacParams.Rx1DrOffset = rxParamSetupReq.DrOffset;
                    MacCtx.NvmCtx->MacParams.Rx2Channel.Frequency = rxParamSetupReq.Frequency;
                    MacCtx.NvmCtx->MacParams.Rx2Channel.Datarate = rxParamSetupReq.Datarate;
                    MacCtx.NvmCtx->MacParams.RxCChannel.Datarate = rxParamSetupReq.Datarate;
                    MacCtx.NvmCtx->MacParams.RxCChannel.Frequency = rxParamSetupReq.Frequency;
#endif
                }
                macCmdPayload[0] = status;
                LoRaMacCommandsAddCmd( MOTE_MAC_RX_PARAM_SETUP_ANS, macCmdPayload, 1 );
                // Setup indication to inform the application
                SetMlmeScheduleUplinkIndication( );
                break;
            }
            case SRV_MAC_DEV_STATUS_REQ:
            {
                // Check command length ... not necessary
                uint8_t batteryLevel = BAT_LEVEL_NO_MEASURE;
                if( ( MacCtx.MacCallbacks != NULL ) && ( MacCtx.MacCallbacks->GetBatteryLevel != NULL ) )
                {
                    batteryLevel = MacCtx.MacCallbacks->GetBatteryLevel( );
                }
                macCmdPayload[0] = batteryLevel;
                macCmdPayload[1] = ( uint8_t )( ((snr > 31)? 31: ((snr < -32)? -32: snr)) & 0x3F);  // -32 < snr <= +31
                LoRaMacCommandsAddCmd( MOTE_MAC_DEV_STATUS_ANS, macCmdPayload, 2 );
                break;
            }
            case SRV_MAC_NEW_CHANNEL_REQ:
            {
                // Check command length
                if( ( commandsSize - macIndex ) < (uint8_t)( LORAMAC_COMMAND_LEN_NEW_CHANNEL_REQ - 1U ) )
                {
                    macIndex = commandsSize;
                    break;
                }
                NewChannelReqParams_t newChannelReq;
                ChannelParams_t chParam;
                status = 0x03;

                newChannelReq.ChannelId = payload[macIndex++];
                newChannelReq.NewChannel = &chParam;

                LoRaMacCommonCopyArrayToUint32( &(chParam.Frequency), &(payload[macIndex]), 3 );
                macIndex += 3;
                chParam.Frequency *= 100;
#ifdef LORAMAC_SET_CH_RX1FREQ_ENABLED
                chParam.Rx1Frequency = 0;
#endif
                chParam.DrRange.Value = payload[macIndex++];

                status = RegionNewChannelReq( MacCtx.NvmCtx->Region, &newChannelReq );

                if( (int8_t)status >= 0 )
                {
                    macCmdPayload[0] = status;
                    LoRaMacCommandsAddCmd( MOTE_MAC_NEW_CHANNEL_ANS, macCmdPayload, 1 );

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
                    if( status == 0x03 )  // success
                    {
                        LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_CHANNELS |
                                                 LORAMAC_NVM_MIBFLG_CHANNELS_MASK );
                    }
#endif
                }
                break;
            }
            case SRV_MAC_RX_TIMING_SETUP_REQ:
            {
                // Check command length
                if( ( commandsSize - macIndex ) < (uint8_t)( LORAMAC_COMMAND_LEN_RX_TIMING_SETUP_REQ - 1U ) )
                {
                    macIndex = commandsSize;
                    break;
                }
                uint8_t delay = payload[macIndex++] & 0x0F;

                if( delay == 0 )
                {
                    delay++;
                }

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
                if( MacCtx.NvmCtx->MacParams.ReceiveDelay1 != (delay * 1000) )
                {
                    MacCtx.NvmCtx->MacParams.ReceiveDelay1 = delay * 1000;
                    MacCtx.NvmCtx->MacParams.ReceiveDelay2 = MacCtx.NvmCtx->MacParams.ReceiveDelay1 + 1000;
                    LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_RECEIVE_DELAY_1 );
                }
#else
                MacCtx.NvmCtx->MacParams.ReceiveDelay1 = delay * 1000;
                MacCtx.NvmCtx->MacParams.ReceiveDelay2 = MacCtx.NvmCtx->MacParams.ReceiveDelay1 + 1000;
#endif

                LoRaMacCommandsAddCmd( MOTE_MAC_RX_TIMING_SETUP_ANS, macCmdPayload, 0 );
                // Setup indication to inform the application
                SetMlmeScheduleUplinkIndication( );
                break;
            }
            case SRV_MAC_TX_PARAM_SETUP_REQ:
            {
                // Check command length
                if( ( commandsSize - macIndex ) < (uint8_t)( LORAMAC_COMMAND_LEN_TX_PARAM_SETUP_REQ - 1U ) )
                {
                    macIndex = commandsSize;
                    break;
                }
                TxParamSetupReqParams_t txParamSetupReq;
                GetPhyParams_t getPhy;
                PhyParam_t phyParam;
                uint8_t eirpDwellTime = payload[macIndex++];

                txParamSetupReq.UplinkDwellTime = 0;
                txParamSetupReq.DownlinkDwellTime = 0;

                if( ( eirpDwellTime & 0x20 ) == 0x20 )
                {
                    txParamSetupReq.DownlinkDwellTime = 1;
                }
                if( ( eirpDwellTime & 0x10 ) == 0x10 )
                {
                    txParamSetupReq.UplinkDwellTime = 1;
                }
                txParamSetupReq.MaxEirp = eirpDwellTime & 0x0F;

                // Check the status for correctness
                if( RegionTxParamSetupReq( MacCtx.NvmCtx->Region, &txParamSetupReq ) != -1 )
                {
                    // Accept command
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
                    if( MacCtx.NvmCtx->MacParams.UplinkDwellTime != txParamSetupReq.UplinkDwellTime )
                    {
                        MacCtx.NvmCtx->MacParams.UplinkDwellTime = txParamSetupReq.UplinkDwellTime;
                        LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_UPLINK_DWELLTIME );
                    }
                    if( MacCtx.NvmCtx->MacParams.DownlinkDwellTime != txParamSetupReq.DownlinkDwellTime )
                    {
                        MacCtx.NvmCtx->MacParams.DownlinkDwellTime = txParamSetupReq.DownlinkDwellTime;
                        LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_DOWNLINK_DWELLTIME );
                    }
                    if( MacCtx.NvmCtx->MacParams.MaxEirp != LoRaMacMaxEirpTable[txParamSetupReq.MaxEirp] )
                    {
                        MacCtx.NvmCtx->MacParams.MaxEirp = LoRaMacMaxEirpTable[txParamSetupReq.MaxEirp];
                        LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_MAX_EIRP );
                    }
#else
                    MacCtx.NvmCtx->MacParams.UplinkDwellTime = txParamSetupReq.UplinkDwellTime;
                    MacCtx.NvmCtx->MacParams.DownlinkDwellTime = txParamSetupReq.DownlinkDwellTime;
                    MacCtx.NvmCtx->MacParams.MaxEirp = LoRaMacMaxEirpTable[txParamSetupReq.MaxEirp];
#endif

                    // Update the datarate in case of the new configuration limits it
                    getPhy.Attribute = PHY_MIN_TX_DR;
                    getPhy.UplinkDwellTime = MacCtx.NvmCtx->MacParams.UplinkDwellTime;
                    phyParam = RegionGetPhyParam( MacCtx.NvmCtx->Region, &getPhy );
                    stmp8 = R_MAX( MacCtx.NvmCtx->MacParams.ChannelsDatarate,
                                   ( int8_t )phyParam.Value );
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
                    if( MacCtx.NvmCtx->MacParams.ChannelsDatarate != stmp8 )
                    {
                        MacCtx.NvmCtx->MacParams.ChannelsDatarate = stmp8;
                        LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_CHANNELS_DATARATE );
                    }
#else
                    MacCtx.NvmCtx->MacParams.ChannelsDatarate = stmp8;
#endif

                    // Add command response
                    LoRaMacCommandsAddCmd( MOTE_MAC_TX_PARAM_SETUP_ANS, macCmdPayload, 0 );
                }
                break;
            }
            case SRV_MAC_DL_CHANNEL_REQ:
            {
                // Check command length
                if( ( commandsSize - macIndex ) < (uint8_t)( LORAMAC_COMMAND_LEN_DL_CHANNEL_REQ - 1U ) )
                {
                    macIndex = commandsSize;
                    break;
                }
                DlChannelReqParams_t dlChannelReq;
                status = 0x03;

                dlChannelReq.ChannelId = payload[macIndex++];
                LoRaMacCommonCopyArrayToUint32( &(dlChannelReq.Rx1Frequency), &(payload[macIndex]), 3 );
                macIndex += 3;
                dlChannelReq.Rx1Frequency *= 100;

                status = RegionDlChannelReq( MacCtx.NvmCtx->Region, &dlChannelReq );

                if( (int8_t)status >= 0 )
                {
                    macCmdPayload[0] = status;
                    LoRaMacCommandsAddCmd( MOTE_MAC_DL_CHANNEL_ANS, macCmdPayload, 1 );
                    // Setup indication to inform the application
                    SetMlmeScheduleUplinkIndication( );

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
                    if( status == 0x03 )  // success
                    {
                        LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_CHANNELS );
                    }
#endif
                }
                break;
            }
            case SRV_MAC_DEVICE_TIME_ANS:
            {
                // Check command length
                if( ( commandsSize - macIndex ) < (uint8_t)( LORAMAC_COMMAND_LEN_DEVICE_TIME_ANS - 1U ) )
                {
                    macIndex = commandsSize;
                    break;
                }
                if( LoRaMacConfirmQueueIsCmdActive( MLME_DEVICE_TIME ) == true )
                {
                    LoRaMacConfirmQueueSetStatus( LORAMAC_EVENT_INFO_STATUS_OK, MLME_DEVICE_TIME );

                    SysTime_t gpsEpochTime = { 0 };
                    SysTime_t sysTime = { 0 };
                    SysTime_t sysTimeCurrent = { 0 };

                    LoRaMacCommonCopyArrayToUint32( &(gpsEpochTime.Seconds), &(payload[macIndex]), 4 );
                    macIndex += 4;
                    gpsEpochTime.SubSeconds = payload[macIndex++];

                    // Convert the fractional second received in ms
                    // round( pow( 0.5, 8.0 ) * 1000 ) = 3.90625
                    gpsEpochTime.SubSeconds = ( int16_t )( ( ( int32_t )gpsEpochTime.SubSeconds * 1000 ) >> 8 );

                    // Copy received GPS Epoch time into system time
                    sysTime = gpsEpochTime;
                    // Add Unix to Gps epcoh offset. The system time is based on Unix time.
                    sysTime.Seconds += UNIX_GPS_EPOCH_OFFSET;

                    // Compensate time difference between Tx Done time and now
                    sysTimeCurrent = SysTimeGet( );
                    sysTime = SysTimeAdd( sysTimeCurrent, SysTimeSub( sysTime, MacCtx.LastTxSysTime ) );

                    // Apply the new system time.
                    SysTimeSet( sysTime );
#ifdef LORAMAC_CLASSB_ENABLED
                    LoRaMacClassBSetRxBeaconTimingTime( RxDoneParams.LastRxDone );
                    LoRaMacClassBDeviceTimeAns( );
#endif
                    MacCtx.McpsIndication.DeviceTimeAnsReceived = true;
                }
                else
                {
                    // Skip command
                    macIndex += ( LORAMAC_COMMAND_LEN_DEVICE_TIME_ANS - 1U );
                }

                break;
            }
#ifdef LORAMAC_CLASSB_ENABLED
            case SRV_MAC_PING_SLOT_INFO_ANS:
            {
                // Check command length ... not necessary
                if( LoRaMacConfirmQueueIsCmdActive( MLME_PING_SLOT_INFO ) == true )
                {
                    LoRaMacConfirmQueueSetStatus( LORAMAC_EVENT_INFO_STATUS_OK, MLME_PING_SLOT_INFO );

                    // According to the specification, it is not allowed to process this answer in
                    // a ping or multicast slot
                    if( ( MacCtx.RxSlot != RX_SLOT_WIN_CLASS_B_PING_SLOT ) && ( MacCtx.RxSlot != RX_SLOT_WIN_CLASS_B_MULTICAST_SLOT ) )
                    {
                        LoRaMacClassBPingSlotInfoAns( );
                    }
                }
                else
                {
                    // Skip command ... not necessary
                }
                break;
            }
#endif  // LORAMAC_CLASSB_ENABLED
            case SRV_MAC_PING_SLOT_CHANNEL_REQ:
            {
                // Check command length
                if( ( commandsSize - macIndex ) < (uint8_t)( LORAMAC_COMMAND_LEN_PING_SLOT_CHANNEL_REQ - 1U ) )
                {
                    macIndex = commandsSize;
                    break;
                }
#ifdef LORAMAC_CLASSB_ENABLED
                uint8_t status = 0x03;
                uint32_t frequency = 0;
                uint8_t datarate;

                frequency = ( uint32_t )payload[macIndex++];
                frequency |= ( uint32_t )payload[macIndex++] << 8;
                frequency |= ( uint32_t )payload[macIndex++] << 16;
                frequency *= 100;
                datarate = payload[macIndex++] & 0x0F;

                status = LoRaMacClassBPingSlotChannelReq( datarate, frequency );
                macCmdPayload[0] = status;
#else
                macIndex = macIndex + (uint8_t)( LORAMAC_COMMAND_LEN_PING_SLOT_CHANNEL_REQ - 1U );
                macCmdPayload[0] = 0;
#endif
                LoRaMacCommandsAddCmd( MOTE_MAC_PING_SLOT_CHANNEL_ANS, macCmdPayload, 1 );
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
                // Setup indication to inform the application
                SetMlmeScheduleUplinkIndication( );
#endif
                break;
            }
#ifdef LORAMAC_CLASSB_ENABLED
            case SRV_MAC_BEACON_TIMING_ANS:
            {
                // Check command length
                if( ( commandsSize - macIndex ) < (uint8_t)( LORAMAC_COMMAND_LEN_BEACON_TIMING_ANS - 1U ) )
                {
                    macIndex = commandsSize;
                    break;
                }
                if( LoRaMacConfirmQueueIsCmdActive( MLME_BEACON_TIMING ) == true )
                {
                    LoRaMacConfirmQueueSetStatus( LORAMAC_EVENT_INFO_STATUS_OK, MLME_BEACON_TIMING );

                    uint16_t beaconTimingDelay = 0;
                    uint8_t beaconTimingChannel = 0;

                    beaconTimingDelay = ( uint16_t )payload[macIndex++];
                    beaconTimingDelay |= ( uint16_t )payload[macIndex++] << 8;
                    beaconTimingChannel = payload[macIndex++];

                    LoRaMacClassBBeaconTimingAns( beaconTimingDelay, beaconTimingChannel, RxDoneParams.LastRxDone );
                    LoRaMacClassBSetRxBeaconTimingTime( RxDoneParams.LastRxDone );
                }
                else
                {
                    // Skip command
                    macIndex += ( LORAMAC_COMMAND_LEN_BEACON_TIMING_ANS - 1U );
                }
                break;
            }
#endif  // LORAMAC_CLASSB_ENABLED
            case SRV_MAC_BEACON_FREQ_REQ:
            {
                // Check command length
                if( ( commandsSize - macIndex ) < (uint8_t)( LORAMAC_COMMAND_LEN_BEACON_FREQ_REQ - 1U ) )
                {
                    macIndex = commandsSize;
                    break;
                }
#ifdef LORAMAC_CLASSB_ENABLED
                uint32_t frequency = 0;

                frequency = ( uint32_t )payload[macIndex++];
                frequency |= ( uint32_t )payload[macIndex++] << 8;
                frequency |= ( uint32_t )payload[macIndex++] << 16;
                frequency *= 100;

                if( LoRaMacClassBBeaconFreqReq( frequency ) == true )
                {
                    macCmdPayload[0] = 1;
                }
                else
                {
                    macCmdPayload[0] = 0;
                }
#else
                macIndex = macIndex + (uint8_t)( LORAMAC_COMMAND_LEN_BEACON_FREQ_REQ - 1U );
                macCmdPayload[0] = 0;
#endif
                LoRaMacCommandsAddCmd( MOTE_MAC_BEACON_FREQ_ANS, macCmdPayload, 1 );
                break;
            }
            default:
                // Unknown command. ABORT MAC commands processing
                return;
        }
    }
}

LoRaMacStatus_t Send( LoRaMacHeader_t* macHdr, uint8_t fPort, void* fBuffer, uint16_t fBufferSize )
{
    SBC(LWBC_M4);
    LoRaMacFrameCtrl_t fCtrl;
    LoRaMacStatus_t status = LORAMAC_STATUS_PARAMETER_INVALID;
    int8_t datarate = MacCtx.NvmCtx->MacParams.ChannelsDatarate;
    int8_t txPower = MacCtx.NvmCtx->MacParams.ChannelsTxPower;
    uint32_t adrAckCounter = MacCtx.NvmCtx->AdrAckCounter;
    CalcNextAdrParams_t adrNext;
    LoRaMacStatus_t frameStatus;
    int8_t adrCalcDataRate, adrCalcTxPower;
    uint8_t adrCalcNbTrans;

    /* NO_NETWORK_JOINED early return bypasses M5/M6 — clean status path,
     * not a fault. */
    if( MacCtx.NvmCtx->NetworkActivation == ACTIVATION_TYPE_NONE )
    {
        return LORAMAC_STATUS_NO_NETWORK_JOINED;
    }
    if( MacCtx.NvmCtx->MaxDCycle == 0 )
    {
        MacCtx.NvmCtx->AggregatedTimeOff = 0;
    }

    fCtrl.Value = 0;
    fCtrl.Bits.FOptsLen      = 0;
    fCtrl.Bits.Adr           = MacCtx.NvmCtx->AdrCtrlOn;

#ifdef LORAMAC_CLASSB_ENABLED
    // Check class b
    if( MacCtx.NvmCtx->DeviceClass == CLASS_B )
    {
        fCtrl.Bits.FPending      = 1;
    }
    else
#endif
    {
        fCtrl.Bits.FPending      = 0;
    }

    // Check server ack
    if( MacCtx.NvmCtx->SrvAckRequested == true )
    {
        fCtrl.Bits.Ack = 1;
    }

    // ADR next request
#if (LORAMAC_VERSION >= 0x01010000)
    adrNext.Version = MacCtx.NvmCtx->Version;
#endif
    adrNext.UpdateChanMask = true;
    adrNext.AdrEnabled = fCtrl.Bits.Adr;
    adrNext.AdrAckCounter = MacCtx.NvmCtx->AdrAckCounter;
    adrNext.AdrAckLimit = MacCtx.AdrAckLimit;
    adrNext.AdrAckDelay = MacCtx.AdrAckDelay;
    adrNext.Datarate = MacCtx.NvmCtx->MacParams.ChannelsDatarate;
    adrNext.TxPower = MacCtx.NvmCtx->MacParams.ChannelsTxPower;
    adrNext.NbTrans = MacCtx.NvmCtx->MacParams.ChannelsNbTrans;
    adrNext.UplinkDwellTime = MacCtx.NvmCtx->MacParams.UplinkDwellTime;
    adrNext.Region = MacCtx.NvmCtx->Region;

    fCtrl.Bits.AdrAckReq = LoRaMacAdrCalcNext( &adrNext, &adrCalcDataRate,
                                               &adrCalcTxPower, &adrCalcNbTrans, &adrAckCounter );
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    if( MacCtx.NvmCtx->MacParams.ChannelsDatarate != adrCalcDataRate )
    {
        MacCtx.NvmCtx->MacParams.ChannelsDatarate = adrCalcDataRate;
        LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_CHANNELS_DATARATE );
    }
    if( MacCtx.NvmCtx->MacParams.ChannelsNbTrans != adrCalcNbTrans )
    {
        MacCtx.NvmCtx->MacParams.ChannelsNbTrans = adrCalcNbTrans;
        LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_CHANNELS_NB_TRANS );
    }
    if( MacCtx.NvmCtx->MacParams.ChannelsTxPower != adrCalcTxPower )
    {
        MacCtx.NvmCtx->MacParams.ChannelsTxPower = adrCalcTxPower;
        LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_CHANNELS_TXPOWER );
    }
#else
    MacCtx.NvmCtx->MacParams.ChannelsDatarate = adrCalcDataRate;
    MacCtx.NvmCtx->MacParams.ChannelsNbTrans = adrCalcNbTrans;
    MacCtx.NvmCtx->MacParams.ChannelsTxPower = adrCalcTxPower;
#endif

    // Prepare the frame
    status = PrepareFrame( macHdr, &fCtrl, fPort, fBuffer, fBufferSize );
    SBC(LWBC_M5);
    frameStatus = status;  // Store the result.

    // Validate status
    if( ( status == LORAMAC_STATUS_OK ) || ( status == LORAMAC_STATUS_SKIPPED_APP_DATA ) )
    {
        SBC(LWBC_M6);
        // Schedule frame, do not allow delayed transmissions
        status = ScheduleTx( false );
    }

    // Post processing
    if( status != LORAMAC_STATUS_OK )
    {
        // Bad case - restore
        // Store local variables
        MacCtx.NvmCtx->MacParams.ChannelsDatarate = datarate;
        MacCtx.NvmCtx->MacParams.ChannelsTxPower = txPower;
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
        LoRaMacClearNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_CHANNELS_DATARATE );
        LoRaMacClearNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_CHANNELS_TXPOWER );
#endif
    }
    else
    {
        // Good case
        MacCtx.NvmCtx->SrvAckRequested = false;
        MacCtx.NvmCtx->AdrAckCounter = adrAckCounter;
        // Remove all none sticky MAC commands
        if( LoRaMacCommandsRemoveNoneStickyCmds( ) != LORAMAC_COMMANDS_SUCCESS )
        {
            return LORAMAC_STATUS_MAC_COMMAD_ERROR;
        }

        status = frameStatus;  // frameStatus is either LORAMAC_STATUS_OK or LORAMAC_STATUS_SKIPPED_APP_DATA
    }
    return status;
}

LoRaMacStatus_t SendReJoinReq( JoinReqIdentifier_t joinReqType )
{
    LoRaMacStatus_t status = LORAMAC_STATUS_OK;
    LoRaMacHeader_t macHdr;
    macHdr.Value = 0;
    bool allowDelayedTx = true;

    // Setup join/rejoin message
#if (LORAMAC_VERSION >= 0x01010000)
    switch( joinReqType )
    {
        case JOIN_REQ:
        {
#endif
            SwitchClass( CLASS_A );

            MacCtx.TxMsg.Type = LORAMAC_MSG_TYPE_JOIN_REQUEST;
            MacCtx.TxMsg.Message.JoinReq.Buffer = MacCtx.PktBuffer;
            MacCtx.TxMsg.Message.JoinReq.BufSize = LORAMAC_PHY_MAXPAYLOAD;

            macHdr.Bits.MType = FRAME_TYPE_JOIN_REQ;
            MacCtx.TxMsg.Message.JoinReq.MHDR.Value = macHdr.Value;

            memcpy1( MacCtx.TxMsg.Message.JoinReq.JoinEUI, SecureElementGetJoinEui( ), LORAMAC_JOIN_EUI_FIELD_SIZE );
            memcpy1( MacCtx.TxMsg.Message.JoinReq.DevEUI, SecureElementGetDevEui( ), LORAMAC_DEV_EUI_FIELD_SIZE );

            allowDelayedTx = false;

#if (LORAMAC_VERSION >= 0x01010000)
            break;
        }
        default:
            status = LORAMAC_STATUS_SERVICE_UNKNOWN;
            break;
    }
#endif

    // Schedule frame
    status = ScheduleTx( allowDelayedTx );
    return status;
}

#ifdef LORAMAC_CLASSB_ENABLED
static LoRaMacStatus_t CheckForClassBCollision( void )
{
    if( LoRaMacClassBIsBeaconExpected( ) == true )
    {
        return LORAMAC_STATUS_BUSY_BEACON_RESERVED_TIME;
    }

    if( MacCtx.NvmCtx->DeviceClass == CLASS_B )
    {
        if( LoRaMacClassBIsPingExpected( ) == true )
        {
            return LORAMAC_STATUS_BUSY_PING_SLOT_WINDOW_TIME;
        }
        else if( LoRaMacClassBIsMulticastExpected( ) == true )
        {
            return LORAMAC_STATUS_BUSY_PING_SLOT_WINDOW_TIME;
        }
    }
    return LORAMAC_STATUS_OK;
}
#endif

static LoRaMacStatus_t ScheduleTx( bool allowDelayedTx )
{
    SBC(LWBC_M7);
    LoRaMacStatus_t status = LORAMAC_STATUS_PARAMETER_INVALID;
    LoRaMacStatus_t retval = LORAMAC_STATUS_PARAMETER_INVALID;
    NextChanParams_t nextChan;

#ifdef LORAMAC_CLASSB_ENABLED
    // Check class b collisions
    status = CheckForClassBCollision( );

    if( status != LORAMAC_STATUS_OK )
    {
        return status;
    }
#endif

    // Update back-off
    CalculateBackOff( MacCtx.NvmCtx->LastTxChannel );

    nextChan.AggrTimeOff = MacCtx.NvmCtx->AggregatedTimeOff;
    nextChan.Datarate = MacCtx.NvmCtx->MacParams.ChannelsDatarate;
    nextChan.DutyCycleEnabled = MacCtx.NvmCtx->DutyCycleOn;
    // Setup the parameters based on the join status
    if( MacCtx.NvmCtx->NetworkActivation == ACTIVATION_TYPE_NONE )
    {
        nextChan.Joined = false;
    }
    else
    {
        nextChan.Joined = true;
    }
    nextChan.LastAggrTx = MacCtx.NvmCtx->LastTxDoneTime;

    LORAMAC_RADIOWAKEUP_TX( 0 );
    // Select channel
    status = RegionNextChannel( MacCtx.NvmCtx->Region, &nextChan, &MacCtx.Channel, &MacCtx.DutyCycleWaitTime, &MacCtx.NvmCtx->AggregatedTimeOff );

    if( status != LORAMAC_STATUS_OK )
    {
        if( ( status == LORAMAC_STATUS_DUTYCYCLE_RESTRICTED ) &&
            ( allowDelayedTx == true ) )
        {
            // Allow delayed transmissions. We have to allow it in case
            // the MAC must retransmit a frame with the frame repetitions
            {
                // Send later - prepare timer
                MacCtx.MacState |= LORAMAC_TX_DELAYED;
                TimerSetValue( &MacCtx.TxDelayedTimer, MacCtx.DutyCycleWaitTime );
                TimerStart( &MacCtx.TxDelayedTimer );
            }
            retval = LORAMAC_STATUS_OK;
        }
        else
        {
            // State where the MAC cannot send a frame
            retval = status;
        }
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
        LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_CHANNELS_MASK );
#endif
    }

    if( status == LORAMAC_STATUS_OK )
    {
        /* r13-fix — join RX1 only: raise SystemMaxRxError to at least
         * JOIN_RX1_MAX_RX_ERROR_MS so RX1 opens early enough to catch the
         * SF7 JoinAccept preamble. Data RX1 and all RX2 paths keep the
         * configured value. ResetMacParameters() leaves MinRxSymbols /
         * SystemMaxRxError untouched (they are seeded once at L4351-4352),
         * so reading them here gives the user-installed value. */
        uint8_t  rx1_min_sym = MacCtx.NvmCtx->MacParams.MinRxSymbols;
        uint32_t rx1_max_err = MacCtx.NvmCtx->MacParams.SystemMaxRxError;
        uint8_t  join_override_used = 0;
        if( MacCtx.NvmCtx->NetworkActivation == ACTIVATION_TYPE_NONE )
        {
            if( rx1_max_err < (uint32_t)JOIN_RX1_MAX_RX_ERROR_MS )
            {
                rx1_max_err = (uint32_t)JOIN_RX1_MAX_RX_ERROR_MS;
                join_override_used = 1;
            }
        }

        // Compute Rx1 windows parameters
        RegionComputeRxWindowParameters( MacCtx.NvmCtx->Region,
                                         RegionApplyDrOffset( MacCtx.NvmCtx->Region,
                                                              MacCtx.NvmCtx->MacParams.DownlinkDwellTime,
                                                              MacCtx.NvmCtx->MacParams.ChannelsDatarate,
                                                              MacCtx.NvmCtx->MacParams.Rx1DrOffset ),
                                         rx1_min_sym,
                                         rx1_max_err,
                                         &MacCtx.RxWindow1Config );
        MacCtx.RxWindow1Config.WindowOffset -= LoRaMacGetStackProcessTime( LORAMAC_STACK_PROCTIME_SEL_RX1_ON );
        // Compute Rx2 windows parameters
        RegionComputeRxWindowParameters( MacCtx.NvmCtx->Region,
                                         MacCtx.NvmCtx->MacParams.Rx2Channel.Datarate,
                                         MacCtx.NvmCtx->MacParams.MinRxSymbols,
                                         MacCtx.NvmCtx->MacParams.SystemMaxRxError,
                                         &MacCtx.RxWindow2Config );
        MacCtx.RxWindow2Config.WindowOffset -= LoRaMacGetStackProcessTime( LORAMAC_STACK_PROCTIME_SEL_RX2_ON );

        if( MacCtx.NvmCtx->NetworkActivation == ACTIVATION_TYPE_NONE )
        {
            MacCtx.RxWindow1Delay = MacCtx.NvmCtx->MacParams.JoinAcceptDelay1 + MacCtx.RxWindow1Config.WindowOffset;
            MacCtx.RxWindow2Delay = MacCtx.NvmCtx->MacParams.JoinAcceptDelay2 + MacCtx.RxWindow2Config.WindowOffset;
            /* r13 Phase 1 — capture WindowTimeout (in SYMBOLS) computed by
             * Region for the JOIN attempt. Reset by MLME_JOIN_REQ enqueue. */
            __atomic_store_n((uint32_t *)&lorawan_rxc_diag.last_join_rx1_window_timeout_symbols,
                             (uint32_t)MacCtx.RxWindow1Config.WindowTimeout, __ATOMIC_RELAXED);
            __atomic_store_n((uint32_t *)&lorawan_rxc_diag.last_join_rx2_window_timeout_symbols,
                             (uint32_t)MacCtx.RxWindow2Config.WindowTimeout, __ATOMIC_RELAXED);
            /* r13-fix — capture the override decision + effective RX1 inputs
             * + computed RX1 WindowOffset. WindowOffset captured BEFORE the
             * stack-process-time subtraction so the gate-arithmetic is
             * directly verifiable. Saturated to int16 (actual values are a
             * few tens of ms). */
            {
                int32_t wo32 = MacCtx.RxWindow1Config.WindowOffset
                             + (int32_t)LoRaMacGetStackProcessTime( LORAMAC_STACK_PROCTIME_SEL_RX1_ON );
                int16_t wo16;
                if( wo32 >  INT16_MAX ) { wo16 = INT16_MAX; }
                else if( wo32 < INT16_MIN ) { wo16 = INT16_MIN; }
                else { wo16 = (int16_t)wo32; }
                __atomic_store_n(&lorawan_rxc_diag.last_join_used_override_flag,
                                 join_override_used, __ATOMIC_RELAXED);
                __atomic_store_n(&lorawan_rxc_diag.last_join_effective_min_rx_symbols,
                                 rx1_min_sym, __ATOMIC_RELAXED);
                __atomic_store_n((uint16_t *)&lorawan_rxc_diag.last_join_effective_system_max_rx_error_ms,
                                 (uint16_t)((rx1_max_err > 0xFFFFu) ? 0xFFFFu : rx1_max_err),
                                 __ATOMIC_RELAXED);
                __atomic_store_n((int16_t *)&lorawan_rxc_diag.last_join_rx1_window_offset_ms,
                                 wo16, __ATOMIC_RELAXED);
            }
        }
        else
        {
            if( MacCtx.ChannelsNbTransCounter >= 1 )
            {
                // Nothing to do; available length is not changed in case of NbTrans.
            }
            else
            {
                // LoRaMacCommandsGetSizeSerializedCmds() cannot be used because...
                //   - Result of LoRaMacCommandsGetSizeSerializedCmds() and FOptsLen may be different
                //   - Commands had been removed in case of confirmed uplink retransmission
                bool validateLen = ValidatePayloadLength( MacCtx.TxMsg.Message.Data.FRMPayloadSize,
                                                          MacCtx.NvmCtx->MacParams.ChannelsDatarate,
                                                          MacCtx.TxMsg.Message.Data.FHDR.FCtrl.Bits.FOptsLen );
                if( validateLen == false )
                {
                    status = LORAMAC_STATUS_LENGTH_ERROR;
                    retval = status;
                }
            }
            MacCtx.RxWindow1Delay = MacCtx.NvmCtx->MacParams.ReceiveDelay1 + MacCtx.RxWindow1Config.WindowOffset;
            MacCtx.RxWindow2Delay = MacCtx.NvmCtx->MacParams.ReceiveDelay2 + MacCtx.RxWindow2Config.WindowOffset;
        }
    }

    if (status == LORAMAC_STATUS_OK)
    {
        // Secure frame
        // No need encryption for V.1.0.x uplink retransmission.
        if( ( MacCtx.NvmCtx->Version.Fields.Minor == 0 ) &&
            (( MacCtx.ChannelsNbTransCounter >= 1 ) || ( MacCtx.AckTimeoutRetriesCounter > 1 )) )
        {
            retval = LORAMAC_STATUS_OK;
        }
        else
        {
            retval = SecureFrame( MacCtx.NvmCtx->MacParams.ChannelsDatarate, MacCtx.Channel );
        }
        if (retval == LORAMAC_STATUS_OK)
        {
            SBC(LWBC_M8);
            // Try to send now
            retval = SendFrameOnChannel( MacCtx.Channel );
        }
    }

    LORAMAC_RADIOSLEEP_TXFAILED( 0 );

    return retval;
}

static LoRaMacStatus_t SecureFrame( uint8_t txDr, uint8_t txCh )
{
    LoRaMacCryptoStatus_t macCryptoStatus = LORAMAC_CRYPTO_ERROR;
    uint32_t fCntUp = 0;

    switch( MacCtx.TxMsg.Type )
    {
        case LORAMAC_MSG_TYPE_JOIN_REQUEST:
            macCryptoStatus = LoRaMacCryptoPrepareJoinRequest( &MacCtx.TxMsg.Message.JoinReq );
            if( LORAMAC_CRYPTO_SUCCESS != macCryptoStatus )
            {
                return LORAMAC_STATUS_CRYPTO_ERROR;
            }
            MacCtx.PktBufferLen = MacCtx.TxMsg.Message.JoinReq.BufSize;
            break;
        case LORAMAC_MSG_TYPE_DATA:

            if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoGetFCntUp( &fCntUp ) )
            {
                return LORAMAC_STATUS_FCNT_HANDLER_ERROR;
            }

            if( ( MacCtx.ChannelsNbTransCounter >= 1 ) || ( MacCtx.AckTimeoutRetriesCounter > 1 ) )
            {
                fCntUp -= 1;
            }

            macCryptoStatus = LoRaMacCryptoSecureMessage( fCntUp, txDr, txCh, &MacCtx.TxMsg.Message.Data );
            if( LORAMAC_CRYPTO_SUCCESS != macCryptoStatus )
            {
                return LORAMAC_STATUS_CRYPTO_ERROR;
            }
            MacCtx.PktBufferLen = MacCtx.TxMsg.Message.Data.BufSize;
            break;
        case LORAMAC_MSG_TYPE_JOIN_ACCEPT:
        case LORAMAC_MSG_TYPE_UNDEF:
        default:
            return LORAMAC_STATUS_PARAMETER_INVALID;
    }
    return LORAMAC_STATUS_OK;
}

static void CalculateBackOff( uint8_t channel )
{
    CalcBackOffParams_t calcBackOff;

    if( MacCtx.NvmCtx->NetworkActivation == ACTIVATION_TYPE_NONE )
    {
        calcBackOff.Joined = false;
    }
    else
    {
        calcBackOff.Joined = true;
    }
    calcBackOff.DutyCycleEnabled = MacCtx.NvmCtx->DutyCycleOn;
    calcBackOff.Channel = channel;
    calcBackOff.ElapsedTime = SysTimeSub( SysTimeGetMcuTime( ), MacCtx.NvmCtx->InitializationTime );
    calcBackOff.TxTimeOnAir = MacCtx.TxTimeOnAir;
    calcBackOff.LastTxIsJoinRequest = false;
    if( ( MacCtx.MacFlags.Bits.MlmeReq == 1 ) && ( LoRaMacConfirmQueueIsCmdActive( MLME_JOIN ) == true ) )
    {
        calcBackOff.LastTxIsJoinRequest = true;
    }

    // Update regional back-off
    RegionCalcBackOff( MacCtx.NvmCtx->Region, &calcBackOff );

    // Update aggregated time-off. This must be an assignment and no incremental
    // update as we do only calculate the time-off based on the last transmission
    MacCtx.NvmCtx->AggregatedTimeOff = ( MacCtx.TxTimeOnAir * MacCtx.NvmCtx->AggregatedDCycle - MacCtx.TxTimeOnAir );
}

static void RemoveMacCommands( LoRaMacRxSlot_t rxSlot, LoRaMacFrameCtrl_t fCtrl, Mcps_t request )
{
    if( rxSlot == RX_SLOT_WIN_1 || rxSlot == RX_SLOT_WIN_2 )
    {
        // Remove all sticky MAC commands answers since we can assume
        // that they have been received by the server.
        if( request == MCPS_CONFIRMED )
        {
            if( fCtrl.Bits.Ack == 1 )
            {  // For confirmed uplinks only if we have received an ACK.
                LoRaMacCommandsRemoveStickyAnsCmds( );
            }
        }
        else
        {
            LoRaMacCommandsRemoveStickyAnsCmds( );
        }
    }
}


static void ResetMacParameters( void )
{
    uint32_t    notifyFlg = 0;

    MacCtx.NvmCtx->NetworkActivation = ACTIVATION_TYPE_NONE;

    // ADR counter
    MacCtx.NvmCtx->AdrAckCounter = 0;

    MacCtx.ChannelsNbTransCounter = 0;
    MacCtx.AckTimeoutRetries = 1;
    MacCtx.AckTimeoutRetriesCounter = 1;
    MacCtx.AckTimeoutRetry = false;

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
    if( MacCtx.NvmCtx->MaxDCycle != 0 )
    {
        MacCtx.NvmCtx->MaxDCycle = 0;
        notifyFlg |= LORAMAC_NVM_MIBFLG_MAX_DCYCLE;
    }
    MacCtx.NvmCtx->AggregatedDCycle = 1;

    if( MacCtx.NvmCtx->MacParams.ChannelsTxPower != MacCtx.NvmCtx->MacParamsDefaults.ChannelsTxPower )
    {
        MacCtx.NvmCtx->MacParams.ChannelsTxPower = MacCtx.NvmCtx->MacParamsDefaults.ChannelsTxPower;
        notifyFlg |= LORAMAC_NVM_MIBFLG_CHANNELS_TXPOWER;
    }
    if( MacCtx.NvmCtx->MacParams.ChannelsDatarate != MacCtx.NvmCtx->MacParamsDefaults.ChannelsDatarate )
    {
        MacCtx.NvmCtx->MacParams.ChannelsDatarate = MacCtx.NvmCtx->MacParamsDefaults.ChannelsDatarate;
        notifyFlg |= LORAMAC_NVM_MIBFLG_CHANNELS_DATARATE;
    }
    if( MacCtx.NvmCtx->MacParams.Rx1DrOffset != MacCtx.NvmCtx->MacParamsDefaults.Rx1DrOffset )
    {
        MacCtx.NvmCtx->MacParams.Rx1DrOffset = MacCtx.NvmCtx->MacParamsDefaults.Rx1DrOffset;
        notifyFlg |= LORAMAC_NVM_MIBFLG_RX1_DROFFSET;
    }
    if( MacCtx.NvmCtx->MacParams.Rx2Channel.Frequency != MacCtx.NvmCtx->MacParamsDefaults.Rx2Channel.Frequency )
    {
        MacCtx.NvmCtx->MacParams.Rx2Channel.Frequency = MacCtx.NvmCtx->MacParamsDefaults.Rx2Channel.Frequency;
        notifyFlg |= LORAMAC_NVM_MIBFLG_RX2_FREQUENCY;
    }
    if( MacCtx.NvmCtx->MacParams.Rx2Channel.Datarate != MacCtx.NvmCtx->MacParamsDefaults.Rx2Channel.Datarate )
    {
        MacCtx.NvmCtx->MacParams.Rx2Channel.Datarate = MacCtx.NvmCtx->MacParamsDefaults.Rx2Channel.Datarate;
        notifyFlg |= LORAMAC_NVM_MIBFLG_RX2_DATARATE;
    }
    MacCtx.NvmCtx->MacParams.RxCChannel = MacCtx.NvmCtx->MacParamsDefaults.RxCChannel;
    if( MacCtx.NvmCtx->MacParams.UplinkDwellTime != MacCtx.NvmCtx->MacParamsDefaults.UplinkDwellTime )
    {
        MacCtx.NvmCtx->MacParams.UplinkDwellTime = MacCtx.NvmCtx->MacParamsDefaults.UplinkDwellTime;
        notifyFlg |= LORAMAC_NVM_MIBFLG_UPLINK_DWELLTIME;
    }
    if( MacCtx.NvmCtx->MacParams.DownlinkDwellTime != MacCtx.NvmCtx->MacParamsDefaults.DownlinkDwellTime )
    {
        MacCtx.NvmCtx->MacParams.DownlinkDwellTime = MacCtx.NvmCtx->MacParamsDefaults.DownlinkDwellTime;
        notifyFlg |= LORAMAC_NVM_MIBFLG_DOWNLINK_DWELLTIME;
    }
    if( MacCtx.NvmCtx->MacParams.MaxEirp != MacCtx.NvmCtx->MacParamsDefaults.MaxEirp )
    {
        MacCtx.NvmCtx->MacParams.MaxEirp = MacCtx.NvmCtx->MacParamsDefaults.MaxEirp;
        notifyFlg |= LORAMAC_NVM_MIBFLG_MAX_EIRP;
    }
#else
    MacCtx.NvmCtx->MaxDCycle = 0;
    MacCtx.NvmCtx->AggregatedDCycle = 1;
    MacCtx.NvmCtx->MacParams.ChannelsTxPower = MacCtx.NvmCtx->MacParamsDefaults.ChannelsTxPower;
    MacCtx.NvmCtx->MacParams.ChannelsDatarate = MacCtx.NvmCtx->MacParamsDefaults.ChannelsDatarate;
    MacCtx.NvmCtx->MacParams.Rx1DrOffset = MacCtx.NvmCtx->MacParamsDefaults.Rx1DrOffset;
    MacCtx.NvmCtx->MacParams.Rx2Channel.Frequency = MacCtx.NvmCtx->MacParamsDefaults.Rx2Channel.Frequency;
    MacCtx.NvmCtx->MacParams.Rx2Channel.Datarate = MacCtx.NvmCtx->MacParamsDefaults.Rx2Channel.Datarate;
    MacCtx.NvmCtx->MacParams.RxCChannel = MacCtx.NvmCtx->MacParamsDefaults.RxCChannel;
    MacCtx.NvmCtx->MacParams.UplinkDwellTime = MacCtx.NvmCtx->MacParamsDefaults.UplinkDwellTime;
    MacCtx.NvmCtx->MacParams.DownlinkDwellTime = MacCtx.NvmCtx->MacParamsDefaults.DownlinkDwellTime;
    MacCtx.NvmCtx->MacParams.MaxEirp = MacCtx.NvmCtx->MacParamsDefaults.MaxEirp;
#endif

    MacCtx.NvmCtx->MacParams.AntennaGain = MacCtx.NvmCtx->MacParamsDefaults.AntennaGain;
    MacCtx.NvmCtx->MacParams.EnableCca = MacCtx.NvmCtx->MacParamsDefaults.EnableCca;

    // Notify to the app
    if( MacCtx.MacState != LORAMAC_STOPPED )
    {
        LoRaMacSetNvmEvtMibFlag( notifyFlg );
    }

    MacCtx.NodeAckRequested = false;
    MacCtx.NvmCtx->SrvAckRequested = false;
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
    MacCtx.NvmCtx->ChannelsDatarateChangedLinkAdrReq = false;
#endif

    // Reset to application defaults
    InitDefaultsParams_t params;
    params.Type = INIT_TYPE_RESTORE_DEFAULT_CHANNELS;
    params.NvmCtx = NULL;
    RegionInitDefaults( MacCtx.NvmCtx->Region, &params );

    // Initialize channel index.
    MacCtx.Channel = 0;
    MacCtx.NvmCtx->LastTxChannel = MacCtx.Channel;

    // Initialize Rx2 config parameters.
    MacCtx.RxWindow2Config.Channel = MacCtx.Channel;
    MacCtx.RxWindow2Config.Frequency = MacCtx.NvmCtx->MacParams.Rx2Channel.Frequency;
    MacCtx.RxWindow2Config.DownlinkDwellTime = MacCtx.NvmCtx->MacParams.DownlinkDwellTime;
    MacCtx.RxWindow2Config.RxContinuous = false;
    MacCtx.RxWindow2Config.RxSlot = RX_SLOT_WIN_2;
    MacCtx.RxWindow2Config.NetworkActivation = MacCtx.NvmCtx->NetworkActivation;

    // Initialize RxC config parameters.
    MacCtx.RxWindowCConfig = MacCtx.RxWindow2Config;
#ifdef LORAMAC_RXC_CONTINUOUS_ENABLED
    MacCtx.RxWindowCConfig.RxContinuous = true;
#else
    MacCtx.RxWindowCConfig.RxContinuous = false;
#endif
    MacCtx.RxWindowCConfig.RxSlot = RX_SLOT_WIN_CLASS_C;
    MacCtx.RxWindowCConfig.NetworkActivation = MacCtx.NvmCtx->NetworkActivation;

}

/*!
 * \brief Initializes and opens the reception window
 *
 * \param [IN] rxTimer  Window timer to be topped.
 * \param [IN] rxConfig Window parameters to be setup
 */
static void RxWindowSetup( TimerEvent_t* rxTimer, RxConfigParams_t* rxConfig )
{
    TimerStop( rxTimer );

    // Ensure the radio is Idle
    LORAMAC_RADIOWAKEUP_RXWIN( 0 );

    if( RegionRxConfig( MacCtx.NvmCtx->Region, rxConfig, ( int8_t* )&MacCtx.McpsIndication.RxDatarate ) == true )
    {
        RadioResult_t   result;

        /* r13 Phase 1 — DWT CYCCNT just before Radio.Rx() opens the window.
         * DWT_CYCCNT (0xE0001004) is enabled in mphalport.c. */
        {
            uint32_t cyc = *(volatile uint32_t *)0xE0001004UL;
            if( rxConfig->RxSlot == RX_SLOT_WIN_1 )
            {
                __atomic_store_n((uint32_t *)&lorawan_rxc_diag.last_rx1_open_cyc,
                                 cyc, __ATOMIC_RELAXED);
            }
            else if( rxConfig->RxSlot == RX_SLOT_WIN_2 )
            {
                __atomic_store_n((uint32_t *)&lorawan_rxc_diag.last_rx2_open_cyc,
                                 cyc, __ATOMIC_RELAXED);
            }
        }

        result = Radio.Rx( MacCtx.NvmCtx->MacParams.MaxRxWindow );
        MacCtx.RxSlot = rxConfig->RxSlot;

        if( result != RADIO_SUCCESS )
        {
            // If it fails to set RX mode due to invalid Rx parameter,
            // process same as Rx error and notify to application layer via callback
            OnRadioRxError();

            LoRaMacErrorNotify( LORAMAC_ERROR_NOTIFICATION_STATUS_RADIO_CHECK_FAIL_RX_CFG );
        }
    }
}

static void OpenContinuousRxCWindow( void )
{
    RadioState_t radioStatus;

    LORAWAN_RXC_INC_U16(rxc_open_attempts);  /* T-V3.1 #1 */

    radioStatus = Radio.GetStatus();
    if( (radioStatus == RF_RX_RUNNING) || (radioStatus == RF_TX_RUNNING) )
    {
        /* T-V3.1 #2, #3 — distinguish which busy state caused the skip */
        if( radioStatus == RF_RX_RUNNING )
        {
            LORAWAN_RXC_INC_U16(rxc_open_skipped_rf_rx);
        }
        else
        {
            LORAWAN_RXC_INC_U16(rxc_open_skipped_rf_tx);
        }
        return;
    }

    // Compute RxC windows parameters
    RegionComputeRxWindowParameters( MacCtx.NvmCtx->Region,
                                     MacCtx.NvmCtx->MacParams.RxCChannel.Datarate,
                                     MacCtx.NvmCtx->MacParams.MinRxSymbols,
                                     MacCtx.NvmCtx->MacParams.SystemMaxRxError,
                                     &MacCtx.RxWindowCConfig );

    MacCtx.RxWindowCConfig.RxSlot = RX_SLOT_WIN_CLASS_C;
    MacCtx.RxWindowCConfig.NetworkActivation = MacCtx.NvmCtx->NetworkActivation;
#ifdef LORAMAC_RXC_CONTINUOUS_ENABLED
    // Setup continuous listening
    MacCtx.RxWindowCConfig.RxContinuous = true;
#else
    MacCtx.RxWindowCConfig.RxContinuous = false;
#endif

    /* T-V3.1 #7, #8, #9 — snapshot decision inputs before RegionRxConfig */
    LORAWAN_RXC_STORE_U32(last_rxc_freq, MacCtx.RxWindowCConfig.Frequency);
    LORAWAN_RXC_STORE_U8(last_rxc_dr,
        (uint8_t)MacCtx.NvmCtx->MacParams.RxCChannel.Datarate);
    LORAWAN_RXC_STORE_U8(last_rxc_continuous,
        MacCtx.RxWindowCConfig.RxContinuous ? 1u : 0u);

    LORAMAC_RADIOWAKEUP_RXC( 0 );

    // At this point the Radio should be idle.
    // Thus, there is no need to set the radio in standby mode.
    if( RegionRxConfig( MacCtx.NvmCtx->Region, &MacCtx.RxWindowCConfig, ( int8_t* )&MacCtx.McpsIndication.RxDatarate ) == true )
    {
        RadioResult_t   result;

        LORAWAN_RXC_INC_U16(rxc_region_ok);  /* T-V3.1 #4 */

        result = Radio.Rx( 0 ); // Continuous mode
        MacCtx.RxSlot = MacCtx.RxWindowCConfig.RxSlot;

        if( result == RADIO_SUCCESS )
        {
            LORAWAN_RXC_INC_U16(rxc_radio_rx_result);  /* T-V3.1 #6 */
        }
        else
        {
            // If it fails to set RX mode due to invalid Rx parameter, notify to application layer via callback
            LoRaMacErrorNotify( LORAMAC_ERROR_NOTIFICATION_STATUS_RADIO_CHECK_FAIL_RX_CFG );
        }
    }
    else
    {
        LORAWAN_RXC_INC_U16(rxc_region_fail);  /* T-V3.1 #5 */
    }
}

LoRaMacStatus_t PrepareFrame( LoRaMacHeader_t* macHdr, LoRaMacFrameCtrl_t* fCtrl, uint8_t fPort, void* fBuffer, uint16_t fBufferSize )
{
    MacCtx.PktBufferLen = 0;
    MacCtx.NodeAckRequested = false;
    uint32_t fCntUp = 0;
    size_t macCmdsSize = 0;
    uint8_t availableSize = 0;

    if( fBuffer == NULL )
    {
        fBufferSize = 0;
    }

#if (LORAMAC_VERSION >= 0x01010000)  // no need following in case of LW10x
    memcpy1( MacCtx.AppData, ( uint8_t* ) fBuffer, fBufferSize );
#endif
    MacCtx.AppDataSize = fBufferSize;
    MacCtx.PktBuffer[0] = macHdr->Value;

    switch( macHdr->Bits.MType )
    {
        case FRAME_TYPE_DATA_CONFIRMED_UP:
            MacCtx.NodeAckRequested = true;
            // Intentional fall through
            // no break
        case FRAME_TYPE_DATA_UNCONFIRMED_UP:
            MacCtx.TxMsg.Type = LORAMAC_MSG_TYPE_DATA;
            MacCtx.TxMsg.Message.Data.Buffer = MacCtx.PktBuffer;
            MacCtx.TxMsg.Message.Data.BufSize = LORAMAC_PHY_MAXPAYLOAD;
            MacCtx.TxMsg.Message.Data.MHDR.Value = macHdr->Value;
            MacCtx.TxMsg.Message.Data.FPort = fPort;
            MacCtx.TxMsg.Message.Data.FHDR.DevAddr = MacCtx.NvmCtx->DevAddr;
            MacCtx.TxMsg.Message.Data.FHDR.FCtrl.Value = fCtrl->Value;
            MacCtx.TxMsg.Message.Data.FRMPayloadSize = MacCtx.AppDataSize;
#if (LORAMAC_VERSION >= 0x01010000)  // LW11x
            MacCtx.TxMsg.Message.Data.FRMPayload = MacCtx.AppData;
#else  // LW10x
            MacCtx.TxMsg.Message.Data.FRMPayload = fBuffer;
#endif

            if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoGetFCntUp( &fCntUp ) )
            {
                return LORAMAC_STATUS_FCNT_HANDLER_ERROR;
            }
            MacCtx.TxMsg.Message.Data.FHDR.FCnt = ( uint16_t )fCntUp;

            // Reset confirm parameters
            MacCtx.McpsConfirm.NbRetries = 0;
            MacCtx.McpsConfirm.AckReceived = false;
            MacCtx.McpsConfirm.UpLinkCounter = fCntUp;

            // Handle the MAC commands if there are any available
            if( LoRaMacCommandsGetSizeSerializedCmds( &macCmdsSize ) != LORAMAC_COMMANDS_SUCCESS )
            {
                return LORAMAC_STATUS_MAC_COMMAD_ERROR;
            }

            if( macCmdsSize > 0 )
            {
                availableSize = GetMaxAppPayloadWithoutFOptsLength( MacCtx.NvmCtx->MacParams.ChannelsDatarate );

                // There is application payload available and the MAC commands fit into FOpts field.
                // Skip applicationdata (go to else{}) if both data and commands cannot be stored in the frame.
                if( ( MacCtx.AppDataSize > 0 ) && ( macCmdsSize <= LORA_MAC_COMMAND_MAX_FOPTS_LENGTH ) &&
                    ( ValidatePayloadLength( MacCtx.AppDataSize, MacCtx.NvmCtx->MacParams.ChannelsDatarate, macCmdsSize ) == true) )
                {
                    if( LoRaMacCommandsSerializeCmds( LORA_MAC_COMMAND_MAX_FOPTS_LENGTH, &macCmdsSize, MacCtx.TxMsg.Message.Data.FHDR.FOpts ) != LORAMAC_COMMANDS_SUCCESS )
                    {
                        return LORAMAC_STATUS_MAC_COMMAD_ERROR;
                    }
                    fCtrl->Bits.FOptsLen = macCmdsSize;
                    // Update FCtrl field with new value of FOptionsLength
                    MacCtx.TxMsg.Message.Data.FHDR.FCtrl.Value = fCtrl->Value;
                }
                // There is application payload available but the MAC commands does NOT fit into FOpts field.
                // No application payload available therefore add all mac commands to the FRMPayload.
                else
                {
                    if( LoRaMacCommandsSerializeCmds( availableSize, &macCmdsSize, MacCtx.NvmCtx->MacCommandsBuffer ) != LORAMAC_COMMANDS_SUCCESS )
                    {
                        return LORAMAC_STATUS_MAC_COMMAD_ERROR;
                    }
                    // Force FPort to be zero
                    MacCtx.TxMsg.Message.Data.FPort = 0;

                    MacCtx.TxMsg.Message.Data.FRMPayload = MacCtx.NvmCtx->MacCommandsBuffer;
                    MacCtx.TxMsg.Message.Data.FRMPayloadSize = macCmdsSize;
                    if (MacCtx.AppDataSize > 0)
                    {
                        // AppData will be discarded.
                        MacCtx.AppDataSize = 0;
                        return LORAMAC_STATUS_SKIPPED_APP_DATA;
                    }
                }
            }

            break;
        default:
            return LORAMAC_STATUS_SERVICE_UNKNOWN;
    }

    return LORAMAC_STATUS_OK;
}

LoRaMacStatus_t SendFrameOnChannel( uint8_t channel )
{
    SBC(LWBC_M9);
    TxConfigParams_t txConfig;
    int8_t txPower = 0;
    RadioResult_t       result;
    LoRaMacStatus_t     status;

    txConfig.Channel = channel;
    txConfig.Datarate = MacCtx.NvmCtx->MacParams.ChannelsDatarate;
    txConfig.TxPower = MacCtx.NvmCtx->MacParams.ChannelsTxPower;
    txConfig.MaxEirp = MacCtx.NvmCtx->MacParams.MaxEirp;
    txConfig.AntennaGain = MacCtx.NvmCtx->MacParams.AntennaGain;
    txConfig.PktLen = MacCtx.PktBufferLen;

    RegionTxConfig( MacCtx.NvmCtx->Region, &txConfig, &txPower, &MacCtx.TxTimeOnAir );

    MacCtx.McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
    MacCtx.McpsConfirm.Datarate = MacCtx.NvmCtx->MacParams.ChannelsDatarate;
    MacCtx.McpsConfirm.TxPower = txPower;
    MacCtx.McpsConfirm.Channel = channel;

    // Store the time on air
    MacCtx.McpsConfirm.TxTimeOnAir = MacCtx.TxTimeOnAir;
    MacCtx.MlmeConfirm.TxTimeOnAir = MacCtx.TxTimeOnAir;

#ifdef LORAMAC_CLASSB_ENABLED
    if( LoRaMacClassBIsBeaconModeActive( ) == true )
    {
        // Currently, the Time-On-Air can only be computed when the radio is configured with
        // the TX configuration
        TimerTime_t collisionTime = LoRaMacClassBIsUplinkCollision( MacCtx.TxTimeOnAir );

        if( collisionTime > 0 )
        {
            return LORAMAC_STATUS_BUSY_UPLINK_COLLISION;
        }
    }

    if( MacCtx.NvmCtx->DeviceClass == CLASS_B )
    {
        // Stop slots for class b
        LoRaMacClassBStopRxSlots( );
    }

    LoRaMacClassBHaltBeaconing( );
#endif

    // Update transmission couner
    if( MacCtx.NodeAckRequested == false )
    {
        MacCtx.ChannelsNbTransCounter++;
    }
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
    MacCtx.ResponseTimeoutStartTime = 0;
#endif

    // Send now
    result = Radio.Send( MacCtx.PktBuffer, MacCtx.PktBufferLen );
    SBC(LWBC_M10);

    if( result == RADIO_SUCCESS )
    {
        MacCtx.MacState |= LORAMAC_TX_RUNNING;
        status = LORAMAC_STATUS_OK;
    }
    else
    {
        if( result == RADIO_CHECK_FAIL_TX_CHANNEL_BUSY )      // Channel is busy
        {
            status = LORAMAC_STATUS_NO_FREE_CHANNEL_FOUND;
        }
        else if( result == RADIO_CHECK_FAIL_TX_DUTY_CYCLE )
        {
            status = LORAMAC_STATUS_DUTYCYCLE_RESTRICTED;
        }
        else
        {
            status = LORAMAC_STATUS_RADIO_PARAMETER_INVALID;  // RF parameter error: Current RF parameter setting is not supported
        }

#ifdef LORAMAC_CLASSB_ENABLED
        LoRaMacClassBResumeBeaconing();
#endif
    }

    return status;
}

LoRaMacStatus_t SetTxContinuousWave( uint16_t timeout, uint32_t frequency, int8_t power )
{
    Radio.SetTxContinuousWave( frequency, power, timeout );

    MacCtx.MacState |= LORAMAC_TX_RUNNING;

    return LORAMAC_STATUS_OK;
}

#ifdef LORAMAC_CTX_SAVERESTORE_ENALED
LoRaMacCtxs_t* GetCtxs( void )
{
    Contexts.MacNvmCtx = &NvmMacCtx;
    Contexts.MacNvmCtxSize = sizeof( NvmMacCtx );
    Contexts.CryptoNvmCtx = LoRaMacCryptoGetNvmCtx( &Contexts.CryptoNvmCtxSize );
    GetNvmCtxParams_t params ={ 0 };
    Contexts.RegionNvmCtx = RegionGetNvmCtx( MacCtx.NvmCtx->Region, &params );
    Contexts.RegionNvmCtxSize = params.nvmCtxSize;
    Contexts.SecureElementNvmCtx = SecureElementGetNvmCtx( &Contexts.SecureElementNvmCtxSize );
    Contexts.CommandsNvmCtx = LoRaMacCommandsGetNvmCtx( &Contexts.CommandsNvmCtxSize );
    Contexts.ClassBNvmCtx = LoRaMacClassBGetNvmCtx( &Contexts.ClassBNvmCtxSize );
    Contexts.ConfirmQueueNvmCtx = LoRaMacConfirmQueueGetNvmCtx( &Contexts.ConfirmQueueNvmCtxSize );
    return &Contexts;
}

LoRaMacStatus_t RestoreCtxs( LoRaMacCtxs_t* contexts )
{
    if( contexts == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }
    if( MacCtx.MacState != LORAMAC_STOPPED )
    {
        return LORAMAC_STATUS_BUSY;
    }

    if( contexts->MacNvmCtx != NULL )
    {
        memcpy1( ( uint8_t* ) &NvmMacCtx, ( uint8_t* ) contexts->MacNvmCtx, contexts->MacNvmCtxSize );
    }

    InitDefaultsParams_t params;
    params.Type = INIT_TYPE_RESTORE_CTX;
    params.NvmCtx = contexts->RegionNvmCtx;
    RegionInitDefaults( MacCtx.NvmCtx->Region, &params );

    // Initialize RxC config parameters.
    MacCtx.RxWindowCConfig.Channel = MacCtx.Channel;
    MacCtx.RxWindowCConfig.Frequency = MacCtx.NvmCtx->MacParams.RxCChannel.Frequency;
    MacCtx.RxWindowCConfig.DownlinkDwellTime = MacCtx.NvmCtx->MacParams.DownlinkDwellTime;
#ifdef LORAMAC_RXC_CONTINUOUS_ENABLED
    MacCtx.RxWindowCConfig.RxContinuous = true;
#else
    MacCtx.RxWindowCConfig.RxContinuous = false;
#endif
    MacCtx.RxWindowCConfig.RxSlot = RX_SLOT_WIN_CLASS_C;

    if( SecureElementRestoreNvmCtx( contexts->SecureElementNvmCtx ) != SECURE_ELEMENT_SUCCESS )
    {
        return LORAMAC_STATUS_CRYPTO_ERROR;
    }

    if( LoRaMacCryptoRestoreNvmCtx( contexts->CryptoNvmCtx ) != LORAMAC_CRYPTO_SUCCESS )
    {
        return LORAMAC_STATUS_CRYPTO_ERROR;
    }

    if( LoRaMacCommandsRestoreNvmCtx( contexts->CommandsNvmCtx ) != LORAMAC_COMMANDS_SUCCESS )
    {
        return LORAMAC_STATUS_MAC_COMMAD_ERROR;
    }

#ifdef LORAMAC_CLASSB_ENABLED
    if( LoRaMacClassBRestoreNvmCtx( contexts->ClassBNvmCtx ) != true )
    {
        return LORAMAC_STATUS_CLASS_B_ERROR;
    }
#endif

    if( LoRaMacConfirmQueueRestoreNvmCtx( contexts->ConfirmQueueNvmCtx ) != true )
    {
        return LORAMAC_STATUS_CONFIRM_QUEUE_ERROR;
    }

    return LORAMAC_STATUS_OK;
}
#endif

LoRaMacStatus_t DetermineFrameType( LoRaMacMessageData_t* macMsg, FType_t* fType )
{
    if( ( macMsg == NULL ) || ( fType == NULL ) )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

    /* The LoRaWAN specification allows several possible configurations how data up/down frames are built up.
     * In sake of clearness the following naming is applied. Please keep in mind that this is
     * implementation specific since there is no definition in the LoRaWAN specification included.
     *
     * X -> Field is available
     * - -> Field is not available
     *
     * +-------+  +----------+------+-------+--------------+
     * | FType |  | FOptsLen | Fopt | FPort |  FRMPayload  |
     * +-------+  +----------+------+-------+--------------+
     * |   A   |  |    > 0   |   X  |  > 0  |       X      |
     * +-------+  +----------+------+-------+--------------+
     * |   B   |  |   >= 0   |  X/- |   -   |       -      |
     * +-------+  +----------+------+-------+--------------+
     * |   C   |  |    = 0   |   -  |  = 0  | MAC commands |
     * +-------+  +----------+------+-------+--------------+
     * |   D   |  |    = 0   |   -  |  > 0  |       X      |
     * +-------+  +----------+------+-------+--------------+
     */

    if( ( macMsg->FHDR.FCtrl.Bits.FOptsLen > 0 ) && ( macMsg->FPort > 0 ) )
    {
        *fType = FRAME_TYPE_A;
    }
    else if( macMsg->FRMPayloadSize == 0 )
    {
        *fType = FRAME_TYPE_B;
    }
    else if( ( macMsg->FHDR.FCtrl.Bits.FOptsLen == 0 ) && ( macMsg->FPort == 0 ) )
    {
        *fType = FRAME_TYPE_C;
    }
    else if( ( macMsg->FHDR.FCtrl.Bits.FOptsLen == 0 ) && ( macMsg->FPort > 0 ) )
    {
        *fType = FRAME_TYPE_D;
    }
    else
    {
        // Should never happen.
        return LORAMAC_STATUS_ERROR;
    }

    return LORAMAC_STATUS_OK;
}

static bool CheckRetransUnconfirmedUplink( void )
{
    // Unconfirmed uplink, when all retransmissions are done.
    if( MacCtx.ChannelsNbTransCounter >=
        MacCtx.NvmCtx->MacParams.ChannelsNbTrans )
    {
        return true;
    }
    else if( MacCtx.MacFlags.Bits.McpsInd == 1 )
    {
        // For Class A stop in each case
        if( MacCtx.NvmCtx->DeviceClass == CLASS_A )
        {
            return true;
        }
        else
        {// For Class B & C stop only if the frame was received in RX1 window
            if( MacCtx.McpsIndication.RxSlot == RX_SLOT_WIN_1 )
            {
                return true;
            }
        }
    }
    return false;
}

static bool CheckRetransConfirmedUplink( void )
{
    // Confirmed uplink, when all retransmissions ( tries to get a ack ) are done.
    if( MacCtx.AckTimeoutRetriesCounter >=
        MacCtx.AckTimeoutRetries )
    {
        return true;
    }
    else if( MacCtx.MacFlags.Bits.McpsInd == 1 )
    {
        if( MacCtx.McpsConfirm.AckReceived == true )
        {
            return true;
        }
    }
    return false;
}

static bool StopRetransmission( void )
{
    if( ( MacCtx.MacFlags.Bits.McpsInd == 0 ) ||
        ( ( MacCtx.McpsIndication.RxSlot != RX_SLOT_WIN_1 ) &&
          ( MacCtx.McpsIndication.RxSlot != RX_SLOT_WIN_2 ) ) )
    {   // Maximum repetitions without downlink. Increase ADR Ack counter.
        // Only process the case when the MAC did not receive a downlink.
        if( MacCtx.NvmCtx->AdrCtrlOn == true )
        {
            MacCtx.NvmCtx->AdrAckCounter++;
        }
    }

    MacCtx.ChannelsNbTransCounter = 0;
    MacCtx.NodeAckRequested = false;
    MacCtx.AckTimeoutRetry = false;
    MacCtx.MacState &= ~LORAMAC_TX_RUNNING;

    return true;
}

static void OnMacProcessNotify( void )
{
    if( ( MacCtx.MacCallbacks != NULL ) && ( MacCtx.MacCallbacks->MacProcessNotify != NULL ) )
    {
        MacCtx.MacCallbacks->MacProcessNotify( );
    }
}

static void AckTimeoutRetriesProcess( void )
{
    if( MacCtx.AckTimeoutRetriesCounter < MacCtx.AckTimeoutRetries )
    {
        MacCtx.AckTimeoutRetriesCounter++;
#if (LORAMAC_VERSION < LORAWAN_VERSION_1_0_4)  // LW1.0.3
        if( ( MacCtx.AckTimeoutRetriesCounter % 2 ) == 1 )
        {
            GetPhyParams_t getPhy;
            PhyParam_t phyParam;

            getPhy.Attribute = PHY_NEXT_LOWER_TX_DR;
            getPhy.UplinkDwellTime = MacCtx.NvmCtx->MacParams.UplinkDwellTime;
            getPhy.Datarate = MacCtx.NvmCtx->MacParams.ChannelsDatarate;
            phyParam = RegionGetPhyParam( MacCtx.NvmCtx->Region, &getPhy );
            if( MacCtx.NvmCtx->MacParams.ChannelsDatarate != (int8_t)phyParam.Value )
            {
                MacCtx.NvmCtx->MacParams.ChannelsDatarate = (int8_t)phyParam.Value;

#ifdef LORAMAC_ACTMODE_ABP_ENABLED
                if( MacCtx.NvmCtx->AdrCtrlOn == true )
                {
                    LoRaMacSetNvmEvtMibFlag( LORAMAC_NVM_MIBFLG_CHANNELS_DATARATE );
                }
#endif
            }
        }
#endif
    }
}

static void AckTimeoutRetriesFinalize( void )
{
    if( MacCtx.McpsConfirm.AckReceived == false )
    {
        MacCtx.NodeAckRequested = false;
        MacCtx.McpsConfirm.AckReceived = false;
    }
    MacCtx.McpsConfirm.NbRetries = MacCtx.AckTimeoutRetriesCounter;
}

static void CallNvmCtxCallback( void )
{
    if( MacCtx.notifyMibFlag != 0 )
    {
        if( ( MacCtx.MacCallbacks != NULL ) && ( MacCtx.MacCallbacks->NvmContextChange != NULL ) )
        {
            MacCtx.MacCallbacks->NvmContextChange( MacCtx.notifyMibFlag );
        }
        LoRaMacResetNvmEvtMibFlag();
    }
}

#ifdef LORAMAC_CLASSB_ENABLED
static uint8_t IsRequestPending( void )
{
    if( ( MacCtx.MacFlags.Bits.MlmeReq == 1 ) ||
        ( MacCtx.MacFlags.Bits.McpsReq == 1 ) )
    {
        return 1;
    }
    return 0;
}
#endif

LoRaMacStatus_t LoRaMacInitialization( LoRaMacPrimitives_t* primitives, LoRaMacCallback_t* callbacks, LoRaMacRegion_t region )
{
#ifdef LORAMAC_CLASSB_ENABLED
    LoRaMacClassBCallback_t classBCallbacks;
    LoRaMacClassBParams_t classBParams;
#endif
#if defined(REGION_AS923)
    LoRaMacRegion_t regionGroup;
#endif

    if( ( primitives == NULL ) ||
        ( callbacks == NULL ) )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

    if( ( primitives->MacMcpsConfirm == NULL ) ||
        ( primitives->MacMcpsIndication == NULL ) ||
        ( primitives->MacMlmeConfirm == NULL ) ||
        ( primitives->MacMlmeIndication == NULL ) )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

#if defined(REGION_AS923)
    regionGroup = region;
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
    if( ( region >= LORAMAC_REGION_AS923_JPN ) && ( region < LORAMAC_REGION_AS923_MAXGROUP ) )  // JPN, AS923_2,3,...
#else
    if( ( region == LORAMAC_REGION_AS923_JPN ) )  // JPN
#endif
    {
        region = LORAMAC_REGION_AS923;
    }
#endif

    // Verify if the region is supported
    if( RegionIsActive( region ) == false )
    {
        return LORAMAC_STATUS_REGION_NOT_SUPPORTED;
    }

    // Confirm queue reset
    LoRaMacConfirmQueueInit( primitives, NULL );

    // Initialize the module context with zeros
    memset1( ( uint8_t* ) &NvmMacCtx, 0x00, sizeof( LoRaMacNvmCtx_t ) );
    memset1( ( uint8_t* ) &MacCtx, 0x00, sizeof( LoRaMacCtx_t ) );
    MacCtx.NvmCtx = &NvmMacCtx;

    // Set non zero variables to its default value
    MacCtx.AckTimeoutRetriesCounter = 1;
    MacCtx.AckTimeoutRetries = 1;
    MacCtx.NvmCtx->Region = region;
    MacCtx.NvmCtx->DeviceClass = CLASS_A;

    Version_t lrWanVersion;
    lrWanVersion.Fields.Major    = 1;
    lrWanVersion.Fields.Minor    = 0;

#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
    lrWanVersion.Fields.Patch    = 4;
#else
    lrWanVersion.Fields.Patch    = 3;
#endif
    lrWanVersion.Fields.Revision = 0;
    MacCtx.NvmCtx->Version = lrWanVersion;

    InitDefaultsParams_t params;
    params.Type = INIT_TYPE_INIT;
    params.NvmCtx = NULL;
#if defined(REGION_AS923)
    if( MacCtx.NvmCtx->Region == LORAMAC_REGION_AS923 )
    {
        switch( regionGroup )
        {
            case LORAMAC_REGION_AS923_JPN:
                params.initAs923Group = AS923_GROUP_1_JPN;
                break;
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
            case LORAMAC_REGION_AS923_2:
                params.initAs923Group = AS923_GROUP_2;
                break;
            case LORAMAC_REGION_AS923_3:
                params.initAs923Group = AS923_GROUP_3;
                break;
            case LORAMAC_REGION_AS923_4:
                params.initAs923Group = AS923_GROUP_4;
                break;
#endif
            default:  //= case LORAMAC_REGION_AS923:
                params.initAs923Group = AS923_GROUP_1;
                break;
        }
    }
#endif
    RegionInitDefaults( MacCtx.NvmCtx->Region, &params );
    // Reset to defaults (Parameters are fetched now)
    MacCtx.NvmCtx->DutyCycleOn                            = params.DefaultDutyCycleOn;         // PHY_DUTY_CYCLE
    MacCtx.NvmCtx->MacParamsDefaults.ChannelsTxPower      = params.DefaultChannelsTxPower;     // PHY_DEF_TX_POWER
    MacCtx.NvmCtx->MacParamsDefaults.ChannelsDatarate     = params.DefaultChannelsDatarate;    // PHY_DEF_TX_DR
    MacCtx.NvmCtx->MacParamsDefaults.MaxRxWindow          = params.DefaultMaxRxWindow;         // PHY_MAX_RX_WINDOW
    MacCtx.NvmCtx->MacParamsDefaults.ReceiveDelay1        = params.DefaultReceiveDelay1;       // PHY_RECEIVE_DELAY1
    MacCtx.NvmCtx->MacParamsDefaults.ReceiveDelay2        = params.DefaultReceiveDelay2;       // PHY_RECEIVE_DELAY2
    MacCtx.NvmCtx->MacParamsDefaults.JoinAcceptDelay1     = params.DefaultJoinAcceptDelay1;    // PHY_JOIN_ACCEPT_DELAY1
    MacCtx.NvmCtx->MacParamsDefaults.JoinAcceptDelay2     = params.DefaultJoinAcceptDelay2;    // PHY_JOIN_ACCEPT_DELAY2
    MacCtx.NvmCtx->MacParamsDefaults.Rx1DrOffset          = params.DefaultRx1DrOffset;         // PHY_DEF_DR1_OFFSET
    MacCtx.NvmCtx->MacParamsDefaults.Rx2Channel.Frequency = params.DefaultRx2Frequency;        // PHY_DEF_RX2_FREQUENCY
    MacCtx.NvmCtx->MacParamsDefaults.RxCChannel.Frequency = params.DefaultRx2Frequency;
    MacCtx.NvmCtx->MacParamsDefaults.Rx2Channel.Datarate  = params.DefaultRx2Dr;               // PHY_DEF_RX2_DR
    MacCtx.NvmCtx->MacParamsDefaults.RxCChannel.Datarate  = params.DefaultRx2Dr;
    MacCtx.NvmCtx->MacParamsDefaults.UplinkDwellTime      = params.DefaultUplinkDwellTime;     // PHY_DEF_UPLINK_DWELL_TIME
    MacCtx.NvmCtx->MacParamsDefaults.DownlinkDwellTime    = params.DefaultDownlinkDwellTime;   // PHY_DEF_DOWNLINK_DWELL_TIME
    MacCtx.NvmCtx->MacParamsDefaults.MaxEirp              = params.DefaultMaxEirp;             // PHY_DEF_MAX_EIRP
    MacCtx.NvmCtx->MacParamsDefaults.AntennaGain          = params.DefaultAntennaGain;         // PHY_DEF_ANTENNA_GAIN
    MacCtx.AdrAckLimit                                    = params.DefaultAdrAckLimit;         // PHY_DEF_ADR_ACK_LIMIT
    MacCtx.AdrAckDelay                                    = params.DefaultAdrAckDelay;         // PHY_DEF_ADR_ACK_DELAY

    // Init parameters which are not set in function ResetMacParameters
    MacCtx.NvmCtx->MacParamsDefaults.ChannelsNbTrans  = 1;
    MacCtx.NvmCtx->MacParamsDefaults.SystemMaxRxError = 10;
    // Board-default robustness policy for VK_RA4M2 + Wio-SX1262:
    // MinRxSymbols=6 yields ~6.1 ms RX window at SF7, shorter than the
    // 8-symbol JoinAccept preamble (~8.2 ms) — chip cannot lock and
    // every OTAA fails with RX2_TIMEOUT. 24 gives ~24 ms window, safe
    // across SF7..SF12. Was previously forced via Python boilerplate
    // mac.set_min_rx_symbols(24). See project_lorawan_otaa_success.
    MacCtx.NvmCtx->MacParamsDefaults.MinRxSymbols     = 24;
    MacCtx.NvmCtx->MacParamsDefaults.EnableCca        = LORAMAC_ENABLE_CCA_DEFAULT;

    MacCtx.NvmCtx->MacParams.SystemMaxRxError = MacCtx.NvmCtx->MacParamsDefaults.SystemMaxRxError;
    MacCtx.NvmCtx->MacParams.MinRxSymbols = MacCtx.NvmCtx->MacParamsDefaults.MinRxSymbols;
    MacCtx.NvmCtx->MacParams.MaxRxWindow = MacCtx.NvmCtx->MacParamsDefaults.MaxRxWindow;
    MacCtx.NvmCtx->MacParams.ReceiveDelay1 = MacCtx.NvmCtx->MacParamsDefaults.ReceiveDelay1;
    MacCtx.NvmCtx->MacParams.ReceiveDelay2 = MacCtx.NvmCtx->MacParamsDefaults.ReceiveDelay2;
    MacCtx.NvmCtx->MacParams.JoinAcceptDelay1 = MacCtx.NvmCtx->MacParamsDefaults.JoinAcceptDelay1;
    MacCtx.NvmCtx->MacParams.JoinAcceptDelay2 = MacCtx.NvmCtx->MacParamsDefaults.JoinAcceptDelay2;
    MacCtx.NvmCtx->MacParams.ChannelsNbTrans = MacCtx.NvmCtx->MacParamsDefaults.ChannelsNbTrans;

    // FPort 224 is enabled by default.
    MacCtx.NvmCtx->IsCertPortOn = true;

    MacCtx.NvmCtx->PublicNetwork = true;

    MacCtx.MacPrimitives = primitives;
    MacCtx.MacCallbacks = callbacks;
    MacCtx.MacFlags.Value = 0;
    MacCtx.MacState = LORAMAC_STOPPED;

    // Reset duty cycle times
    MacCtx.NvmCtx->LastTxDoneTime = 0;
    MacCtx.NvmCtx->AggregatedTimeOff = 0;

    ResetMacParameters( );

    // Initialize timers
    TimerInit( &MacCtx.TxDelayedTimer, OnTxDelayedTimerEvent );
    TimerInit( &MacCtx.RxWindowTimer1, OnRxWindow1TimerEvent );
    TimerInit( &MacCtx.RxWindowTimer2, OnRxWindow2TimerEvent );
    TimerInit( &MacCtx.AckTimeoutTimer, OnAckTimeoutTimerEvent );

    // Store the current initialization time
    MacCtx.NvmCtx->InitializationTime = SysTimeGetMcuTime( );

    // Initialize Radio driver
    MacCtx.RadioEvents.TxDone = OnRadioTxDone;
    MacCtx.RadioEvents.RxDone = OnRadioRxDone;
    MacCtx.RadioEvents.RxError = OnRadioRxError;
    MacCtx.RadioEvents.TxTimeout = OnRadioTxTimeout;
    MacCtx.RadioEvents.RxTimeout = OnRadioRxTimeout;
    if (LORAMAC_RADIO_INIT( &MacCtx.RadioEvents ) != RADIO_SUCCESS)
    {
        return LORAMAC_STATUS_RADIO_FAIL;
    }

    // Initialize the Secure Element driver
    if( SecureElementInit( NULL ) != SECURE_ELEMENT_SUCCESS )
    {
        return LORAMAC_STATUS_CRYPTO_ERROR;
    }

    // Initialize Crypto module
    if( LoRaMacCryptoInit( NULL ) != LORAMAC_CRYPTO_SUCCESS )
    {
        return LORAMAC_STATUS_CRYPTO_ERROR;
    }

    // Initialize MAC commands module
    if( LoRaMacCommandsInit( NULL ) != LORAMAC_COMMANDS_SUCCESS )
    {
        return LORAMAC_STATUS_MAC_COMMAD_ERROR;
    }

#if (LORAMAC_MAX_MC_CTX > 0)
    // Set multicast downlink counter reference
    if( LoRaMacCryptoSetMulticastReference( MacCtx.NvmCtx->MulticastChannelList ) != LORAMAC_CRYPTO_SUCCESS )
    {
        return LORAMAC_STATUS_CRYPTO_ERROR;
    }
#endif

    // Random seed initialization
    srand1( Radio.Random( ) );

    // not call Radio.SetPublicNetwork() here
    LORAMAC_RADIOSLEEP_MACINIT( 0 );

#ifdef LORAMAC_CLASSB_ENABLED
    // Initialize class b
    // Apply callback
    classBCallbacks.GetTemperatureLevel = NULL;
    classBCallbacks.MacProcessNotify = NULL;
    if( callbacks != NULL )
    {
        classBCallbacks.GetTemperatureLevel = callbacks->GetTemperatureLevel;
        classBCallbacks.MacProcessNotify = callbacks->MacProcessNotify;
    }

    // Must all be static. Don't use local references.
    classBParams.MlmeIndication = &MacCtx.MlmeIndication;
    classBParams.McpsIndication = &MacCtx.McpsIndication;
    classBParams.MlmeConfirm = &MacCtx.MlmeConfirm;
    classBParams.LoRaMacFlags = &MacCtx.MacFlags;
    classBParams.LoRaMacDevAddr = &MacCtx.NvmCtx->DevAddr;
    classBParams.LoRaMacRegion = &MacCtx.NvmCtx->Region;
    classBParams.LoRaMacParams = &MacCtx.NvmCtx->MacParams;
#if (LORAMAC_MAX_MC_CTX > 0)
    classBParams.MulticastChannels = &MacCtx.NvmCtx->MulticastChannelList[0];
#endif
    classBParams.NetworkActivation = &MacCtx.NvmCtx->NetworkActivation;

    LoRaMacClassBInit( &classBParams, &classBCallbacks, NULL );
#endif

    LoRaMacEnableRequests( LORAMAC_REQUEST_HANDLING_ON );

    return LORAMAC_STATUS_OK;
}

LoRaMacStatus_t LoRaMacStart( void )
{
    LoRaMacStatus_t ret = LORAMAC_STATUS_OK;

    if (MacCtx.MacState == LORAMAC_STOPPED)
    {
#if defined(RP_USE_RADIO_CFG_CHECK)
        InitDefaultsParams_t params;

        // init radio cfg
        params.Type   = INIT_TYPE_RADIO_CFG;
        params.NvmCtx = NULL;
        RegionInitDefaults( MacCtx.NvmCtx->Region, &params );
#endif

        MacCtx.MacState = LORAMAC_IDLE;
        LORAMAC_RADIO_SET_LORAMODE();

        if( MacCtx.NvmCtx->DeviceClass == CLASS_A )
        {
#ifdef LORAMAC_CLASSB_ENABLED
            if( LoRaMacClassBIsBeaconModeActive() == true )
            {
                LoRaMacClassBResumeBeaconing();
            }
#endif
        }
#ifdef LORAMAC_CLASSB_ENABLED
        else if( MacCtx.NvmCtx->DeviceClass == CLASS_B )
        {
            LoRaMacClassBResumeBeaconing();
        }
#endif
        else if( MacCtx.NvmCtx->DeviceClass == CLASS_C )
        {
            MacCtx.NvmCtx->DeviceClass = CLASS_A;  // temporarily change to swich class C
            SwitchClass( CLASS_C );
        }
        else
        {
            // nothing to do
        }
    }
    else
    {
        ret = LORAMAC_STATUS_BUSY;
    }

    return( ret );
}

LoRaMacStatus_t LoRaMacStop( void )
{
    if( LoRaMacIsBusy( ) == false )
    {
        if( MacCtx.NvmCtx->DeviceClass == CLASS_A )
        {
#ifdef LORAMAC_CLASSB_ENABLED
            if( LoRaMacClassBIsAcquisitionInProgress() == true )
            {
                // Stop beaconing
                LoRaMacClassBStopBeaconig();
                if( LoRaMacConfirmQueueIsCmdActive( MLME_BEACON_ACQUISITION ) == true )
                {
                    MacCtx.MacFlags.Bits.MacDone = 1;
                }
            }
            else if( LoRaMacClassBIsBeaconModeActive() == true )
            {
                LoRaMacClassBHaltBeaconing();
            }
            else
            {
                // nothing to do
            }
#endif
        }
#ifdef LORAMAC_CLASSB_ENABLED
        else if( MacCtx.NvmCtx->DeviceClass == CLASS_B )
        {
            LoRaMacClassBHaltBeaconing();
        }
#endif
        else if( MacCtx.NvmCtx->DeviceClass == CLASS_C )
        {
            SwitchClass( CLASS_A );  // to stop continuous RX
            MacCtx.NvmCtx->DeviceClass = CLASS_C;  // keep DeviceClass = Class C
        }
        else
        {
            return LORAMAC_STATUS_SERVICE_UNKNOWN;
        }

        // discard Rx event
        LoRaMacRadioEvents.Events.RxProcessPending = 0;
        LoRaMacRadioEvents.Events.RxDone = 0;

        MacCtx.MacState = LORAMAC_STOPPED;
        return LORAMAC_STATUS_OK;
    }
    else if( MacCtx.MacState == LORAMAC_STOPPED )
    {
        return LORAMAC_STATUS_OK;
    }

    return LORAMAC_STATUS_BUSY;
}

LoRaMacStatus_t LoRaMacQueryTxPossible( uint8_t size, LoRaMacTxInfo_t* txInfo )
{
    CalcNextAdrParams_t adrNext;
    uint32_t adrAckCounter;
    int8_t datarate;
    int8_t txPower;
    uint8_t nbTrans;
    size_t macCmdsSize = 0;

    if( txInfo == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

    // Setup ADR request
#if (LORAMAC_VERSION >= 0x01010000)
    adrAckCounter = MacCtx.NvmCtx->AdrAckCounter;
    datarate      = MacCtx.NvmCtx->MacParamsDefaults.ChannelsDatarate;
    txPower       = MacCtx.NvmCtx->MacParamsDefaults.ChannelsTxPower;
    nbTrans       = MacCtx.NvmCtx->MacParams.ChannelsNbTrans;
    adrNext.Version = MacCtx.NvmCtx->Version;
#endif
    adrNext.UpdateChanMask = false;
    adrNext.AdrEnabled = MacCtx.NvmCtx->AdrCtrlOn;
    adrNext.AdrAckCounter = MacCtx.NvmCtx->AdrAckCounter;
    adrNext.AdrAckLimit = MacCtx.AdrAckLimit;
    adrNext.AdrAckDelay = MacCtx.AdrAckDelay;
    adrNext.Datarate = MacCtx.NvmCtx->MacParams.ChannelsDatarate;
    adrNext.TxPower = MacCtx.NvmCtx->MacParams.ChannelsTxPower;
    adrNext.NbTrans = MacCtx.NvmCtx->MacParams.ChannelsNbTrans;
    adrNext.UplinkDwellTime = MacCtx.NvmCtx->MacParams.UplinkDwellTime;
    adrNext.Region = MacCtx.NvmCtx->Region;

    // We call the function for information purposes only. We don't want to
    // apply the datarate, the tx power and the ADR ack counter.
    LoRaMacAdrCalcNext( &adrNext, &datarate, &txPower, &nbTrans, &adrAckCounter );

    txInfo->CurrentPossiblePayloadSize = GetMaxAppPayloadWithoutFOptsLength( datarate );

    if( LoRaMacCommandsGetSizeSerializedCmds( &macCmdsSize ) != LORAMAC_COMMANDS_SUCCESS )
    {
        return LORAMAC_STATUS_MAC_COMMAD_ERROR;
    }

    // Verify if the MAC commands fit into the FOpts and into the maximum payload.
    if( ( LORA_MAC_COMMAND_MAX_FOPTS_LENGTH >= macCmdsSize ) && ( txInfo->CurrentPossiblePayloadSize >= macCmdsSize ) )
    {
        txInfo->MaxPossibleApplicationDataSize = txInfo->CurrentPossiblePayloadSize - macCmdsSize;

        // Verify if the application data together with MAC command fit into the maximum payload.
        if( txInfo->CurrentPossiblePayloadSize >= ( macCmdsSize + size ) )
        {
            return LORAMAC_STATUS_OK;
        }
        else
        {
           return LORAMAC_STATUS_LENGTH_ERROR;
        }
    }
    else
    {
        txInfo->MaxPossibleApplicationDataSize = 0;
        return LORAMAC_STATUS_LENGTH_ERROR;
    }
}

LoRaMacStatus_t LoRaMacMibGetRequestConfirm( MibRequestConfirm_t* mibGet )
{
    LoRaMacStatus_t status = LORAMAC_STATUS_OK;
    GetPhyParams_t getPhy;
    PhyParam_t phyParam;

    if( mibGet == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

    switch( mibGet->Type )
    {
        case MIB_DEVICE_CLASS:
        {
            mibGet->Param.Class = MacCtx.NvmCtx->DeviceClass;
            break;
        }
        case MIB_NETWORK_ACTIVATION:
        {
            mibGet->Param.NetworkActivation = MacCtx.NvmCtx->NetworkActivation;
            break;
        }
        case MIB_DEV_EUI:
        {
            mibGet->Param.DevEui = SecureElementGetDevEui( );
            break;
        }
        // Also in case of MIB_APP_EUI execute the following process.
        // JoinEui and AppEui are in the same union.
        case MIB_JOIN_EUI:
        {
            mibGet->Param.JoinEui = SecureElementGetJoinEui( );
            break;
        }
        case MIB_ADR:
        {
            mibGet->Param.AdrEnable = MacCtx.NvmCtx->AdrCtrlOn;
            break;
        }
        case MIB_NET_ID:
        {
            mibGet->Param.NetID = MacCtx.NvmCtx->NetID;
            break;
        }
        case MIB_DEV_ADDR:
        {
            mibGet->Param.DevAddr = MacCtx.NvmCtx->DevAddr;
            break;
        }
        case MIB_PUBLIC_NETWORK:
        {
            mibGet->Param.EnablePublicNetwork = MacCtx.NvmCtx->PublicNetwork;
            break;
        }
        case MIB_CHANNELS:
        {
            getPhy.Attribute = PHY_CHANNELS;
            phyParam = RegionGetPhyParam( MacCtx.NvmCtx->Region, &getPhy );

            mibGet->Param.ChannelList = phyParam.Channels;
            break;
        }
        case MIB_RX2_CHANNEL:
        {
            mibGet->Param.Rx2Channel = MacCtx.NvmCtx->MacParams.Rx2Channel;
            break;
        }
        case MIB_RX2_DEFAULT_CHANNEL:
        {
            mibGet->Param.Rx2Channel = MacCtx.NvmCtx->MacParamsDefaults.Rx2Channel;
            break;
        }
        case MIB_RXC_CHANNEL:
        {
            mibGet->Param.RxCChannel = MacCtx.NvmCtx->MacParams.RxCChannel;
            break;
        }
        case MIB_RXC_DEFAULT_CHANNEL:
        {
            mibGet->Param.RxCChannel = MacCtx.NvmCtx->MacParamsDefaults.RxCChannel;
            break;
        }
        case MIB_CHANNELS_DEFAULT_MASK:
        {
            getPhy.Attribute = PHY_CHANNELS_DEFAULT_MASK;
            phyParam = RegionGetPhyParam( MacCtx.NvmCtx->Region, &getPhy );

            mibGet->Param.ChannelsDefaultMask = phyParam.ChannelsMask;
            break;
        }
        case MIB_CHANNELS_MASK:
        {
            getPhy.Attribute = PHY_CHANNELS_MASK;
            phyParam = RegionGetPhyParam( MacCtx.NvmCtx->Region, &getPhy );

            mibGet->Param.ChannelsMask = phyParam.ChannelsMask;
            break;
        }
        case MIB_CHANNELS_NB_TRANS:
        {
            mibGet->Param.ChannelsNbTrans = MacCtx.NvmCtx->MacParams.ChannelsNbTrans;
            break;
        }
        case MIB_MAX_RX_WINDOW_DURATION:
        {
            mibGet->Param.MaxRxWindow = MacCtx.NvmCtx->MacParams.MaxRxWindow;
            break;
        }
        case MIB_RECEIVE_DELAY_1:
        {
            mibGet->Param.ReceiveDelay1 = MacCtx.NvmCtx->MacParams.ReceiveDelay1;
            break;
        }
        case MIB_RECEIVE_DELAY_2:
        {
            mibGet->Param.ReceiveDelay2 = MacCtx.NvmCtx->MacParams.ReceiveDelay2;
            break;
        }
        case MIB_JOIN_ACCEPT_DELAY_1:
        {
            mibGet->Param.JoinAcceptDelay1 = MacCtx.NvmCtx->MacParams.JoinAcceptDelay1;
            break;
        }
        case MIB_JOIN_ACCEPT_DELAY_2:
        {
            mibGet->Param.JoinAcceptDelay2 = MacCtx.NvmCtx->MacParams.JoinAcceptDelay2;
            break;
        }
        case MIB_CHANNELS_MIN_TX_DATARATE:
        {
            getPhy.Attribute = PHY_MIN_TX_DR;
            getPhy.UplinkDwellTime = MacCtx.NvmCtx->MacParams.UplinkDwellTime;
            phyParam = RegionGetPhyParam( MacCtx.NvmCtx->Region, &getPhy );

            mibGet->Param.ChannelsMinTxDatarate = phyParam.Value;
            break;
        }
        case MIB_CHANNELS_DEFAULT_DATARATE:
        {
            mibGet->Param.ChannelsDefaultDatarate = MacCtx.NvmCtx->MacParamsDefaults.ChannelsDatarate;
            break;
        }
        case MIB_CHANNELS_DATARATE:
        {
            mibGet->Param.ChannelsDatarate = MacCtx.NvmCtx->MacParams.ChannelsDatarate;
            break;
        }
        case MIB_CHANNELS_DEFAULT_TX_POWER:
        {
            mibGet->Param.ChannelsDefaultTxPower = MacCtx.NvmCtx->MacParamsDefaults.ChannelsTxPower;
            break;
        }
        case MIB_CHANNELS_TX_POWER:
        {
            mibGet->Param.ChannelsTxPower = MacCtx.NvmCtx->MacParams.ChannelsTxPower;
            break;
        }
        case MIB_SYSTEM_MAX_RX_ERROR:
        {
            mibGet->Param.SystemMaxRxError = MacCtx.NvmCtx->MacParams.SystemMaxRxError;
            break;
        }
        case MIB_MIN_RX_SYMBOLS:
        {
            mibGet->Param.MinRxSymbols = MacCtx.NvmCtx->MacParams.MinRxSymbols;
            break;
        }
        case MIB_ANTENNA_GAIN:
        {
            mibGet->Param.AntennaGain = MacCtx.NvmCtx->MacParams.AntennaGain;
            break;
        }
#ifdef LORAMAC_CTX_SAVERESTORE_ENALED
        case MIB_NVM_CTXS:
        {
            mibGet->Param.Contexts = GetCtxs( );
            break;
        }
#endif
        case MIB_DEFAULT_ANTENNA_GAIN:
        {
            mibGet->Param.DefaultAntennaGain = MacCtx.NvmCtx->MacParamsDefaults.AntennaGain;
            break;
        }
        case MIB_DUTY_CYCLE:
        {
            getPhy.Attribute = PHY_DUTY_CYCLE;
            phyParam = RegionGetPhyParam( MacCtx.NvmCtx->Region, &getPhy );
            mibGet->Param.DCycleEnabled = phyParam.Value;
            break;
        }
        case MIB_MAX_TX_DR:
        {
            getPhy.Attribute = PHY_MAX_TX_DR;
            phyParam = RegionGetPhyParam( MacCtx.NvmCtx->Region, &getPhy );
            mibGet->Param.MaxTxDr = phyParam.Value;
            break;
        }
        case MIB_MIN_TX_DR:
        {
            getPhy.Attribute = PHY_MIN_TX_DR;
            getPhy.UplinkDwellTime = MacCtx.NvmCtx->MacParams.UplinkDwellTime;
            phyParam = RegionGetPhyParam( MacCtx.NvmCtx->Region, &getPhy );
            mibGet->Param.MinTxDr = phyParam.Value;
            break;
        }
        case MIB_AS923_ENABLE_CCA:
        {
            mibGet->Param.EnableCca = MacCtx.NvmCtx->MacParams.EnableCca;
            break;
        }
        case MIB_IS_CERT_FPORT_ON:
        {
            mibGet->Param.IsCertPortOn = MacCtx.NvmCtx->IsCertPortOn;
            break;
        }
        case MIB_DEV_NONCE:
        {
            mibGet->Param.devNonce = LoRaMacCryptoGetDevNonce();
            break;
        }

        case MIB_APP_NONCE:
        {
            mibGet->Param.appNonce = LoRaMacCryptoGetJoinNonce();
            break;
        }

        case MIB_MAX_DCYCLE:
        {
            mibGet->Param.maxDcycle = MacCtx.NvmCtx->MaxDCycle;
            break;
        }

        case MIB_RX1_DROFFSET:
        {
            mibGet->Param.rx1DrOffset = MacCtx.NvmCtx->MacParams.Rx1DrOffset;
            break;
        }

        case MIB_MAX_EIRP:
        {
            mibGet->Param.maxEirp = MacCtx.NvmCtx->MacParams.MaxEirp;
            break;
        }

        case MIB_DOWNLINK_DWELLTIME:
        {
            mibGet->Param.downlinkDwellTime = MacCtx.NvmCtx->MacParams.DownlinkDwellTime;
            break;
        }

        case MIB_UPLINK_DWELLTIME:
        {
            mibGet->Param.uplinkDwellTime = MacCtx.NvmCtx->MacParams.UplinkDwellTime;
            break;
        }

        case MIB_DOWNLINK_FCNT:
        {
            mibGet->Param.downlinkFCnt = LoRaMacCryptoGetDownlinkFCnt();
            break;
        }

        case MIB_UPLINK_FCNT:
        {
            mibGet->Param.uplinkFCnt = LoRaMacCryptoGetUplinkFCnt();
            break;
        }
        case MIB_RSSI_FREE_THRESHOLD:
        {
#if defined(REGION_AS923) || defined(REGION_KR920)
            if( ( MacCtx.NvmCtx->Region == LORAMAC_REGION_AS923 ) ||
                ( MacCtx.NvmCtx->Region == LORAMAC_REGION_KR920 ) )
            {
                getPhy.Attribute = PHY_RSSI_FREE_THRESHOLD;
                phyParam = RegionGetPhyParam( MacCtx.NvmCtx->Region, &getPhy );
                mibGet->Param.RssiFreeThreshold = phyParam.RssiFreeThreshold;
            }
            else
#endif
            {
                status = LORAMAC_STATUS_ERROR;
            }
            break;
        }
        case MIB_CARRIER_SENSE_TIME:
        {
#if defined(REGION_AS923) || defined(REGION_KR920)
            if( ( MacCtx.NvmCtx->Region == LORAMAC_REGION_AS923 ) ||
                ( MacCtx.NvmCtx->Region == LORAMAC_REGION_KR920 ) )
            {
                getPhy.Attribute = PHY_CARRIER_SENSE_TIME;
                phyParam = RegionGetPhyParam( MacCtx.NvmCtx->Region, &getPhy );
                mibGet->Param.CarrierSenseTime = phyParam.CarrierSenseTime;
            }
            else
#endif
            {
                status = LORAMAC_STATUS_ERROR;
            }
            break;
        }
        default:
        {
#ifdef LORAMAC_CLASSB_ENABLED
            status = LoRaMacClassBMibGetRequestConfirm( mibGet );
#else
            status = LORAMAC_STATUS_SERVICE_UNKNOWN;
#endif
            break;
        }
    }
    return status;
}

LoRaMacStatus_t LoRaMacMibSetRequestConfirm( MibRequestConfirm_t* mibSet )
{
    LoRaMacStatus_t status = LORAMAC_STATUS_OK;
    ChanMaskSetParams_t chanMaskSet;
    VerifyParams_t verify;
    SetPhyParams_t setPhy;

    if( mibSet == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }
    if( ( MacCtx.MacState & LORAMAC_TX_RUNNING ) == LORAMAC_TX_RUNNING )
    {
        return LORAMAC_STATUS_BUSY;
    }

    switch( mibSet->Type )
    {
        case MIB_DEVICE_CLASS:
        {
            status = SwitchClass( mibSet->Param.Class );
            break;
        }
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
        case MIB_NETWORK_ACTIVATION:
        {
            if( mibSet->Param.NetworkActivation != ACTIVATION_TYPE_OTAA )
            {
                MacCtx.NvmCtx->NetworkActivation = mibSet->Param.NetworkActivation;
            }
            else
            {   // Do not allow to set ACTIVATION_TYPE_OTAA since the MAC will set it automatically after a successful join process.
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
#endif
        case MIB_DEV_EUI:
        {
            if( SecureElementSetDevEui( mibSet->Param.DevEui ) != SECURE_ELEMENT_SUCCESS )
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        // Also in case of MIB_APP_EUI execute the following process.
        // JoinEui and AppEui are in the same union.
        case MIB_JOIN_EUI:
        {
            if( SecureElementSetJoinEui( mibSet->Param.JoinEui ) != SECURE_ELEMENT_SUCCESS )
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_ADR:
        {
            MacCtx.NvmCtx->AdrCtrlOn = mibSet->Param.AdrEnable;
            break;
        }
        case MIB_NET_ID:
        {
            MacCtx.NvmCtx->NetID = mibSet->Param.NetID;
            break;
        }
        case MIB_DEV_ADDR:
        {
            MacCtx.NvmCtx->DevAddr = mibSet->Param.DevAddr;
            break;
        }
        case MIB_GEN_APP_KEY:
        {
            if( mibSet->Param.GenAppKey != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( GEN_APP_KEY, mibSet->Param.GenAppKey ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_APP_KEY:
        case MIB_NWK_KEY:
        {
            if( mibSet->Param.NwkKey != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( NWK_KEY, mibSet->Param.NwkKey ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
#if (LORAMAC_VERSION >= 0x01010000)  // LW11x
        case MIB_J_S_INT_KEY:
        {
            if( mibSet->Param.JSIntKey != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( J_S_INT_KEY, mibSet->Param.JSIntKey ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_J_S_ENC_KEY:
        {
            if( mibSet->Param.JSEncKey != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( J_S_ENC_KEY, mibSet->Param.JSEncKey ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_F_NWK_S_INT_KEY:
        {
            if( mibSet->Param.FNwkSIntKey != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( F_NWK_S_INT_KEY, mibSet->Param.FNwkSIntKey ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_S_NWK_S_INT_KEY:
        {
            if( mibSet->Param.SNwkSIntKey != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( S_NWK_S_INT_KEY, mibSet->Param.SNwkSIntKey ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
#endif  // LW11x
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
        // Also in case of MIB_NWK_SKEY execute the following process.
        // NwkSEncKey and NwkSKey are in the same union.
        case MIB_NWK_S_ENC_KEY:
        {
            if( mibSet->Param.NwkSEncKey != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( NWK_S_ENC_KEY, mibSet->Param.NwkSEncKey ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        // Also in case of MIB_APP_SKEY execute the following process.
        case MIB_APP_S_KEY:
        {
            if( mibSet->Param.AppSKey != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( APP_S_KEY, mibSet->Param.AppSKey ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
#endif
#if (LORAMAC_MAX_MC_CTX > 0)
        case MIB_MC_KE_KEY:
        {
            if( mibSet->Param.McKEKey != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( MC_KE_KEY, mibSet->Param.McKEKey ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_MC_KEY_0:
        {
            if( mibSet->Param.McKey0 != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( MC_KEY_0, mibSet->Param.McKey0 ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_MC_APP_S_KEY_0:
        {
            if( mibSet->Param.McAppSKey0 != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( MC_APP_S_KEY_0, mibSet->Param.McAppSKey0 ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_MC_NWK_S_KEY_0:
        {
            if( mibSet->Param.McNwkSKey0 != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( MC_NWK_S_KEY_0, mibSet->Param.McNwkSKey0 ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
#endif
#if (LORAMAC_MAX_MC_CTX > 1)
        case MIB_MC_KEY_1:
        {
            if( mibSet->Param.McKey1 != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( MC_KEY_1, mibSet->Param.McKey1 ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_MC_APP_S_KEY_1:
        {
            if( mibSet->Param.McAppSKey1 != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( MC_APP_S_KEY_1, mibSet->Param.McAppSKey1 ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_MC_NWK_S_KEY_1:
        {
            if( mibSet->Param.McNwkSKey1 != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( MC_NWK_S_KEY_1, mibSet->Param.McNwkSKey1 ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
#endif
#if (LORAMAC_MAX_MC_CTX > 2)
        case MIB_MC_KEY_2:
        {
            if( mibSet->Param.McKey2 != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( MC_KEY_2, mibSet->Param.McKey2 ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_MC_APP_S_KEY_2:
        {
            if( mibSet->Param.McAppSKey2 != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( MC_APP_S_KEY_2, mibSet->Param.McAppSKey2 ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_MC_NWK_S_KEY_2:
        {
            if( mibSet->Param.McNwkSKey2 != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( MC_NWK_S_KEY_2, mibSet->Param.McNwkSKey2 ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
#endif
#if (LORAMAC_MAX_MC_CTX > 3)
        case MIB_MC_KEY_3:
        {
            if( mibSet->Param.McKey3 != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( MC_KEY_3, mibSet->Param.McKey3 ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_MC_APP_S_KEY_3:
        {
            if( mibSet->Param.McAppSKey3 != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( MC_APP_S_KEY_3, mibSet->Param.McAppSKey3 ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_MC_NWK_S_KEY_3:
        {
            if( mibSet->Param.McNwkSKey3 != NULL )
            {
                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetKey( MC_NWK_S_KEY_3, mibSet->Param.McNwkSKey3 ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
#endif
        case MIB_PUBLIC_NETWORK:
        {
            MacCtx.NvmCtx->PublicNetwork = mibSet->Param.EnablePublicNetwork;
            // not call Radio.SetPublicNetwork() here
            break;
        }
        case MIB_RX2_CHANNEL:
        {
            verify.DatarateParams.Datarate = mibSet->Param.Rx2Channel.Datarate;
            verify.DatarateParams.DownlinkDwellTime = MacCtx.NvmCtx->MacParams.DownlinkDwellTime;

            if( RegionVerify( MacCtx.NvmCtx->Region, &verify, PHY_RX_DR ) == true )
            {
                MacCtx.NvmCtx->MacParams.Rx2Channel = mibSet->Param.Rx2Channel;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_RX2_DEFAULT_CHANNEL:
        {
            verify.DatarateParams.Datarate = mibSet->Param.Rx2Channel.Datarate;
            verify.DatarateParams.DownlinkDwellTime = MacCtx.NvmCtx->MacParams.DownlinkDwellTime;

            if( RegionVerify( MacCtx.NvmCtx->Region, &verify, PHY_RX_DR ) == true )
            {
                MacCtx.NvmCtx->MacParamsDefaults.Rx2Channel = mibSet->Param.Rx2DefaultChannel;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_RXC_CHANNEL:
        {
            verify.DatarateParams.Datarate = mibSet->Param.RxCChannel.Datarate;
            verify.DatarateParams.DownlinkDwellTime = MacCtx.NvmCtx->MacParams.DownlinkDwellTime;

            if( RegionVerify( MacCtx.NvmCtx->Region, &verify, PHY_RX_DR ) == true )
            {
                MacCtx.NvmCtx->MacParams.RxCChannel = mibSet->Param.RxCChannel;

                if( ( MacCtx.NvmCtx->DeviceClass == CLASS_C ) && ( MacCtx.NvmCtx->NetworkActivation != ACTIVATION_TYPE_NONE ) )
                {
                    // We can only compute the RX window parameters directly, if we are already
                    // in class c mode and joined. We cannot setup an RX window in case of any other
                    // class type.
                    Radio.Standby();  // stop Rx before setting class c multicast rx
                    OpenContinuousRxCWindow( );
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_RXC_DEFAULT_CHANNEL:
        {
            verify.DatarateParams.Datarate = mibSet->Param.RxCChannel.Datarate;
            verify.DatarateParams.DownlinkDwellTime = MacCtx.NvmCtx->MacParams.DownlinkDwellTime;

            if( RegionVerify( MacCtx.NvmCtx->Region, &verify, PHY_RX_DR ) == true )
            {
                MacCtx.NvmCtx->MacParamsDefaults.RxCChannel = mibSet->Param.RxCDefaultChannel;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_CHANNELS_DEFAULT_MASK:
        {
            chanMaskSet.ChannelsMaskIn = mibSet->Param.ChannelsDefaultMask;
            chanMaskSet.ChannelsMaskType = CHANNELS_DEFAULT_MASK;

            if( RegionChanMaskSet( MacCtx.NvmCtx->Region, &chanMaskSet ) == false )
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_CHANNELS_MASK:
        {
            chanMaskSet.ChannelsMaskIn = mibSet->Param.ChannelsMask;
            chanMaskSet.ChannelsMaskType = CHANNELS_MASK;

            if( RegionChanMaskSet( MacCtx.NvmCtx->Region, &chanMaskSet ) == false )
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_CHANNELS_NB_TRANS:
        {
            if( ( mibSet->Param.ChannelsNbTrans >= 1 ) &&
                ( mibSet->Param.ChannelsNbTrans <= 15 ) )
            {
                MacCtx.NvmCtx->MacParams.ChannelsNbTrans = mibSet->Param.ChannelsNbTrans;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_MAX_RX_WINDOW_DURATION:
        {
            MacCtx.NvmCtx->MacParams.MaxRxWindow = mibSet->Param.MaxRxWindow;
            break;
        }
        case MIB_RECEIVE_DELAY_1:
        {
            MacCtx.NvmCtx->MacParams.ReceiveDelay1 = mibSet->Param.ReceiveDelay1;
            break;
        }
        case MIB_RECEIVE_DELAY_2:
        {
            MacCtx.NvmCtx->MacParams.ReceiveDelay2 = mibSet->Param.ReceiveDelay2;
            break;
        }
        case MIB_JOIN_ACCEPT_DELAY_1:
        {
            MacCtx.NvmCtx->MacParams.JoinAcceptDelay1 = mibSet->Param.JoinAcceptDelay1;
            break;
        }
        case MIB_JOIN_ACCEPT_DELAY_2:
        {
            MacCtx.NvmCtx->MacParams.JoinAcceptDelay2 = mibSet->Param.JoinAcceptDelay2;
            break;
        }
        case MIB_CHANNELS_DEFAULT_DATARATE:
        {
            verify.DatarateParams.Datarate = mibSet->Param.ChannelsDefaultDatarate;

            if( RegionVerify( MacCtx.NvmCtx->Region, &verify, PHY_DEF_TX_DR ) == true )
            {
                MacCtx.NvmCtx->MacParamsDefaults.ChannelsDatarate = verify.DatarateParams.Datarate;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_CHANNELS_DATARATE:
        {
            verify.DatarateParams.Datarate = mibSet->Param.ChannelsDatarate;
            verify.DatarateParams.UplinkDwellTime = MacCtx.NvmCtx->MacParams.UplinkDwellTime;

            if( RegionVerify( MacCtx.NvmCtx->Region, &verify, PHY_TX_DR ) == true )
            {
                MacCtx.NvmCtx->MacParams.ChannelsDatarate = verify.DatarateParams.Datarate;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_CHANNELS_DEFAULT_TX_POWER:
        {
            verify.TxPower = mibSet->Param.ChannelsDefaultTxPower;

            if( RegionVerify( MacCtx.NvmCtx->Region, &verify, PHY_DEF_TX_POWER ) == true )
            {
                MacCtx.NvmCtx->MacParamsDefaults.ChannelsTxPower = verify.TxPower;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_CHANNELS_TX_POWER:
        {
            verify.TxPower = mibSet->Param.ChannelsTxPower;

            if( RegionVerify( MacCtx.NvmCtx->Region, &verify, PHY_TX_POWER ) == true )
            {
                MacCtx.NvmCtx->MacParams.ChannelsTxPower = verify.TxPower;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_SYSTEM_MAX_RX_ERROR:
        {
            // Only apply the new value if in range 0..500 ms else keep current value.
            if( mibSet->Param.SystemMaxRxError <= 500 )
            {
                MacCtx.NvmCtx->MacParams.SystemMaxRxError = mibSet->Param.SystemMaxRxError;
#ifdef LORAMAC_CLASSB_ENABLED
                LoRaMacClassBComputeBeaconWindowParameters();
                LoRaMacClassBComputePingSlotWindowParameters();
#endif
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_MIN_RX_SYMBOLS:
        {
            MacCtx.NvmCtx->MacParams.MinRxSymbols = mibSet->Param.MinRxSymbols;
            MacCtx.NvmCtx->MacParamsDefaults.MinRxSymbols = mibSet->Param.MinRxSymbols;
#ifdef LORAMAC_CLASSB_ENABLED
            LoRaMacClassBComputeBeaconWindowParameters();
            LoRaMacClassBComputePingSlotWindowParameters();
#endif
            break;
        }
        case MIB_ANTENNA_GAIN:
        {
            MacCtx.NvmCtx->MacParams.AntennaGain = mibSet->Param.AntennaGain;
            break;
        }
        case MIB_DEFAULT_ANTENNA_GAIN:
        {
            MacCtx.NvmCtx->MacParamsDefaults.AntennaGain = mibSet->Param.DefaultAntennaGain;
            break;
        }
#ifdef LORAMAC_CTX_SAVERESTORE_ENALED
        case MIB_NVM_CTXS:
        {
            if( mibSet->Param.Contexts != 0 )
            {
                status = RestoreCtxs( mibSet->Param.Contexts );
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
#endif
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
        case MIB_ABP_LORAWAN_VERSION:
        {
            if( mibSet->Param.AbpLrWanVersion.Fields.Minor <= 1 )
            {
                MacCtx.NvmCtx->Version = mibSet->Param.AbpLrWanVersion;

                if( LORAMAC_CRYPTO_SUCCESS != LoRaMacCryptoSetLrWanVersion( mibSet->Param.AbpLrWanVersion ) )
                {
                    return LORAMAC_STATUS_CRYPTO_ERROR;
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
#endif
        case MIB_DUTY_CYCLE:
        {
            InitDefaultsParams_t phyDefaultInit;

            // set regional parameter
            setPhy.Attribute = PHY_DUTY_CYCLE;
            setPhy.param.dcycle_enabled = mibSet->Param.DCycleEnabled;
            RegionSetPhyParam( MacCtx.NvmCtx->Region, &setPhy );

            // init timeoff parameters
            MacCtx.NvmCtx->AggregatedTimeOff = 0;
            phyDefaultInit.Type   = INIT_TYPE_BANDS_DCYCLE;
            phyDefaultInit.NvmCtx = NULL;
            RegionInitDefaults(MacCtx.NvmCtx->Region, &phyDefaultInit);

            // update mac parameter
            MacCtx.NvmCtx->DutyCycleOn = ( bool ) mibSet->Param.DCycleEnabled;
            break;
        }
        case MIB_AS923_ENABLE_CCA:
        {
            if( mibSet->Param.EnableCca == true )
            {
                MacCtx.NvmCtx->MacParamsDefaults.EnableCca = true;
                MacCtx.NvmCtx->MacParams.EnableCca         = true;
            }
            else
            {
                MacCtx.NvmCtx->MacParamsDefaults.EnableCca = false;
                MacCtx.NvmCtx->MacParams.EnableCca         = false;
            }
            break;
        }
        case MIB_IS_CERT_FPORT_ON:
        {
            MacCtx.NvmCtx->IsCertPortOn = mibSet->Param.IsCertPortOn;
            break;
        }
        case MIB_DEV_NONCE:
        {
            LoRaMacCryptoSetDevNonce( mibSet->Param.devNonce );
            break;
        }

        case MIB_APP_NONCE:
        {
            LoRaMacCryptoSetJoinNonce( mibSet->Param.appNonce );
            break;
        }

        case MIB_MAX_DCYCLE:
        {
            if( mibSet->Param.maxDcycle <= 15 )
            {
                MacCtx.NvmCtx->MaxDCycle = mibSet->Param.maxDcycle;
                MacCtx.NvmCtx->AggregatedDCycle = 1 << MacCtx.NvmCtx->MaxDCycle;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }

        case MIB_RX1_DROFFSET:
        {
            if( mibSet->Param.rx1DrOffset <= 7 )
            {
                MacCtx.NvmCtx->MacParams.Rx1DrOffset = mibSet->Param.rx1DrOffset;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }

        case MIB_MAX_EIRP:
        {
            MacCtx.NvmCtx->MacParams.MaxEirp = mibSet->Param.maxEirp;
            break;
        }

        case MIB_DOWNLINK_DWELLTIME:
        {
            if( mibSet->Param.downlinkDwellTime <= 1 )
            {
                MacCtx.NvmCtx->MacParams.DownlinkDwellTime = mibSet->Param.downlinkDwellTime;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }

        case MIB_UPLINK_DWELLTIME:
        {
            if( mibSet->Param.uplinkDwellTime <= 1 )
            {
                MacCtx.NvmCtx->MacParams.UplinkDwellTime = mibSet->Param.uplinkDwellTime;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_DOWNLINK_FCNT:
        {
            LoRaMacCryptoSetDownlinkFCnt( mibSet->Param.downlinkFCnt );
            break;
        }
        case MIB_UPLINK_FCNT:
        {
            LoRaMacCryptoSetUplinkFCnt( mibSet->Param.uplinkFCnt );
            break;
        }
        case MIB_RSSI_FREE_THRESHOLD:
        {
#if defined(REGION_AS923) || defined(REGION_KR920)
            if( ( MacCtx.NvmCtx->Region == LORAMAC_REGION_AS923 ) ||
                ( MacCtx.NvmCtx->Region == LORAMAC_REGION_KR920 ) )
            {
                setPhy.Attribute = PHY_RSSI_FREE_THRESHOLD;
                setPhy.param.RssiFreeThreshold = mibSet->Param.RssiFreeThreshold;
                RegionSetPhyParam( MacCtx.NvmCtx->Region, &setPhy );
            }
            else
#endif
            {
                status = LORAMAC_STATUS_ERROR;
            }
            break;
        }
        case MIB_CARRIER_SENSE_TIME:
        {
#if defined(REGION_AS923) || defined(REGION_KR920)
            if( ( MacCtx.NvmCtx->Region == LORAMAC_REGION_AS923 ) ||
                ( MacCtx.NvmCtx->Region == LORAMAC_REGION_KR920 ) )
            {
                setPhy.Attribute = PHY_CARRIER_SENSE_TIME;
                setPhy.param.CarrierSenseTime = mibSet->Param.CarrierSenseTime;
                RegionSetPhyParam( MacCtx.NvmCtx->Region, &setPhy );
            }
            else
#endif
            {
                status = LORAMAC_STATUS_ERROR;
            }
            break;
        }
        default:
        {
#ifdef LORAMAC_CLASSB_ENABLED
            status = LoRaMacMibClassBSetRequestConfirm( mibSet );
#else
            status = LORAMAC_STATUS_SERVICE_UNKNOWN;
#endif
            break;
        }
    }
    return status;
}

LoRaMacStatus_t LoRaMacChannelAdd( uint8_t id, ChannelParams_t params )
{
    ChannelAddParams_t channelAdd;

    // Validate if the MAC is in a correct state
    if( ( MacCtx.MacState & LORAMAC_TX_RUNNING ) == LORAMAC_TX_RUNNING )
    {
        {
            return LORAMAC_STATUS_BUSY;
        }
    }

    channelAdd.NewChannel = &params;
    channelAdd.ChannelId = id;

    return RegionChannelAdd( MacCtx.NvmCtx->Region, &channelAdd );
}

LoRaMacStatus_t LoRaMacChannelRemove( uint8_t id )
{
    ChannelRemoveParams_t channelRemove;

    if( ( MacCtx.MacState & LORAMAC_TX_RUNNING ) == LORAMAC_TX_RUNNING )
    {
        {
            return LORAMAC_STATUS_BUSY;
        }
    }

    channelRemove.ChannelId = id;

    if( RegionChannelsRemove( MacCtx.NvmCtx->Region, &channelRemove ) == false )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

    return LORAMAC_STATUS_OK;
}

LoRaMacStatus_t LoRaMacMcChannelSetup( McChannelSetup_t *setup )
{
#if (LORAMAC_MAX_MC_CTX > 0)
    AddressIdentifier_t   groupID;
    MulticastCtx_t        *pMcListEntry;
    const KeyIdentifier_t mcKeys[LORAMAC_MAX_MC_CTX] =
    {
        MC_KEY_0,
#if (LORAMAC_MAX_MC_CTX > 1)
        MC_KEY_1,
#endif
#if (LORAMAC_MAX_MC_CTX > 2)
        MC_KEY_2,
#endif
#if (LORAMAC_MAX_MC_CTX > 3)
        MC_KEY_3,
#endif
    };

    if( setup == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

    if( ( MacCtx.MacState & LORAMAC_TX_RUNNING ) == LORAMAC_TX_RUNNING )
    {
        return LORAMAC_STATUS_BUSY;
    }

    groupID = setup->GroupID;

    if( groupID >= LORAMAC_MAX_MC_CTX )
    {
        return LORAMAC_STATUS_MC_GROUP_UNDEFINED;
    }
#if LORAMAC_CHECK_MCFCNT_RANGE
    if( setup->FCountMin > setup->FCountMax )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }
#endif

    if( setup->McKeyE != NULL )
    {
        if( LoRaMacCryptoSetKey( mcKeys[groupID], setup->McKeyE ) != LORAMAC_CRYPTO_SUCCESS )
        {
            return LORAMAC_STATUS_CRYPTO_ERROR;
        }
        if( LoRaMacCryptoDeriveMcSessionKeyPair( groupID, setup->Address ) != LORAMAC_CRYPTO_SUCCESS )
        {
            return LORAMAC_STATUS_CRYPTO_ERROR;
        }
    }

    pMcListEntry = &(MacCtx.NvmCtx->MulticastChannelList[groupID]);
    memset1( (uint8_t *)&(pMcListEntry->ChannelParams), 0x00, sizeof( McChannelParams_t ) );

    // pMcListEntry->ChannelParams.Class     = CLASS_A;  // 0x00
    pMcListEntry->ChannelParams.IsEnabled = true;
    pMcListEntry->ChannelParams.GroupID   = groupID;
    pMcListEntry->ChannelParams.Address   = setup->Address;
    pMcListEntry->ChannelParams.FCountMin = setup->FCountMin;
    pMcListEntry->ChannelParams.FCountMax = setup->FCountMax;

    // Reset multicast channel downlink counter to initial value.
    *(pMcListEntry->DownLinkCounter) = FCNT_DOWN_INITAL_VALUE;

    return LORAMAC_STATUS_OK;

#else  // LORAMAC_MAX_MC_CTX == 0
    return LORAMAC_STATUS_SERVICE_UNKNOWN;
#endif
}

LoRaMacStatus_t LoRaMacMcChannelDelete( AddressIdentifier_t groupID )
{
#if (LORAMAC_MAX_MC_CTX > 0)
    DeviceClass_t     mcClassDeleted;
    McChannelParams_t channel;

    if( ( MacCtx.MacState & LORAMAC_TX_RUNNING ) == LORAMAC_TX_RUNNING )
    {
        return LORAMAC_STATUS_BUSY;
    }

    if( ( groupID >= LORAMAC_MAX_MC_CTX ) ||
        ( MacCtx.NvmCtx->MulticastChannelList[groupID].ChannelParams.IsEnabled == false ) )
    {
        return LORAMAC_STATUS_MC_GROUP_UNDEFINED;
    }

    // Store multicast class before delete
    mcClassDeleted = MacCtx.NvmCtx->MulticastChannelList[groupID].ChannelParams.Class;

    // Set all channel fields with 0
    memset1( ( uint8_t* )&channel, 0, sizeof( McChannelParams_t ) );
    MacCtx.NvmCtx->MulticastChannelList[groupID].ChannelParams = channel;

    if( ( MacCtx.NvmCtx->DeviceClass != CLASS_A ) &&
        ( MacCtx.NvmCtx->DeviceClass == mcClassDeleted ) )
    {
        uint8_t i;

        for( i = 0; i < LORAMAC_MAX_MC_CTX; i++ )
        {
            if( ( MacCtx.NvmCtx->MulticastChannelList[i].ChannelParams.IsEnabled == true ) &&
                ( MacCtx.NvmCtx->MulticastChannelList[i].ChannelParams.Class == mcClassDeleted ) )
            {
                break;
            }
        }
        if( i == LORAMAC_MAX_MC_CTX )
        {
#ifdef LORAMAC_CLASSB_ENABLED
            if( mcClassDeleted == CLASS_B )
            {
                // ClassB multicast entry is not in the table.
                // Stop classB multicast rx and timer
                LoRaMacClassBStopMulticastRxSlots();
            }
            else // if( mcClassDeleted == CLASS_C )
#endif
            {
                // ClassC multicast entry is not in the table.
                // Revert back RxC parameters
                MacCtx.RxWindowCConfig = MacCtx.RxWindow2Config;
                MacCtx.RxWindowCConfig.RxSlot = RX_SLOT_WIN_CLASS_C;

                MacCtx.NvmCtx->MacParams.RxCChannel = MacCtx.NvmCtx->MacParams.Rx2Channel;

                MacCtx.NodeAckRequested = false;

                Radio.Standby();  // stop Rx before setting class c multicast rx
                OpenContinuousRxCWindow( );
            }
        }
    }

    return LORAMAC_STATUS_OK;

#else  // LORAMAC_MAX_MC_CTX == 0
    return LORAMAC_STATUS_SERVICE_UNKNOWN;
#endif
}

uint8_t LoRaMacMcChannelGetGroupId( uint32_t mcAddress )
{
#if (LORAMAC_MAX_MC_CTX > 0)
    for( uint8_t i = 0; i < LORAMAC_MAX_MC_CTX; i++ )
    {
        if( mcAddress == MacCtx.NvmCtx->MulticastChannelList[i].ChannelParams.Address )
        {
            return i;
        }
    }
#endif

    return 0xFF;
}

LoRaMacStatus_t LoRaMacMcChannelGetAddress( AddressIdentifier_t groupID, uint32_t *mcAddress )
{
#if (LORAMAC_MAX_MC_CTX > 0)
    if( mcAddress == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

    if( ( groupID >= LORAMAC_MAX_MC_CTX ) ||
        ( MacCtx.NvmCtx->MulticastChannelList[groupID].ChannelParams.IsEnabled == false ) )
    {
        return LORAMAC_STATUS_MC_GROUP_UNDEFINED;
    }

    *mcAddress = MacCtx.NvmCtx->MulticastChannelList[groupID].ChannelParams.Address;

    return LORAMAC_STATUS_OK;

#else  // LORAMAC_MAX_MC_CTX == 0
    return LORAMAC_STATUS_SERVICE_UNKNOWN;
#endif
}

LoRaMacStatus_t LoRaMacMcChannelSetupRxParams( AddressIdentifier_t groupID, DeviceClass_t mcClass,
                                               McRxParams_t *rxParams, uint8_t *status )
{
#if (LORAMAC_MAX_MC_CTX > 0)
    VerifyParams_t verify;
    MulticastCtx_t *pMcListEntry;
    McRxParams_t   *pMcClassCEntryRx;
    uint8_t        i;

    // Check status
    if( status == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }
    (*status) = 0x1C + ( groupID & 0x03 );  // init status. Mac does not care "bit5:StartMissed".

    // Check other argument
    if( rxParams == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

#ifdef LORAMAC_CLASSB_ENABLED
    if( ( mcClass != CLASS_B ) && ( mcClass != CLASS_C ) )
#else
    if( mcClass != CLASS_C )
#endif
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

    if( ( MacCtx.MacState & LORAMAC_TX_RUNNING ) == LORAMAC_TX_RUNNING )
    {
        return LORAMAC_STATUS_BUSY;
    }

    // Check GroupID
    if( ( groupID >= LORAMAC_MAX_MC_CTX ) ||
        ( MacCtx.NvmCtx->MulticastChannelList[groupID].ChannelParams.IsEnabled == false ) )
    {
        return LORAMAC_STATUS_MC_GROUP_UNDEFINED;
    }
    (*status) &= 0xEF; // groupID OK

    // Check frequency and datarate
    pMcClassCEntryRx = NULL;  // init
    if( mcClass == CLASS_C )
    {
        // search class c entry
        for( i = 0; i < LORAMAC_MAX_MC_CTX; i++ )
        {
            if( ( i != groupID ) &&
                ( MacCtx.NvmCtx->MulticastChannelList[i].ChannelParams.IsEnabled == true ) &&
                ( MacCtx.NvmCtx->MulticastChannelList[i].ChannelParams.Class == CLASS_C) )
            {
                pMcClassCEntryRx = &(MacCtx.NvmCtx->MulticastChannelList[i].ChannelParams.RxParams);
                break;
            }
        }
    }

    if( pMcClassCEntryRx != NULL )
    {
        // compare with registered entry
        // (datarate/frequency of the registered class c entry are valid.)
        if( pMcClassCEntryRx->ClassC.Datarate != rxParams->ClassC.Datarate )
        {
            return LORAMAC_STATUS_PARAMETER_INVALID;
        }
        (*status) &= 0xFB; // datarate OK

        if( pMcClassCEntryRx->ClassC.Frequency != rxParams->ClassC.Frequency )
        {
            return LORAMAC_STATUS_PARAMETER_INVALID;
        }
        (*status) &= 0xF7; // frequency OK
    }
    else
    {
        // Check datarate
        verify.DatarateParams.Datarate = rxParams->Common.Datarate;
        verify.DatarateParams.DownlinkDwellTime = MacCtx.NvmCtx->MacParams.DownlinkDwellTime;
        if( RegionVerify( MacCtx.NvmCtx->Region, &verify, PHY_RX_DR ) != true )
        {
            return LORAMAC_STATUS_PARAMETER_INVALID;
        }
        (*status) &= 0xFB; // datarate OK

        // Check frequency
        verify.Frequency = rxParams->Common.Frequency;
        if( RegionVerify( MacCtx.NvmCtx->Region, &verify, PHY_FREQUENCY ) != true )
        {
            return LORAMAC_STATUS_PARAMETER_INVALID;
        }
        (*status) &= 0xF7; // frequency OK
    }

    if( ((*status) & 0xFC) == 0x00 )
    {
        pMcListEntry = &(MacCtx.NvmCtx->MulticastChannelList[groupID]);

        // Apply parameters
        pMcListEntry->ChannelParams.RxParams = *rxParams;
        pMcListEntry->ChannelParams.Class = mcClass;

#ifdef LORAMAC_CLASSB_ENABLED
        if( mcClass == CLASS_B )
        {
            // Calculate class b parameters
            LoRaMacClassBSetMulticastPeriodicity( pMcListEntry );
        }
#endif

#ifdef LORAMAC_CLASSB_ENABLED
        if( ( MacCtx.NvmCtx->DeviceClass == CLASS_B ) &&
            ( MacCtx.NvmCtx->DeviceClass == mcClass ) )
        {
            // restart classB multicast
            LoRaMacClassBStopMulticastRxSlots();
            LoRaMacClassBStartMulticastRxSlots();
        }
#endif

        if( ( MacCtx.NvmCtx->DeviceClass == CLASS_C ) &&
            ( MacCtx.NvmCtx->DeviceClass == mcClass ) &&
            ( pMcClassCEntryRx == NULL ) )
        {
            // first class C entry has been registered.
            // so change RxC setting from unicast to multicast.
            MacCtx.NvmCtx->MacParams.RxCChannel.Frequency = rxParams->ClassC.Frequency;
            MacCtx.NvmCtx->MacParams.RxCChannel.Datarate = rxParams->ClassC.Datarate;

            MacCtx.RxWindowCConfig.Channel = MacCtx.Channel;
            MacCtx.RxWindowCConfig.Frequency = MacCtx.NvmCtx->MacParams.RxCChannel.Frequency;
            MacCtx.RxWindowCConfig.DownlinkDwellTime = MacCtx.NvmCtx->MacParams.DownlinkDwellTime;
            MacCtx.RxWindowCConfig.RxSlot = RX_SLOT_WIN_CLASS_C_MULTICAST;
#ifdef LORAMAC_RXC_CONTINUOUS_ENABLED
            MacCtx.RxWindowCConfig.RxContinuous = true;
#else
            MacCtx.RxWindowCConfig.RxContinuous = false;
#endif

            // Set the NodeAckRequested indicator to default
            MacCtx.NodeAckRequested = false;

            Radio.Standby();  // stop Rx before setting class c multicast rx
            OpenContinuousRxCWindow( );
        }
     }

    return LORAMAC_STATUS_OK;

#else  // LORAMAC_MAX_MC_CTX == 0
    if( status != NULL )
    {
        (*status) = 0x1C + ( groupID & 0x03 );  // init status. Mac does not care "bit5:StartMissed".
    }
    return LORAMAC_STATUS_SERVICE_UNKNOWN;
#endif
}

LoRaMacStatus_t LoRaMacMlmeRequest( MlmeReq_t* mlmeRequest )
{
    LoRaMacStatus_t status = LORAMAC_STATUS_SERVICE_UNKNOWN;
    MlmeConfirmQueue_t queueElement;
    uint8_t macCmdPayload[2] = { 0x00, 0x00 };

    if( mlmeRequest == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

    // Initialize mlmeRequest->ReqReturn.DutyCycleWaitTime to 0 in order to
    // return a valid value in case the MAC is busy.
    mlmeRequest->ReqReturn.DutyCycleWaitTime = 0;

    if( LoRaMacIsBusy( ) == true )
    {
        return LORAMAC_STATUS_BUSY;
    }
    if( LoRaMacConfirmQueueIsFull( ) == true )
    {
        return LORAMAC_STATUS_BUSY;
    }

    if( MacCtx.NvmCtx->NetworkActivation == ACTIVATION_TYPE_NONE )
    {
        // reject request if device is not joined. (except join and txcw)
        if( ( mlmeRequest->Type != MLME_JOIN ) &&
            ( mlmeRequest->Type != MLME_TXCW ) )
        {
            return LORAMAC_STATUS_NO_NETWORK_JOINED;
        }
    }

    {
        MlmeConfirmQueue_t *p_queueElmSearch = NULL;

        LoRaMacConfirmQueueSearch( mlmeRequest->Type, &p_queueElmSearch );
        if (p_queueElmSearch != NULL)
        {
            // same MLME-Request has already been requested.
            return LORAMAC_STATUS_BUSY;
        }
    }

    if( LoRaMacConfirmQueueGetCnt( ) == 0 )
    {
        memset1( ( uint8_t* ) &MacCtx.MlmeConfirm, 0, sizeof( MacCtx.MlmeConfirm ) );
    }
    MacCtx.MlmeConfirm.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;

    MacCtx.MacFlags.Bits.MlmeReq = 1;
    queueElement.Request = mlmeRequest->Type;
    queueElement.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
    queueElement.RestrictCommonReadyToHandle = false;

    switch( mlmeRequest->Type )
    {
        case MLME_JOIN:
        {
#ifdef LORAMAC_CLASSB_ENABLED
            if( MacCtx.NvmCtx->DeviceClass == CLASS_B )
            {
                return( LORAMAC_STATUS_SERVICE_UNKNOWN );
            }
#endif
            ResetMacParameters( );

#ifdef LORAMAC_CLASSB_ENABLED
            // Stop beaconing
            LoRaMacClassBStopBeaconig();
#endif

            MacCtx.NvmCtx->MacParams.ChannelsDatarate = RegionAlternateDr( MacCtx.NvmCtx->Region, mlmeRequest->Req.Join.Datarate, ALTERNATE_DR );

            queueElement.Status = LORAMAC_EVENT_INFO_STATUS_JOIN_FAIL;

            /* r13 Phase 1 — reset per-JOIN snapshots. rx2_skipped_total
             * stays cumulative. r13-fix added the 4 override-visibility
             * fields below; they reset to 0 here and get populated by the
             * RX-window compute site (see L33xx area in this file). */
            __atomic_store_n((uint32_t *)&lorawan_rxc_diag.last_rx1_open_cyc,  0u, __ATOMIC_RELAXED);
            __atomic_store_n((uint32_t *)&lorawan_rxc_diag.last_rx1_close_cyc, 0u, __ATOMIC_RELAXED);
            __atomic_store_n((uint32_t *)&lorawan_rxc_diag.last_rx2_open_cyc,  0u, __ATOMIC_RELAXED);
            __atomic_store_n((uint32_t *)&lorawan_rxc_diag.last_rx2_close_cyc, 0u, __ATOMIC_RELAXED);
            __atomic_store_n(&lorawan_rxc_diag.last_rx_done_slot_id,           0u, __ATOMIC_RELAXED);
            __atomic_store_n((uint32_t *)&lorawan_rxc_diag.last_join_rx1_window_timeout_symbols, 0u, __ATOMIC_RELAXED);
            __atomic_store_n((uint32_t *)&lorawan_rxc_diag.last_join_rx2_window_timeout_symbols, 0u, __ATOMIC_RELAXED);
            __atomic_store_n(&lorawan_rxc_diag.last_join_used_override_flag,         0u, __ATOMIC_RELAXED);
            __atomic_store_n(&lorawan_rxc_diag.last_join_effective_min_rx_symbols,   0u, __ATOMIC_RELAXED);
            __atomic_store_n((uint16_t *)&lorawan_rxc_diag.last_join_effective_system_max_rx_error_ms, 0u, __ATOMIC_RELAXED);
            __atomic_store_n((int16_t  *)&lorawan_rxc_diag.last_join_rx1_window_offset_ms,             0,  __ATOMIC_RELAXED);

            status = SendReJoinReq( JOIN_REQ );

            if( status != LORAMAC_STATUS_OK )
            {
                // Revert back the previous datarate ( mainly used for US915 like regions )
                MacCtx.NvmCtx->MacParams.ChannelsDatarate = RegionAlternateDr( MacCtx.NvmCtx->Region, mlmeRequest->Req.Join.Datarate, ALTERNATE_DR_RESTORE );
            }
            break;
        }
        case MLME_LINK_CHECK:
        {
            // LoRaMac will send this command piggy-pack
            status = LORAMAC_STATUS_OK;
            if( LoRaMacCommandsAddCmd( MOTE_MAC_LINK_CHECK_REQ, macCmdPayload, 0 ) != LORAMAC_COMMANDS_SUCCESS )
            {
                status = LORAMAC_STATUS_MAC_COMMAD_ERROR;
            }
            break;
        }
        case MLME_TXCW:
        {
            status = SetTxContinuousWave( mlmeRequest->Req.TxCw.Timeout, mlmeRequest->Req.TxCw.Frequency, mlmeRequest->Req.TxCw.Power );
            break;
        }
        case MLME_DEVICE_TIME:
        {
#ifdef LORAMAC_CLASSB_ENABLED
            if( ( LoRaMacClassBIsAcquisitionInProgress() != true ) &&
                ( LoRaMacClassBIsBeaconModeActive() != true ) )
#endif
            {
                // LoRaMac will send this command piggy-pack
                status = LORAMAC_STATUS_OK;
                if( LoRaMacCommandsAddCmd( MOTE_MAC_DEVICE_TIME_REQ, macCmdPayload, 0 ) != LORAMAC_COMMANDS_SUCCESS )
                {
                    status = LORAMAC_STATUS_MAC_COMMAD_ERROR;
                }
            }
            break;
        }
#ifdef LORAMAC_CLASSB_ENABLED
        case MLME_PING_SLOT_INFO:
        {
            if( MacCtx.NvmCtx->DeviceClass == CLASS_A )
            {
                PingSlotInfo_t PingSlotValue;
                uint8_t value;

                PingSlotValue.Value = mlmeRequest->Req.PingSlotInfo.PingSlot.Value;
                PingSlotValue.Fields.RFU = 0;
                value = PingSlotValue.Value;

                // LoRaMac will send this command piggy-pack
                LoRaMacClassBSetPingSlotInfo( mlmeRequest->Req.PingSlotInfo.PingSlot.Fields.Periodicity );
                macCmdPayload[0] = value;
                status = LORAMAC_STATUS_OK;
                if( LoRaMacCommandsAddCmd( MOTE_MAC_PING_SLOT_INFO_REQ, macCmdPayload, 1 ) != LORAMAC_COMMANDS_SUCCESS )
                {
                    status = LORAMAC_STATUS_MAC_COMMAD_ERROR;
                }
            }
            break;
        }
        case MLME_BEACON_TIMING:
        {
            if( ( LoRaMacClassBIsAcquisitionInProgress() != true ) &&
                ( LoRaMacClassBIsBeaconModeActive() != true ) )
            {
                // LoRaMac will send this command piggy-pack
                status = LORAMAC_STATUS_OK;
                if( LoRaMacCommandsAddCmd( MOTE_MAC_BEACON_TIMING_REQ, macCmdPayload, 0 ) != LORAMAC_COMMANDS_SUCCESS )
                {
                    status = LORAMAC_STATUS_MAC_COMMAD_ERROR;
                }
            }
            break;
        }
        case MLME_BEACON_ACQUISITION:
        {
            if( MacCtx.NvmCtx->DeviceClass != CLASS_C )
            {
                // Apply the request
                queueElement.RestrictCommonReadyToHandle = true;

                if( LoRaMacClassBIsAcquisitionInProgress( ) == false )
                {
                    // Stop ping slot and multicast slot if they are running
                    LoRaMacClassBStopRxSlots();

                    // Calculate rx parameter for beacon acquisition
                    LoRaMacClassBComputeBeaconAcquisitionWindowParameters();
                    // Start class B algorithm
                    LoRaMacClassBSetBeaconState( BEACON_STATE_ACQUISITION );
                    LoRaMacClassBBeaconTimerEvent();

                    status = LORAMAC_STATUS_OK;
                }
                else
                {
                    status = LORAMAC_STATUS_BUSY;
                }
            }
            break;
        }
#endif  // LORAMAC_CLASSB_ENABLED
        default:
            break;
    }

    // Fill return structure
    mlmeRequest->ReqReturn.DutyCycleWaitTime = MacCtx.DutyCycleWaitTime;

    if( status != LORAMAC_STATUS_OK )
    {
        if( LoRaMacConfirmQueueGetCnt( ) == 0 )
        {
            MacCtx.NodeAckRequested = false;
            MacCtx.MacFlags.Bits.MlmeReq = 0;
        }
    }
    else
    {
        LoRaMacConfirmQueueAdd( &queueElement );
    }
    return status;
}

LoRaMacStatus_t LoRaMacMcpsRequest( McpsReq_t* mcpsRequest )
{
    SBC(LWBC_M0);
    GetPhyParams_t getPhy;
    PhyParam_t phyParam;
    LoRaMacStatus_t status = LORAMAC_STATUS_SERVICE_UNKNOWN;
    LoRaMacHeader_t macHdr;
    VerifyParams_t verify;
    uint8_t fPort = 0;
    void* fBuffer;
    uint16_t fBufferSize;
    int8_t datarate = DR_0;
    bool readyToSend = false;

    /* Early return mcpsRequest==NULL → LORAMAC_STATUS_PARAMETER_INVALID
     * bypasses M1 (returns to Python cleanly, no fault path). */
    if( mcpsRequest == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

    // Initialize mcpsRequest->ReqReturn.DutyCycleWaitTime to 0 in order to
    // return a valid value in case the MAC is busy.
    mcpsRequest->ReqReturn.DutyCycleWaitTime = 0;

    /* BUSY early return bypasses M1 too — also a clean status path. */
    if( LoRaMacIsBusy( ) == true )
    {
        return LORAMAC_STATUS_BUSY;
    }
    SBC(LWBC_M1);

    macHdr.Value = 0;
    memset1( ( uint8_t* ) &MacCtx.McpsConfirm, 0, sizeof( MacCtx.McpsConfirm ) );
    MacCtx.McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;

    // AckTimeoutRetriesCounter must be reset every time a new request (unconfirmed or confirmed) is performed.
    MacCtx.AckTimeoutRetriesCounter = 1;
    MacCtx.ChannelsNbTransCounter = 0;

    switch( mcpsRequest->Type )
    {
        case MCPS_UNCONFIRMED:
        {
            readyToSend = true;
            MacCtx.AckTimeoutRetries = 1;

            macHdr.Bits.MType = FRAME_TYPE_DATA_UNCONFIRMED_UP;
            fPort = mcpsRequest->Req.Unconfirmed.fPort;
            fBuffer = mcpsRequest->Req.Unconfirmed.fBuffer;
            fBufferSize = mcpsRequest->Req.Unconfirmed.fBufferSize;
            datarate = mcpsRequest->Req.Unconfirmed.Datarate;
            break;
        }
        case MCPS_CONFIRMED:
        {
            readyToSend = true;
#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
            // mcpsRequest->Req.Confirmed.NbTrials is not used
            MacCtx.AckTimeoutRetries = MacCtx.NvmCtx->MacParams.ChannelsNbTrans;
#else  // LW103
            MacCtx.AckTimeoutRetries = R_MIN( mcpsRequest->Req.Confirmed.NbTrials, MAX_ACK_RETRIES );
#endif

            macHdr.Bits.MType = FRAME_TYPE_DATA_CONFIRMED_UP;
            fPort = mcpsRequest->Req.Confirmed.fPort;
            fBuffer = mcpsRequest->Req.Confirmed.fBuffer;
            fBufferSize = mcpsRequest->Req.Confirmed.fBufferSize;
            datarate = mcpsRequest->Req.Confirmed.Datarate;
            break;
        }
        default:
            break;
    }

    if( readyToSend == true )
    {
        // Get the minimum possible datarate
        getPhy.Attribute = PHY_MIN_TX_DR;
        getPhy.UplinkDwellTime = MacCtx.NvmCtx->MacParams.UplinkDwellTime;
        phyParam = RegionGetPhyParam( MacCtx.NvmCtx->Region, &getPhy );
        SBC(LWBC_M2);
        // Apply the minimum possible datarate.
        // Some regions have limitations for the minimum datarate.
        datarate = R_MAX( datarate, ( int8_t )phyParam.Value );

#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
#ifdef LORAMAC_ACTMODE_ABP_ENABLED
        // Apply minimum datarate in this special case.
        //  (ADR=On, Activation=ABP, and Datarate is not changed by LinkADRReq)
        if( CheckForMinimumAbpDatarate( MacCtx.NvmCtx->AdrCtrlOn, MacCtx.NvmCtx->NetworkActivation,
                                        MacCtx.NvmCtx->ChannelsDatarateChangedLinkAdrReq ) == true )
        {
            MacCtx.NvmCtx->MacParams.ChannelsDatarate = ( int8_t )phyParam.Value;
        }
#endif
#endif
        if( MacCtx.NvmCtx->AdrCtrlOn == false )
        {
            verify.DatarateParams.Datarate = datarate;
            verify.DatarateParams.UplinkDwellTime = MacCtx.NvmCtx->MacParams.UplinkDwellTime;

            if( RegionVerify( MacCtx.NvmCtx->Region, &verify, PHY_TX_DR ) == true )
            {
                MacCtx.NvmCtx->MacParams.ChannelsDatarate = verify.DatarateParams.Datarate;
            }
            else
            {
                return LORAMAC_STATUS_PARAMETER_INVALID;
            }
        }

#if (LORAMAC_VERSION >= LORAWAN_VERSION_1_0_4)
        // Verification of response timeout for class b and class c
        LoRaMacHandleResponseTimeout( REGION_COMMON_CLASS_B_C_RESP_TIMEOUT,
                                      MacCtx.ResponseTimeoutStartTime );
#endif

        SBC(LWBC_M3);
        status = Send( &macHdr, fPort, fBuffer, fBufferSize );
        if( ( status == LORAMAC_STATUS_OK ) || (status == LORAMAC_STATUS_SKIPPED_APP_DATA) )
        {
            MacCtx.McpsConfirm.McpsRequest = mcpsRequest->Type;
            MacCtx.MacFlags.Bits.McpsReq = 1;
        }
        else
        {
            MacCtx.NodeAckRequested = false;

            // Remove all mac command that are requested from API/MLME, and up the confirm
            LoRaMacRemoveConfirmQueue();
        }
    }

    // Fill return structure
    mcpsRequest->ReqReturn.DutyCycleWaitTime = MacCtx.DutyCycleWaitTime;

    return status;
}

static void LoRaMacRemoveConfirmQueue( void )
{
    MlmeConfirmQueue_t  *p_mlmeConfirm;
    MacCommand_t        *p_macCmd;
    Mlme_t              mlmeSearchTbl[4] = {MLME_LINK_CHECK, MLME_DEVICE_TIME,
                                            MLME_PING_SLOT_INFO, MLME_BEACON_TIMING};
    uint8_t             cidTbl[4] = {MOTE_MAC_LINK_CHECK_REQ, MOTE_MAC_DEVICE_TIME_REQ,
                                     MOTE_MAC_PING_SLOT_INFO_REQ, MOTE_MAC_BEACON_TIMING_REQ};
    uint8_t             i;

    // Remove MAC commands requested from API/MLME-Req.
    // 4 = LinkCheckReq, DeviceTimeReq, PingSlotInfo, and BeaconTimingReq
    for (i = 0; i < 4; i++)
    {
        // search MLME-Req
        p_mlmeConfirm = NULL;
        LoRaMacConfirmQueueSearch( mlmeSearchTbl[i], &p_mlmeConfirm );
        if (p_mlmeConfirm != NULL)
        {
            // remove command
            p_macCmd = NULL;
            LoRaMacCommandsGetCmd( cidTbl[i], &p_macCmd );
            LoRaMacCommandsRemoveCmd( p_macCmd );
        }
    }

    // Confirm
    LoRaMacConfirmQueueSetStatusCmn( LORAMAC_EVENT_INFO_STATUS_ERROR );
    MacCtx.MacFlags.Bits.MacDone = 1;

}

void LoRaMacTestSetDutyCycleOn( bool enable )
{
    VerifyParams_t verify;

    verify.DutyCycle = enable;

    if( RegionVerify( MacCtx.NvmCtx->Region, &verify, PHY_DUTY_CYCLE ) == true )
    {
        MacCtx.NvmCtx->DutyCycleOn = enable;
    }
}

DeviceClass_t LoRaMacGetDeviceClass( void )
{
    return (MacCtx.NvmCtx->DeviceClass);
}

void LoRaMacGetMibVal( Mib_t varType, void * pDest )
{
    CRITICAL_SECTION_BEGIN();

    switch( varType )
    {
        case MIB_AS923_ENABLE_CCA:
            *( (bool *)pDest) = MacCtx.NvmCtx->MacParams.EnableCca;
            break;

        case MIB_PUBLIC_NETWORK:
            *( (bool *)pDest) = MacCtx.NvmCtx->PublicNetwork;
            break;

        default:
            break;
    }

    CRITICAL_SECTION_END();
}

static int32_t LoRaMacGetStackProcessTime( uint8_t kind )
{
    int32_t retVal;

    switch( kind )
    {
        case LORAMAC_STACK_PROCTIME_SEL_RX1_ON:
            retVal = LORAMAC_STACK_PROCTIMEMS_RX1ON;
            break;

        case LORAMAC_STACK_PROCTIME_SEL_RX2_ON:
            retVal = LORAMAC_STACK_PROCTIMEMS_RX2ON;
            break;

        default:
            return 0;  // never comes here
    }

    if (SX126xGetClockSelect() == RADIO_CLOCK_TCXO_SEL)
    {
        retVal += ((uint32_t)((RP_TCXO_STAB_TIME * 15.625)/1000.0));
    }

    return retVal;
}

#if defined( LORACOMBO_ENABLED )
bool LoRaMacIsStarted( void )
{
    bool    bRet;

    bRet = true;
    if( MacCtx.MacState == LORAMAC_STOPPED )
    {
        bRet = false;
    }

    return bRet;
}
#endif

//-----------------------------------------
// for MCU low power
extern volatile bool IrqFired;  // radio.c

LoRaMacStatus_t LoRaMacSetLowPower( void )
{
    LoRaMacStatus_t retVal;

    /* START disabling interrupts in this function */
    BoardDisableAllIrq();  // same function used in SetLowPower()

    // init
    retVal = LORAMAC_STATUS_OK;

    /* Check class */
    if (MacCtx.NvmCtx->DeviceClass == CLASS_C)
    {
        retVal = LORAMAC_STATUS_BUSY;
    }

    /* Chack MacState/RadioIRQ */
    if (retVal == LORAMAC_STATUS_OK)
    {
        if (IrqFired == true)
        {
            retVal = LORAMAC_STATUS_BUSY;
        }
        else
        {
            if( LoRaMacTimerEvents.Value != 0 )
            {
                retVal = LORAMAC_STATUS_BUSY;
            }
        }
    }

#ifdef LORAMAC_CLASSB_ENABLED
    /* Chack beacon/pingslot/multicast */
    if (retVal == LORAMAC_STATUS_OK)
    {
        retVal = LoRaMacClassBSetLowPower();
    }
#endif

    /* MCU low power */
    if (retVal == LORAMAC_STATUS_OK)
    {
        SetLowPower();
    }

    /* END of disabling interrupts in this function */
    BoardEnableAllIrq();

    return( retVal );
}

void LoRaMacErrorNotify( LoRaMacErrorNotificationStatus_t status )
{
    if( ( MacCtx.MacCallbacks != NULL ) && ( MacCtx.MacCallbacks->MacErrorNotify != NULL ) )
    {
        MacCtx.MacCallbacks->MacErrorNotify( status );
    }
}

//-----------------------------------------
//
void LoRaMacSetNvmEvtMibFlag( uint32_t evtMibFlag )
{
    MacCtx.notifyMibFlag |= evtMibFlag;
}

void LoRaMacClearNvmEvtMibFlag( uint32_t evtMibFlag )
{
    MacCtx.notifyMibFlag &= ~evtMibFlag;
}

static void LoRaMacResetNvmEvtMibFlag( void )
{
    MacCtx.notifyMibFlag = 0;
}

//-----------------------------------------
//
/*!
 * \brief Common function; set uint32 value from array
 *
 * \param [OUT] *p_value32  uint32 value (Not NULL)
 * \param [IN]  *p_array    uint8 array (Not NULL)
 * \param [IN]  size        size (<= 4)
 */
void LoRaMacCommonCopyArrayToUint32( uint32_t *p_value32, uint8_t *p_array, uint8_t size )
{
    uint8_t     i;

    (*p_value32) = 0;
    for( i = 0; i < size; i++ )
    {
        (*p_value32) |= ( (uint32_t)p_array[ i ] ) << (i * 8);
    }
}

/*!
 * \brief Common function; set array from uint32 value
 *
 * \param [OUT] *p_array    uint8 array (Not NULL)
 * \param [IN]  *p_value32  uint32 value (Not NULL)
 * \param [IN]  size        size (<= 4)
 */
void LoRaMacCommonCopyUint32ToArray( uint8_t *p_array, uint32_t *p_value32, uint8_t size )
{
    uint32_t    tmp32;
    uint8_t     i;

    tmp32 = (*p_value32);
    for( i = 0; i < size; i++ )
    {
        p_array[ i ] = (uint8_t)tmp32;
        tmp32 >>= 8;
    }
}
