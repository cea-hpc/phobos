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
