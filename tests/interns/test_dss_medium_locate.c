/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2021 CEA/DAM.
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
 * \brief  Tests for dss_medium_locate function
 */

/* phobos stuff */
#include "dss_lock.h"
#include "pho_dss.h"
#include "../test_setup.h"

/* standard stuff */

/* cmocka stuff */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

static void fill_medium_info(struct media_info *medium_info, struct pho_id id)
{
    /* fill medium_info */
    medium_info->rsc.id = id;
    medium_info->rsc.model = "dir";
    medium_info->rsc.adm_status = PHO_RSC_ADM_ST_UNLOCKED;
    medium_info->addr_type = PHO_ADDR_HASH1;
    medium_info->fs.type = PHO_FS_POSIX;
    medium_info->fs.status = PHO_FS_STATUS_USED;
    medium_info->fs.label[0] = '\0';
    medium_info->stats.nb_obj = 1;
    medium_info->stats.logc_spc_used = 7;
    medium_info->stats.phys_spc_used = 7;
    medium_info->stats.phys_spc_free = 7;
    medium_info->stats.nb_load = 7;
    medium_info->stats.nb_errors = 0;
    medium_info->stats.last_load = 7;
    medium_info->tags.tags = NULL;
    medium_info->tags.n_tags = 0;
    medium_info->flags.put = true;
    medium_info->flags.get = true;
    medium_info->flags.delete = true;
}

/**
 * dss_medium_locate returns -ENOENT on an unexisting medium
 */
static void dml_enoent(void **state)
{
    struct dss_handle *dss = (struct dss_handle *)*state;
    struct pho_id medium_id = {
        .family = PHO_RSC_TAPE,
        .name = "unexisting_medium_name",
    };
    char *hostname;
    int rc;

    rc = dss_medium_locate(dss, &medium_id, &hostname);
    assert_int_equal(rc, -ENOENT);
}

/**
 * dss_medium_locate returns -EACCES on an admin locked medium
 */
static struct pho_id admin_locked_medium = {
    .family = PHO_RSC_DIR,
    .name = "admin_locked_medium",
};

static int dml_eacces_setup(void **state)
{
    struct dss_handle *dss = (struct dss_handle *)*state;
    struct media_info medium_info;

    fill_medium_info(&medium_info, admin_locked_medium);
    medium_info.rsc.adm_status = PHO_RSC_ADM_ST_LOCKED;

    /* insert medium */
    if (dss_media_set(dss, &medium_info, 1, DSS_SET_INSERT))
        return -1;

    return 0;
}

static void dml_eacces(void **state)
{
    struct dss_handle *dss = (struct dss_handle *)*state;
    char *hostname;
    int rc;

    rc = dss_medium_locate(dss, &admin_locked_medium, &hostname);
    assert_int_equal(rc, -EACCES);
}

/**
 * dss_medium_locate returns -EPERM on a medium with get flag to false
 */
static struct pho_id false_get_medium = {
    .family = PHO_RSC_DIR,
    .name = "false_get_medium",
};

static int dml_eperm_setup(void **state)
{
    struct dss_handle *dss = (struct dss_handle *)*state;
    struct media_info medium_info;

    fill_medium_info(&medium_info, false_get_medium);
    medium_info.flags.get = false;

    /* insert medium */
    if (dss_media_set(dss, &medium_info, 1, DSS_SET_INSERT))
        return -1;

    return 0;
}

static void dml_eperm(void **state)
{
    struct dss_handle *dss = (struct dss_handle *)*state;
    char *hostname;
    int rc;

    rc = dss_medium_locate(dss, &false_get_medium, &hostname);
    assert_int_equal(rc, -EPERM);
}

/**
 * successfull dss_medium_locate on a free medium
 */
static struct pho_id free_medium = {
    .family = PHO_RSC_DIR,
    .name = "free_medium",
};

static int dml_ok_free_setup(void **state)
{
    struct dss_handle *dss = (struct dss_handle *)*state;
    struct media_info medium_info;

    fill_medium_info(&medium_info, free_medium);

    /* insert medium */
    if (dss_media_set(dss, &medium_info, 1, DSS_SET_INSERT))
        return -1;

    return 0;
}

static void dml_ok_free(void **state)
{
    struct dss_handle *dss = (struct dss_handle *)*state;
    char *hostname;
    int rc;

    rc = dss_medium_locate(dss, &free_medium, &hostname);
    assert_return_code(rc, -rc);
    assert_null(hostname);
}

/**
 * successfull dss_medium_locate on a locked medium
 */
static struct pho_id locked_medium = {
    .family = PHO_RSC_DIR,
    .name = "locked_medium",
};

#define HOSTNAME "hostname"

static int dml_ok_lock_setup(void **state)
{
    struct dss_handle *dss = (struct dss_handle *)*state;
    struct media_info medium_info;

    fill_medium_info(&medium_info, locked_medium);

    /* insert medium */
    if (dss_media_set(dss, &medium_info, 1, DSS_SET_INSERT))
        return -1;

    /* lock medium */
    if (_dss_lock(dss, DSS_MEDIA, &medium_info, 1, HOSTNAME, 12345))
        return -1;

    return 0;
}

static void dml_ok_lock(void **state)
{
    struct dss_handle *dss = (struct dss_handle *)*state;
    char *hostname = NULL;
    int rc;

    rc = dss_medium_locate(dss, &locked_medium, &hostname);
    assert_return_code(rc, -rc);
    assert_string_equal(hostname, HOSTNAME);
    free(hostname);
}

int main(void)
{
    const struct CMUnitTest dss_medium_locate_cases[] = {
        cmocka_unit_test(dml_enoent),
        cmocka_unit_test_setup(dml_eacces, dml_eacces_setup),
        cmocka_unit_test_setup(dml_eperm, dml_eperm_setup),
        cmocka_unit_test_setup(dml_ok_free, dml_ok_free_setup),
        cmocka_unit_test_setup(dml_ok_lock, dml_ok_lock_setup),
    };

    return cmocka_run_group_tests(dss_medium_locate_cases, global_setup_dss,
                                  global_teardown_dss);
}
