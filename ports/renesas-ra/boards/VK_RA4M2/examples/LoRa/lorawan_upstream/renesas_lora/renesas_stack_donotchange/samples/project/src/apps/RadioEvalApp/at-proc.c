/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/
#include <math.h>
#include "board.h"
#include "at-command.h"
#include "at-parser.h"
#include "at-proc.h"
#include "radio-eval.h"

#define APP_ATPROC_CMDR_BUFFER_ARRAYSIZE     (512+32)

uint8_t  atcmdr_buffer[ APP_ATPROC_CMDR_BUFFER_ARRAYSIZE ];

extern AppCb_t      *appCb;
extern TxCb_t       *txCb;
extern RxCb_t       *rxCb;
extern RadioCb_t    *rfCb;
extern PacketData_t *pktData;

/*!
 * \brief initialize at-command procedures
 */
void AtProcInit (void)
{
    /* initialize at-command */
    AtInit(&atcmdr_buffer[0], APP_ATPROC_CMDR_BUFFER_ARRAYSIZE);

    /* register at-command sets */
    if (AtProcReg() != 0) {
        print("FATAL: at-commands registration error.\r\n");
        while(1);
    }
}

int8_t AtProcReg (void)
{
    int8_t r;

    /* Register extended commands to the table. */
    {
        AtExtendCmdRegist ( "+STAT",       NULL,                 NULL,                 AtExtendActStat   );
        AtExtendCmdRegist ( "+RESET",      NULL,                 NULL,                 AtExtendActReset  );
        AtExtendCmdRegist ( "+MODEM",      AtExtendSetModem,     AtExtendGetModem,     NULL              );
        AtExtendCmdRegist ( "+FREQ",       AtExtendSetFreq,      AtExtendGetFreq,      NULL              );
        AtExtendCmdRegist ( "+TXPWR",      AtExtendSetTxPwr,     AtExtendGetTxPwr,     NULL              );
        AtExtendCmdRegist ( "+SEND",       AtExtendSetSend,      AtExtendGetSend,      AtExtendActSend   );
        AtExtendCmdRegist ( "+RECV",       AtExtendSetRecv,      AtExtendGetRecv,      AtExtendActRecv   );
        AtExtendCmdRegist ( "+TXCW",       NULL,                 NULL,                 AtExtendActTxCW   );
        AtExtendCmdRegist ( "+TXCP",       NULL,                 NULL,                 AtExtendActTxCP   );
        AtExtendCmdRegist ( "+STOP",       NULL,                 NULL,                 AtExtendActStop   );
        AtExtendCmdRegist ( "+LMCFG",      AtExtendSetLMCfg,     AtExtendGetLMCfg,     NULL              );
        AtExtendCmdRegist ( "+LPCFG",      AtExtendSetLPCfg,     AtExtendGetLPCfg,     NULL              );
        AtExtendCmdRegist ( "+FMCFG",      AtExtendSetFMCfg,     AtExtendGetFMCfg,     NULL              );
        AtExtendCmdRegist ( "+FPCFG",      AtExtendSetFPCfg,     AtExtendGetFPCfg,     NULL              );
        AtExtendCmdRegist ( "+PKT",        AtExtendSetPkt,       AtExtendGetPkt,       NULL              );
        AtExtendCmdRegist ( "+TXTO",       AtExtendSetTxTo,      AtExtendGetTxTo,      NULL              );
        AtExtendCmdRegist ( "+RXTO",       AtExtendSetRxTo,      AtExtendGetRxTo,      NULL              );
        AtExtendCmdRegist ( "+RXGAIN",     AtExtendSetRxGain,    AtExtendGetRxGain,    NULL              );
        AtExtendCmdRegist ( "+RSSI",       AtExtendSetRssi,      AtExtendGetRssi,      AtExtendActRssi   );
        AtExtendCmdRegist ( "+LBT",        AtExtendSetLBT,       AtExtendGetLBT,       AtExtendActLBT    );
        AtExtendCmdRegist ( "+XTRIM",      AtExtendSetXTrim,     AtExtendGetXTrim,     NULL              );
        AtExtendCmdRegist ( "+SAVE",       NULL,                 NULL,                 AtExtendActSave   );
        AtExtendCmdRegist ( "+ERASE",      NULL,                 NULL,                 AtExtendActErase  );
        AtExtendCmdRegist ( "+REGW",       AtExtendSetRegW,      NULL,                 NULL              );
        AtExtendCmdRegist ( "+REGR",       AtExtendSetRegR,      NULL,                 NULL              );
        AtExtendCmdRegist ( "+DEVEUI",     AtExtendSetDevEui,    AtExtendGetDevEui,    NULL              );
        AtExtendCmdRegist ( "+SLEEP",      AtExtendSetSleep,     AtExtendGetSleep,     NULL              );
        AtExtendCmdRegist ( "+SAFE",       AtExtendSetSafe,      AtExtendGetSafe,      NULL              );
        AtExtendCmdRegist ( "+REGION",     AtExtendSetRegion,    AtExtendGetRegion,    NULL              );
        AtExtendCmdRegist ( "+TOA",        NULL,                 NULL,                 AtExtendActTOA    );
        AtExtendCmdRegist ( "+HELP",       NULL,                 NULL,                 AtExtendActHelp   );
        AtExtendCmdRegist ( "+LIST",       NULL,                 NULL,                 AtExtendActList   );
        /* Unofficial Commands */
        AtExtendCmdRegist ( "+SA",         AtExtendSetSA,        AtExtendGetSA,        AtExtendActSA     );
    }

    /* Confirm whether the table to register extend commands overflows. */
    {
        r = AtExtendCmdRegist ( "+VER", NULL, AtExtendGetVer, NULL );
    }
	return (r);
}

/*!
 */
void AtProcParser (void)
{
    AtCmdParse();
}

/*!
 * AT+STAT
 */
AtResultCode_t AtExtendActStat (void *p)
{
    AtPrintHeader();

    switch(AppGetState(appCb)) {
    case APP_IDLE:
        print("IDLE");
        break;
    case APP_TX_RUNNING:
    case APP_TX_WAITING:
    case APP_TX_TIMEOUT:
    case APP_TX:
    case APP_TX_CONFIG:
    case APP_TX_DONE:
    case APP_TX_STOP:
        print("TX,");
        print_dec(txCb->txPktNum - txCb->txPktCount + 1, 10, 0);
        break;
    case APP_RX_RUNNING:
    case APP_RX_TIMEOUT:
    case APP_RX_ERROR:
    case APP_RX:
    case APP_RX_CONFIG:
    case APP_RX_STOP:
        print("RX,");
        print_dec( rxCb->rxPktCount, 10, 0 );
        break;
    case APP_TXCX_RUNNING:
        print("CX");
        break;
    default:
        print("BUSY");
        break;
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+SAVE
 */
AtResultCode_t AtExtendActSave (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    } else {
        AppSaveCb();

        rc  = AT_RC_OK;
    }
    return rc;
}

/*!
 * AT+RELOAD
 */
AtResultCode_t AtExtendActErase (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    } else {
        AppReloadDefaultCb();
        AppSetTranState(appCb, APP_RESET);

        rc  = AT_RC_NOANS;
    }
    return rc;
}

/*!
 * AT+VER?
 */
AtResultCode_t AtExtendGetVer (void *p)
{
    AtPrintHeader();
    {
        print_dec(APP_REVAL_VERMAJOR, 2, 0); putchar('.');
        print_dec(APP_REVAL_VERMINOR, 2, 0); putchar(' ');
        print((char *)APP_REVAL_VERINFO); putchar(' ');

        if (RADIO_LOPOWER_SEL == SX126xGetPaSelect()){
            print("SX1261/");
        }else{
            print("SX1262/");
        }

        if (RADIO_CLOCK_XTAL_SEL == SX126xGetClockSelect()){
            print("XTAL");
        }else{
            print("TCXO");
        }
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+RESET
 */
AtResultCode_t AtExtendActReset (void *p)
{
    AtResultCode_t  rc = AT_RC_NOANS;

    AppSetTranState(appCb, APP_RESET);

    return rc;
}

/*!
 * AT+REGW=addr,byte1,byte2, ... ,byte8(max)
 */
AtResultCode_t AtExtendSetRegW (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;
    int16_t         i, narg;
    int8_t          *cp;
    int16_t         num;
    int16_t         ret;
    uint32_t        addr;
    uint8_t         argv[8] = {0,};
    uint8_t         fncRetVal = false;

    narg = AtParseListLen();
    if (narg < 2) {
        ;
    }
    else {
        do{
            AtParsePopOne(&cp, &num);
            if (cp == NULL || num < 1)
                break;

            addr = AtParseGet4ByteHexValue(cp, AT_CMD_NUL);
            if ((addr > 0xFFFFU) || ((narg - 1) > 8))
                break;

            addr = addr & 0x0000ffff;

            for (i=0; i < (narg -1); i++) {

                AtParsePopOne(&cp, &num);
                if (cp == NULL || num < 1)
                    break;

                ret = AtParseGetHexValue( cp, AT_CMD_NUL );
                if ( ret < 0 )
                    break;

                argv[i] = (uint8_t )(ret & 0x00ff);
            }

            if (i < (narg - 1)) {
                ;
            } else {
                for (i=0; i < (narg -1); i++) {
                    if ((addr + i) <= 0xFFFFU){
                        Radio.Write((uint16_t )(addr + i), argv[i]);
                    }
                }
                fncRetVal = true;
            }
        } while(0);
    }

    if (! fncRetVal) {
        rc = AT_RC_ERR;
    } else {
        rc = AT_RC_OK;
    }
    return rc;
}

/*!
 * AT+REGR=addr,size(1 .. 8)
 */
AtResultCode_t AtExtendSetRegR (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;
    int16_t         narg;
    int8_t          *cp;
    int16_t         num;
    uint16_t        i, regAddr;
    uint8_t         regBuffer[8] = {0,};
    uint8_t         fncRetVal = false;
    uint32_t        addr;
    uint16_t        maxCount = 0;
    int32_t         decVal;

    narg = AtParseListLen();
    if (narg != 2) {
        ;
    }
    else {
        do{
            AtParsePopOne(&cp, &num);
            if (cp == NULL || num < 1)
                break;

            addr = AtParseGet4ByteHexValue(cp, AT_CMD_NUL);
            if (addr > 0xFFFFU)
                break;

            regAddr = (uint16_t)addr;

            AtParsePopOne(&cp, &num);
            if (cp == NULL || num < 1)
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if (decVal < 1 || decVal > 8)
                break;

            for (i=0; i < decVal; i++) {
                if (regAddr <= 0xFFFFU - i){
                    maxCount = i;
                    regBuffer[i] = Radio.Read((regAddr + i));
                }
            }
            fncRetVal = true;
        } while(0);
    }

    if(!fncRetVal) {
        rc = AT_RC_ERR;
    }
    else {
        AtPrintHeader();
        for (i=0; i <= maxCount; i++) {
            print_hex((uint8_t)regBuffer[i], 2);
        }
        AtPrintTrailer();
        rc = AT_RC_OK;
    }
    return rc;
}

/*!
 * AT+MODEM=modem(0:GFSK, 1:LoRa)
 */
AtResultCode_t AtExtendSetModem (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    }
    else
    {
        do {
            int16_t     narg;
            int8_t      *cp;
            int16_t     num;
            int32_t     decVal;

            narg = AtParseListLen();
            if ( narg != 1 )
                break;

            AtParsePopOne( &cp, &num );
            if ((cp == NULL) || (num != 1))
                break;

            decVal =  AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( (decVal != APP_RADIO_GFSK) && (decVal != APP_RADIO_LORA) )
                break;

            Radio.Standby();

            if (APP_RADIO_GFSK == decVal) {
                rfCb->modem = PACKET_TYPE_GFSK;
                Radio.SetModem(MODEM_FSK);
            } else {
                rfCb->modem = PACKET_TYPE_LORA;
                Radio.SetModem(MODEM_LORA);
            }

            rc = AT_RC_OK;
        } while(0);
    }
    return rc;
}

/*!
 * AT+MODEM?
 */
AtResultCode_t AtExtendGetModem (void *p)
{
    AtPrintHeader();
    {
        if (AppMatchPacketType(PACKET_TYPE_GFSK)) {
            print("0");
        } else {
            print("1");
        }
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+FREQ=freq
 */
AtResultCode_t AtExtendSetFreq (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;
    int16_t         narg;
    int8_t          *cp;
    int16_t         num;
    int32_t         decVal;
    uint32_t        freq;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    }
    else {
        do {
            narg = AtParseListLen();
            if ( narg != 1 )
                break;

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            freq = (uint32_t)decVal;

            if ( freq < APP_RADIO_FREQ_LO || APP_RADIO_FREQ_HI < freq )
                break;

            if ( Radio.CheckRfFrequency(freq) == false)
                break;

            rfCb->freq = freq;
            Radio.SetChannel( freq );

            rc = AT_RC_OK;
        } while (0);
    }
    return rc;
}

/*!
 * AT+FREQ?
 */
AtResultCode_t AtExtendGetFreq (void *p)
{
    AtPrintHeader();
    {
        print_dec( rfCb->freq, 10, 0 );
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+TXPWR=power
 */
AtResultCode_t AtExtendSetTxPwr (void *p)
{
    AtResultCode_t      rc = AT_RC_ERR;
    int16_t             narg;
    int8_t              *cp;
    int16_t             num;
    int32_t             decVal;
    int16_t             power;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    } else {
        do {
            narg = AtParseListLen();
            if ( narg != 1 )
                break;

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue((*cp == '-') ? cp+1 : cp, AT_CMD_NUL );
            if (decVal < 0 )
                break;
            power = (*cp == '-') ? -(int16_t)decVal : (int16_t)decVal;

            if (((RADIO_LOPOWER_SEL == SX126xGetPaSelect())&&( -17 <= power && power <= 15 ))
               ||((RADIO_HIPOWER_SEL == SX126xGetPaSelect())&&( -9 <= power && power <= 22 ))) {

                rfCb->txPower = power;

                if (AppMatchPacketType(PACKET_TYPE_GFSK)) {
                    AppRadioSetTxConfig( MODEM_FSK );
                } else {
                    AppRadioSetTxConfig( MODEM_LORA );
                }
                rc = AT_RC_OK;
            }
        } while (0);
    }
    return rc;
}

/*!
 * AT+TXPWR?
 */
AtResultCode_t AtExtendGetTxPwr (void *p)
{
    AtPrintHeader();
    {
        print_dec( rfCb->txPower, 10, 0 );
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+TXCW
 */
AtResultCode_t AtExtendActTxCW (void *p)
{
    if (rfCb->pib.radioCfgCheckEnable)
        return AT_RC_ERR;

    if (APP_IDLE != AppGetState(appCb)) {
        return AT_RC_BUSY;
    } else {
        AppSetTranState(appCb, APP_TXCW);
        return AT_RC_OK;
    }
}

/*!
 * AT+TXCP
 */
AtResultCode_t AtExtendActTxCP (void *p)
{
    if (rfCb->pib.radioCfgCheckEnable)
        return AT_RC_ERR;

    if (APP_IDLE != AppGetState(appCb)) {
        return AT_RC_BUSY;
    } else {
        AppSetTranState(appCb, APP_TXCP);
        return AT_RC_OK;
    }
}

/*!
 * AT+SEND=pktNum[,pktDelay[,verbose(0:OFF, 1:ON=default)]]
 */
AtResultCode_t AtExtendSetSend (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    }
    else {
        do {
            int16_t     narg;
            int8_t      *cp;
            int16_t     num;
            int32_t     decVal;
            uint32_t    pktNum;
            uint32_t    pktDelay;
            bool        verbose;

            narg = AtParseListLen();
            if ( narg != 1 && narg != 2 && narg != 3)
                break;

            // pktNum

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal <= 0 || decVal > (int32_t)AT_EXTCMD_LIMIT_TXPKTNUM )
                break;
            pktNum = (uint32_t)decVal;

            if (narg == 1) {
                txCb->txPktNum = pktNum;
                if ( APP_FRAMETYPE_PER == appCb->appPacketDataType ||
                     APP_FRAMETYPE_EXT == appCb->appPacketDataType) {
                    txCb->txSeqNum = 1;
                    AppSetupPacketData( appCb->appPacketDataType );
                }
                AppSetTranState(appCb, APP_TX_CONFIG);
                rc = AT_RC_OK;
                break;
            }

            // pktDelay

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal <= 0 || decVal > AT_EXTCMD_LIMIT_TXINTVL )
                break;
            pktDelay = (uint32_t)decVal;

            if (narg == 2) {
                txCb->txPktNum = pktNum;
                txCb->txDutyCycle = pktDelay;
                if ( APP_FRAMETYPE_PER == appCb->appPacketDataType ||
                     APP_FRAMETYPE_EXT == appCb->appPacketDataType) {
                    txCb->txSeqNum = 1;
                    AppSetupPacketData( appCb->appPacketDataType );
                }
                AppSetTranState(appCb, APP_TX_CONFIG);
                rc = AT_RC_OK;
                break;
            }

            // verbose

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal != 0 && decVal != 1 )
                break;
            verbose = (decVal == 1);

            // update parameters

            txCb->txPktNum = pktNum;
            txCb->txDutyCycle = pktDelay;
            txCb->isVerbose = verbose;
            if ( APP_FRAMETYPE_PER == appCb->appPacketDataType ||
                 APP_FRAMETYPE_EXT == appCb->appPacketDataType) {
                txCb->txSeqNum = 1;
                AppSetupPacketData( appCb->appPacketDataType );
            }
            AppSetTranState(appCb, APP_TX_CONFIG);
            rc = AT_RC_OK;
        } while(0);
    }
    return rc;
}

/*!
 * AT+SEND?
 */
AtResultCode_t AtExtendGetSend (void *p)
{
    AtPrintHeader();
    {
        print_dec( txCb->txPktNum,           9, 0 ); putchar(',');
        print_dec( txCb->txDutyCycle,        7, 0 ); putchar(',');
        print_dec((txCb->isVerbose ? 1 : 0), 2, 0 );
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+SEND
 */
AtResultCode_t AtExtendActSend (void *p)
{
    if (APP_IDLE != AppGetState(appCb)) {
        return AT_RC_BUSY;
    } else {
        if (APP_FRAMETYPE_PER == appCb->appPacketDataType ||
            APP_FRAMETYPE_EXT == appCb->appPacketDataType) {
            txCb->txSeqNum = 1;
            AppSetupPacketData( appCb->appPacketDataType );
        }

        AppSetTranState(appCb, APP_TX_CONFIG);
    }
    return AT_RC_OK;
}

/*!
 * AT+RECV=rxMode(0:continuous, single)[,verbose(0:OFF,1:ON=default)]
 */
AtResultCode_t AtExtendSetRecv (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    } else {
        do {
            int16_t     narg;
            int8_t      *cp;
            int16_t     num;
            int32_t     decVal;
            bool        rxMode;
            bool        verbose;

            narg = AtParseListLen();
            if ( narg != 1 && narg != 2)
                break;

            /* rxMode */

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if (decVal != 0 && decVal != 1)
                break;
            rxMode = (decVal == 0/* rx continuous */);

            if (narg == 1) {
                if (AppMatchPacketType(PACKET_TYPE_GFSK)) {
                    rfCb->fsk.rxContinuous  = rxMode;
                } else {
                    rfCb->lora.rxContinuous = rxMode;
                }
                AppSetTranState(appCb, APP_RX_CONFIG);
                rc = AT_RC_OK;
                break;
            }

            /* verbose */

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if (decVal != 0 && decVal != 1)
                break;
            verbose = (decVal == 1);

            /* update parameters */

            if (AppMatchPacketType(PACKET_TYPE_GFSK)) {
                rfCb->fsk.rxContinuous  = rxMode;
            } else {
                rfCb->lora.rxContinuous = rxMode;
            }
            rxCb->isVerbose = verbose;
            AppSetTranState(appCb, APP_RX_CONFIG);
            rc = AT_RC_OK;
        } while (0);
    }
    return rc;
}

/*!
 * AT+RECV?
 */
AtResultCode_t AtExtendGetRecv (void *p)
{
    AtPrintHeader();
    {
        if (AppMatchPacketType(PACKET_TYPE_GFSK)) {
            print_dec((rfCb->fsk.rxContinuous ? 0 : 1), 2, 0);
        } else {
            print_dec((rfCb->lora.rxContinuous ? 0 : 1), 2, 0);
        }
        putchar(',');
        print_dec((rxCb->isVerbose ? 1 : 0), 2, 0 );
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+RECV
 */
AtResultCode_t AtExtendActRecv (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    } else {
        AppSetTranState(appCb, APP_RX_CONFIG);
        rc = AT_RC_OK;
    }
    return rc;
}

/*!
 * AT+RSSI=rssiOffset
 */
AtResultCode_t AtExtendSetRssi (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    } else {
        do {
            int16_t     narg;
            int8_t      *cp;
            int16_t     num;
            int32_t     decVal;
            bool        isNegative = false;
            int8_t      rssiOffset = 0;

            narg = AtParseListLen();
            if ( narg != 1 )
                break;

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            /* RssiOffset */
            if ( *cp == '-' ) {
                cp++;
                isNegative = true;
            }

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if (decVal < 0 || decVal > AT_EXTCMD_LIMIT_RSSIOFFSET )
                break;

            rssiOffset = isNegative ? -(int8_t)decVal : (int8_t)decVal;

            if ( Radio.SetPib( PIB_RSSI_OFFSET, (uint8_t * )&rssiOffset) ) {
                rfCb->pib.rssiOffset = rssiOffset;
                rc = AT_RC_OK;
            }
        } while(0);
    }
    return rc;
}

/*!
 * AT+RSSI?
 */
AtResultCode_t AtExtendGetRssi (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;
    int8_t  rssiOffset;

    if ( Radio.GetPib( PIB_RSSI_OFFSET, (uint8_t *)&rssiOffset ) ) {
        AtPrintHeader();
        {
            print_dec( rssiOffset, 4, 0 );
        }
        AtPrintTrailer();

        rc = AT_RC_OK;
    }
    return rc;
}

/*!
 * AT+RSSI
 */
AtResultCode_t AtExtendActRssi (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;
    int8_t          rssi;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    }
    else {
        do {
            if (AppMatchPacketType(PACKET_TYPE_LORA)) {
                break;
            }
            Radio.Standby();
            Radio.SetModem(MODEM_FSK);
            Radio.SetChannel(rfCb->freq);
            AppRadioSetRxConfig(MODEM_FSK);
            Radio.Rx(0xffffff);

            DelayMs(100);

            rssi = Radio.Rssi(MODEM_FSK);

            Radio.Standby();

            AtPrintHeader();
            {
                print_dec( rssi, 4, 0 );
            }
            AtPrintTrailer();

            rc = AT_RC_OK;
        } while(0);
    }
    return rc;
}

/*!
 * AT+LBT=ccaEnable[,rssiThresh,ccaDuration,ccaBandwidth]
 */
AtResultCode_t AtExtendSetLBT (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    }
    else {
        do {
            int16_t     narg;
            int8_t      *cp;
            int16_t     num;
            int32_t     decVal;
            bool        isRssiNegative = false;
            bool        ccaEnable;
            int16_t     rssiThresh;
            uint32_t    scanDuration;
            uint32_t    ccaBandWidth;

            narg = AtParseListLen();
            if ( narg != 1 && narg != 4)
                break;

            // check ccaEnable

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal != 0  && decVal != 1 )
                break;
            ccaEnable = (decVal == 1);

            if ( narg == 1) {
                txCb->ccaEnable = ccaEnable;
                rc = AT_RC_OK;
                break;
            }

            // check rssiThresh

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            if ( *cp == '-' ) {
                cp++;
                isRssiNegative = true;
            }

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if (decVal < 0 || decVal > 127 )
                break;
            rssiThresh = isRssiNegative ? -(int16_t)decVal : (int16_t)decVal;

            // check scanDuration

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal < 1 || decVal > 50)
                break;
            scanDuration = (uint32_t )decVal;

            // check ccaBandWidth

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal < 0 || AT_EXTCMD_LIMIT_GFMOD_BW_HI < decVal )
                break;
            ccaBandWidth = (uint32_t)decVal;

            // update rssiThresh, ccaDuration, ccaBandWidth

            if(Radio.SetPib( PIB_CCA_BANDWIDTH, (uint8_t * )&ccaBandWidth)){
                txCb->ccaEnable = ccaEnable;
                txCb->ccaRssiThresh = rssiThresh;
                txCb->ccaScanDuration = scanDuration;
                rfCb->pib.ccaBandWidth = ccaBandWidth;
                rc = AT_RC_OK;
            }
            break;
        } while(0);
    }
    return rc;
}

/*!
 * AT+LBT?
 */
AtResultCode_t AtExtendGetLBT (void *p)
{
    AtPrintHeader();
    {
        print_dec((txCb->ccaEnable ? 1 : 0), 2, 0 ); putchar(',');
        print_dec( txCb->ccaRssiThresh,      4, 0 ); putchar(',');
        print_dec( txCb->ccaScanDuration,    4, 0 ); putchar(',');
        print_dec( rfCb->pib.ccaBandWidth,  10, 0 );
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+LBT
 */
AtResultCode_t AtExtendActLBT (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;
    uint8_t         detected      = 1;
    int16_t         aRssiSense    = 0;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    } else {
        if (AppMatchPacketType(PACKET_TYPE_GFSK)) {
            AppRadioSetRxConfig( MODEM_FSK );
        } else {
            AppRadioSetRxConfig( MODEM_LORA );
        }

        aRssiSense = Radio.Ed(rfCb->freq, txCb->ccaScanDuration);
        if ( aRssiSense < txCb->ccaRssiThresh ) {
            detected = 0;
        }

        AtPrintHeader();
        {
            print_hex( detected, 1); putchar(',');
            print_dec( aRssiSense, 4, 0 );
        }
        AtPrintTrailer();

        rc = AT_RC_OK;
    }
    return rc;
}

/*!
 * AT+RXGAIN=gainMode(0:PowerSaveGain,1:BoostedGain)
 */
AtResultCode_t AtExtendSetRxGain (void *p)
{
    AtResultCode_t	rc = AT_RC_ERR;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    }
    else {
       do {
            int16_t     narg;
            int8_t      *cp;
            int16_t     num;
            uint32_t    decVal;

            narg = AtParseListLen();
            if ( narg != 1 )
                break;

            AtParsePopOne( &cp, &num );
            if ( cp == NULL || num != 1 )
                break;

            decVal = AtParseGetHexValue( cp, AT_CMD_NUL );
            if ( decVal != APP_RADIO_RXGAIN_POWERSAVING && decVal != APP_RADIO_RXGAIN_BOOSTED )
                break;

            rfCb->pib.gainBoosted = (APP_RADIO_RXGAIN_BOOSTED == decVal);
            Radio.SetPib(PIB_GAIN_BOOSTED, (uint8_t *)&(rfCb->pib.gainBoosted));

            rc = AT_RC_OK;
        } while(0);
    }
    return rc;
}

/*!
 * AT+RXGAIN?
 */
AtResultCode_t AtExtendGetRxGain (void *p)
{
    AtPrintHeader();
    {
        print_dec(((APP_RADIO_RXGAIN_BOOSTED == rfCb->pib.gainBoosted) ? 1 : 0), 2, 0);
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

void AppRXResultPrint ( void );

/*!
 * AT+STOP
 */
AtResultCode_t AtExtendActStop (void * p)
{
    AtResultCode_t  rc = AT_RC_ERR;

    switch (AppGetState(appCb)) {
        case APP_RX_CONFIG:
        case APP_RX:
        case APP_RX_TIMEOUT:
        case APP_RX_ERROR:
        case APP_RX_RUNNING:
            {
                AppSetTranState(appCb, APP_RX_STOP);
                rc = AT_RC_NOANS;
                break;
            }
        case APP_TX_CONFIG:
        case APP_TX:
        case APP_TX_TIMEOUT:
        case APP_TX_RUNNING:
        case APP_TX_WAITING:
        case APP_TXCW:
        case APP_TXCP:
        case APP_TXCX_RUNNING:
            {
                AppSetTranState(appCb, APP_TX_STOP);
                rc = AT_RC_NOANS;
                break;
            }
        case APP_IDLE:
            {
                rc = AT_RC_OK;
                break;
            }
        default:
            {
                break;
            }
    }
    return rc;
}

/*!
 * AT+SLEEP=mode(0:cold,1:warm),period(1..1000[s])
 */
AtResultCode_t AtExtendSetSleep (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    } else {
        do {
            int16_t     narg;
            int8_t      *cp;
            int16_t     num;
            uint8_t     mode;
            uint32_t    period;
            int32_t     decVal;

            narg = AtParseListLen();
            if ( narg != 2 )
                break;

            // mode

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal != 0 && decVal != 1 )
                break;
            mode = (uint8_t)decVal;

            // period

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal  = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal < 0 || decVal > 1000 )
                break;
            period = (uint32_t)decVal;

            // update parameters

            appCb->powerSaveCb.mode   = mode;
            appCb->powerSaveCb.period = period;
            AppSetTranState(appCb, APP_LOWPOWER);

            rc = AT_RC_NOANS;
        } while(0);
    }
    return rc;
}

/*!
 * AT+SLEEP?
 */
AtResultCode_t AtExtendGetSleep (void *p)
{
    AtPrintHeader();
    {
        print_hex( appCb->powerSaveCb.mode, 1 ); putchar(',');
        print_dec( appCb->powerSaveCb.period, 4, 0 );
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+TXTO=txTimeout
 */
AtResultCode_t AtExtendSetTxTo (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    } else {
        do {
            int16_t     narg;
            int8_t      *cp;
            int16_t     num;
            int32_t     decVal;

            narg = AtParseListLen();
            if ( narg != 1 )
                break;

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal < 1 || decVal > AT_EXTCMD_LIMIT_TXTO)
                break;
            rfCb->txTimeout = (uint32_t)decVal;

            rc = AT_RC_OK;
        } while(0);
    }
    return rc;
}

/*!
 * AT+TXTO?
 */
AtResultCode_t AtExtendGetTxTo (void *p)
{
    AtPrintHeader();
    {
        print_dec( rfCb->txTimeout, 10, 0 );
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+RXTO=rxTimeout[,symbolTimeout]
 */
AtResultCode_t AtExtendSetRxTo (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    } else {
        do {
            int16_t     narg;
            int8_t      *cp;
            int16_t     num;
            int32_t     decVal;
            uint32_t    rxTimeout;
            uint16_t    symbTimeout;

            narg = AtParseListLen();
            if ( narg != 1 && narg != 2)
                break;

            // rxTimeout

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal < 1 || decVal > AT_EXTCMD_LIMIT_RXTO )
                break;
            rxTimeout = (uint32_t)decVal;

            if ( narg == 1 ) {
                rfCb->rxTimeout = rxTimeout;
                rc = AT_RC_OK;
                break;
            }

            // symbolTimeout

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if (decVal < 1)
                break;
            if ( AppMatchPacketType(PACKET_TYPE_GFSK) && (decVal > AT_EXTCMD_LIMIT_GFSK_SYMBTO))
                break;
            if ( AppMatchPacketType(PACKET_TYPE_LORA) && (decVal > AT_EXTCMD_LIMIT_LORA_SYMBTO))
                break;
            symbTimeout = (uint16_t)decVal;

            // update parameters

            rfCb->rxTimeout = rxTimeout;

            if (AppMatchPacketType(PACKET_TYPE_GFSK)) {
                rfCb->fsk.symbTimeout = symbTimeout;
            } else {
                rfCb->lora.symbTimeout = symbTimeout;
            }

            rc = AT_RC_OK;
        } while(0);
    }
    return rc;
}

/*
 * AT+RXTO?
 */
AtResultCode_t AtExtendGetRxTo (void *p)
{
    AtPrintHeader();
    {
        print_dec( rfCb->rxTimeout, 10, 0 ); putchar(',');
        print_dec((AppMatchPacketType(PACKET_TYPE_GFSK) ?
            rfCb->fsk.symbTimeout : rfCb->lora.symbTimeout), 6, 0);
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+XTRIM=xtaTrim,xtbTrim
 */
AtResultCode_t AtExtendSetXTrim (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;
    uint8_t         xtaTrim, xtbTrim;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    } else {
        do {
            int16_t     narg;
            int8_t      *cp;
            int16_t     num;
            int16_t     hexVal;

            narg = AtParseListLen();
            if ( narg != 2)
                break;

            /* XTA */
            AtParsePopOne( &cp, &num );
            if ( cp == NULL || num < 1 )
                break;

            hexVal = AtParseGetHexValue( cp, AT_CMD_NUL );
            if ( hexVal < 0 || hexVal > 0x2F )
                break;
            xtaTrim = (uint8_t )(hexVal & 0x00ff);

            /* XTB */
             AtParsePopOne( &cp, &num );
             if ( cp == NULL || num < 1 )
                 break;

            hexVal = AtParseGetHexValue( cp, AT_CMD_NUL );
            if ( hexVal < 0 || hexVal > 0x2F )
                break;
             xtbTrim = (uint8_t )(hexVal & 0x00ff);

            /* update parameters */

            rfCb->pib.xtalXtaTrim = xtaTrim;
            rfCb->pib.xtalXtbTrim = xtbTrim;
            Radio.SetPib(PIB_XTAL_XTA_TRIM, (uint8_t *)&(rfCb->pib.xtalXtaTrim));
            Radio.SetPib(PIB_XTAL_XTB_TRIM, (uint8_t *)&(rfCb->pib.xtalXtbTrim));

            rc = AT_RC_OK;
        } while(0);
    }
    return rc;
}

/*
 * AT+XTRIM?
 */
AtResultCode_t AtExtendGetXTrim (void *p)
{
    AtPrintHeader();
    {
        print_hex( rfCb->pib.xtalXtaTrim, 2 ); putchar(',');
        print_hex( rfCb->pib.xtalXtbTrim, 2 );
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+PKT=pktType,pktLen|pktData
 */
AtResultCode_t AtExtendSetPkt (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    } else {
        do {
            int16_t     i, narg;
            int8_t      *cp;
            int16_t     num;
            int32_t     decVal;
            uint8_t     pktType;
            uint8_t     payloadLen;

            narg = AtParseListLen();
            if ( narg != 2 )
                break;

            /* pktType */

            AtParsePopOne( &cp, &num );
            if ( cp == NULL || num < 1 )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( APP_FRAMETYPE_PER != decVal
                 && APP_FRAMETYPE_PN9 != decVal
                 && APP_FRAMETYPE_ANY != decVal
                 && APP_FRAMETYPE_EUI != decVal
            #if defined(RM_HS300X_H)
                 && APP_FRAMETYPE_EXT != decVal
            #endif
                 )
                break;
            pktType = (uint8_t)decVal;

            /* pktLen or pktData */

            AtParsePopOne( &cp, &num );
            if ( cp == NULL || num < 1 || (num > (255 * 2)))
                break;

            if (APP_FRAMETYPE_PER == pktType ||
                APP_FRAMETYPE_PN9 == pktType ||
                APP_FRAMETYPE_EUI == pktType ||
                APP_FRAMETYPE_EXT == pktType) {

                decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
                if ( decVal < 0 || decVal > 255 )
                    break;
                payloadLen = (uint8_t)decVal;

                rfCb->payloadLen = payloadLen;
                appCb->appPacketDataType = pktType;
                txCb->txSeqNum = 1;
                AppSetupPacketData( pktType );

                rc = AT_RC_OK;
                break;
            } else {
                /* APP_FRAMETYPE_ANY */

                if (((num % 2) == 1) || (AtParseCountHexString( cp, AT_CMD_NUL) < 0))
                    break;

                AppClearBuffer( &(appCb->anyPktCb.bCb), &(appCb->anyPktCb.pktData[0]), APP_PACKETDATA_ARRAYSIZE );

                for ( i=0, appCb->anyPktCb.bCb.pos=0; *cp != AT_CMD_NUL; i++, appCb->anyPktCb.bCb.pos++ ) {
                    uint8_t temp = AtParseGetNibble(*cp++) * 16;
                    appCb->anyPktCb.pktData[i] = temp + AtParseGetNibble(*cp++);
                }

                rfCb->payloadLen = (uint8_t)(i & 0x00ff);
                pktData->txPktCb.bCb.options = pktType;
                appCb->appPacketDataType = pktType;

                rc = AT_RC_OK;
            }
        } while(0);
    }
    return rc;
}

/*!
 * AT+PKT?
 */
AtResultCode_t AtExtendGetPkt (void *p)
{
    uint8_t     pktType = appCb->appPacketDataType;
    uint8_t     pktLen = rfCb->payloadLen;
    int8_t      *cp;

    AtPrintHeader();
    {
        uint16_t    i;

        print_dec( pktType, 2, 0 ); putchar(',');
        print_dec( pktLen, 4, 0 ); putchar(',');

        if ( APP_FRAMETYPE_ANY == pktType ) {
            cp   = &(appCb->anyPktCb.pktData[0]);
        } else {
            cp   = &(pktData->txPktCb.pktData[0]);
        }

        for ( i=0; i<pktLen; i++ ) {
            print_hex( (uint8_t) *(cp+i), 2 );
        }
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+FMCFG=dataRate,bandWidth,fDev
 */
AtResultCode_t AtExtendSetFMCfg (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    } else {
        do {
            int16_t     narg;
            int8_t      *cp;
            int16_t     num;
            int32_t     decVal;
            uint32_t    dataRate;
            uint32_t    bandWidth;
            uint32_t    fDev;

            narg = AtParseListLen();
            if ( narg != 3 )
                break;

            /* dataRate */

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal < AT_EXTCMD_LIMIT_GFMOD_BR_LO || decVal > AT_EXTCMD_LIMIT_GFMOD_BR_HI )
                break;
            dataRate = (uint32_t)decVal;

            /* bandWidth */

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal < AT_EXTCMD_LIMIT_GFMOD_BW_LO || decVal > AT_EXTCMD_LIMIT_GFMOD_BW_HI )
                break;
            bandWidth = (uint32_t)decVal;

            /* fDev */

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal < 1 )
                break;
            fDev = (uint32_t)decVal;

            /* Update paramters */

            rfCb->fsk.dataRate = dataRate;
            rfCb->fsk.bandWidth = bandWidth;
            rfCb->fsk.fDev = fDev;

            rc = AT_RC_OK;
        } while(0);
    }
    return rc;
}

/*!
 * AT+FMCFG?
 */
AtResultCode_t AtExtendGetFMCfg (void *p)
{
    AtPrintHeader();
    {
        print_dec( rfCb->fsk.dataRate,   10, 0 ); putchar(',');
        print_dec( rfCb->fsk.bandWidth,  10, 0 ); putchar(',');
        print_dec( rfCb->fsk.fDev,       10, 0 ); }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+FPCFG=preambleLen,fixedLen,crcOn
 */
AtResultCode_t AtExtendSetFPCfg (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    } else {
        do {
            int16_t     narg;
            int8_t      *cp;
            int16_t     num;
            int32_t     decVal;
            uint16_t    preambleLen;
            bool        fixLen;
            bool        crcOn;

            narg = AtParseListLen();
            if ( narg != 3 )
                break;

            /* preambleLen */

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal < 1 || decVal > AT_EXTCMD_LIMIT_GFPKT_PRELEN)
                break;
            preambleLen = (uint16_t )(decVal & 0x00001fff);

            /* fixLen */

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal != 1/*Fixed*/ && decVal != 0/*Variable*/ )
                break;
            fixLen = (decVal == 1/*Fixed*/);

            /* crcOn */

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal != 1/*ON*/ && decVal != 0/*OFF*/ )
                break;
            crcOn = (decVal == 1/*ON*/);

            /* Update parameters */

            rfCb->fsk.preambleLen = preambleLen;
            rfCb->fsk.fixLen = fixLen;
            rfCb->fsk.crcOn = crcOn;

            rc = AT_RC_OK;
        } while(0);
    }

    return rc;
}

/*!
 * AT+FPCFG?
 */
AtResultCode_t AtExtendGetFPCfg (void *p)
{
    AtPrintHeader();
    {
        print_dec( rfCb->fsk.preambleLen,       7, 0 ); putchar(',');
        print_dec( (rfCb->fsk.fixLen ? 1 : 0),  2, 0 ); putchar(',');
        print_dec( (rfCb->fsk.crcOn ? 1 : 0),   2, 0 );
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+LMCFG=spreadFactor,bandWidth,codeRate
 */
AtResultCode_t AtExtendSetLMCfg (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    } else {
        do {
            int16_t     narg;
            int8_t      *cp;
            int16_t     num;
            int32_t     decVal;
            uint8_t     spreadFactor;
            uint8_t     bandWidth;
            uint8_t     codeRate;

            narg = AtParseListLen();
            if ( narg != 3)
                break;

            /* spreadFactor */

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal < AT_EXTCMD_LIMIT_LRMOD_SF_LO || decVal > AT_EXTCMD_LIMIT_LRMOD_SF_HI )
                break;
            spreadFactor = (uint8_t )decVal;

            /* bacnWidth */

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal < 0 || decVal > 9 )
                break;
            bandWidth = (uint8_t )decVal;

            /* codeRate */

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal < 1 || decVal > 4 )
                break;
            codeRate = (uint8_t )decVal;

            /* Update parameters */

            rfCb->lora.spreadFactor = spreadFactor;
            rfCb->lora.bandWidth = bandWidth;
            rfCb->lora.codeRate = codeRate;

            rc = AT_RC_OK;
        } while(0);
    }
    return rc;
}

/*!
 * AT+LMCFG?
 */
AtResultCode_t AtExtendGetLMCfg (void *p)
{
    AtPrintHeader();
    {
        print_dec( rfCb->lora.spreadFactor, 2, 0 ); putchar(',');
        print_dec( rfCb->lora.bandWidth,    2, 0 ); putchar(',');
        print_dec( rfCb->lora.codeRate,     2, 0 );
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+LPCFG=preambleLen,fixLen,crcOn,iqInverted,publicNetowrk
 */
AtResultCode_t AtExtendSetLPCfg (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    } else {
        do {
            int16_t     narg;
            int8_t      *cp;
            int16_t     num;
            int32_t     decVal;
            uint16_t    preambleLen;
            bool        fixLen;
            bool        crcOn;
            bool        iqInverted;
            bool        publicNetwork;

            narg = AtParseListLen();
            if ( narg != 5 )
                break;

            /* preambleLen */

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal < 1 || decVal > AT_EXTCMD_LIMIT_LRPKT_PRELEN )
                break;
            preambleLen = (uint16_t )decVal;

            /* fixLen */

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal != 1/*Fixed*/ && decVal != 0/*Variable*/ )
                break;
            fixLen = (decVal == 1/*Fixed*/);

            /* crcOn */

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal != 1/*ON*/ && decVal != 0/*OFF*/)
                break;
            crcOn = (decVal == 1/*ON*/);

            /* iqInverted */

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal != 1/*INV*/ && decVal != 0/*STD*/)
                break;
            iqInverted = (decVal == 1/*INV*/);

            /* publicNetwork */

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal != 0/*Private*/ && decVal != 1/*Public*/)
                break;
            publicNetwork = (decVal == 1/*Pubclic*/);

            /* Update parameters */

            rfCb->lora.preambleLen = preambleLen;
            rfCb->lora.fixLen = fixLen;
            rfCb->lora.crcOn = crcOn;
            rfCb->lora.iqInverted = iqInverted;
            rfCb->lora.publicNetwork = publicNetwork;
            Radio.SetPublicNetwork(publicNetwork);

            rc = AT_RC_OK;
        } while(0);
    }
    return rc;
}

/*!
 * AT+LPCFG?
 */
AtResultCode_t AtExtendGetLPCfg (void *p)
{
    AtPrintHeader();
    {
        print_dec( rfCb->lora.preambleLen,             6, 0 ); putchar(',');
        print_dec( (rfCb->lora.fixLen ? 1 : 0),        2, 0 ); putchar(',');
        print_dec( (rfCb->lora.crcOn ? 1 : 0),         2, 0 ); putchar(',');
        print_dec( (rfCb->lora.iqInverted ? 1 : 0),    2, 0 ); putchar(',');
        print_dec( (rfCb->lora.publicNetwork ? 1 : 0), 2, 0 );
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+DEVEUI=
 */
AtResultCode_t AtExtendSetDevEui (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;
    int16_t         narg, i;
    uint8_t         v;
    int8_t          *cp;
    int16_t         num;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    }
    else {
        do {
            narg = AtParseListLen();
            if ( narg < 1 )
                break;

            AtParsePopOne( &cp, &num );
            if ( cp == NULL || num != (8<<1) )
                break;

            for ( i=0; i<8; i++ )
            {
                v = (uint8_t )(AtParseGetNibble(*cp++) & 0x0f);
                v = (uint8_t )((v << 4) | ((uint8_t )(AtParseGetNibble(*cp++) & 0x0f)));
                appCb->nodeInfo.devEui[i] = v;
            }
            rc = AT_RC_OK;
        } while(0);
    }
    return rc;
}

/*!
 * AT+DEVEUI?
 */
AtResultCode_t AtExtendGetDevEui (void *p)
{
    AtPrintHeader();
    {
        uint8_t i;
        for ( i=0; i<8; i++ ) {
            print_hex( appCb->nodeInfo.devEui[i], 2 );
        }
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+SAFE=enable
 */
AtResultCode_t AtExtendSetSafe (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;
    int16_t         narg;
    int8_t          *cp;
    int16_t         num;
    int32_t         decVal;
    bool            isEnabled;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    }
    else {
        do {
            narg = AtParseListLen();
            if ( narg != 1 )
                break;

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            decVal = AtParseGetDecValue( cp, AT_CMD_NUL );
            if ( decVal != 0/*Disable*/ && decVal != 1/*Enable*/)
                break;
            isEnabled = (decVal == 1/*Enable*/);

            if ( Radio.SetPib( PIB_RADIO_CFG_CHECK_ENABLE, (uint8_t * )&isEnabled ) ){
                rfCb->pib.radioCfgCheckEnable = isEnabled;
                rc = AT_RC_OK;
            }
        } while(0);
    }
    return rc;
}

/*!
 * AT+SAFE?
 */
AtResultCode_t AtExtendGetSafe (void *p)
{
    AtPrintHeader();
    {
        print_dec( (rfCb->pib.radioCfgCheckEnable ? 1 : 0), 2, 0 );
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+REGION=region_num(dec)
 */
AtResultCode_t AtExtendSetRegion (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;
    int16_t         narg;
    int8_t          *cp;
    int16_t         num;
    int8_t          region_num;
    RadioConfigRegion_t radio_cfg_region;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    }
    else {
        do {
            narg = AtParseListLen();
            if ( narg != 1 )
                break;

            AtParsePopOne( &cp, &num );
            if ( cp == NULL )
                break;

            region_num = (uint8_t)AtParseGetDecValue( cp, AT_CMD_NUL );

            rc = AT_RC_OK;
            switch( region_num ) {
#if defined(RADIO_CFG_EU_ENABLED)
                case 0:
                    radio_cfg_region = RADIO_CFG_EU;
                    break;
#endif
#if defined(RADIO_CFG_US_ENABLED)
                case 1:
                    radio_cfg_region = RADIO_CFG_US;
                    break;
#endif
#if defined(RADIO_CFG_AU_ENABLED)
                case 2:
                    radio_cfg_region = RADIO_CFG_AU;
                    break;
#endif
#if defined(RADIO_CFG_KR_ENABLED)
                case 7:
                    radio_cfg_region = RADIO_CFG_KR;
                    break;
#endif
#if defined(RADIO_CFG_IN_ENABLED)
                case 8:
                    radio_cfg_region = RADIO_CFG_IN;
                    break;
#endif
#if defined(RADIO_CFG_AS1_ENABLED)
                case 6:
                    radio_cfg_region = RADIO_CFG_AS1;
                    break;
#endif
#if defined(RADIO_CFG_AS2_ENABLED)
                case 22:
                    radio_cfg_region = RADIO_CFG_AS2;
                    break;
#endif
#if defined(RADIO_CFG_AS3_ENABLED)
                case 23:
                    radio_cfg_region = RADIO_CFG_AS3;
                    break;
#endif
#if defined(RADIO_CFG_AS4_ENABLED)
                case 24:
                    radio_cfg_region = RADIO_CFG_AS4;
                    break;
#endif
#if defined(RADIO_CFG_JP_ENABLED)
                case 30:
                    radio_cfg_region = RADIO_CFG_JP;
                    break;
#endif
#if defined(RADIO_CFG_JP_LDC_ENABLED)
                case 31:
                    radio_cfg_region = RADIO_CFG_JP_LDC;
                    break;
#endif
                default:
                    rc = AT_RC_ERR;
                    break;
            }

            if (rc == AT_RC_OK) {
                if ( Radio.SetPib( PIB_RADIO_CFG_REGION, (uint8_t * )&radio_cfg_region ) ) {
                    rfCb->pib.region = region_num;
                }
                else {
                    rc = AT_RC_ERR;
                }
            }
        } while(0);
    }
    return rc;
}

/*!
 * AT+REGION?
 */
AtResultCode_t AtExtendGetRegion (void *p)
{
    AtPrintHeader();
    {
        print_dec( rfCb->pib.region, 2, 0 );
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+TOA
 */
AtResultCode_t AtExtendActTOA (void *p)
{
    if (APP_IDLE != AppGetState(appCb)) {
        return AT_RC_BUSY;
    }

    uint32_t toa;
    if (AppMatchPacketType(PACKET_TYPE_GFSK)) {
        AppRadioSetTxConfig(MODEM_FSK);
        toa = Radio.TimeOnAir(MODEM_FSK, rfCb->payloadLen);
    }else{
        AppRadioSetTxConfig(MODEM_LORA);
        toa = Radio.TimeOnAir(MODEM_LORA, rfCb->payloadLen);
    }

    AtPrintHeader();
    {
        print_dec(toa, 10, 0);
    }
    AtPrintTrailer();

    return AT_RC_OK;
}

/*!
 * AT+HELP
 */
AtResultCode_t AtExtendActHelp (void *p)
{
    print("AT+STAT      : show application state\r\n");
    print("AT+RESET     : reset and load settings from data flash\r\n");
    print("AT+MODEM     : set modem type (0:FSK,1:LoRa)\r\n");
    print("AT+FREQ      : set center frequency\r\n");
    print("AT+LMCFG     : set LoRa modulation parameters\r\n");
    print("AT+LPCFG     : set LoRa packet parameters\r\n");
    print("AT+FMCFG     : set FSK modulation parameters\r\n");
    print("AT+FPCFG     : set FSK packet parameters\r\n");
    print("AT+RECV      : receive packets\r\n");
    print("AT+RXTO      : set Rx timeout parameters\r\n");
    print("AT+RXGAIN    : set Rx gain mode\r\n");
    print("AT+SEND      : send packets\r\n");
    print("AT+PKT       : set packet data\r\n");
    print("AT+TXPWR     : set Tx power\r\n");
    print("AT+TXTO      : set Tx timeout\r\n");
    print("AT+STOP      : stop Rx/Tx processing\r\n");
    print("AT+TXCW      : send unmodulated continuous wave\r\n");
    print("AT+TXCP      : send modulated continuous wave\r\n");
    print("AT+LBT       : set LBT (Listen Before Talk)\r\n");
    print("AT+RSSI      : get RSSI, set RSSI offset \r\n");
    print("AT+XTRIM     : set XTAL trim to adjust center frequency\r\n");
    print("AT+SAVE      : save all settings to data flash memory\r\n");
    print("AT+ERASE     : initialize data flash with system default\r\n");
    print("AT+DEVEUI    : set Device EUI (MAC Address)\r\n");
    print("AT+REGR      : read RF device registers\r\n");
    print("AT+REGW      : write RF device registers\r\n");
    print("AT+SLEEP     : set sleep mode\r\n");
    print("AT+SAFE      : set safe mode to support radio regulation\r\n");
    print("AT+REGION    : set region used for safe mode\r\n");
    print("AT+TOA       : show time on air\r\n");
    print("AT+VER       : show version\r\n");
    print("AT+HELP      : show all AT commands\r\n");
    print("AT+LIST      : show current parameter settings\r\n");
    print("\r\n");
    print("AT+LMCFG=SF(5-12),BW(0-9),CR(1-4)\r\n");
    print("  BW=0:125,1:250,2:500,3:62,4:41,5:31,6:20,7:15,8:10,9:7[kHz]\r\n");
    print("  CR=1:4/5,2:4/6,3:4/7,4:4/8\r\n");
    print("\r\n");
    print("AT+FMCFG=DR(600-300000[bps]),BW(1-467000[Hz]),Fdev(1-65535[Hz]),\r\n");
    print("\r\n");
    print("AT+LPCFG=preambleLen,fixedLen,crcOn,iqInverted,publicNetwork\r\n");
    print("  preambleLen(1-65535)[symbol]\r\n");
    print("  fixedLen(0:variable/explicit header,1:fixed/implicit header)\r\n");
    print("  crcOn(0:CRC OFF,1:CRC ON)\r\n");
    print("  iqInverted(0:Standard IQ,1:Inverted IQ)\r\n");
    print("  publicNetwork(0:Private SyncWord,1:Public(LoRaWAN) SyncWord)\r\n");
    print("\r\n");
    print("AT+FPCFG=preambleLen,fixedLen,crcOn\r\n");
    print("  preambleLen(1-8191)[byte]\r\n");
    print("  fixedLen(0:variable/explicit header,1:fixed/implicit header)\r\n");
    print("  crcOn(0:CRC OFF,1:CRC ON)\r\n");

    return AT_RC_OK;
}

/*!
 * AT+LIST
 */
AtResultCode_t AtExtendActList (void *p)
{
    print("\r\n[ COMMON ]---------------------------\r\n");

    print("Frequency        : ");
    print_dec(rfCb->freq, 10, 0);
    print("[Hz]\r\n");

    print("Modulation       : ");
    if (AppMatchPacketType(PACKET_TYPE_GFSK)) {
        print("GFSK\r\n");
    } else {
        print("LoRa\r\n");
    }

    print("RxGainMode       : ");
    if (rfCb->pib.gainBoosted){
        print("BoostedGain\r\n");
    }else{
        print("PowerSavingGain\r\n");
    }

    print("TxPower          : ");
    print_dec(rfCb->txPower, 10, 0);
    print("[dBm]\r\n");

    print("PayloadLen       : ");
    print_dec(rfCb->payloadLen, 6, 0);
    print("[bytes]\r\n");

    print("LBT              : ");
    if (txCb->ccaEnable){
        print("Enabled\r\n");
    }else{
        print("Disabled\r\n");
    }

    print("SafeMode         : ");
    if (rfCb->pib.radioCfgCheckEnable) {
        print("Enabled\r\n");
    }else{
        print("Disabled\r\n");
    }

    print("Region used for SafeMode: ");
    switch(rfCb->pib.region)
    {
        case RADIO_CFG_EU: print("EU"); break;
        case RADIO_CFG_IN: print("IN"); break;
        case RADIO_CFG_AS1:print("AS1"); break;
        case RADIO_CFG_AS2:print("AS2"); break;
        case RADIO_CFG_AS3:print("AS3"); break;
        case RADIO_CFG_AS4:print("AS4"); break;
        case RADIO_CFG_US: print("US"); break;
        case RADIO_CFG_AU: print("AU"); break;
        case RADIO_CFG_KR: print("KR"); break;
        case RADIO_CFG_JP: print("JP"); break;
        case RADIO_CFG_JP_LDC:print("JP_LDC"); break;
        default:print("Unknown"); break;
    }

    print("\r\n[ LORA ]-----------------------------\r\n");

    print("SpreadFactor     : SF");
    print_dec(rfCb->lora.spreadFactor, 2, 0);
    print("\r\n");

    print("BandWidth        : ");
    switch(rfCb->lora.bandWidth)
    {
        case 0:print("125"); break;
        case 1:print("250"); break;
        case 2:print("500"); break;
        case 3:print("62"); break;
        case 4:print("41"); break;
        case 5:print("31"); break;
        case 6:print("20"); break;
        case 7:print("15"); break;
        case 8:print("10"); break;
        case 9:print("7"); break;
        default:print("Unknown"); break;
    }
    print("[kHz]\r\n");

    print("CodingRate       : ");
    switch(rfCb->lora.codeRate)
    {
        case 1:print("4/5"); break;
        case 2:print("4/6"); break;
        case 3:print("4/7"); break;
        case 4:print("4/8"); break;
        default:print("Unknown"); break;
    }
    print("\r\n");

    print("PreambleLen      : ");
    print_dec(rfCb->lora.preambleLen, 6, 0);
    print("[symbols]\r\n");

    print("HeaderType       : ");
    if (rfCb->lora.fixLen){
        print("Fixed(Implicit)\r\n");
    }else{
        print("Variable(Explicit)\r\n");
    }

    print("CrcType          : ");
    if (rfCb->lora.crcOn){
        print("CRC ON\r\n");
    }else{
        print("CRC OFF\r\n");
    }

    print("InvertIQ         : ");
    if (rfCb->lora.iqInverted){
        print("Inverted IQ\r\n");
    }else{
        print("Standard IQ\r\n");
    }

    print("SyncWord         : ");
    if (rfCb->lora.publicNetwork){
        print("Public(LoRaWAN)\r\n");
    }else{
        print("Private\r\n");
    }

    print("\r\n[ GFSK ]-----------------------------\r\n");

    print("DataRate         : ");
    print_dec(rfCb->fsk.dataRate, 10, 0);
    print("[bps]\r\n");

    print("BandWidth        : ");
    print_dec(rfCb->fsk.bandWidth, 10, 0);
    print("[Hz]\r\n");

    print("FreqDeviation    : ");
    print_dec(rfCb->fsk.fDev, 10, 0);
    print("[Hz]\r\n");

    print("PreambleLen      : ");
    print_dec(rfCb->fsk.preambleLen, 7, 0);
    print("[bytes]\r\n");

    print("HeaderType       : ");
    if (rfCb->fsk.fixLen){
        print("Fixed(Implicit)\r\n");
    }else{
        print("Variable(Explicit)\r\n");
    }

    print("CrcType          : ");
    if (rfCb->fsk.crcOn){
        print("CRC ON\r\n");
    }else{
        print("CRC OFF\r\n");
    }

    print("\r\n[ BOARD ]----------------------------\r\n");

    print("Device           : ");
    if (RADIO_LOPOWER_SEL == SX126xGetPaSelect()){
        print("SX1261\r\n");
    }else{
        print("SX1262\r\n");
    }

    print("Clock            : ");
    if (RADIO_CLOCK_XTAL_SEL == SX126xGetClockSelect()){
        print("XTAL\r\n");
    }else{
        print("TCXO\r\n");
    }

    return AT_RC_OK;
}

/*!
 ********************************************************************************
  Unofficial AT-Commands
 ********************************************************************************
 */

/*!
 * AT+SA=freqStart,freqStop,freqStep,scanTimes(1-65535),scanDelay(0-100ms)
 */
AtResultCode_t AtExtendSetSA (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    }
    else {
        do {
            int16_t  narg;
            int8_t   *cp;
            int16_t  num;
            uint32_t decVal;
            uint32_t start, stop, step;
            uint16_t nb;
            uint16_t times;
            uint8_t  delay;

            narg = AtParseListLen();
            if (narg != 5)
                break;

            /* freqStart */
            {
                AtParsePopOne(&cp, &num);
                if (cp == NULL)
                    break;
                decVal = (uint32_t)AtParseGetDecValue(cp, AT_CMD_NUL);
                if (decVal < APP_RADIO_FREQ_LO || decVal > APP_RADIO_FREQ_HI)
                    break;
                start = decVal;
            }

            /* freqStop */
            {
                AtParsePopOne(&cp,&num);
                if (cp == NULL)
                    break;
                decVal = (uint32_t)AtParseGetDecValue(cp, AT_CMD_NUL);
                if (decVal < APP_RADIO_FREQ_LO || decVal > APP_RADIO_FREQ_HI)
                    break;
                if (decVal < start)
                    break;
                stop = (uint32_t)decVal;
            }

            /* freqStep */
            {
                AtParsePopOne(&cp, &num);
                if (cp == NULL)
                    break;
                decVal = (uint32_t)AtParseGetDecValue(cp, AT_CMD_NUL);
                if (decVal < 1000UL || decVal >1000000UL)
                    break;
                if ((stop - start) == 0) {
                    ;
                }
                else if ((stop - start) < decVal) {
                    break;
                }
                step = (uint32_t)decVal;
            }
            {
                nb = (uint8_t )((stop - start) == 0) ? 1 : (((stop - start) / step) + 1);
                if (nb > 200)
                    break;
            }

            /* scanTimes */
            {
                AtParsePopOne(&cp, &num);
                if (cp == NULL)
                    break;
                decVal = AtParseGetDecValue(cp, AT_CMD_NUL);
                if (decVal < 1 || decVal > 0xffff)
                    break;
                times = (uint16_t )decVal;
            }

            /* scanDelay */
            {
                AtParsePopOne(&cp, &num);
                if (cp == NULL)
                    break;
                decVal = (uint32_t)AtParseGetDecValue(cp, AT_CMD_NUL);
                if (decVal > 100)
                    break;
                delay = (uint8_t )decVal;
            }

            rfCb->sa.freqStart  = start;
            rfCb->sa.freqStop   = stop;
            rfCb->sa.freqOffset = step;
            rfCb->sa.freqNb     = (uint8_t )nb;
            rfCb->sa.scanTimes  = times;
            rfCb->sa.delayMs    = delay;

            rc = AT_RC_OK;
        } while(1);
    }
    return rc;
}

/*!
 * AT+SA?
 */
AtResultCode_t AtExtendGetSA (void *p)
{
    AtPrintHeader();
    {
        print_dec(rfCb->sa.freqStart, 10, 0);  putchar(',');
        print_dec(rfCb->sa.freqStop,  10, 0);  putchar(',');
        print_dec(rfCb->sa.freqOffset,10, 0);  putchar(',');
        print_dec(rfCb->sa.scanTimes,  3, 0);  putchar(',');
        print_dec(rfCb->sa.delayMs,    3, 0);
    }
    AtPrintTrailer();

    return (AT_RC_OK);
}

/*!
 * AT+SA
 */
AtResultCode_t AtExtendActSA (void *p)
{
    AtResultCode_t  rc = AT_RC_ERR;
    int8_t          aRssi[201];
    int16_t         i, n;

    if (APP_IDLE != AppGetState(appCb)) {
        rc = AT_RC_BUSY;
    } else if (AppMatchPacketType(PACKET_TYPE_GFSK)) {
        Radio.Standby();
        Radio.SetModem(MODEM_FSK);
        Radio.SetChannel(rfCb->sa.freqStart);
        AppRadioSetRxConfig(MODEM_FSK);

        for (n=0; n < rfCb->sa.scanTimes; n++) {

            for (i=0; i < rfCb->sa.freqNb; i++) {
                Radio.SetChannel((rfCb->sa.freqStart + (rfCb->sa.freqOffset * i)));
                Radio.Rx(0xffffff);

                DelayMs(1);

                aRssi[i] = Radio.Rssi(MODEM_FSK);
            }

            Radio.Standby();

            AtPrintHeader();
            {
                for (i=0; i < rfCb->sa.freqNb; i++)
                {
                    putchar(((aRssi[i] & 0x7F) >> 2) + 0x30);
                }
            }
            AtPrintTrailer();

            DelayMs(rfCb->sa.delayMs);
        }
        rc = AT_RC_OK;
    }

    Radio.Standby();

    return rc;
}
