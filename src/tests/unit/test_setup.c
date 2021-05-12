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
 * \brief  Implementation of Cmocka unit test setup and teardown
 */
#include "test_setup.h"

/* Phobos stuff */
#include "pho_dss.h"

/* Standard stuff */
#include <stdlib.h> /* malloc, setenv, unsetenv */
#include <unistd.h> /* execl, exit, fork */
#include <sys/wait.h> /* wait */

int global_setup_dss(void **state)
{
    struct dss_handle *handle;
    int rc;

    handle = malloc(sizeof(*handle));
    if (handle == NULL)
        return -1;

    setenv("PHOBOS_DSS_connect_string", "dbname=phobos host=localhost "
                                        "user=phobos password=phobos", 1);

    if (!fork()) {
        rc = execl("../setup_db.sh", "setup_db.sh", "setup_tables", NULL);
        if (rc)
            exit(EXIT_FAILURE);
    }

    wait(&rc);
    if (rc)
        return -1;

    rc = dss_init(handle);
    if (rc)
        return -1;

    *state = handle;

    return 0;
}

int global_teardown_dss(void **state)
{
    int rc;

    if (*state != NULL) {
        dss_fini(*state);
        free(*state);
    }

    if (!fork()) {
        rc = execl("../setup_db.sh", "setup_db.sh", "drop_tables", NULL);
        if (rc)
            exit(EXIT_FAILURE);
    }

    wait(&rc);
    if (rc)
        return -1;

    unsetenv("PHOBOS_DSS_connect_string");

    return 0;
}
