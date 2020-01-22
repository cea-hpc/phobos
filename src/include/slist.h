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
#ifndef _SLIST_H
#define _SLIST_H

#include <stdbool.h>

struct slist_entry;

/**
 * Add an item to a single linked list.
 * @param  list     Old pointer to the list.
 * @param  item     Item to be added to the list.
 * @return New pointer to the list.
 */
struct slist_entry *list_prepend(struct slist_entry *list, void *item);

/**
 * Function to release an item from the list.
 */
typedef void (*free_func_t)(void *);

/**
 * Release all items from the list and release list resources.
 * After this call, the value pointed by list is no longer valid
 * and should be NULL'ed.
 */
void list_free_all(struct slist_entry *list, free_func_t func);

/**
 * Function to match a data item.
 * @param item  List item, as passed to list_prepend.
 * @param arg   Custom argument passed to the list_find() function.
 */
typedef bool (*match_func_t)(const void *item, const void *arg);

/**
 * Search for an item in the list using a custom function.
 * @param list      Pointer to the list.
 * @param arg       Custom argument passed to the matching function.
 * @param func      Custom (non-NULL) function to match list items.
 * @return Value of the first matching list item, if found, NULL else.
 */
void *list_find(struct slist_entry *list, const void *arg, match_func_t func);

#endif
