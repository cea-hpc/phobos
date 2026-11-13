/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2025 CEA/DAM.
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
 * \brief  Tests for phobos_admin_medium_locate function
 */

/* phobos stuff */
#include "dss_lock.h"
#include "phobos_admin.h"
#include "pho_test_utils.h"
#include "test_setup.h"

/* cmocka stuff */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

/**
 * phobos_admin_medium_locate returns -ENOENT on an unexisting medium
 */
static void paml_enoent(void **state)
{
    struct admin_handle *adm = (struct admin_handle *)*state;
    struct pho_id medium_id = {
        .family = PHO_RSC_TAPE,
        .name = "unexisting_medium_name",
        .library = "legacy",
    };
    char *hostname;
    int rc;

    (void) state;

    rc = phobos_admin_medium_locate(adm, &medium_id, &hostname);
    assert_int_equal(rc, -ENOENT);
}

/**
 * phobos_admin_medium_locate returns -EACCES on an admin locked medium
 */
static struct pho_id admin_locked_medium = {
    .family = PHO_RSC_TAPE,
    .name = "admin_locked_medium",
    .library = "legacy",
};

static int paml_eacces_setup(void **state)
{
    struct admin_handle *adm = (struct admin_handle *)*state;
    struct media_info medium_info;

    fill_medium_info(&medium_info, admin_locked_medium);
    medium_info.rsc.adm_status = PHO_RSC_ADM_ST_LOCKED;

    /* insert medium */
    if (dss_media_insert(&adm->dss, &medium_info, 1))
        return -1;

    return 0;
}

static void paml_eacces(void **state)
{
    struct admin_handle *adm = (struct admin_handle *)*state;
    char *hostname;
    int rc;

    (void) state;

    rc = phobos_admin_medium_locate(adm, &admin_locked_medium, &hostname);
    assert_int_equal(rc, -EACCES);
}

/**
 * phobos_admin_medium_locate returns -EPERM on a medium with get flag to false
 */
static struct pho_id false_get_medium = {
    .family = PHO_RSC_TAPE,
    .name = "false_get_medium",
    .library = "legacy",
};

static int paml_eperm_setup(void **state)
{
    struct admin_handle *adm = (struct admin_handle *)*state;
    struct media_info medium_info;

    fill_medium_info(&medium_info, false_get_medium);
    medium_info.flags.get = false;

    /* insert medium */
    if (dss_media_insert(&adm->dss, &medium_info, 1))
        return -1;

    return 0;
}

static void paml_eperm(void **state)
{
    struct admin_handle *adm = (struct admin_handle *)*state;
    char *hostname;
    int rc;

    (void) state;

    rc = phobos_admin_medium_locate(adm, &false_get_medium, &hostname);
    assert_int_equal(rc, -EPERM);
}

/**
 * successfull phobos_admin_medium_locate on a free medium
 */
static struct pho_id dir_free_medium = {
    .family = PHO_RSC_DIR,
    .name = "dir_free_medium",
    .library = "legacy",
};

static struct pho_id tape_free_medium = {
    .family = PHO_RSC_TAPE,
    .name = "tape_free_medium",
    .library = "legacy",
};

static int paml_ok_free_setup(void **state)
{
    struct admin_handle *adm = (struct admin_handle *)*state;
    struct media_info dir_medium_info;
    struct media_info tape_medium_info;

    fill_medium_info(&dir_medium_info, dir_free_medium);
    fill_medium_info(&tape_medium_info, tape_free_medium);

    /* insert medium */
    if (dss_media_insert(&adm->dss, &dir_medium_info, 1))
        return -1;

    if (dss_media_insert(&adm->dss, &tape_medium_info, 1))
        return -1;

    return 0;
}

static void paml_ok_free(void **state)
{
    struct admin_handle *adm = (struct admin_handle *)*state;
    char *hostname;
    int rc;

    (void) state;

    /* -ENOENT on free dir */
    rc = phobos_admin_medium_locate(adm, &dir_free_medium, &hostname);
    assert_int_equal(rc, -ENOENT);

    /* NULL on free tape */
    rc = phobos_admin_medium_locate(adm, &tape_free_medium, &hostname);
    assert_return_code(rc, -rc);
    assert_null(hostname);
}

/**
 * successfull phobos_admin_medium_locate on a locked medium
 */
static struct pho_id locked_medium = {
    .family = PHO_RSC_TAPE,
    .name = "locked_medium",
    .library = "legacy",
};

#define HOSTNAME "hostname"

static int paml_ok_lock_setup(void **state)
{
    struct admin_handle *adm = (struct admin_handle *)*state;
    struct media_info medium_info;

    fill_medium_info(&medium_info, locked_medium);

    /* insert medium */
    if (dss_media_insert(&adm->dss, &medium_info, 1))
        return -1;

    /* lock medium */
    if (_dss_lock(&adm->dss, DSS_MEDIA, &medium_info, 1, HOSTNAME, 12345,
                  false, false))
        return -1;

    return 0;
}

static void paml_ok_lock(void **state)
{
    struct admin_handle *adm = (struct admin_handle *)*state;
    char *hostname;
    int rc;

    (void) state;

    rc = phobos_admin_medium_locate(adm, &locked_medium, &hostname);
    assert_return_code(rc, -rc);
    assert_string_equal(hostname, HOSTNAME);
    free(hostname);
}

/**
 * phobos_admin_media_update returns -EINVAL on a negative ranking: the
 * negative values are reserved to tell that no ranking was given
 */
static struct pho_id negative_ranking_medium = {
    .family = PHO_RSC_TAPE,
    .name = "negative_ranking_medium",
    .library = "legacy",
};

static void paml_update_negative_ranking(void **state)
{
    struct admin_handle *adm = (struct admin_handle *)*state;
    struct media_info medium_info;

    (void) state;

    fill_medium_info(&medium_info, negative_ranking_medium);
    medium_info.ranking = -1;

    assert_int_equal(phobos_admin_media_update(adm, &medium_info,
                                               &medium_info, 1, RANKING),
                     -EINVAL);
}

int main(void)
{
    const struct CMUnitTest phobos_admin_medium_locate_cases[] = {
        cmocka_unit_test(paml_enoent),
        cmocka_unit_test_setup(paml_eacces, paml_eacces_setup),
        cmocka_unit_test_setup(paml_eperm, paml_eperm_setup),
        cmocka_unit_test_setup(paml_ok_free, paml_ok_free_setup),
        cmocka_unit_test_setup(paml_ok_lock, paml_ok_lock_setup),
        cmocka_unit_test(paml_update_negative_ranking),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(phobos_admin_medium_locate_cases,
                                  global_setup_admin_no_lrs_with_dbinit,
                                  global_teardown_admin_with_dbdrop);
}
