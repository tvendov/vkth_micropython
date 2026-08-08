/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/
#ifndef AT_PARSER_H
#define AT_PARSER_H

#include <stdint.h>
#include <stdbool.h>

#include "at-command.h"


#if defined(MEMBLOCK_ARRAYSIZE_CONFIG)
#   define MEMBLOCK_ARRAYSIZE           (MEMBLOCK_ARRAYSIZE_CONFIG)
#else
#   define MEMBLOCK_ARRAYSIZE           (20)
#endif

/*!
 */
void AtParseInit (void);
/*!
 */
int16_t AtParseListLen (void);
void AtParsePopOne (int8_t **, int16_t *);
void AtParseForcePurgeList (void);

/*!
 */
void AtParseBasic (const AtBasicTab_t *, int8_t *);
#if (AT_EXTCMD_TAB_ARRAYSIZE == 0)
void AtParseExtend (AtExtendTab_t *, uint16_t, int8_t *);
#else
void AtParseExtend (AtExtendTab_t *, uint16_t, int8_t *);
#endif

/*!
 */
uint32_t AtParseGet4ByteHexValue (int8_t *, int8_t);
int16_t AtParseGetHexValue (int8_t *, int8_t);
int8_t AtParseGetNibbleValue (int8_t *, int8_t);
int8_t AtParseGetNibble (int8_t);

/*!
 */
int32_t AtParseGetDecValue (int8_t *, int8_t);
int16_t AtParseCountString (int8_t *, int8_t);

/*!
 */
int16_t AtParseCountHexString (int8_t *, int8_t);
bool AtParseCheckHexChar (int8_t);

/*!
 */
int16_t AtParseCountDecString (int8_t *, int8_t);
bool AtParseCheckDecChar (int8_t);

#endif/*AT_PARSER_H*/
