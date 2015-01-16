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
    if (md->attr_set == NULL)
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

/** Serialize an attribute set by converting it to JSON. */
int pho_attrs_to_json(const struct pho_attrs *md, GString *str, int flags)
{
    GHashTableIter iter;
    gpointer       key, value;
    json_t        *jdata;
    int            rc;

    if (str == NULL)
        return -EINVAL;

    /* make sure the target string is empty */
    g_string_assign(str, "");

    /* return empty string if attr list is empty */
    if (md == NULL)
        return 0;

    jdata = json_object();
    if (jdata == NULL)
        return -ENOMEM;

    g_hash_table_iter_init(&iter, md->attr_set);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        rc = json_object_set_new(jdata, (char *)key,
                                 json_string((char *)value));
        if (rc)
            GOTO(out_free, rc);
    }

    rc = json_dump_callback(jdata, dump_to_gstring, str, flags);

out_free:
    json_decref(jdata);
    /* jansson does not return a meaningful error code, assume EINVAL */
    return (rc != 0) ? -EINVAL : 0;
}

