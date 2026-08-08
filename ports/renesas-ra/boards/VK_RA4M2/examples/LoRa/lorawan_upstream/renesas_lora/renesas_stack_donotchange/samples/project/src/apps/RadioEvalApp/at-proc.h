/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/
#ifndef AT_PROC_H
#define AT_PROC_H

#include "board.h"
#include "radio-eval.h"
#include "at-command.h"

#define AT_VERBOSEMODE_DBUG_MASK        (0x80)              /* Display Option; RX RSSI/SNR      */
#define AT_VERBOSEMODE_EVAL_RC_MASK     (0x08)              /* Display Option; +STOP Caption    */
#define AT_VERBOSEMODE_EVAL_RX_MASK     (0x04)              /* Display Option; RX Payload       */

/*!
 */
#define AT_EXTCMD_DELIM                 (',')
#define AT_EXTCMD_LIMIT_TXINTVL         (3600000L)
#define AT_EXTCMD_LIMIT_LORA_SYMBTO     (255)
#define AT_EXTCMD_LIMIT_GFSK_SYMBTO     (65535)
#define AT_EXTCMD_LIMIT_RXTO            (65535)
#define AT_EXTCMD_LIMIT_TXTO            (65535)
#define AT_EXTCMD_LIMIT_TXPKTNUM        (400000000UL)
#define AT_EXTCMD_LIMIT_GFMOD_BR_LO     (600)
#define AT_EXTCMD_LIMIT_GFMOD_BR_HI     (300000L)
#define AT_EXTCMD_LIMIT_GFMOD_BW_LO     (1)
#define AT_EXTCMD_LIMIT_GFMOD_BW_HI     (467000L)
#define AT_EXTCMD_LIMIT_LRMOD_SF_LO     (5)
#define AT_EXTCMD_LIMIT_LRMOD_SF_HI     (12)
#define AT_EXTCMD_LIMIT_GFPKT_PRELEN    (8191)
#define AT_EXTCMD_LIMIT_LRPKT_PRELEN    (65535)
#define AT_EXTCMD_LIMIT_RSSIOFFSET      (20)

/*!
 */
void AtProcInit (void);
int8_t AtProcReg (void);
void AtProcParser (void);

/*!
 */
AtResultCode_t  AtExtendActStat             ( void * );
AtResultCode_t  AtExtendActReset            ( void * );
AtResultCode_t  AtExtendSetModem            ( void * );
AtResultCode_t  AtExtendGetModem            ( void * );
AtResultCode_t  AtExtendSetFreq             ( void * );
AtResultCode_t  AtExtendGetFreq             ( void * );
AtResultCode_t  AtExtendSetTxPwr            ( void * );
AtResultCode_t  AtExtendGetTxPwr            ( void * );
AtResultCode_t  AtExtendSetSend             ( void * );
AtResultCode_t  AtExtendGetSend             ( void * );
AtResultCode_t  AtExtendActSend             ( void * );
AtResultCode_t  AtExtendSetRecv             ( void * );
AtResultCode_t  AtExtendGetRecv             ( void * );
AtResultCode_t  AtExtendActRecv             ( void * );
AtResultCode_t  AtExtendActTxCW             ( void * );
AtResultCode_t  AtExtendActTxCP             ( void * );
AtResultCode_t  AtExtendActStop             ( void * );
AtResultCode_t  AtExtendSetLMCfg            ( void * );
AtResultCode_t  AtExtendGetLMCfg            ( void * );
AtResultCode_t  AtExtendSetLPCfg            ( void * );
AtResultCode_t  AtExtendGetLPCfg            ( void * );
AtResultCode_t  AtExtendSetFMCfg            ( void * );
AtResultCode_t  AtExtendGetFMCfg            ( void * );
AtResultCode_t  AtExtendSetFPCfg            ( void * );
AtResultCode_t  AtExtendGetFPCfg            ( void * );
AtResultCode_t  AtExtendSetPkt              ( void * );
AtResultCode_t  AtExtendGetPkt              ( void * );
AtResultCode_t  AtExtendSetTxTo             ( void * );
AtResultCode_t  AtExtendGetTxTo             ( void * );
AtResultCode_t  AtExtendSetRxTo             ( void * );
AtResultCode_t  AtExtendGetRxTo             ( void * );
AtResultCode_t  AtExtendSetRxGain           ( void * );
AtResultCode_t  AtExtendGetRxGain           ( void * );
AtResultCode_t  AtExtendSetRssi             ( void * );
AtResultCode_t  AtExtendGetRssi             ( void * );
AtResultCode_t  AtExtendActRssi             ( void * );
AtResultCode_t  AtExtendSetLBT              ( void * );
AtResultCode_t  AtExtendGetLBT              ( void * );
AtResultCode_t  AtExtendActLBT              ( void * );
AtResultCode_t  AtExtendSetXTrim            ( void * );
AtResultCode_t  AtExtendGetXTrim            ( void * );
AtResultCode_t  AtExtendActSave             ( void * );
AtResultCode_t  AtExtendActErase            ( void * );
AtResultCode_t  AtExtendSetRegW             ( void * );
AtResultCode_t  AtExtendSetRegR             ( void * );
AtResultCode_t  AtExtendSetDevEui           ( void * );
AtResultCode_t  AtExtendGetDevEui           ( void * );
AtResultCode_t  AtExtendSetSleep            ( void * );
AtResultCode_t  AtExtendGetSleep            ( void * );
AtResultCode_t  AtExtendSetSafe             ( void * );
AtResultCode_t  AtExtendGetSafe             ( void * );
AtResultCode_t  AtExtendSetRegion           ( void * );
AtResultCode_t  AtExtendGetRegion           ( void * );
AtResultCode_t  AtExtendActHelp             ( void * );
AtResultCode_t  AtExtendActList             ( void * );
AtResultCode_t  AtExtendGetVer              ( void * );

AtResultCode_t  AtExtendActTOA              ( void * );
AtResultCode_t  AtExtendSetSA               ( void * );
AtResultCode_t  AtExtendGetSA               ( void * );
AtResultCode_t  AtExtendActSA               ( void * );

#endif/*AT_PROC_H*/
