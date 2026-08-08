/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/
#include <stdint.h>
#include <string.h>

#include "at-command.h"
#include "at-parser.h"
#include "list.h"
#include "memblock.h"


/*!
 * \brief list to manage the memory block
 */
void        *atTag = NULL;
List_t      atList = (List_t )&atTag;
MemBlock_t  memblock[ MEMBLOCK_ARRAYSIZE ];
int8_t      usedflag[ MEMBLOCK_ARRAYSIZE ];

/*!
 */
AtErrorCode_t AtParseSplitStringByDelim (int8_t *, int8_t);

/*!
 */
void AtParseInit (void)
{
    ListInit(atList);
    MemBlockInit((void *)&memblock[0], &usedflag[0], MEMBLOCK_ARRAYSIZE);
}

/*!
 */
int16_t AtParseListLen (void)
{
    return (ListLen(atList));
}

/*!
 */
void AtParsePopOne (int8_t **cp, int16_t *n)
{
    MemBlock_t *tp;
    void *vp;

    *cp = NULL;
    *n  = -1;

    vp = ListPop(atList);
    if (NULL != vp) {
        tp  = (MemBlock_t *)vp;
        *cp = tp->cp;
        *n  =  tp->n;
        MemBlockFree(vp);
    }
}

/*!
 */
void AtParseForcePurgeList (void)
{
    void    *vp;

    while (NULL != (vp=ListPop(atList)))
        MemBlockFree(vp);
}


/*!
 */
void AtParseBasic (const AtBasicTab_t *tp, int8_t *cp)
{
    int16_t i;

    for (i=0; tp->cmd != AT_CMD_NUL; i++, tp++) {

        if (tp->cmd == *cp) {
            int16_t val;

            val = AtParseGetNibbleValue(cp+1, AT_CMD_NUL);
            tp->proc(val);
            break;
        }
    }
}

/*!
 */
void AtParseExtend (AtExtendTab_t *tp, uint16_t n, int8_t *cp)
{
    int16_t i;

    AtCmdSetEcStatus( AT_EC_ERR );
    AtCmdSetRcStatus( AT_RC_ERR );

    for (i=0; i<n; i++, tp++) {

        if (tp->len != 0UL) {
            if (strncmp((const char *)(tp->name), (const char *)cp, (size_t )(tp->len)) == 0) {

                /* match */
                AtResultCode_t  rc;
                int8_t *p = (int8_t *)cp+(tp->len);

                AtCmdSetCurrentCmd(i);

                switch (*p) {
                    case '=':
                        {
                            /* set */
                            if (AT_CMD_NUL == *(p+1))
                                break;

                            AtParseSplitStringByDelim(p+1, AT_EXTCMD_DELIM);
                            AtCmdSetEcStatus(AT_EC_NOTSUP);

                            if (NULL != tp->set) {
                                rc = tp->set((void *)cp);
                                AtCmdSetEcStatus(AT_EC_OK);
                                AtCmdSetRcStatus(rc);
                            }

                            AtParseForcePurgeList();
                            break;
                        }

                    case '?':
                        {
                            /* get */
                            if (AT_CMD_NUL != *(p+1))
                                break;

                            AtCmdSetEcStatus(AT_EC_NOTSUP);

                            if (NULL != tp->get) {
                                rc = tp->get((void *)cp);
                                AtCmdSetEcStatus(AT_EC_OK);
                                AtCmdSetRcStatus(rc);
                            }
                            break;
                        }
                    case '\0':
                        {
                            /* act */
                            AtCmdSetEcStatus(AT_EC_NOTSUP);

                            if (NULL != tp->act) {
                                rc = tp->act((void *)cp);
                                AtCmdSetEcStatus(AT_EC_OK);
                                AtCmdSetRcStatus(rc);
                            }
                            break;
                        }

                    default:
                        {
                            AtCmdSetEcStatus( AT_EC_LOGIC );
                            break;
                        }
                } /* end of switch */
            }
            if (AtCmdGetEcStatus() == AT_EC_OK || AtCmdGetEcStatus() == AT_EC_NOTSUP) {
                break;
            }
            else if (AtCmdGetEcStatus() == AT_EC_LOGIC) {
                AtCmdSetEcStatus( AT_EC_ERR );
                continue;
            }
        } /* if */
    } /* for */
}


AtErrorCode_t AtParseSplitStringByDelim (int8_t *cp, int8_t delim)
{
    int8_t  *p, *p1;
    void    *vp;
    int8_t  i, cnt;
    int16_t n;

    cnt = 0;
    p = p1 = cp;
    while (*p != AT_CMD_NUL) {
        if (*p == delim || *(p+1) == AT_CMD_NUL) cnt++;
        p++;
    }

    p = p1;
    for (i=0; i < cnt; i++) {
        n = AtParseCountString(p, delim);
        if (n <= 0) {
            /* ERR: */
            n = ListLen(atList);
            if (n != 0)
                AtParseForcePurgeList();
            break;
        }
        *(p+n) = AT_CMD_NUL;

        vp = MemBlockAlloc();
        {
            MemBlock_t *t = (MemBlock_t *)vp;
            t->n  = n;
            t->cp = p1;
        }
        ListAdd(atList, vp);

        p1 = p + n + 1;
        p  = p1;
    }

    return (AT_EC_OK);
}

/*!
 */
int16_t AtParseGetHexValue (int8_t *cp, int8_t delim)
{
    int16_t res = -1;
    int16_t n;
    int8_t  *p  = cp;

    n = AtParseCountHexString(p, delim);
    if (n == 2) {
        res = AtParseGetNibble(*p);

        res =  res << 4;
        res += AtParseGetNibble( *(p+1) );
    }
    else if (n == 1) {
        res = AtParseGetNibble(*p);
    }
    else if (n < res) {
        res = n;
    }

    return (res);
}

/*!
 */
int8_t AtParseGetNibbleValue (int8_t *cp, int8_t delim)
{
    int8_t  res = -1;
    int16_t n;
    int8_t  *p  = cp;

    n = AtParseCountHexString(p, delim);
    if (n == 1) {
        res = AtParseGetNibble(*p);
    }
    else if (n < res) {
        res = (int8_t )n;
    }

    return (res);
}

/*!
 */
int8_t AtParseGetNibble (int8_t c)
{
    int8_t  res = -1;

    if (NUMBERS(c)) {
        res = ((uint8_t )(c - '0'));
    }
    else if (HEXUPPERCASE(c)) {
        res = ((uint8_t )((c - 'A') + 10));
    }
    return (res);
}

int32_t AtParseGetDecValue (int8_t *cp, int8_t delim)
{
    int32_t num = 0, co = 1;
    int16_t i, n;
    int8_t  *p = cp;

    n = AtParseCountDecString(p, delim);
    if (n < 0)
        return (-1);
    for (i=0; i < n; i++) {
        num += (p[n-1-i] - 48) * co;
        co  *= 10;
    }
    return (num);
}

/*!
 */
uint32_t AtParseGet4ByteHexValue (int8_t *cp, int8_t delim)
{
    uint32_t res = 0;
    uint32_t readVal= 0;
    int16_t i, n;
    int8_t  *p  = cp;

    n = AtParseCountHexString(p, delim);
    for (i=0 ;  i < n ; i++) {
        readVal = AtParseGetNibble(*p);
        res =  (res << (4)) + (readVal);
        p++;
        if ((*p) == delim)
            break;
    }
    return (res);
}
/*!
 */
int16_t AtParseCountString (int8_t *cp, int8_t delim)
{
    int16_t res = 0;
    int8_t  *p  = cp;

    while (*p != AT_CMD_NUL) {
        if (*p == delim)
            return (res);
        p++;
        res++;
    }
    return (res);
}
/*!
 */
int16_t AtParseCountHexString (int8_t *cp, int8_t delim)
{
    int16_t res = 0;
    int8_t  *p  = cp;

    while (*p != AT_CMD_NUL) {
        if (*p == delim)
            return (res);
        if (! AtParseCheckHexChar(*p))
            return (-9);
        p++;
        res++;
    }
    return (res);
}
int16_t AtParseCountDecString (int8_t *cp, int8_t delim)
{
    int16_t res = 0;
    int8_t  *p  = cp;

    while (*p != AT_CMD_NUL) {
        if (*p == delim)
            return (res);
        if (! AtParseCheckDecChar(*p))
            return (-9);
        p++;
        res++;
    }
    return (res);
}

/*!
 */
bool AtParseCheckHexChar (int8_t c)
{
    bool res = false;

    if (NUMBERS(c) || HEXUPPERCASE(c))
        res = true;
    return (res);
}

/*!
 */
bool AtParseCheckDecChar (int8_t c)
{
    bool res = false;

    if (NUMBERS(c))
        res = true;
    return (res);
}
