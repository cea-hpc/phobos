/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos attributes management
 */
#ifndef _PHO_ATTRS_H
#define _PHO_ATTRS_H

#include <glib.h>
#include <jansson.h> /* for JSON flags */

struct pho_attrs {
    GHashTable *attr_set;
};

typedef int (*pho_attrs_iter_t)(const char *key, const char *val, void *udata);


/** create or update a key-value item in the attribute set */
int pho_attr_set(struct pho_attrs *md, const char *key,
                 const char *value);

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
 * Deserialize an attribute set from JSON string representation.
 * @param md  key-value set to fill.
 * @param str json string to decode.
 */
int pho_json_to_attrs(struct pho_attrs *md, const char *str);

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

#endif
