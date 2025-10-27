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
 * \brief  Test common tools
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cmocka.h>

#include "phobos_store.h"
#include "pho_test_utils.h"
#include "pho_common.h"

/* callback function for parsing */
static int parse_line(void *arg, char *line, size_t size, int stream)
{
    GList **ctx = (GList **)arg;
    int len;

    if (line == NULL)
        return -EINVAL;

    len = strnlen(line, size);
    /* terminate the string */
    if (len >= size)
        line[len - 1] = '\0';

    *ctx = g_list_append(*ctx, xstrdup(line));
    return 0;
}

static void command_call_success(void **state)
{
    GList *lines = NULL;
    char buffer[256];
    GList *line;
    FILE *hosts;
    int rc = 0;

    hosts = fopen("/etc/hosts", "r");
    assert_non_null(hosts);

    /** call a command and call cb_func for each output line */
    rc = command_call("cat /etc/hosts", parse_line, &lines);
    assert_return_code(rc, -rc);

    line = lines;
    while (fgets(buffer, sizeof(buffer), hosts)) {
        size_t buffer_length = strlen(buffer);

        if (buffer[buffer_length - 1] == '\n')
            buffer[buffer_length - 1] = '\0';

        assert_string_equal(buffer, (char *) line->data);
        line = line->next;
    }

    fclose(hosts);

    g_list_free_full(lines, free);
}

#define ERROR_MAKER_SCRIPT "test_common_error.sh"

static void command_call_failure(void **state)
{
    char *full_command;
    char buffer[4096];
    const char *pwd;
    int rc;

    pwd = getcwd(buffer, 4096);
    rc = asprintf(&full_command, "%s/%s 42", pwd, ERROR_MAKER_SCRIPT);
    assert(rc > 0);

    rc = command_call(full_command, NULL, NULL);
    assert_int_equal(rc, 42);

    free(full_command);
}

static void check_str2int64(void **state)
{
    assert_int_equal(str2int64("32"), 32);
    assert_int_equal(str2int64("-1"), -1);
    assert_int_equal(str2int64("58000000000"), 58000000000);
    assert_int_equal(str2int64("-63000000000"), -63000000000);

    assert_int_equal(str2int64("90000000000000000000"), INT64_MIN);
    assert_int_not_equal(errno, 0);

    assert_int_equal(str2int64("-90000000000000000000"), INT64_MIN);
    assert_int_not_equal(errno, 0);

    assert_int_equal(str2int64("dqs2167"), INT64_MIN);
    assert_int_not_equal(errno, 0);

    assert_int_equal(str2int64("2167dqs"), INT64_MIN);
    assert_int_not_equal(errno, 0);
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

static int item_count_callback(__attribute__((unused)) const void *key,
                               __attribute__((unused)) void *value,
                               void *user_data)
{
    int *views = user_data;

    *views += 1;
    return 0;
}

static void hashtable_foreach_success(void **state)
{
    GHashTable *ht = test_hash_table_new();
    int views = 0;
    int rc = 0;

    rc = pho_ht_foreach(ht, item_count_callback, &views);
    assert_return_code(rc, -rc);

    assert_int_equal(views, g_hash_table_size(ht));

    g_hash_table_destroy(ht);
}

static int item_stop_at_2nd_iter(__attribute__((unused)) const void *key,
                                 __attribute__((unused)) void *vvalue,
                                 void *user_data)
{
    int *views = user_data;

    *views += 1;
    if (*views == 2) {
        /* return anything but zero to stop iteration;
         * chose EMULTIHOP so that I can use it once in my life */
        return -EMULTIHOP;
    }

    return 0;
}

static void hashtable_foreach_failure(void **state)
{
    GHashTable *ht = test_hash_table_new();
    int views = 0;
    int rc = 0;

    rc = pho_ht_foreach(ht, item_stop_at_2nd_iter, &views);
    assert_int_equal(rc, -EMULTIHOP);
    assert_int_equal(views, 2);

    g_hash_table_destroy(ht);
}

static void check_phobos_version(void **state)
{
    assert_true(__PHOBOS_PREREQ(2, 2));
    assert_false(__PHOBOS_PREREQ(777, 42));
    assert_true(__PHOBOS_PREREQ_PATCH(2, 2, 63));
    assert_false(__PHOBOS_PREREQ_PATCH(777, 42, -2));
}

int main(int argc, char **argv)
{
    const struct CMUnitTest test_common[] = {
        cmocka_unit_test(command_call_success),
        cmocka_unit_test(command_call_failure),

        cmocka_unit_test(check_str2int64),

        cmocka_unit_test(hashtable_foreach_success),
        cmocka_unit_test(hashtable_foreach_failure),

        cmocka_unit_test(check_phobos_version),
    };

    test_env_initialize();

    return cmocka_run_group_tests(test_common, NULL, NULL);
}
