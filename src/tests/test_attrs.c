/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief test atttributes management
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_attrs.h"
#include <stdlib.h>
#include <stdio.h>

struct key_value {
    char *key;
    char *value;
};

static const struct key_value kvs[] = {
    {"foo", "bar"},
    {"size", "1024"},
    {"owner", "toto"},
    {"class", "test"},
    {NULL, NULL}
};


static void dump_hash(GHashTable *h)
{
    GHashTableIter iter;
    gpointer key, value;

    if (h == NULL)
        return;

    g_hash_table_iter_init(&iter, h);
    while (g_hash_table_iter_next(&iter, &key, &value))
        printf("%s='%s'\n", (char *)key, (char *)value);
}

int main(int argc, char **argv)
{
    struct pho_attrs attrs = {0};
    const struct key_value *kv;

    int rc;

    /* set attributes in an attribute set */
    for (kv = &kvs[0]; kv->key != NULL; kv++) {
        rc = pho_attr_set(&attrs, kv->key, kv->value);

        if (rc)
            fprintf(stderr, "pho_attr_set failed with code %d\n", rc);
        else
            printf("%s set to '%s'\n", kv->key,
                   pho_attr_get(&attrs, kv->key));
    }
    printf("----------------\n");

    /* set attributes again */
    for (kv = &kvs[0]; kv->key != NULL; kv++) {
        rc = pho_attr_set(&attrs, kv->key, kv->value);

        if (rc)
            fprintf(stderr, "pho_attr_set failed with code %d\n", rc);
        else
            printf("%s reset to '%s'\n", kv->key,
                   pho_attr_get(&attrs, kv->key));
    }
    printf("----------------\n");

    /* get them back (lookup by key) */
    for (kv = &kvs[0]; kv->key != NULL; kv++)
        printf("%s = '%s'\n", kv->key, pho_attr_get(&attrs, kv->key));

    printf("----------------\n");
    /* get them back (iterate on keys) */
    dump_hash(attrs.attr_set);

    printf("----------------\n");
    pho_attrs_free(&attrs);

    exit(0);
}
