/*
 * All rights reserved (c) 2014-2022 CEA/DAM.
 *
 * This file is part of Phobos.
 *
 * Phobos is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * Phobos is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Phobos. If not, see <http://www.gnu.org/licenses/>.
 */
/**
 * \brief Test lock clean API call
 */

#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "test_setup.h"
#include "phobos_admin.h"

#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))

static void lc_test_errors(void **state)
{
    struct admin_handle *adm = (struct admin_handle *)*state;
    int rc;

    /* Global without --force attribute  */
    rc = phobos_admin_clean_locks(adm, true, false, DSS_NONE,
                                  PHO_RSC_NONE, NULL, 0);
    assert_int_equal(rc, -EPERM);

    /* Invalid type parameter */
    rc = phobos_admin_clean_locks(adm, false, false, -5,
                                  PHO_RSC_NONE, NULL, 0);
    assert_int_equal(rc, -EINVAL);

    /* Invalid family parameter */
    rc = phobos_admin_clean_locks(adm, false, false, DSS_NONE,
                                  -5, NULL, 0);
    assert_int_equal(rc, -EINVAL);

    /* No type given with valid family parameter */
    rc = phobos_admin_clean_locks(adm, false, false, DSS_NONE,
                                  PHO_RSC_DIR, NULL, 0);
    assert_int_equal(rc, -EINVAL);

    /* Object type given with valid family parameter  */
    rc = phobos_admin_clean_locks(adm, false, false, DSS_OBJECT,
                                  PHO_RSC_DIR, NULL, 0);
    assert_int_equal(rc, -EINVAL);
}

static void lc_test_local_daemon_on(void **state)
{
    struct admin_handle *adm = (struct admin_handle *)*state;
    int rc;

    adm->daemon_is_online = true;

    /* Using phobos command without force attribute */
    rc = phobos_admin_clean_locks(adm, false, false, DSS_NONE,
                                  PHO_RSC_NONE, NULL, 0);
    assert_int_equal(rc, -EPERM);

    /* Using phobos command with force attribute when deamon is on */
    rc = phobos_admin_clean_locks(adm, false, true, DSS_NONE,
                                  PHO_RSC_NONE, NULL, 0);
    assert_int_equal(rc, -rc);
}

/* TODO: Verify if the database is empty */
static void lc_test_clean_all(void **state)
{
    struct admin_handle *adm = (struct admin_handle *)*state;
    int rc;

    rc = phobos_admin_clean_locks(adm, false, true, DSS_NONE,
                                  PHO_RSC_NONE, NULL, 0);
    assert_int_equal(rc, -rc);

    rc = phobos_admin_clean_locks(adm, true, true, DSS_NONE,
                                  PHO_RSC_NONE, NULL, 0);
    assert_int_equal(rc, -rc);
}

/* TODO: verify if the right locks are cleaned */
static void lc_test_ids_param(void **state)
{
    struct admin_handle *adm = (struct admin_handle *)*state;
    char *ids_array[2] = {"3", "3"};
    int rc;

    /* remove object with id '3' on localhost */
    rc = phobos_admin_clean_locks(adm, false, false, DSS_OBJECT,
                                  PHO_RSC_NONE, ids_array, 1);
    assert_int_equal(rc, -rc);

    /* globally remove media_update of id '2' and '3' */
    ids_array[0] = "2";
    rc = phobos_admin_clean_locks(adm, true, true, DSS_MEDIA_UPDATE_LOCK,
                                  PHO_RSC_NONE, ids_array, 2);
    assert_int_equal(rc, -rc);

    /* clean an element of id '2' with all parameters */
    rc = phobos_admin_clean_locks(adm, true, true, DSS_DEVICE,
                                  PHO_RSC_DIR, ids_array, 1);
    assert_int_equal(rc, -rc);

    /* clean all elements with id '1' */
    ids_array[0] = "1";
    rc = phobos_admin_clean_locks(adm, true, true, DSS_NONE,
                                  PHO_RSC_NONE, ids_array, 1);
    assert_int_equal(rc, -rc);
}

/* TODO: verify if the right family locks are cleaned */
static void lc_test_family_param(void **state)
{
    struct admin_handle *adm = (struct admin_handle *)*state;
    int rc;

    rc = phobos_admin_clean_locks(adm, true, true, DSS_MEDIA,
                                  PHO_RSC_DIR, NULL, 0);
    assert_int_equal(rc, -rc);

    rc = phobos_admin_clean_locks(adm, false, false,
                                  DSS_MEDIA_UPDATE_LOCK,
                                  PHO_RSC_DISK, NULL, 0);
    assert_int_equal(rc, -rc);

    rc = phobos_admin_clean_locks(adm, false, false, DSS_DEVICE,
                                  PHO_RSC_TAPE, NULL, 0);
    assert_int_equal(rc, -rc);
}

/* TODO: verify if the right type locks are cleaned */
static void lc_test_type_param(void **state)
{
    struct admin_handle *adm = (struct admin_handle *)*state;
    int rc;

    rc = phobos_admin_clean_locks(adm, false, false, DSS_DEVICE,
                                  PHO_RSC_NONE, NULL, 0);
    assert_int_equal(rc, -rc);

    rc = phobos_admin_clean_locks(adm, false, false,
                                  DSS_MEDIA_UPDATE_LOCK,
                                  PHO_RSC_NONE, NULL, 0);
    assert_int_equal(rc, -rc);

    rc = phobos_admin_clean_locks(adm, false, false, DSS_MEDIA,
                                  PHO_RSC_NONE, NULL, 0);
    assert_int_equal(rc, -rc);

    rc = phobos_admin_clean_locks(adm, false, false, DSS_OBJECT,
                                  PHO_RSC_NONE, NULL, 0);
    assert_int_equal(rc, -rc);
}

int main(void)
{
    const struct CMUnitTest lock_clean_test_params[] = {
        cmocka_unit_test(lc_test_ids_param),
        cmocka_unit_test(lc_test_family_param),
        cmocka_unit_test(lc_test_type_param),
        cmocka_unit_test(lc_test_local_daemon_on),
    };

    const struct CMUnitTest lock_clean_test_errors[] = {
        cmocka_unit_test(lc_test_errors),
        cmocka_unit_test(lc_test_local_daemon_on),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(lock_clean_test_errors,
                                  global_setup_admin_no_lrs,
                                  global_teardown_admin) +
           cmocka_run_group_tests(lock_clean_test_params,
                                  global_setup_admin_no_lrs,
                                  global_teardown_admin);
}
