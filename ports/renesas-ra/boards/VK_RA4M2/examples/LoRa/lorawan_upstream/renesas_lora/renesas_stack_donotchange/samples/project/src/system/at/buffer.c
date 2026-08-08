/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/
#include "buffer.h"


/*!
 */
void BufferInit (Buffer_t *bp, uint8_t *data, int16_t size)
{
    if (NULL != bp) {
        bp->bcb.head = 0;
        bp->bcb.tail = 0;
        bp->bcb.lock = BUFFER_UNLOCK;
        bp->bcb.opt = 0;

        bp->bcb.size = size;
        bp->buffer = data;
    }
}

/*!
 */
void BufferReset (Buffer_t *bp)
{
    if (NULL != bp) {
        bp->bcb.head = 0;
        bp->bcb.tail = 0;
    }
}

/*!
 */
void BufferClear (Buffer_t *bp)
{
    int16_t i;

    if (NULL != bp) {
        BufferReset(bp);
        for (i=0; i < bp->bcb.size; i++)
            bp->buffer[ i ] = '\0';
    }
}

/*!
 */
int16_t buffer_next (Buffer_t *bp, int16_t ind)
{
    return ((ind + 1) % bp->bcb.size);
}

/*!
 */
int16_t buffer_prev (Buffer_t *bp, int16_t ind)
{
    return ((ind - 1) % bp->bcb.size);
}

/*!
 */
bool BufferIsLocked (Buffer_t *bp)
{
    if (NULL != bp) {
        return (bp->bcb.lock == BUFFER_LOCKED);
    }
    return (false);
}

/*!
 */
void BufferPushBack (Buffer_t *bp, uint8_t data)
{
    if (NULL != bp) {
        if (! BufferIsFull(bp)) {
            if (! BufferIsLocked(bp)) {
                bp->bcb.tail = buffer_next(bp, bp->bcb.tail);
                bp->buffer[ bp->bcb.tail ] = data;
            }
        }
    }
}

uint8_t BufferPopFront (Buffer_t *bp)
{
    uint8_t data = 0;
    int16_t head;

    if (NULL != bp) {
        if (! BufferIsEmpty(bp)) {
            if (! BufferIsLocked(bp)) {
                head = buffer_next(bp, bp->bcb.head);
                data = bp->buffer[ head ];
                bp->buffer[ head ] = '\0';
                bp->bcb.head = head;
            }
            else {
                head = buffer_next(bp, bp->bcb.head);
                data = bp->buffer[ head ];
                bp->bcb.head = head;
            }
        }
    }
    return (data);
}

uint8_t BufferPopBack (Buffer_t *bp)
{
    uint8_t data = 0;

    if (NULL != bp) {
        if (! BufferIsEmpty(bp)) {
            if (! BufferIsLocked(bp)) {
                data = bp->buffer[ bp->bcb.tail ];
                bp->buffer[ bp->bcb.tail ] = '\0';
                bp->bcb.tail = buffer_prev(bp, bp->bcb.tail);
            }
        }
    }
    return (data);
}

uint8_t *BufferHead (Buffer_t *bp)
{
    if (NULL != bp) {
        return ((uint8_t *)&(bp->buffer[ bp->bcb.head ]));
    }
    return (NULL);
}


/*!
 */
void BufferSetLocked (Buffer_t *bp)
{
    if (NULL != bp) {
        bp->bcb.lock = BUFFER_LOCKED;
    }
}

void BufferSetUnlock (Buffer_t *bp)
{
    if (NULL != bp) {
        bp->bcb.lock = BUFFER_UNLOCK;
    }
}


/*!
 */
void BufferSetOpt (Buffer_t *bp, uint8_t opt)
{
    if (NULL != bp) {
        bp->bcb.opt = opt;
    }
}

uint8_t BufferGetOpt (Buffer_t *bp)
{
    uint8_t opt = 0;

    if (NULL != bp) {
        opt = (bp->bcb.opt);
    }
    return (opt);
}


/*!
 */
bool BufferIsEmpty (Buffer_t *bp)
{
    return (bp->bcb.head == bp->bcb.tail);
}

bool BufferIsFull (Buffer_t *bp)
{
    return (buffer_next(bp, bp->bcb.tail) == bp->bcb.head);
}


/*!
 */
void BufferTracePushBack (Buffer_t *bp, uint8_t data)
{
    if (NULL != bp) {
        if (! BufferIsFull(bp)) {
            bp->bcb.tail = buffer_next(bp, bp->bcb.tail);
        }
        else {
            bp->bcb.tail = buffer_next(bp, bp->bcb.tail);
            bp->bcb.head = buffer_next(bp, bp->bcb.tail);
        }
        bp->buffer[ bp->bcb.tail ] = data;
    }
}

uint8_t BufferTracePopFront (Buffer_t *bp)
{
    uint8_t data = 0;
    int16_t head;

    if (NULL != bp) {
        if (! BufferIsEmpty(bp)) {
            head = buffer_next(bp, bp->bcb.head);
            data = bp->buffer[ head ];
            bp->bcb.head = head;
        }
    }
    return (data);
}
