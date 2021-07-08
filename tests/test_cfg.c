/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2017 CEA/DAM.
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
 * \brief  Test configuration management.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_cfg.h"
#include "pho_test_utils.h"
#include "pho_common.h"
#include <libgen.h>
#include <attr/xattr.h>

struct test_item {
    const char *section;
    const char *variable;
    const char *value;
};

struct test_item test_env_items[] = {
    {"section1", "var0", "val0"},
    {"section2", "var0", "value_from_env"},
    {"section3", "var0", NULL}, /* actually not set: no value expected */
    {NULL, NULL, NULL},
};

struct test_item test_file_items[] = {
    {"dss", "connect_string", "dbname = phobos"},
    {"foo", "bar", "42"},
    {"section2", "var0", "value_from_env"}, /**< if a variable is defined in
                                                 both, environment has the
                                                 priority */
    {"section3", "var0", NULL}, /**< this variable doesn't exist:
                                     no value expected */
    {"section2", "very_long", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
    {NULL, NULL, NULL},
};

/* @TODO add tests for global config variables (in DSS) */

/** put test variables to environment */
static int populate_env(void)
{
    struct test_item *item;
    int               rc;

    for (item = test_env_items; item->variable != NULL; item++) {
        if (item->value != NULL) {
            char *key;
            char *sec;
            char *var;

            sec = strdup(item->section);
            var = strdup(item->variable);
            upperstr(sec);
            lowerstr(var);
            rc = asprintf(&key, "PHOBOS_%s_%s", sec, var);
            free(var);
            free(sec);
            if (rc == -1)
                return -ENOMEM;

            rc = setenv(key, item->value, 1);
            free(key);
            if (rc)
                return -rc;
        }
    }
    return 0;
}

static int test(void *hint)
{
    struct test_item *item;
    int               rc;

    for (item = (struct test_item *)hint; item->variable != NULL; item++) {
        const char *val = NULL;

        rc = pho_cfg_get_val(item->section, item->variable, &val);

        /* if value is NULL: -ENODATA is expected */
        if (item->value == NULL) {
            if (rc != -ENODATA) {
                int rc2 = rc ? rc : -EINVAL;
                pho_error(rc2, "pho_cfg_get_val(%s, %s, ...): -ENODATA expected"
                          " (got %d)", item->section, item->variable, rc);
                return rc2;
            }
            /* else: OK */
        } else if (rc) {
            pho_error(rc, "pho_cfg_get_val(%s, %s, ...) returned error %d",
                    item->section, item->variable, rc);
            return rc;
        } else {
            if (strcmp(val, item->value)) {
                rc = -EINVAL;
                pho_error(rc, "unexpected value for '%s'::'%s': '%s' != '%s'",
                          item->section, item->variable, val, item->value);
                return rc;
            }
        }
    }
    return 0;
}

/** List of SCSI library configuration parameters */
enum pho_cfg_params_test {
    PHO_CFG_TEST_param0,
    PHO_CFG_TEST_param1,
    PHO_CFG_TEST_strparam,

    PHO_CFG_TEST_FIRST = PHO_CFG_TEST_param0,
    PHO_CFG_TEST_LAST  = PHO_CFG_TEST_strparam,
};

/** Definition and default values of SCSI library configuration parameters */
const struct pho_config_item cfg_test[] = {
    [PHO_CFG_TEST_param0] = {
        .section = "test",
        .name    = "param0",
        .value   = "0",
    },

    [PHO_CFG_TEST_param1] = {
        .section = "test",
        .name    = "param1",
        .value   = "1",
    },

    [PHO_CFG_TEST_strparam] = {
        .section = "test",
        .name    = "strparam",
        .value   = "foo bar",
    },

};

static int test_get_int(void *param)
{
    int arg = (intptr_t)param;
    int val = _pho_cfg_get_int(PHO_CFG_TEST_FIRST, PHO_CFG_TEST_LAST, arg,
                               cfg_test, -42);

    if (val == -42) {
        pho_verb("failed to get param #%d", arg);
        return -1;
    }

    pho_verb("param #%d = %d", arg, val);
    return 0;
}

int main(int argc, char **argv)
{
    int rc;
    char *test_bin, *test_dir, *test_file;

    test_env_initialize();

    run_test("Test 1: get variables before anything is set",
             test, test_env_items, PHO_TEST_FAILURE);
    run_test("Test 2: get variables before anything is set",
             test, test_file_items, PHO_TEST_FAILURE);

    rc = populate_env();
    if (rc) {
        pho_error(rc, "populate_env failed");
        exit(EXIT_FAILURE);
    }

    run_test("Test 3: get variables from env", test, test_env_items,
             PHO_TEST_SUCCESS);
    run_test("Test 4: get variables from config file (before init)", test,
             test_file_items, PHO_TEST_FAILURE);

    test_bin = strdup(argv[0]);
    test_dir = dirname(test_bin);

    /* try with bad cfg first */
    if (asprintf(&test_file, "%s/bad.cfg", test_dir) == -1)
        exit(EXIT_FAILURE);
    run_test("Test 5: test config parsing (bad syntax)",
             (pho_unit_test_t)pho_cfg_init_local, test_file, PHO_TEST_FAILURE);
    free(test_file);

    /* now the right cfg */
    if (asprintf(&test_file, "%s/test.cfg", test_dir) == -1)
        exit(EXIT_FAILURE);
    run_test("Test 6: test config parsing (right syntax)",
             (pho_unit_test_t)pho_cfg_init_local, test_file, PHO_TEST_SUCCESS);

    free(test_file);
    free(test_bin);

    run_test("Test 7: get variables from config file (after init)",
             test, test_file_items, PHO_TEST_SUCCESS);
    run_test("Test 8: get variables from env (after loading file)",
             test, test_env_items, PHO_TEST_SUCCESS);

    /* test pho_cfg_get_int() */
    run_test("Test 9: get numeric param", test_get_int,
             (void *)PHO_CFG_TEST_param0, PHO_TEST_SUCCESS);

    if (setenv("PHOBOS_TEST_param1", "120", 1)) {
        pho_error(errno, "setenv failed");
        exit(EXIT_FAILURE);
    }
    run_test("Test 10: get numeric param != 0", test_get_int,
             (void *)PHO_CFG_TEST_param1, PHO_TEST_SUCCESS);

    if (setenv("PHOBOS_TEST_param1", "-210", 1)) {
        pho_error(errno, "setenv failed");
        exit(EXIT_FAILURE);
    }
    run_test("Test 11: get numeric param < 0", test_get_int,
             (void *)PHO_CFG_TEST_param1, PHO_TEST_SUCCESS);

    if (setenv("PHOBOS_TEST_param1", "5000000000", 1)) {
        pho_error(errno, "setenv failed");
        exit(EXIT_FAILURE);
    }
    run_test("Test 12: get numeric param over int size", test_get_int,
             (void *)PHO_CFG_TEST_param1, PHO_TEST_FAILURE);

    run_test("Test 13: get non-numeric param", test_get_int,
             (void *)PHO_CFG_TEST_strparam, PHO_TEST_FAILURE);

    pho_info("CFG: All tests succeeded");
    exit(EXIT_SUCCESS);
}
