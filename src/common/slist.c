/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2020 CEA/DAM.
 *
 *  This file is part of Phobos.
 *
 *  Phobos is free software: you can redistribute it and/or modify it under
 *  the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  Phobos is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with Phobos. If not, see <http://www.gnu.org/licenses/>.
 */
/**
 * \brief  Simple linked list implementation.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "slist.h"

#include <assert.h>
#include <stdlib.h>

struct slist_entry {
    void *data;
    void *next;
};

struct slist_entry *list_prepend(struct slist_entry *list, void *item)
{
    struct slist_entry *slot;

    slot = malloc(sizeof(*slot));
    if (!slot)
        return NULL;

    slot->data = item;
    slot->next = list;

    return slot;
}

void list_free_all(struct slist_entry *list, free_func_t func)
{
    struct slist_entry *item;
    struct slist_entry *item_next;

    for (item = list; item != NULL; item = item_next) {
        item_next = item->next;
        if (func)
            func(item->data);
        free(item);
    }
}

void *list_find(struct slist_entry *list, const void *arg, match_func_t func)
{
    struct slist_entry *item;

    assert(func != NULL);

    for (item = list; item != NULL; item = item->next) {
        if (func(item->data, arg))
            return item->data;
    }

    return NULL;
}
