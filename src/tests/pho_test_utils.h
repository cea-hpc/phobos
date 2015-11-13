/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */

/**
 * \brief  Testsuite helpers
 */

#ifndef _PHO_TEST_UTILS_H
#define _PHO_TEST_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "pho_common.h"

typedef int (*pho_unit_test_t)(void *);

enum pho_test_result {
    PHO_TEST_SUCCESS,
    PHO_TEST_FAILURE,
};

static inline void run_test(const char *descr, pho_unit_test_t test, void *hint,
                            enum pho_test_result xres)
{
    int rc;

    assert(descr != NULL);
    assert(test != NULL);

    pho_info("Starting %s...", descr);

    rc = test(hint);
    if ((xres == PHO_TEST_SUCCESS) != (rc == 0)) {
        pho_error(rc, "%s FAILED", descr);
        exit(EXIT_FAILURE);
    }

    pho_info("%s OK", descr);
}

static inline void test_env_initialize(void)
{
    if (getenv("DEBUG"))
        pho_log_level_set(PHO_LOG_DEBUG);
    else
        pho_log_level_set(PHO_LOG_VERB);
}

#endif
