/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
    (C)2013 Semtech
 ___ _____ _   ___ _  _____ ___  ___  ___ ___
/ __|_   _/_\ / __| |/ / __/ _ \| _ \/ __| __|
\__ \ | |/ _ \ (__| ' <| _| (_) |   / (__| _|
|___/ |_/_/ \_\___|_|\_\_| \___/|_|_\\___|___|
embedded.connectivity.solutions===============

Description: LoRa MAC layer message parser functionality implementation

License: Revised BSD License, see LICENSE.TXT file include in the project

Maintainer: Miguel Luis ( Semtech ), Gregory Cristian ( Semtech ),
            Daniel Jaeckle ( STACKFORCE ),  Johannes Bruder ( STACKFORCE )
*/
/*
    (C) 2020 Renesas Electronics Corporation.
     This software is released under the terms and conditions described in LICENSE_RENESAS.TXT included in the project.
*/
#include "LoRaMac.h"
#include "LoRaMacParser.h"
#include "utilities.h"

LoRaMacParserStatus_t LoRaMacParserJoinAccept( LoRaMacMessageJoinAccept_t* macMsg )
{
    if( ( macMsg == 0 ) || ( macMsg->Buffer == 0 ) )
    {
        return LORAMAC_PARSER_ERROR_NPE;
    }

    uint16_t bufItr = 0;

    macMsg->MHDR.Value = macMsg->Buffer[bufItr++];

    memcpy1( macMsg->JoinNonce, &macMsg->Buffer[bufItr], 3 );
    bufItr = bufItr + 3;

    memcpy1( macMsg->NetID, &macMsg->Buffer[bufItr], 3 );
    bufItr = bufItr + 3;

    LoRaMacCommonCopyArrayToUint32( &(macMsg->DevAddr), &(macMsg->Buffer[bufItr]), 4 );
    bufItr += 4;

    macMsg->DLSettings.Value = macMsg->Buffer[bufItr++];

    macMsg->RxDelay = macMsg->Buffer[bufItr++];

    if( ( macMsg->BufSize - LORAMAC_MIC_FIELD_SIZE - bufItr ) == LORAMAC_CF_LIST_FIELD_SIZE )
    {
        memcpy1( macMsg->CFList, &macMsg->Buffer[bufItr], LORAMAC_CF_LIST_FIELD_SIZE );
        bufItr = bufItr + LORAMAC_CF_LIST_FIELD_SIZE;
    }
    else if( ( macMsg->BufSize - LORAMAC_MIC_FIELD_SIZE - bufItr ) > 0 )
    {
        return LORAMAC_PARSER_FAIL;
    }

    LoRaMacCommonCopyArrayToUint32( &(macMsg->MIC), &(macMsg->Buffer[bufItr]), 4 );

    return LORAMAC_PARSER_SUCCESS;
}

LoRaMacParserStatus_t LoRaMacParserData( LoRaMacMessageData_t* macMsg )
{
    if( ( macMsg == 0 ) || ( macMsg->Buffer == 0 ) )
    {
        return LORAMAC_PARSER_ERROR_NPE;
    }
    // length check of mandatory part
    if( macMsg->BufSize < (LORAMAC_MHDR_FIELD_SIZE + LORAMAC_DEVADDR_FIELD_SIZE + 
                           LORAMAC_F_CTRL_FIELD_SIZE + LORAMAC_F_CNT_FIELD_SIZE + LORAMAC_MIC_FIELD_SIZE) )
    {
        return LORAMAC_PARSER_FAIL;
    }

    uint16_t bufItr = 0;

    macMsg->MHDR.Value = macMsg->Buffer[bufItr++];

    LoRaMacCommonCopyArrayToUint32( &(macMsg->FHDR.DevAddr), &(macMsg->Buffer[bufItr]), 4 );
    bufItr += 4;

    macMsg->FHDR.FCtrl.Value = macMsg->Buffer[bufItr++];

    macMsg->FHDR.FCnt = macMsg->Buffer[bufItr++];
    macMsg->FHDR.FCnt |= macMsg->Buffer[bufItr++] << 8;

    // macMsg->FHDR.FCtrl.Bits.FOptsLen is 4bit. So maximum value is 15.
    if( ( macMsg->BufSize - bufItr - LORAMAC_MIC_FIELD_SIZE ) >= macMsg->FHDR.FCtrl.Bits.FOptsLen ) 
    {
        memcpy1( macMsg->FHDR.FOpts, &macMsg->Buffer[bufItr], macMsg->FHDR.FCtrl.Bits.FOptsLen );
        bufItr = bufItr + macMsg->FHDR.FCtrl.Bits.FOptsLen;
    }
    else
    {
        return LORAMAC_PARSER_FAIL;
    }

    // Initialize anyway with zero.
    macMsg->FPort = 0;
    macMsg->FRMPayloadSize = 0;

    if( ( macMsg->BufSize - bufItr - LORAMAC_MIC_FIELD_SIZE ) > 0 )
    {
        macMsg->FPort = macMsg->Buffer[bufItr++];

        macMsg->FRMPayloadSize = ( macMsg->BufSize - bufItr - LORAMAC_MIC_FIELD_SIZE );
#ifdef LORAMAC_RXC_CONTINUOUS_ENABLED
        memcpy1( macMsg->FRMPayload, &macMsg->Buffer[bufItr], macMsg->FRMPayloadSize );
#else
        macMsg->FRMPayload = &macMsg->Buffer[bufItr];
#endif
        bufItr = bufItr + macMsg->FRMPayloadSize;
    }

    LoRaMacCommonCopyArrayToUint32( &(macMsg->MIC), 
                                    &(macMsg->Buffer[ macMsg->BufSize - LORAMAC_MIC_FIELD_SIZE ]), 4 );

    return LORAMAC_PARSER_SUCCESS;
}
