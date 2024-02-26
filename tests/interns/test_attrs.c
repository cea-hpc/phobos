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
 * \brief test atttributes management
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_attrs.h"
#include "pho_common.h"
#include "pho_test_utils.h"
#include <stdlib.h>
#include <stdio.h>

struct key_value {
    char *key;
    char *value;
};

#define TEST_ATTR_COUNT 5

static const struct key_value kvs[TEST_ATTR_COUNT+1] = {
    {"foo", "bar"},
    {"size", "1024"},
    {"owner", "toto"},
    {"class", "trash\"\n;\\"},
    {"misc", "\\\\\\\\"},
    {NULL, NULL}
};

static const struct key_value kvs2[TEST_ATTR_COUNT+1] = {
    {"foo", "xxxx"},
    {"size", "2382094829048"},
    {"owner", "phobos"},
    {"class", "blabla"},
    {"misc", "//////////"},
    {NULL, NULL}
};

/** \return the number of listed items */
static int dump_hash(GHashTable *h)
{
    GHashTableIter iter;
    gpointer key, value;
    int c = 0;

    if (h == NULL)
        return 0;

    g_hash_table_iter_init(&iter, h);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        pho_info("%s='%s'", (char *)key, (char *)value);
        c++;
    }
    return c;
}


static int test1a(void *arg)
{
    struct pho_attrs *attrs = (struct pho_attrs *)arg;
    const struct key_value *kv;
    const char *val;

    /* set attributes in an attribute set */
    for (kv = &kvs[0]; kv->key != NULL; kv++)
        pho_attr_set(attrs, kv->key, kv->value);

    /* get attributes */
    for (kv = &kvs[0]; kv->key != NULL; kv++) {
        val = pho_attr_get(attrs, kv->key);
        if (val == NULL)
            LOG_RETURN(-EINVAL, "pho_attr_get(%s) returned no attr", kv->key);
        if (strcmp(val, kv->value) != 0)
            LOG_RETURN(-EINVAL, "pho_attr_get(%s) returned wrong attr value: "
                       "'%s' != '%s'", kv->key, val, kv->value);
    }

    return 0;
}

static int test1b(void *arg)
{
    struct pho_attrs *attrs = (struct pho_attrs *)arg;
    const struct key_value *kv;
    const char *val;

    /* set attributes again (values from kvs2) */
    for (kv = &kvs2[0]; kv->key != NULL; kv++)
        pho_attr_set(attrs, kv->key, kv->value);

    /* check attributes */
    for (kv = &kvs2[0]; kv->key != NULL; kv++) {
        val = pho_attr_get(attrs, kv->key);
        if (val == NULL)
            LOG_RETURN(-EINVAL, "pho_attr_get(%s) returned no attr", kv->key);
        if (strcmp(val, kv->value) != 0)
            LOG_RETURN(-EINVAL, "pho_attr_get(%s) returned wrong attr value: "
                       "'%s' != '%s'", kv->key, val, kv->value);
    }

    return 0;
}

static int test1c(void *arg)
{
    struct pho_attrs *attrs = (struct pho_attrs *)arg;

    return !(dump_hash(attrs->attr_set) == TEST_ATTR_COUNT);
}

static int test1d(void *arg)
{
    struct pho_attrs       *attrs = (struct pho_attrs *)arg;
    const struct key_value *kv;
    GString                *str = g_string_new("");
    int                     expected_min = 0;
    int                     rc;

    rc = pho_attrs_to_json(attrs, str, JSON_COMPACT | JSON_SORT_KEYS);
    if (rc)
        LOG_RETURN(rc, "pho_attrs_to_json failed");
    if (gstring_empty(str))
        LOG_RETURN(-EINVAL, "Empty or NULL JSON dump");

    pho_info("Attributes: %s", str->str);

    /* length should be at least the sum of all keys and values lengths
     * +1 ':' separator for each. */
    for (kv = &kvs2[0]; kv->key != NULL; kv++)
        expected_min += strlen(kv->key) + strlen(kv->value) + 1;

    if (str->len < expected_min)
        LOG_RETURN(-EINVAL, "Unexpected length for JSON dump %zd < %d",
                   str->len, expected_min);

    g_string_free(str, TRUE);

    return 0;
}

static int test1e(void *arg)
{
    struct pho_attrs *attrs = (struct pho_attrs *)arg;

    return (pho_attr_get(attrs, "don't exist") == NULL);
}

static int test1f(void *arg)
{
    struct pho_attrs *attrs = (struct pho_attrs *)arg;

    pho_attrs_free(attrs);
    return (attrs != NULL && attrs->attr_set != NULL);
}

static int testget(void *arg)
{
    struct pho_attrs *attrs = (struct pho_attrs *)arg;

    return (pho_attr_get(attrs, "foo") == NULL);
}

int main(int argc, char **argv)
{
    struct pho_attrs attrs = {0};

    test_env_initialize();

    pho_run_test("Test 1a: Set and get key values",
             test1a, &attrs, PHO_TEST_SUCCESS);

    pho_run_test("Test 1b: Overwrite attrs",
             test1b, &attrs, PHO_TEST_SUCCESS);

    pho_run_test("Test 1c: List attrs",
             test1c, &attrs, PHO_TEST_SUCCESS);

    pho_run_test("Test 1d: Dump attrs (JSON)",
             test1d, &attrs, PHO_TEST_SUCCESS);

    pho_run_test("Test 1e: Get missing attribute",
             test1e, NULL, PHO_TEST_FAILURE);

    pho_run_test("Test 1f: Release attrs struct",
             test1f, &attrs, PHO_TEST_SUCCESS);

    pho_run_test("Test 2: Get attribute from NULL struct",
             testget, NULL, PHO_TEST_FAILURE);

    memset(&attrs, 0, sizeof(attrs));
    pho_run_test("Test 3: Get attribute from zero-ed struct",
             testget, &attrs, PHO_TEST_FAILURE);

    pho_info("ATTRS: All tests succeeded");

    exit(EXIT_SUCCESS);
}
