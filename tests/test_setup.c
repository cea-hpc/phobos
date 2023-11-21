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
 * \brief  Implementation of Cmocka unit test setup and teardown
 */
#include "test_setup.h"

/* Phobos stuff */
#include "pho_cfg.h"
#include "pho_dss.h"
#include "phobos_admin.h"

/* Standard stuff */
#include <stdlib.h> /* malloc, setenv, unsetenv */
#include <unistd.h> /* execl, exit, fork */
#include <sys/wait.h> /* wait */

static int setup_db_calls(char *action)
{
    int rc;

    rc = fork();
    if (rc == 0) {
        /* XXX: to change once DB is unified between tests */
        execl("../setup_db.sh", "setup_db.sh", action, NULL);
        if (errno == ENOENT)
            execl("../../setup_db.sh", "setup_db.sh", action, NULL);

        perror("execl");
        exit(EXIT_FAILURE);
    } else if (rc == -1) {
        perror("fork");
        return -1;
    }

    wait(&rc);
    if (rc)
        return -1;

    return 0;
}

static int setup(bool setup_db)
{
    const char *connect_string;
    int rc;

    rc = pho_cfg_init_local("../phobos.conf");
    if (rc == -ENOENT)
        rc = pho_cfg_init_local("../../phobos.conf");

    if (rc && rc != -EALREADY)
        return rc;

    rc = pho_cfg_get_val("dss", "connect_string", &connect_string);
    if (rc)
        return rc;

    setenv("PHOBOS_DSS_connect_string", connect_string, 1);

    if (setup_db)
        return setup_db_calls("setup_tables");
    return 0;
}

static int teardown(bool drop_db)
{
    int rc = drop_db ? setup_db_calls("drop_tables") : 0;

    unsetenv("PHOBOS_DSS_connect_string");

    pho_cfg_local_fini();

    return rc;
}

static int setup_dss(void **state, bool setup_db)
{
    struct dss_handle *handle;
    int rc;

    handle = malloc(sizeof(*handle));
    if (handle == NULL)
        return -1;

    rc = setup(setup_db);
    if (rc)
        return rc;

    rc = dss_init(handle);
    if (rc)
        return -1;

    *state = handle;

    return 0;
}

int global_setup_dss(void **state)
{
    return setup_dss(state, false);
}

int global_setup_dss_with_dbinit(void **state)
{
    return setup_dss(state, true);
}

static int teardown_dss(void **state, bool drop_db)
{
    if (*state != NULL) {
        dss_fini(*state);
        free(*state);
    }

    return teardown(drop_db);
}

int global_teardown_dss(void **state)
{
    return teardown_dss(state, false);
}

int global_teardown_dss_with_dbdrop(void **state)
{
    return teardown_dss(state, true);
}

static int setup_admin_no_lrs(void **state, bool setup_db)
{
    struct admin_handle *handle;
    int rc;

    handle = malloc(sizeof(*handle));
    if (handle == NULL)
        return -1;

    rc = setup(setup_db);
    if (rc)
        return rc;

    rc = phobos_admin_init(handle, false, false, NULL);
    if (rc)
        return -1;

    *state = handle;

    return 0;
}

int global_setup_admin_no_lrs(void **state)
{
    return setup_admin_no_lrs(state, false);
}

int global_setup_admin_no_lrs_with_dbinit(void **state)
{
    return setup_admin_no_lrs(state, true);
}

static int teardown_admin(void **state, bool drop_db)
{
    if (*state != NULL) {
        phobos_admin_fini(*state);
        free(*state);
    }

    return teardown(drop_db);
}

int global_teardown_admin(void **state)
{
    return teardown_admin(state, false);
}

int global_teardown_admin_with_dbdrop(void **state)
{
    return teardown_admin(state, true);
}
