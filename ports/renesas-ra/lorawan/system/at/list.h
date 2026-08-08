/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/
#ifndef LIST_H
#define LIST_H

#include <stddef.h>
#include <stdint.h>


/*!
 */
typedef void ** List_t;


/*!
 */
void ListInit (List_t);
/*!
 */
void *ListTail (List_t);
/*!
 */
void *ListPop (List_t);
void ListPush (List_t, void *);

/*!
 */
void ListAdd (List_t, void *);
void ListRemove (List_t, void *);

/*!
 */
int16_t ListLen (List_t);

#endif/*LIST_H*/
