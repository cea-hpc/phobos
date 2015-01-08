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

void pho_attrs_free(struct pho_attrs *md)
{
    if (md->attr_set == NULL)
        return;

    g_hash_table_destroy(md->attr_set);
    md->attr_set = NULL;
}

const char *pho_attr_get(struct pho_attrs *md, const char *key)
{
    if (md->attr_set == NULL)
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
