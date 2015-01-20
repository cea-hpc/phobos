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


typedef int (*pho_unit_test_t)(void *);

enum pho_test_result {
    PHO_TEST_SUCCESS,
    PHO_TEST_FAILURE,
};

static inline int run_test(const char *descr, pho_unit_test_t test, void *hint,
                           enum pho_test_result xres)
{
    int rc;

    assert(descr != NULL);
    assert(test != NULL);

    printf("%s\n", descr);
    fflush(stdout);

    rc = test(hint);
    if ((xres == PHO_TEST_SUCCESS) != (rc == 0)) {
        printf("FAILED (%d: %s)\n", rc, strerror(-rc));
        exit(EXIT_FAILURE);
    }

    printf("OK\n");
    return 0;
}

#endif
