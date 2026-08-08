/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/
#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>


/*!
 */
#define BUFFER_UNLOCK                               (0)
#define BUFFER_LOCKED                               (1)


/*!
 */
struct buffer_cb_s {
    int16_t head;
    int16_t tail;
    int16_t size;
    int8_t  lock;
    uint8_t opt;
};
typedef struct buffer_cb_s  bcb_t;

/*!
 */
struct buffer_s {
    bcb_t   bcb;
    uint8_t *buffer;
};
typedef struct buffer_s     Buffer_t;


/*!
 */
void BufferInit (Buffer_t *, uint8_t *, int16_t);
void BufferReset (Buffer_t *);
void BufferClear (Buffer_t *);

/*!
 */
void BufferPushBack (Buffer_t *, uint8_t);
uint8_t BufferPopFront (Buffer_t *);
uint8_t BufferPopBack (Buffer_t *);
uint8_t *BufferHead(Buffer_t *);

/*!
 */
void BufferSetLocked (Buffer_t *);
void BufferSetUnlock (Buffer_t *);
bool BufferIsLocked (Buffer_t *);

/*!
 */
void BufferSetOpt (Buffer_t *, uint8_t);
uint8_t BufferGetOpt (Buffer_t *);

/*!
 */
bool BufferIsEmpty (Buffer_t *);
bool BufferIsFull (Buffer_t *);

/*!
 */
void BufferTracePushBack (Buffer_t *, uint8_t);
uint8_t BufferTracePopFront (Buffer_t *);


#endif/*BUFFER_H*/
