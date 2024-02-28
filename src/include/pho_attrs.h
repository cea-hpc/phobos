/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2022 CEA/DAM.
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
 * \brief  Phobos attributes management
 */
#ifndef _PHO_ATTRS_H
#define _PHO_ATTRS_H

#include <glib.h>
#include <jansson.h> /* for JSON flags */
#include <stdbool.h>

struct pho_attrs {
    GHashTable *attr_set;
};

typedef int (*pho_attrs_iter_t)(const char *key, const char *val, void *udata);


static inline bool pho_attrs_is_empty(const struct pho_attrs *attrs)
{
    return attrs->attr_set == NULL;
}

/** create or update a key-value item in the attribute set */
void pho_attr_set(struct pho_attrs *md, const char *key, const char *value);

/** get a key-value item by key name */
const char *pho_attr_get(struct pho_attrs *md, const char *key);

/** empty the attribute list and release memory */
void pho_attrs_free(struct pho_attrs *md);

/**
 * Serialize an attribute set by converting it to JSON.
 * @param md key-value set.
 * @param str GString that must be allocated by the caller.
 * @param flags JSON_* flags (from jansson).
 */
int pho_attrs_to_json(const struct pho_attrs *md, GString *str, int json_flags);

/**
 * Serialize an attribute set by converting it to raw JSON.
 * @param md    key-value set.
 * @param obj   json object that must be allocated by the caller.
 */
int pho_attrs_to_json_raw(const struct pho_attrs *md, json_t *obj);

/**
 * Deserialize an attribute set from JSON string representation.
 * @param md  key-value set to fill.
 * @param str json string to decode.
 */
int pho_json_to_attrs(struct pho_attrs *md, const char *str);

/**
 * Deserialize an attribute set from raw JSON object.
 * @param md  key-value set to fill.
 * @param str json object to decode.
 */
void pho_json_raw_to_attrs(struct pho_attrs *md, json_t *obj);

/**
 * Invoke a callback on all items of the attribute set.
 * Iteration stops if the callback returns a non-null value,
 * which is then propagated back to the caller.
 *
 * @param[in]       md      The attribute set to iterate over.
 * @param[in]       cb      The processing callback.
 * @param[in,out]   udata   User data to be passed in to the callback.
 *
 * @return 0 on success or first non-zero value returned by the callback.
 */
int pho_attrs_foreach(const struct pho_attrs *md, pho_attrs_iter_t cb,
                      void *udata);

/**
 * Remove all key/value pairs if the value is NULL
 *
 * @param[in]       md      The attribute set to check.
 */
void pho_attrs_remove_null(struct pho_attrs *md);

#endif
