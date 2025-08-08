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
#include "tlc_cfg.h"
#include "tlc_library.h"

/* Standard stuff */
#include <stdlib.h> /* malloc, setenv, unsetenv */
#include <unistd.h> /* execl, exit, fork */
#include <sys/wait.h> /* wait */

static int setup_db_calls(char *action)
{
    int rc;

    ENTRY;

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

    ENTRY;

    rc = pho_cfg_init_local("../phobos.conf");
    if (rc == -ENOENT)
        rc = pho_cfg_init_local("../../phobos.conf");

    if (rc && rc != -EALREADY) {
        pho_error(rc, "pho_cfg_init_local failed");
        return rc;
    }

    rc = pho_cfg_get_val("dss", "connect_string", &connect_string);
    if (rc) {
        pho_error(rc, "get dss:connect_string failed");
        return rc;
    }

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

    handle = xmalloc(sizeof(*handle));

    rc = setup(setup_db);
    if (rc)
        return rc;

    rc = dss_init(handle);
    if (rc)
        return -1;

    *state = handle;

    return 0;
}

static int setup_dss_and_tlc_lib(void **state, bool setup_db)
{
    struct dss_and_tlc_lib *dss_and_tlc_lib;
    json_t *json_message;
    int rc;

    ENTRY;

    dss_and_tlc_lib = xcalloc(1, sizeof(*dss_and_tlc_lib));

    rc = setup(setup_db);
    if (rc) {
        pho_error(rc, "setup failed");
        goto free_libs;
    }

    rc = dss_init(&dss_and_tlc_lib->dss);
    if (rc) {
        pho_error(rc, "dss_init failed");
        goto free_libs;
    }

    strcpy(dss_and_tlc_lib->tlc_lib.name, "legacy");

    rc = tlc_lib_device_from_cfg("legacy",
                                 &dss_and_tlc_lib->tlc_lib.lib_devices,
                                 &dss_and_tlc_lib->tlc_lib.nb_lib_device);
    if (rc) {
        pho_error(rc, "Failed to get lib_device configuration");
        goto fini;
    }

    rc = tlc_library_open(&dss_and_tlc_lib->tlc_lib, &json_message);
    if (rc) {
        pho_error(rc, "Failed to open TLC library");
        goto fini;
    }

    pho_debug("Initialization successful");
    *state = dss_and_tlc_lib;
    return 0;
fini:
    dss_fini(&dss_and_tlc_lib->dss);
free_libs:
    free(dss_and_tlc_lib);
    return rc;
}

int global_setup_dss(void **state)
{
    return setup_dss(state, false);
}

int global_setup_dss_with_dbinit(void **state)
{
    return setup_dss(state, true);
}

int global_setup_dss_and_tlc_lib_with_dbinit(void **state)
{
    return setup_dss_and_tlc_lib(state, true);
}

static int teardown_dss(void **state, bool drop_db)
{
    if (*state != NULL) {
        dss_fini(*state);
        free(*state);
    }

    return teardown(drop_db);
}

static int teardown_dss_and_tlc_lib(void **state, bool drop_db)
{
    struct dss_and_tlc_lib *dss_and_tlc_lib = (struct dss_and_tlc_lib *)*state;

    if (*state != NULL) {
        tlc_library_close(&dss_and_tlc_lib->tlc_lib);
        dss_fini(&dss_and_tlc_lib->dss);
        free(*state);
        *state = NULL;
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

int global_teardown_dss_and_tlc_lib_with_dbdrop(void **state)
{
    return teardown_dss_and_tlc_lib(state, true);
}

static int setup_admin_no_lrs(void **state, bool setup_db)
{
    struct admin_handle *handle;
    int rc;

    handle = xmalloc(sizeof(*handle));

    rc = setup(setup_db);
    if (rc)
        return rc;

    rc = phobos_admin_init(handle, false, NULL);
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
