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

            sec = xstrdup(item->section);
            var = xstrdup(item->variable);
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
                pho_error(rc2, "pho_cfg_get_val(%s, %s, ...): -ENODATA "
                               "expected (got %d)", item->section,
                          item->variable, rc);
                return rc2;
            }
        } else if (rc) {
            pho_error(rc, "pho_cfg_get_val(%s, %s, ...) returned error "
                          "%d", item->section, item->variable, rc);
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

static bool test_cfg_lvl(struct test_item *item, enum pho_cfg_level level)
{
    const char *val = NULL;
    int rc;

    rc = pho_cfg_get_val_from_level(item->section, item->variable, level, &val);
    if (rc != 0)
        return false;

    if (item->value != NULL && strcmp(val, item->value) != 0) {
        pho_info("unexpected value for '%s'::'%s': '%s' != '%s'",
                 item->section, item->variable, val, item->value);
        return false;
    }

    return true;
}

/** List of SCSI library configuration parameters */
enum pho_cfg_params_test {
    PHO_CFG_TEST_FIRST,

    PHO_CFG_TEST_param0,
    PHO_CFG_TEST_param1,
    PHO_CFG_TEST_strparam,
    PHO_CFG_TEST_boolparam,

    PHO_CFG_TEST_LAST,
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

    [PHO_CFG_TEST_boolparam] = {
        .section = "test",
        .name    = "boolparam",
        .value   = "true",
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

struct csv_test_data {
    char *input;
    const char * const *expected;
    size_t n;
};

static int test_get_csv(void *param)
{
    struct csv_test_data *td = param;
    const char *csv_value;
    char **values;
    int rc = 0;
    size_t n;
    size_t i;

    if (setenv("PHOBOS_CFG_TEST_csvparam", td->input, 1)) {
        pho_error(errno, "setenv failed");
        exit(EXIT_FAILURE);
    }

    rc = pho_cfg_get_val("CFG_TEST", "csvparam", &csv_value);
    if (rc) {
        pho_error(rc, "failed to get param");
        return -1;
    }

    get_val_csv(csv_value, &values, &n);

    if (n != td->n) {
        pho_info("Invalid number of items returned. Expected: %lu, got: %lu",
                 td->n, n);
        return -1;
    }

    for (i = 0; i < n; i++) {
        if (!values[i] || strcmp(td->expected[i], values[i])) {
            pho_error(-EINVAL, "Invalid value. Expected: %s, got: %s",
                      td->expected[i], values[i]);
            rc = -1;
        }

        free(values[i]);
    }
    free(values);

    return rc;
}

static int test_get_bool(void *param)
{
    bool res;

    res = PHO_CFG_GET_BOOL(cfg_test, PHO_CFG_TEST, boolparam, false);
    if (!res) {
        pho_error(0, "Default boolean should exist and be true");
        return -1;
    }

    if (setenv("PHOBOS_TEST_boolparam", "false", 1)) {
        pho_error(errno, "setenv failed");
        exit(EXIT_FAILURE);
    }

    res = PHO_CFG_GET_BOOL(cfg_test, PHO_CFG_TEST, boolparam, true);
    if (res) {
        pho_error(0, "Env should overwrite boolean to false");
        return -1;
    }

    if (setenv("PHOBOS_TEST_boolparam", "invalid", 1)) {
        pho_error(errno, "setenv failed");
        exit(EXIT_FAILURE);
    }

    res = PHO_CFG_GET_BOOL(cfg_test, PHO_CFG_TEST, boolparam, false);
    if (res) {
        pho_error(0, "Invalid value should default to false");
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    static const char * const expected_items[] = {
        "param1",
        "param2",
        "param3",
    };
    struct csv_test_data td;
    int rc;

    test_env_initialize();

    pho_run_test("Test 1: get variables before anything is set",
             test, test_env_items, PHO_TEST_FAILURE);
    pho_run_test("Test 2: get variables before anything is set",
             test, test_file_items, PHO_TEST_FAILURE);

    if (test_cfg_lvl(&test_env_items[1], PHO_CFG_LEVEL_PROCESS)) {
        pho_info("test_cfg_lvl in process before anything is set should have "
                 "failed");
        exit(EXIT_FAILURE);
    }

    if (test_cfg_lvl(&test_file_items[1], PHO_CFG_LEVEL_LOCAL)) {
        pho_info("test_cfg_lvl in local before anything is set should have "
                 "failed");
        exit(EXIT_FAILURE);
    }

    if (test_cfg_lvl(&test_env_items[1], PHO_CFG_LEVEL_GLOBAL)) {
        pho_info("test_cfg_lvl in global before anything is set should have "
                 "failed");
        exit(EXIT_FAILURE);
    }

    rc = populate_env();
    if (rc) {
        pho_error(rc, "populate_env failed");
        exit(EXIT_FAILURE);
    }

    pho_run_test("Test 3: get variables from env", test, test_env_items,
             PHO_TEST_SUCCESS);

    if (!test_cfg_lvl(&test_env_items[1], PHO_CFG_LEVEL_PROCESS)) {
        pho_info("valid test_cfg_lvl test in process level should have "
                 "suceeded");
        exit(EXIT_FAILURE);
    }

    pho_run_test("Test 4: get variables from config file (before init)", test,
             test_file_items, PHO_TEST_FAILURE);


    /* try with bad cfg first */
    pho_run_test("Test 5: test config parsing (bad syntax)",
             (pho_unit_test_t)pho_cfg_init_local, "bad.cfg", PHO_TEST_FAILURE);
    pho_run_test("Test 6: test config parsing (right syntax)",
             (pho_unit_test_t)pho_cfg_init_local, "test.cfg", PHO_TEST_SUCCESS);

    pho_run_test("Test 7: get variables from config file (after init)",
             test, test_file_items, PHO_TEST_SUCCESS);
    pho_run_test("Test 8: get variables from env (after loading file)",
             test, test_env_items, PHO_TEST_SUCCESS);

    if (!test_cfg_lvl(&test_file_items[1], PHO_CFG_LEVEL_LOCAL)) {
        pho_info("valid test_cfg_lvl in local level should have suceeded");
        exit(EXIT_FAILURE);
    }

    /* test pho_cfg_get_int() */
    pho_run_test("Test 9: get numeric param", test_get_int,
             (void *)PHO_CFG_TEST_param0, PHO_TEST_SUCCESS);

    if (setenv("PHOBOS_TEST_param1", "120", 1)) {
        pho_error(errno, "setenv failed");
        exit(EXIT_FAILURE);
    }
    pho_run_test("Test 10: get numeric param != 0", test_get_int,
             (void *)PHO_CFG_TEST_param1, PHO_TEST_SUCCESS);

    if (setenv("PHOBOS_TEST_param1", "-210", 1)) {
        pho_error(errno, "setenv failed");
        exit(EXIT_FAILURE);
    }
    pho_run_test("Test 11: get numeric param < 0", test_get_int,
             (void *)PHO_CFG_TEST_param1, PHO_TEST_SUCCESS);

    if (setenv("PHOBOS_TEST_param1", "5000000000", 1)) {
        pho_error(errno, "setenv failed");
        exit(EXIT_FAILURE);
    }
    pho_run_test("Test 12: get numeric param over int size", test_get_int,
             (void *)PHO_CFG_TEST_param1, PHO_TEST_FAILURE);

    pho_run_test("Test 13: get non-numeric param", test_get_int,
             (void *)PHO_CFG_TEST_strparam, PHO_TEST_FAILURE);

    td.input = "param1";
    td.expected = expected_items;
    td.n = 1;
    pho_run_test("Test 14: get CSV param", test_get_csv,
             (void *)&td, PHO_TEST_SUCCESS);

    td.input = "param1,";
    td.expected = expected_items;
    td.n = 1;
    pho_run_test("Test 14: get CSV param", test_get_csv,
             (void *)&td, PHO_TEST_SUCCESS);

    td.input = "param1,param2";
    td.expected = expected_items;
    td.n = 2;
    pho_run_test("Test 14: get CSV param", test_get_csv,
             (void *)&td, PHO_TEST_SUCCESS);

    td.input = "param1,param2,";
    td.expected = expected_items;
    td.n = 2;
    pho_run_test("Test 14: get CSV param", test_get_csv,
             (void *)&td, PHO_TEST_SUCCESS);

    td.input = "param1,param2,param3";
    td.expected = expected_items;
    td.n = 3;
    pho_run_test("Test 14: get CSV param", test_get_csv,
             (void *)&td, PHO_TEST_SUCCESS);

    td.input = "param1,param2,param3,";
    td.expected = expected_items;
    td.n = 3;
    pho_run_test("Test 14: get CSV param", test_get_csv,
             (void *)&td, PHO_TEST_SUCCESS);

    td.input = "";
    td.expected = expected_items;
    td.n = 0;
    pho_run_test("Test 14: get CSV param", test_get_csv,
             (void *)&td, PHO_TEST_SUCCESS);

    td.input = ",";
    td.expected = expected_items;
    td.n = 0;
    pho_run_test("Test 14: get CSV param", test_get_csv,
             (void *)&td, PHO_TEST_SUCCESS);

    pho_run_test("Test 15: get boolean param", test_get_bool, NULL,
                 PHO_TEST_SUCCESS);

    pho_info("CFG: All tests succeeded");
    exit(EXIT_SUCCESS);
}
