/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2015 CEA/DAM. All Rights Reserved.
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
            char *sec;
            char *var;
            char *stmt;

            sec = strdup(item->section);
            var = strdup(item->variable);
            upperstr(sec);
            lowerstr(var);
            asprintf(&stmt, "PHOBOS_%s_%s=%s", sec, var, item->value);

            rc = putenv(stmt);
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

        rc = pho_cfg_get(item->section, item->variable, &val);

        /* if value is NULL: -ENOATTR is expected */
        if (item->value == NULL) {
            if (rc != -ENOATTR) {
                fprintf(stderr, "pho_cfg_get(%s, %s, ...): -ENOATTR expected"
                        " (got %d)\n", item->section, item->variable, rc);
                return rc ? rc : -EINVAL;
            }
            /* else: OK */
        } else if (rc) {
            fprintf(stderr, "pho_cfg_get(%s, %s, ...) returned error %d\n",
                    item->section, item->variable, rc);
            return rc;
        } else {
            if (strcmp(val, item->value)) {
                fprintf(stderr, "unexpected value for '%s'::'%s':"
                        " '%s' != '%s'\n", item->section, item->variable, val,
                        item->value);
                return -EINVAL;
            }
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    int rc;
    char *test_bin, *test_dir, *test_file;

    pho_log_level_set(PHO_LOG_DEBUG);

    run_test("Test 1: get variables before anything is set",
             test, test_env_items, PHO_TEST_FAILURE);
    run_test("Test 2: get variables before anything is set",
             test, test_file_items, PHO_TEST_FAILURE);

    rc = populate_env();
    if (rc)
        return rc;

    run_test("Test 3: get variables from env", test, test_env_items,
             PHO_TEST_SUCCESS);
    run_test("Test 4: get variables from config file (before init)", test,
             test_file_items, PHO_TEST_FAILURE);

    test_bin = strdup(argv[0]);
    test_dir = dirname(test_bin);

    /* try with bad cfg first */
    asprintf(&test_file, "%s/bad.cfg", test_dir);
    run_test("Test 5: test config parsing (bad syntax)",
             (pho_unit_test_t)pho_cfg_init_local, test_file, PHO_TEST_FAILURE);
    free(test_file);

    /* now the right cfg */
    asprintf(&test_file, "%s/test.cfg", test_dir);
    run_test("Test 6: test config parsing (right syntax)",
             (pho_unit_test_t)pho_cfg_init_local, test_file, PHO_TEST_SUCCESS);

    free(test_file);
    free(test_bin);

    run_test("Test 7: get variables from config file (after init)",
             test, test_file_items, PHO_TEST_SUCCESS);
    run_test("Test 8: get variables from env (after loading file)",
             test, test_env_items, PHO_TEST_SUCCESS);

    printf("CFG: All tests succeeded\n");
    return 0;
}
