/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/
#include <stdint.h>
#include <stdio.h>

#include "memblock.h"


/*!
 */
#define MEMBLOCK_FREE                   (0)
#define MEMBLOCK_USED                   (1)


/*!
 */
struct MemBlock_cb_s {
    void    *memblock;
    int8_t  *flag;
    int16_t available;
    int16_t size;
};

/*!
 */
static int8_t mb_initialized = 0;
static struct MemBlock_cb_s mbCb;


/*!
 */
void *MemBlockAlloc (void)
{
    MemBlock_t  *mb = NULL;
    int16_t     i;

    if (! mb_initialized)
        return (NULL);

    if (mbCb.available < 1)
        return (NULL);

    mb = (MemBlock_t *)mbCb.memblock;
    for (i=0; i < mbCb.size; i++, mb++) {

        if (MEMBLOCK_FREE == mbCb.flag[ i ]) {
            mbCb.available--;
            mbCb.flag[ i ] = MEMBLOCK_USED;
            break;
        }
    }

    return ((void *)mb);
}

/*!
 */
void MemBlockFree (void *mblock)
{
    MemBlock_t  *mb;
    int16_t     i;

    if (NULL != mblock) {

        mb = (MemBlock_t *)mbCb.memblock;
        for (i=0; i < mbCb.size; i++, mb++) {

            if (mb == mblock) {
                mbCb.available++;
                mbCb.flag[ i ] = MEMBLOCK_FREE;
                break;
            }
        }
    }
}

/*!
 */
void MemBlockInit (void *mblock, int8_t *f, int16_t size)
{
    if (mb_initialized)
        return;

    if (size < 1)
        return;

    if (NULL != mblock && NULL != f) {

        mbCb.memblock = mblock;
        mbCb.flag = f;
        mbCb.available = size;
        mbCb.size = size;

        mb_initialized = 1;
    }
}
