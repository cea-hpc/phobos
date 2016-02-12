/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief Phobos attribute management
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_attrs.h"
#include "pho_common.h"
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>

void pho_attrs_free(struct pho_attrs *md)
{
    if (md == NULL || md->attr_set == NULL)
        return;

    g_hash_table_destroy(md->attr_set);
    md->attr_set = NULL;
}

const char *pho_attr_get(struct pho_attrs *md, const char *key)
{
    if (md == NULL || md->attr_set == NULL)
        return NULL;

    return g_hash_table_lookup(md->attr_set, key);
}

int pho_attr_set(struct pho_attrs *md, const char *key,
                 const char *value)
{
    if (md->attr_set == NULL) {
        md->attr_set = g_hash_table_new_full(g_str_hash, g_str_equal, free,
                                             free);
        if (md->attr_set == NULL)
            return -ENOMEM;
    }

    /* use ght_replace, so that previous key and values are freed */
    g_hash_table_replace(md->attr_set, strdup(key), strdup(value));
    return 0;
}

/** callback function to dump a JSON to a GString
 * It must follow json_dump_callback_t prototype
 * and specified behavior.
 * @return 0 on success, -1 on error.
 */
static int dump_to_gstring(const char *buffer, size_t size, void *data)
{
    if ((ssize_t)size < 0)
        return -1;

    g_string_append_len((GString *)data, buffer, size);
    return 0;
}

static int attr_json_dump_cb(const char *key, const char *value, void *udata)
{
    return json_object_set_new((json_t *)udata, key, json_string(value));
}

/** Serialize an attribute set by converting it to JSON. */
int pho_attrs_to_json(const struct pho_attrs *md, GString *str, int flags)
{
    json_t  *jdata;
    int      rc = 0;

    if (str == NULL)
        return -EINVAL;

    /* make sure the target string is empty */
    g_string_assign(str, "");

    /* return empty JSON object if attr list is empty */
    if (md == NULL || md->attr_set == NULL) {
        g_string_append(str, "{}");
        goto out_nojson;
    }

    jdata = json_object();
    if (jdata == NULL)
        return -ENOMEM;

    rc = pho_attrs_foreach(md, attr_json_dump_cb, jdata);
    if (rc != 0)
        goto out_free;

    rc = json_dump_callback(jdata, dump_to_gstring, str, flags);

out_free:
    json_decref(jdata);

out_nojson:
    /* jansson does not return a meaningful error code, assume EINVAL */
    return (rc != 0) ? -EINVAL : 0;
}

int pho_json_to_attrs(struct pho_attrs *md, const char *str)
{
    json_error_t     jerror;
    json_t          *jdata;
    void            *iter;
    int              rc = 0;

    if (str == NULL)
        return -EINVAL;

    jdata = json_loads(str, JSON_REJECT_DUPLICATES, &jerror);
    if (jdata == NULL)
        LOG_RETURN(-EINVAL, "JSON parsing error: %s at position %d",
                   jerror.text, jerror.position);

    iter = json_object_iter(jdata);
    while (iter) {
        const char  *key = json_object_iter_key(iter);
        json_t      *val = json_object_iter_value(iter);

        rc = pho_attr_set(md, key, json_string_value(val));
        if (rc)
            break;

        iter = json_object_iter_next(jdata, iter);
    }

    json_decref(jdata);
    return rc;
}

int pho_attrs_foreach(const struct pho_attrs *md, pho_attrs_iter_t cb,
                      void *udata)
{
    GHashTableIter  iter;
    gpointer        key;
    gpointer        value;
    int             rc = 0;

    if (md == NULL || md->attr_set == NULL)
        return 0;

    g_hash_table_iter_init(&iter, md->attr_set);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        rc = cb((const char *)key, (const char *)value, udata);
        if (rc != 0)
            break;
    }

    return rc;
}
