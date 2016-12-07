/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Test common tools
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_test_utils.h"
#include "pho_common.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* callback function for parsing */
static int parse_line(void *arg, char *line, size_t size, int stream)
{
    GList   **ctx = (GList **)arg;
    int       len;

    if (line == NULL)
        return -EINVAL;

    len = strnlen(line, size);
    /* terminate the string */
    if (len >= size)
        line[len - 1] = '\0';

    /* remove '\n' */
    if ((len > 0) && (line[len - 1] == '\n'))
        line[len - 1] = '\0';

    *ctx = g_list_append(*ctx, strdup(line));
    return 0;
}

static void print_lines(GList *lines)
{
    GList *l;
    int i = 0;

    /* print the list */
    for (l = lines; l != NULL; l = l->next) {
        i++;
        pho_debug("%d: <%s>", i, (char *)l->data);
    }
}

static int test_cmd(void *arg)
{
    GList   *lines = NULL;
    int      rc = 0;

    /** call a command and call cb_func for each output line */
    rc = command_call((char *)arg, parse_line, &lines);
    if (rc) {
        fprintf(stderr, "command '%s' return with status %d\n", (char *)arg,
                rc);
        return rc;
    }

    print_lines(lines);
    return 0;
}

static int test_convert(void *arg)
{
    int64_t val = str2int64(arg);

    if (val == INT64_MIN)
        return -1;

    if (val != strtoll(arg, NULL, 10))
        return -1;

    return 0;
}

static GHashTable *test_hash_table_new(void)
{
    GHashTable *ht = g_hash_table_new(g_str_hash, g_str_equal);

    g_hash_table_insert(ht, "A", "0");
    g_hash_table_insert(ht, "B", "1");
    g_hash_table_insert(ht, "C", "2");
    g_hash_table_insert(ht, "D", "3");
    g_hash_table_insert(ht, "E", "4");
    g_hash_table_insert(ht, "F", "5");
    return ht;
}

static int itm_cnt_cb(const void *k, void *v, void *ud)
{
    int *views = ud;

    *views += 1;
    return 0;
}

static int test_iter(void *arg)
{
    GHashTable  *ht = test_hash_table_new();
    int          views = 0;
    int          rc = 0;

    rc = pho_ht_foreach(ht, itm_cnt_cb, &views);
    if (rc)
        goto out_free;

    if (views != g_hash_table_size(ht)) {
        rc = -EPROTO;
        goto out_free;
    }

out_free:
    g_hash_table_destroy(ht);
    return rc;
}

static int itm_stop_at_2nd_cb(const void *k, void *v, void *ud)
{
    int *views = ud;

    *views += 1;
    if (*views == 2) {
        /* return anything but zero to stop iteration;
         * chose EMULTIHOP so that I can use it once in my life */
        return -EMULTIHOP;
    }

    return 0;
}

static int test_iter_err(void *arg)
{
    GHashTable  *ht = test_hash_table_new();
    int          views = 0;
    int          rc;

    rc = pho_ht_foreach(ht, itm_stop_at_2nd_cb, &views);
    if (rc == 0 || views != 2) {
        /* itm_stop_at_2nd_cb is expected to fail */
        rc = -EPROTO;
        goto out_free;
    }

    rc = 0;

out_free:
    g_hash_table_destroy(ht);
    return rc;
}

int main(int argc, char **argv)
{
    test_env_initialize();

    /* test commands */
    run_test("Test1: command calls + output callback", test_cmd,
             "cat /etc/passwd", PHO_TEST_SUCCESS);
    run_test("Test2: failing command", test_cmd,
             "cat /foo/bar", PHO_TEST_FAILURE);

    /* test str2int64 */
    run_test("Test3a: str2int64 positive val", test_convert, "32",
             PHO_TEST_SUCCESS);
    run_test("Test3b: str2int64 negative val", test_convert, "-1",
             PHO_TEST_SUCCESS);
    run_test("Test3c: str2int64 positive 64", test_convert, "58000000000",
             PHO_TEST_SUCCESS);
    run_test("Test3d: str2int64 negative 64", test_convert, "-63000000000",
             PHO_TEST_SUCCESS);
    run_test("Test3e: str2int64 value over 2^64", test_convert,
             "90000000000000000000", PHO_TEST_FAILURE);
    run_test("Test3e: str2int64 value below -2^64", test_convert,
             "-90000000000000000000", PHO_TEST_FAILURE);
    run_test("Test3f: str2int64 value with prefix", test_convert, "dqs2167",
             PHO_TEST_FAILURE);
    run_test("Test3g: str2int64 value with suffix", test_convert, "2167s",
             PHO_TEST_FAILURE);

    /* test phobos custom iterators over GHashTable */
    run_test("Test4: traverse ghashtable", test_iter, NULL, PHO_TEST_SUCCESS);
    run_test("Test4: error stops ghashtable traversal", test_iter_err, NULL,
             PHO_TEST_SUCCESS);

    fprintf(stderr, "test_common: all tests successful\n");
    exit(EXIT_SUCCESS);
}
