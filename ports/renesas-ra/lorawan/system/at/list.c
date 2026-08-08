/*
    Copyright (c) 2022 Renesas Electronics Corporation
    This software is released under the terms and conditions
    described in LICENSE_RENESAS.TXT included in the project.
*/
#include <stdint.h>
#include <stdio.h>

#include "list.h"


/*!
 */
struct List_s {
    struct List_s   *next;
};


/*!
 */
void ListInit (List_t list)
{
    *list = NULL;
}

/*!
 */
void *ListTail (List_t list)
{
    struct List_s *l;

    if (NULL == *list)
        return (NULL);

    for (l=*list; l->next != NULL; l=l->next) ;

    return (l);
}

/*!
 */
void *ListPop (List_t list)
{
    struct List_s *l;

    l = *list;
    if (NULL != *list)
        *list = ((struct List_s *)*list)->next;
    return (l);
}

/*!
 */
void ListPush (List_t list, void *item)
{
    ListRemove(list, item);

    ((struct List_s *)item)->next = *list;
    *list = item;
}

/*!
 */
void ListAdd (List_t list, void *item)
{
    struct List_s *l;

    ListRemove(list, item);

    ((struct List_s *)item)->next = NULL;

    l = ListTail(list);
    if (NULL == l) {
        *list = item;
    }
    else {
        l->next = item;
    }
}

/*!
 */
void ListRemove (List_t list, void *item)
{
    struct List_s *l, *r;

    if (NULL == *list)
        return;

    r = NULL;
    for (l=*list; l != NULL; l=l->next) {
        if (l == item) {
            if (NULL == r) {
                *list = l->next;
            }
            else {
                r->next = l->next;
            }
            l->next = NULL;
            return;
        }
        r = l;
    }
}

/*!
 */
int16_t ListLen (List_t list)
{
    struct List_s *l;
    int16_t n = 0;

    for (l=*list; l != NULL; l=l->next)
        ++n;
    return (n);
}

