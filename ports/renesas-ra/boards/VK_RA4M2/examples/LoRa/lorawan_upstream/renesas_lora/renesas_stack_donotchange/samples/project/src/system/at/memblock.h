/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/
#ifndef MEMBLOCK_H
#define MEMBLOCK_H

#include <stddef.h>
#include <stdint.h>


/*!
 *
 */
struct MemBlock_s {
    struct MemBlock_s   *next;
    int8_t              *cp;
    int16_t             n;
};

typedef struct MemBlock_s   MemBlock_t;


/*!
 *
 */
void *MemBlockAlloc (void);
/*!
 *
 */
void MemBlockFree (void *);
/*!
 *
 */
void MemBlockInit (void *, int8_t *, int16_t);

#endif/*MEMBLOCK_H*/
