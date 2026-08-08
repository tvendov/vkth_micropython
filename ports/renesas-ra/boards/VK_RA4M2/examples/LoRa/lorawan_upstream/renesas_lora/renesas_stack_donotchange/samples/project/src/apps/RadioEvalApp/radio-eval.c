/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/
#include "board.h"
#include "radio-eval.h"
#include "at-proc.h"

#if !defined(RP_DFLASH_UNUSED)
#include "dflash.h"
#endif

/*!
 * \brief Variable that manages application state
 */
static AppStates_t state = APP_INIT;

/*!
 * \brief Variables that point to application controlblock
 */
AppCb_t     appControlBlock;
AppCb_t     *appCb  = &(appControlBlock);
RadioCb_t   *rfCb   = &(appControlBlock.radioCb);
RxCb_t      *rxCb   = &(appControlBlock.rxCb);
TxCb_t      *txCb   = &(appControlBlock.txCb);
PacketCb_t  *anyPkt = &(appControlBlock.anyPktCb);

/*!
 * \brief Variables that stores the packet payload data for last reception and the transmission and verification
 */
PacketData_t appPacketData;
PacketData_t *pktData = &(appPacketData);

/*!
 * \brief Prototypes of application functions
 */
void AppInit (void);
void AppLoadCb (void);
void AppClearTxCb (void);
void AppClearRxCb (void);
void AppSetupPacketData (uint8_t);
void AppGetPayloadData (uint8_t, uint8_t *, uint8_t **);
uint8_t AppCountBits (uint8_t);
void AppRXResultPrint (void);
uint32_t AppGetEvent (AppCb_t *, uint32_t);
uint32_t AppSetEvent (AppCb_t *, uint32_t);

/*!
 * \brief Variable that registers callback functions to be called when a radio event occurs
 */
static RadioEvents_t RadioEvents;

/*!
 * \brief Prototypes of callback functions to be called when a radio event occurs
 */
void OnTxDone (void);
void OnRxDone (uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);
void OnTxTimeout (void);
void OnRxTimeout (void);
void OnRxError (void);

extern void RpMcuResourceTimerStop( void );

/*!
 * \brief Variable that registers a timer event to be used for generating the transmission interval
 */
static TimerEvent_t txNextPacketTimer;
static TimerEvent_t sleepNextWakeUpTimer;


/*!
 * \brief Entry point of application
 */
void app_main (void)
{
    /* initialize hardware	*/
    BoardInitMcu();

    /* initialize radio and application */
    AppInit();

    uint32_t        e = 0UL;
    RadioModems_t   aModem = MODEM_LORA;
    uint32_t        aRxTimeout = 0UL;
    uint8_t         aPayloadLen = 0U;
    uint8_t         aPos = 0U;
    uint8_t         *txBuffer = NULL;
    uint32_t        *txDutyCycle = NULL;

    for(;;) {

        if (AtNotifyCommandReceived()) {
            AtProcParser();
        }

        e = AppGetTranState(appCb, &state);

        if (e) {
            switch (state) {
                case APP_RX_CONFIG:
                    {
                        Radio.SetChannel(rfCb->freq);

                        if (AppMatchPacketType(PACKET_TYPE_GFSK)) {
                            aModem     = MODEM_FSK;
                            aRxTimeout = (rfCb->fsk.rxContinuous) ? 0 : rfCb->rxTimeout;
                        } else {
                            aModem     = MODEM_LORA;
                            aRxTimeout = (rfCb->lora.rxContinuous) ? 0 : rfCb->rxTimeout;
                        }

                        AppRadioSetRxConfig(aModem);
                        AppClearRxCb();

                        AppSetState(appCb, APP_RX);
                    }
                    // no break (fall-through)
                case APP_RX:
                    {
                        switch(Radio.Rx(aRxTimeout))
                        {
                            case RADIO_SUCCESS:
                                AppSetState(appCb, APP_RX_RUNNING);
                                break;
                            case RADIO_CHECK_FAIL_RX_CFG:
                                print("+SAFE:FAIL_RX_CFG\r\n");
                                Radio.Standby();
                                AppSetState(appCb, APP_IDLE);
                                break;
                            default:
                                Radio.Standby();
                                AppSetState(appCb, APP_IDLE);
                                break;
                        }
                        break;
                    }

                case APP_RX_TIMEOUT:
                    {
                        Radio.Standby();

                        AppSetState(appCb, APP_IDLE);
                        break;
                    }

                case APP_RX_ERROR:
                    {
                        Radio.Standby();
                        break;
                    }

                case APP_RX_STOP:
                    {
                        Radio.Standby();

                        AppRXResultPrint();
                        AtCmdSetRcStatus(AT_RC_OK);
                        AtPrintResultCode();

                        AppSetState(appCb, APP_IDLE);
                        break;
                    }

                case APP_TX_CONFIG:
                    {
                        Radio.SetChannel(rfCb->freq);

                        if (AppMatchPacketType(PACKET_TYPE_GFSK)) {
                            aModem = MODEM_FSK;
                            AppRadioSetTxConfig(MODEM_FSK);
                        } else {
                            aModem = MODEM_LORA;
                            AppRadioSetTxConfig(MODEM_LORA);
                        }

                        AppGetPayloadData(appCb->appPacketDataType, &aPos, &txBuffer);
                        aPayloadLen = (aPos < rfCb->payloadLen) ? aPos : rfCb->payloadLen;
                        txCb->txPktCount = txCb->txPktNum;

                        AppSetState(appCb, APP_TX);
                    }
                    // no break (fall-through)
                case APP_TX:
                    {
                        if (txCb->ccaEnable && (rfCb->pib.radioCfgCheckEnable == false)) {
                            AppRadioSetRxConfig(aModem);
                            if (!Radio.IsChannelFree( aModem, rfCb->freq, txCb->ccaRssiThresh, txCb->ccaScanDuration))
                            {
                                /* Carrier detected */
                                AppSetTranState(appCb, APP_TX_WAITING);
                                break;
                            }
                        }

                        switch(Radio.Send(txBuffer, aPayloadLen))
                        {
                            case RADIO_SUCCESS:
                                AppSetState(appCb, APP_TX_RUNNING);
                                break;
                            case RADIO_CHECK_FAIL_TX_CFG:
                                print("+SAFE:FAIL_TX_CFG\r\n");
                                Radio.Standby();
                                AppSetState(appCb, APP_IDLE);
                                break;
                            case RADIO_CHECK_FAIL_TX_DUTY_CYCLE:
                                print("+SAFE:FAIL_TX_DUTY_CYCLE\r\n");
                                Radio.Standby();
                                AppSetState(appCb, APP_IDLE);
                                break;
                            case RADIO_CHECK_FAIL_TX_CHANNEL_BUSY:
                                print("+SAFE:FAIL_TX_CHANNEL_BUSY\r\n");
                                Radio.Standby();
                                AppSetState(appCb, APP_IDLE);
                                break;
                            default:
                                Radio.Standby();
                                AppSetState(appCb, APP_IDLE);
                                break;
                        }
                        break;
                    }

                case APP_TX_WAITING:
                    {
                        Radio.Standby();

                        /* set next transmission interval */
                        txDutyCycle = &(txCb->txDutyCycle);
                        TimerSetValue(&txNextPacketTimer, (uint32_t )(*txDutyCycle));
                        TimerStart   (&txNextPacketTimer);
                        break;
                    }

                case APP_TXCW:
                case APP_TXCP:
                    {
                        Radio.SetChannel(rfCb->freq);

                        if (AppMatchPacketType(PACKET_TYPE_GFSK)) {
                            AppRadioSetTxConfig(MODEM_FSK);
                        } else {
                            AppRadioSetTxConfig(MODEM_LORA);
                        }

                        if (APP_TXCW == AppGetState(appCb)) {
                            Radio.SetTxContinuousWave(rfCb->freq, rfCb->txPower, rfCb->txTimeout);
                        } else {
                            Radio.SetTxInfinitePreamble(rfCb->freq, rfCb->txPower, rfCb->txTimeout);
                        }

                        AppSetState(appCb, APP_TXCX_RUNNING);
                        break;
                    }

                case APP_TX_TIMEOUT:
                case APP_TX_STOP:
                case APP_TX_DONE:
                    {
                        AppStates_t s;

                        TimerStop(&txNextPacketTimer);
                        Radio.Standby();

                        s = AppGetState(appCb);
                        if ((APP_TX_DONE != s) && (APP_TX_TIMEOUT != s)) {
                            AtCmdSetRcStatus(AT_RC_OK);
                            AtPrintResultCode();
                        }

                        AppSetState(appCb, APP_IDLE);
                        break;
                    }

                case APP_LOWPOWER:
                    {
                        if (0 == appCb->powerSaveCb.period) {
                            TimerStop(&txNextPacketTimer);
                            TimerStop(&sleepNextWakeUpTimer);
                            RpMcuResourceTimerStop();
                        } else {
                            TimerSetValue(&sleepNextWakeUpTimer, (uint32_t )(appCb->powerSaveCb.period * 1000UL));
                            TimerStart(&sleepNextWakeUpTimer);
                        }

                        AppSetState(appCb, APP_SLEEP);

                        /* radio sleep mode */
                        if(appCb->powerSaveCb.mode == APP_SLEEP_MODE_WARM) {
                            Radio.SleepWarm();
                        } else {
                            Radio.SleepCold();
                        }

                        SetLowPower();
                        break;
                    }

                case APP_WAKEUP:
                    {
                        Radio.WakeUp();

                        AtCmdSetRcStatus(AT_RC_OK);
                        AtPrintResultCode();

                        AppSetState(appCb, APP_IDLE);
                        break;
                    }

                case APP_RESET:
                    {
                        /* hardware reset */
                        BoardResetMcu();
                        break;
                    }

                case APP_INIT:
                    {
                        AtCmdSetRcStatus(AT_RC_OK);
                        AtPrintResultCode();

                        AppSetState(appCb, APP_IDLE);

                        /* enabling uart reception on application level until the app's buffer initialized */
                        AtInitUart();
                        break;
                    }

                case APP_RX_RUNNING:
                case APP_IDLE:
                case APP_CAD:
                case APP_CAD_DONE:
                default:
                    {
                        break;
                    }
            }/* switch (state) */
        }
        else {
            Radio.IrqProcess();
        }/* AppGetEvent */
    }/* infinite loop */
}

/*!
 */
void OnTxDone (void)
{
    PacketCb_t  *p = &(pktData->txPktCb);

    if (txCb->isVerbose) {
        print("+TX:");
        print_dec(txCb->txPktNum - txCb->txPktCount + 1, 10, 0);
        print("\r\n");
    }

    if (APP_TX_STOP == AppGetState(appCb)) {
        TimerStop(&txNextPacketTimer);
    }
    else {
        txCb->txPktCount--;

        /* if tkPktNum equal ZERO, do not stop transmitting packets */
        if (txCb->txPktNum != 0) {
            if (txCb->txPktCount != 0) {
                /* prepare next packet */
                switch (p->bCb.options) {
                    case APP_FRAMETYPE_PER:
                        {
                            ByteOrder_t num;

                            txCb->txSeqNum++;
                            num.n = txCb->txSeqNum;
                            p->pktData[ 3 ] = num.b[3];
                            p->pktData[ 4 ] = num.b[2];
                            p->pktData[ 5 ] = num.b[1];
                            p->pktData[ 6 ] = num.b[0];
                            break;
                        }
                    case APP_FRAMETYPE_EXT:
                        {
                            AppUpdateSensorData(false);
                            break;
                        }
                    case APP_FRAMETYPE_PN9:
                    case APP_FRAMETYPE_ANY:
                    default:
                        break;
                } /* switch (p->bCb.options) */
                AppSetTranState(appCb, APP_TX_WAITING);
            }
            else {
                /* stop transmitting */
                TimerStop(&txNextPacketTimer);
                AppSetTranState(appCb, APP_TX_DONE);
            }
        }
    }
}

/*!
 */
void OnTxTimeout (void)
{
    print("+INFO:TX_TIMEOUT\r\n");
    if (APP_TX_STOP != AppGetState(appCb)) {
        AppSetTranState(appCb, APP_TX_TIMEOUT);
    }
}

/*!
 */
void OnTxNextPacketTimerEvent (void)
{
    TimerStop(&txNextPacketTimer);

    if (APP_TX_STOP != AppGetState(appCb)) {
        AppSetTranState(appCb, APP_TX);
    }
}

/*!
 */
void OnSleepNextWakeUpTimerEvent (void)
{
    TimerStop(&sleepNextWakeUpTimer);

    AppSetTranState(appCb, APP_WAKEUP);
}

/*!
 */
void OnRxDone (uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    uint16_t    eflag;
    uint16_t    i;

    PacketCb_t *p = &(pktData->rxPktCb);

    eflag = Radio.GetErrorFlag();

    if (rxCb->isVerbose) {
        print("+RX:");
        for (i=0; i < size; i++) {
            p->pktData[i] = *(payload+i);
            print_hex( *(payload+i), 2 );
        }
        putchar(',');
    }
    else {
        for (i=0; i < size; i++) {
            p->pktData[i] = *(payload+i);
        }
    }

    p->bCb.pos = size;

    rxCb->rxPktOkCount++;
    rxCb->rxPktCount++;

    if (APP_FRAMETYPE_PN9 == appCb->appPacketDataType || APP_FRAMETYPE_ANY == appCb->appPacketDataType) {
        /* PN9 or ANY */
        uint8_t n;
        int8_t  *cp;

        cp = (APP_FRAMETYPE_PN9 == appCb->appPacketDataType) ? &(pktData->txPktCb.pktData[0]) : &(anyPkt->pktData[0]);

        rxCb->pktBitsCount   = 0;
        rxCb->pktBitsNgCount = 0;
        rxCb->pktBitsOkCount = 0;

        rxCb->pktBitsCount += size * 8;
        for (i=0; i < size; i++) {
            n = AppCountBits(p->pktData[i] ^ *cp++);
            rxCb->pktBitsNgCount += n;
            rxCb->pktBitsOkCount += (8 - n);
        }
        rxCb->rxBitsCount   += rxCb->pktBitsCount;
        rxCb->rxBitsOkCount += rxCb->pktBitsOkCount;
        rxCb->rxBitsNgCount += rxCb->pktBitsNgCount;

        if (AppMatchPacketType(PACKET_TYPE_GFSK)) {
            /* GFSK */
            if (!rfCb->fsk.crcOn) {
                /* CRC off */
                if (rxCb->pktBitsNgCount != 0) {
                    /* Bit ERR */
                    rxCb->rxPktOkCount--;
                    rxCb->rxPktNgCount++;
                }
            }
        }
        else {
            /* LORA */
            if (!rfCb->lora.crcOn) {
                /* CRC off */
                if (rxCb->pktBitsNgCount != 0) {
                    /* Bit ERR */
                    rxCb->rxPktOkCount--;
                    rxCb->rxPktNgCount++;
                }
            }
        }
    }

    if (rssi <= 0) {
        rxCb->rssiCount++;
        rxCb->rssiMean += rssi;
        rxCb->rssiMin   = (rxCb->rssiMin < rssi ) ? rxCb->rssiMin : rssi;
        rxCb->rssiMax   = (rxCb->rssiMax > rssi ) ? rxCb->rssiMax : rssi;
    }

    if (AppMatchPacketType(PACKET_TYPE_LORA)) {
        rxCb->snrCount++;
        rxCb->snrMean  += snr;
        rxCb->snrMin    = (rxCb->snrMin < snr ) ? rxCb->snrMin : snr;
        rxCb->snrMax    = (rxCb->snrMax > snr ) ? rxCb->snrMax : snr;
    } else {
        snr = 0;
    }

    if (rxCb->isVerbose) {
        uint8_t e;
        e = (eflag & RADIO_PAYLOAD_CRC_ERROR) ? 1 : 0;
        print_dec(rssi, 4, 0); putchar(',');
        print_dec(snr,  4, 0); putchar(',');
        print_dec(e,    4, 0);
        AtPrintTrailer();
    }


    if (APP_RX_STOP == AppGetState(appCb)) {
        ;
    }
    else {
        if (AppMatchPacketType(PACKET_TYPE_GFSK)) {
            AppSetTranState(appCb, ((rfCb->fsk.rxContinuous) ? APP_RX_RUNNING : APP_IDLE));
        }
        else {
            AppSetTranState(appCb, ((rfCb->lora.rxContinuous) ? APP_RX_RUNNING : APP_IDLE));
        }
    }
}

/*!
 */
uint8_t AppCountBits(uint8_t bits)
{
    uint8_t n = bits;
    n = (n & 0x55) + ((n>>1) & 0x55);
    n = (n & 0x33) + ((n>>2) & 0x33);
    n = (n & 0x0F) + ((n>>4) & 0x0F);

    return (n);
}

/*!
 */
void OnRxTimeout (void)
{
    print("+INFO:RX_TIMEOUT\r\n");
    if (rxCb->isVerbose) {
        print("+RX:00,0,0,2\r\n");
    }

    rxCb->rxPktToCount++;

    if (APP_RX_STOP == AppGetState(appCb)) {
        ;
    }
    else {
        if (AppMatchPacketType(PACKET_TYPE_GFSK)) {
            AppSetTranState(appCb, ((rfCb->fsk.rxContinuous) ? APP_RX_RUNNING : APP_RX_TIMEOUT));
        }
        else {
            AppSetTranState(appCb, ((rfCb->lora.rxContinuous) ? APP_RX_RUNNING : APP_RX_TIMEOUT));
        }
    }
}

/*!
 */
void OnRxError (void)
{
    rxCb->rxPktOkCount--;
    rxCb->rxPktNgCount++;

    if (APP_RX_STOP == AppGetState(appCb)) {
        ;
    }
    else {
        if (AppMatchPacketType(PACKET_TYPE_GFSK)) {
            AppSetTranState(appCb, ((rfCb->fsk.rxContinuous) ? APP_RX_RUNNING : APP_RX));
        }
        else {
            AppSetTranState(appCb, ((rfCb->lora.rxContinuous) ? APP_RX_RUNNING : APP_RX));
        }
    }
}

/*!
 */
bool AppMatchPacketType (RadioPacketTypes_t type)
{
    return ((type == rfCb->modem) ? true : false);
}

/*!
 */
void AppGetPayloadData (uint8_t pktDataType, uint8_t *pos, uint8_t **data)
{
    switch (pktDataType) {
        case APP_FRAMETYPE_PER:
        case APP_FRAMETYPE_PN9:
        case APP_FRAMETYPE_EUI:
        case APP_FRAMETYPE_EXT:
            {
                *pos  = (uint8_t )   pktData->txPktCb.bCb.pos;
                *data = (uint8_t *)&(pktData->txPktCb.pktData[0]);
                break;
            }

        case APP_FRAMETYPE_ANY:
            {
                *pos  = (uint8_t )   anyPkt->bCb.pos;
                *data = (uint8_t *)&(anyPkt->pktData[0]);
                break;
            }

        default:
            {
                break;
            }
    }/* pktDataType */
}

/*!
 */
void AppInit (void)
{
    TimerInit(&txNextPacketTimer, OnTxNextPacketTimerEvent);
    TimerInit(&sleepNextWakeUpTimer, OnSleepNextWakeUpTimerEvent);

    AtProcInit();

    AppLoadCb();

    {
        AppClearTxCb();
        AppClearRxCb();
    }

    {
        AppClearBuffer(&(pktData->rxPktCb.bCb), &(pktData->rxPktCb.pktData[0]), APP_PACKETDATA_ARRAYSIZE);
        AppClearBuffer(&(pktData->txPktCb.bCb), &(pktData->txPktCb.pktData[0]), APP_PACKETDATA_ARRAYSIZE);
        AppSetupPacketData(appCb->appPacketDataType);
    }

    AppRadioInit();

    appCb->event = 0UL;

    AppSetTranState(appCb, APP_INIT);
}

/*!
 */
void AppRadioInit (void)
{
    /* initialize radio with hardware default */
    RadioEvents.TxDone              = OnTxDone;
    RadioEvents.RxDone              = OnRxDone;
    RadioEvents.TxTimeout           = OnTxTimeout;
    RadioEvents.RxTimeout           = OnRxTimeout;
    RadioEvents.RxError             = OnRxError;
    RadioEvents.FhssChangeChannel   = NULL;
    RadioEvents.CadDone             = NULL;
    Radio.Init(&RadioEvents);

    /* re-initialize radio with application settings*/
    AppRadioSetTxConfig(MODEM_LORA);
    AppRadioSetRxConfig(MODEM_LORA);

    /* re-initialize PIB with application settings */
    Radio.SetPib(PIB_RSSI_OFFSET, (uint8_t *)&(rfCb->pib.rssiOffset));
    Radio.SetPib(PIB_CCA_BANDWIDTH, (uint8_t *)&(rfCb->pib.ccaBandWidth));
    Radio.SetPib(PIB_XTAL_XTA_TRIM, (uint8_t *)&(rfCb->pib.xtalXtaTrim));
    Radio.SetPib(PIB_XTAL_XTB_TRIM, (uint8_t *)&(rfCb->pib.xtalXtbTrim));
    Radio.SetPib(PIB_CALL_RX_DONE_IN_PAYLOAD_CRC_ERROR, (uint8_t *)&(rfCb->pib.callRxDoneInPayloadCrcError));
    Radio.SetPib(PIB_GAIN_BOOSTED, (uint8_t *)&(rfCb->pib.gainBoosted));
    Radio.SetPib(PIB_RADIO_CFG_CHECK_ENABLE, (uint8_t *)&(rfCb->pib.radioCfgCheckEnable));
    Radio.SetPib(PIB_RADIO_CFG_REGION, (uint8_t *)&(rfCb->pib.region));
}

/*!
 */
void AppLoadCb (void)
{
#if !defined(RP_DFLASH_UNUSED)
    DfLoadLargeSpace((uint8_t *)&appControlBlock, sizeof(AppCb_t), (uint8_t *)&appDefaultCb);
    if ( APP_REVAL_VERSION != appControlBlock.version )
    {
        AppReloadDefaultCb();
    }
#else
    AppReloadDefaultCb();
#endif
}

/*!
 */
void AppSaveCb (void)
{
#if !defined(RP_DFLASH_UNUSED)
    DfSaveLargeSpace((uint8_t *)&appControlBlock, sizeof(AppCb_t));
#endif
}

/*!
 */
void AppReloadDefaultCb (void)
{
    memcpy((uint8_t *)&appControlBlock, (uint8_t *)&appDefaultCb, sizeof(AppCb_t));
#if !defined(RP_DFLASH_UNUSED)
    DfSaveLargeSpace((uint8_t *)&appDefaultCb, sizeof(AppCb_t));
#endif
}

/*!
 */
void AppClearBuffer (BufferCb_t *ibCb, int8_t *buffer, uint16_t arraySize)
{
    ibCb->arraySize = arraySize;
    for (ibCb->pos=0; ibCb->pos < arraySize; ibCb->pos++) {
        buffer[ ibCb->pos ] = 0;
    }
    ibCb->pos       = 0;
    ibCb->locked    = 0;
    ibCb->options   = 0;
}

/*!
 */
void AppClearTxCb (void)
{
    txCb->txPktCount    = 0UL;
    txCb->txSeqNum      = 1UL;
}

/*!
 */
void AppClearRxCb (void)
{
    rxCb->rxPktOkCount  = 0UL;
    rxCb->rxPktNgCount  = 0UL;
    rxCb->rxPktToCount  = 0UL;
    rxCb->rxPktCount    = 0UL;

    rxCb->rxBitsOkCount = 0UL;
    rxCb->rxBitsNgCount = 0UL;
    rxCb->rxBitsToCount = 0UL;
    rxCb->rxBitsCount   = 0UL;

    rxCb->pktBitsOkCount= 0UL;
    rxCb->pktBitsNgCount= 0UL;
    rxCb->pktBitsCount  = 0UL;

    rxCb->rssiCount     = 0UL;
    rxCb->rssiMin       = 0;
    rxCb->rssiMax       = INT8_MIN;
    rxCb->rssiMean      = 0L;

    rxCb->snrCount      = 0UL;
    rxCb->snrMin        = INT8_MAX;
    rxCb->snrMax        = INT8_MIN;
    rxCb->snrMean       = 0L;
}

/*!
 */
const uint8_t appPn9DataTable [APP_PN9_DATA_LEN] = {
 0xFF, 0x83, 0xDF, 0x17, 0x32, 0x09, 0x4E, 0xD1, 0xE7, 0xCD, 0x8A, 0x91, 0xC6, 0xD5, 0xC4, 0xC4, 0x40, 0x21,
 0x18, 0x4E, 0x55, 0x86, 0xF4, 0xDC, 0x8A, 0x15, 0xA7, 0xEC, 0x92, 0xDF, 0x93, 0x53, 0x30, 0x18, 0xCA, 0x34,
 0xBF, 0xA2, 0xC7, 0x59, 0x67, 0x8F, 0xBA, 0x0D, 0x6D, 0xD8, 0x2D, 0x7D, 0x54, 0x0A, 0x57, 0x97, 0x70, 0x39,
 0xD2, 0x7A, 0xEA, 0x24, 0x33, 0x85, 0xED, 0x9A, 0x1D, 0xE1, 0xFF, 0x07, 0xBE, 0x2E, 0x64, 0x12, 0x9D, 0xA3,
 0xCF, 0x9B, 0x15, 0x23, 0x8D, 0xAB, 0x89, 0x88, 0x80, 0x42, 0x30, 0x9C, 0xAB, 0x0D, 0xE9, 0xB9, 0x14, 0x2B,
 0x4F, 0xD9, 0x25, 0xBF, 0x26, 0xA6, 0x60, 0x31, 0x94, 0x69, 0x7F, 0x45, 0x8E, 0xB2, 0xCF, 0x1F, 0x74, 0x1A,
 0xDB, 0xB0, 0x5A, 0xFA, 0xA8, 0x14, 0xAF, 0x2E, 0xE0, 0x73, 0xA4, 0xF5, 0xD4, 0x48, 0x67, 0x0B, 0xDB, 0x34,
 0x3B, 0xC3, 0xFE, 0x0F, 0x7C, 0x5C, 0xC8, 0x25, 0x3B, 0x47, 0x9F, 0x36, 0x2A, 0x47, 0x1B, 0x57, 0x13, 0x11,
 0x00, 0x84, 0x61, 0x39, 0x56, 0x1B, 0xD3, 0x72, 0x28, 0x56, 0x9F, 0xB2, 0x4B, 0x7E, 0x4D, 0x4C, 0xC0, 0x63,
 0x28, 0xD2, 0xFE, 0x8B, 0x1D, 0x65, 0x9E, 0x3E, 0xE8, 0x35, 0xB7, 0x60, 0xB5, 0xF5, 0x50, 0x29, 0x5E, 0x5D,
 0xC0, 0xE7, 0x49, 0xEB, 0xA8, 0x90, 0xCE, 0x17, 0xB6, 0x68, 0x77, 0x87, 0xFC, 0x1E, 0xF8, 0xB9, 0x90, 0x4A,
 0x76, 0x8F, 0x3E, 0x6C, 0x54, 0x8E, 0x36, 0xAE, 0x26, 0x22, 0x01, 0x08, 0xC2, 0x72, 0xAC, 0x37, 0xA6, 0xE4,
 0x50, 0xAD, 0x3F, 0x64, 0x96, 0xFC, 0x9A, 0x99, 0x80, 0xC6, 0x51, 0xA5, 0xFD, 0x16, 0x3A, 0xCB, 0x3C, 0x7D,
 0xD0, 0x6B, 0x6E, 0xC1, 0x6B, 0xEA, 0xA0, 0x52, 0xBC, 0xBB, 0x81, 0xCE, 0x93, 0xD7, 0x51, 0x21, 0x9C, 0x2F,
 0x6C, 0xD0, 0xEF,};

/*!
 */
void AppSetupPacketData (uint8_t type)
{
    uint16_t    i;
    uint8_t     n;
    PacketCb_t  *p = &(pktData->txPktCb);

    switch (type) {
        case APP_FRAMETYPE_PER:
            {
                ByteOrder_t num;

                num.n = txCb->txSeqNum;
                p->bCb.pos = 0;
                p->bCb.options = APP_FRAMETYPE_PER;
                p->pktData[ p->bCb.pos++ ] = 'P';
                p->pktData[ p->bCb.pos++ ] = 'E';
                p->pktData[ p->bCb.pos++ ] = 'R';
                p->pktData[ p->bCb.pos++ ] = num.b[3];
                p->pktData[ p->bCb.pos++ ] = num.b[2];
                p->pktData[ p->bCb.pos++ ] = num.b[1];
                p->pktData[ p->bCb.pos++ ] = num.b[0];
                n = 0;
                for (i=p->bCb.pos; i<p->bCb.arraySize; i++, p->bCb.pos++) {
                    if (n >= APP_PN9_DATA_LEN) n = 0;
                    p->pktData[ i ] = appPn9DataTable[ n++ ];
                }
                break;
            }
        case APP_FRAMETYPE_PN9:
            {
                p->bCb.pos     = 0;
                p->bCb.options = APP_FRAMETYPE_PN9;
                n = 0;
                for (i=p->bCb.pos; i<p->bCb.arraySize; i++, p->bCb.pos++) {
                    if (n >= APP_PN9_DATA_LEN) n = 0;
                    p->pktData[ i ] = appPn9DataTable[ n++ ];
                }
                break;
            }
        case APP_FRAMETYPE_EUI:
            {
                p->bCb.pos = 0;
                p->bCb.options = APP_FRAMETYPE_EUI;
                p->pktData[ p->bCb.pos++ ] = 'E';
                p->pktData[ p->bCb.pos++ ] = 'U';
                p->pktData[ p->bCb.pos++ ] = 'I';
                for (i=0; i<8; i++) {
                    p->pktData[ p->bCb.pos++ ] = appCb->nodeInfo.devEui[i];
                }
                for (n=0,i=p->bCb.pos; i<p->bCb.arraySize; i++, p->bCb.pos++) {
                    if (n >= APP_PN9_DATA_LEN) n = 0;
                    p->pktData[ i ] = appPn9DataTable[ n++ ];
                }
                break;
            }
        case APP_FRAMETYPE_EXT:
            {
                AppUpdateSensorData(true);
                break;
            }
        default:
            break;
    }
}

/*!
 */
void AppUpdateSensorData (bool isPadding)
{
    PacketCb_t  *p = &(pktData->txPktCb);

    p->bCb.pos = 0;
    p->bCb.options = APP_FRAMETYPE_EXT;

#if defined(RM_HS300X_H)
    if (app_hs300x_start_measure())
    {
        DelayMs(HS300X_RESPONSE_TIME);
        app_hs300x_result_t result = app_hs300x_get_result();
        if(result.isValid)
        {
            p->pktData[ p->bCb.pos++ ] = appCb->nodeInfo.devEui[7];
            p->pktData[ p->bCb.pos++ ] = (result.hum >> 8) & 0x00ff;
            p->pktData[ p->bCb.pos++ ] = result.hum & 0x00ff;
            p->pktData[ p->bCb.pos++ ] = (result.temp >> 8) & 0x00ff;
            p->pktData[ p->bCb.pos++ ] = result.temp & 0x00ff;
        }
    }
#endif

    if(isPadding)
    {
        for (; p->bCb.pos < p->bCb.arraySize; p->bCb.pos++) {
            p->pktData[ p->bCb.pos ] = 0x00;
        }
    }
}

/*!
 */
void AppRXResultPrint (void)
{
    AtPrintHeader();

    if (rxCb->rxPktCount == 0) {
        rxCb->rssiMean = rxCb->rssiMin = rxCb->rssiMax  = 0;
        rxCb->snrMean  = rxCb->snrMin  = rxCb->snrMax   = 0;
    }

    if (AppMatchPacketType(PACKET_TYPE_GFSK)) {
        rxCb->snrMean  = rxCb->snrMin  = rxCb->snrMax   = 0;
    }

    print_dec(rxCb->rxPktCount, 10, 0); putchar(',');
    print_dec(rxCb->rxPktOkCount, 10, 0); putchar(',');
    print_dec(rxCb->rxPktNgCount, 10, 0); putchar(',');
    print_dec(rxCb->rxBitsCount, 10, 0); putchar(',');
    print_dec(rxCb->rxBitsOkCount, 10, 0); putchar(',');
    print_dec(rxCb->rxBitsNgCount, 10, 0); putchar(',');
    print_dec(rxCb->rssiMean / (int32_t)rxCb->rssiCount, 10, 0); putchar(',');
    print_dec(rxCb->rssiMin, 10, 0); putchar(',');
    print_dec(rxCb->rssiMax, 10, 0); putchar(',');
    print_dec(rxCb->snrMean / (int32_t)rxCb->snrCount, 10, 0); putchar(',');
    print_dec(rxCb->snrMin,  10, 0); putchar(',');
    print_dec(rxCb->snrMax,  10, 0);

    AtPrintTrailer();
}

/*!
 */
AppStates_t AppGetState (AppCb_t *appCb)
{
    return (appCb->state);
}

/*!
 */
void AppSetState (AppCb_t *appCb, AppStates_t state)
{
    appCb->state = state;
}

/*!
 */
uint32_t AppGetTranState (AppCb_t *appCb, AppStates_t *state)
{
    uint32_t evt = 0UL;

    BoardDisableAllIrq();
    {
        evt = appCb->event & APP_EVT_STATE_TRAN;
        appCb->event &= ~APP_EVT_STATE_TRAN;

        *state = AppGetState(appCb);
    }
    BoardEnableAllIrq();

    return (evt);
}

/*!
 */
uint32_t AppSetTranState (AppCb_t *appCb, AppStates_t state)
{
    uint32_t evt = 0UL;

    BoardDisableAllIrq();
    {
        appCb->event |= APP_EVT_STATE_TRAN;
        evt = appCb->event;

        AppSetState(appCb, state);
    }
    BoardEnableAllIrq();

    return (evt);
}

/*!
 */
uint32_t AppGetEvent (AppCb_t *appCb, uint32_t evtMask)
{
    uint32_t evt = 0UL;

    BoardDisableAllIrq();
    {
        evt  = appCb->event & evtMask;  /* get event    */
        appCb->event &= ~evtMask;       /* clear event  */
    }
    BoardEnableAllIrq();

    return (evt);
}

/*!
 */
uint32_t AppSetEvent (AppCb_t *appCb, uint32_t evtMask)
{
    uint32_t evt = 0UL;

    BoardDisableAllIrq();
    {
        appCb->event |= evtMask;        /* set event    */
        evt = appCb->event;             /* copy event   */
    }
    BoardEnableAllIrq();

    return (evt);
}


/*!
 */
void AppRadioSetRxConfig (RadioModems_t modem)
{
    switch (modem) {
    case MODEM_FSK:
        Radio.SetRxConfig (
            modem,
            rfCb->fsk.bandWidth,
            rfCb->fsk.dataRate,
            0, // coderate
            0, // bandwidthAfc
            rfCb->fsk.preambleLen,
            rfCb->fsk.symbTimeout,
            rfCb->fsk.fixLen,
            rfCb->payloadLen,
            rfCb->fsk.crcOn,
            false, // freqHopOn
            0, // hopPeriod,
            false, // iqInverted
            rfCb->fsk.rxContinuous
        );
        break;
    case MODEM_LORA:
        Radio.SetRxConfig (
            modem,
            rfCb->lora.bandWidth,
            rfCb->lora.spreadFactor,
            rfCb->lora.codeRate,
            0, // bandwidthAfc
            rfCb->lora.preambleLen,
            rfCb->lora.symbTimeout,
            rfCb->lora.fixLen,
            rfCb->payloadLen,
            rfCb->lora.crcOn,
            false, // freqHopOn
            0, // hopPeriod,
            rfCb->lora.iqInverted,
            rfCb->lora.rxContinuous
        );
        break;
    }
}


/*!
 */
void AppRadioSetTxConfig (RadioModems_t modem)
{
    switch (modem) {
    case MODEM_FSK:
        Radio.SetTxConfig (
            modem,
            rfCb->txPower,
            rfCb->fsk.fDev,
            0,      // bandwidth
            rfCb->fsk.dataRate,
            0,      // coderate
            rfCb->fsk.preambleLen,
            rfCb->fsk.fixLen,
            rfCb->fsk.crcOn,
            false,  // freqHopOn
            0,      // hopPeriod,
            false,  // iqInverted
            rfCb->txTimeout
        );
        break;
    case MODEM_LORA:
        Radio.SetTxConfig (
            modem,
            rfCb->txPower,
            0,      // fdev
            rfCb->lora.bandWidth,
            rfCb->lora.spreadFactor,
            rfCb->lora.codeRate,
            rfCb->lora.preambleLen,
            rfCb->lora.fixLen,
            rfCb->lora.crcOn,
            false,  // freqHopOn
            0,      // hopPeriod,
            rfCb->lora.iqInverted,
            rfCb->txTimeout
        );
        break;
    }
}
