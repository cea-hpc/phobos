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
 * \brief  Tests for phobos_locate store API function
 */

/* phobos stuff */
#include "phobos_store.h"
#include "pho_common.h" /* get_hostname */
#include "pho_dss.h"

/* standard stuff */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/* cmocka stuff */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

static struct phobos_locate_state {
    struct dss_handle   *dss;
    enum rsc_family     rsc_family;
    struct object_info  *objs;
    int n_objs;
} phobos_locate_state;

/* global setup connect to the DSS */
static int global_setup(void **state)
{
    int rc;

    phobos_locate_state.dss = malloc(sizeof(*phobos_locate_state.dss));
    if (!phobos_locate_state.dss)
        return -1;

    setenv("PHOBOS_DSS_connect_string", "dbname=phobos host=localhost "
                                        "user=phobos password=phobos", 1);

    rc = dss_init(phobos_locate_state.dss);
    if (rc) {
        free(phobos_locate_state.dss);
        return -1;
    }

    *state = &phobos_locate_state;
    return 0;
}

static int global_teardown(void **state)
{
    struct phobos_locate_state *pl_state = (struct phobos_locate_state *)*state;

    if (pl_state) {
        dss_fini(pl_state->dss);
        free(pl_state->dss);
    }

    unsetenv("PHOBOS_DSS_connect_string");

    return 0;
}

/*********************/
/* pl: phobos_locate */
/*********************/
static char *oid_pl = "oid_pl";
#define BAD_OID "bad_oid_to_locate"
#define BAD_UUID "bad_uuid_to_locate"
#define HOSTNAME "hostname"
static int pl_setup(void **state)
{
    struct phobos_locate_state *pl_state = (struct phobos_locate_state *)*state;
    struct pho_xfer_desc xfer;
    int rc;

    /* put an object to locate */
    xfer.xd_fd = open("/etc/hosts", O_RDONLY);
    if (xfer.xd_fd == -1)
        return -1;
    xfer.xd_objid = oid_pl;
    xfer.xd_attrs.attr_set = NULL;
    xfer.xd_params.put.family = pl_state->rsc_family;

    rc = phobos_put(&xfer, 1, NULL, NULL);
    close(xfer.xd_fd);
    assert_return_code(rc, -rc);

    /* get object info */
    rc = phobos_store_object_list((const char **)&oid_pl, 1,
                                  false, NULL, 0, false, &pl_state->objs,
                                  &pl_state->n_objs);
    assert_return_code(rc, -rc);
    assert_int_equal(pl_state->n_objs, 1);
    assert_string_equal(pl_state->objs[0].oid, oid_pl);

    return 0;
}

static void pl_enoent(void **state)
{
    struct phobos_locate_state *pl_state = (struct phobos_locate_state *)*state;
    char *hostname;
    int rc;

    /* bad OID */
    rc = phobos_locate(BAD_OID, NULL, 0, &hostname);
    assert_int_equal(rc, -ENOENT);

    /* bad UUID */
    rc = phobos_locate(NULL, BAD_UUID, 0, &hostname);
    assert_int_equal(rc, -ENOENT);

    /* OID, bad UUID */
    rc = phobos_locate(pl_state->objs[0].oid, BAD_UUID, 0, &hostname);
    assert_int_equal(rc, -ENOENT);

    /* bad OID, UUID */
    rc = phobos_locate(BAD_OID, pl_state->objs[0].uuid, 0, &hostname);
    assert_int_equal(rc, -ENOENT);

    /* OID, bad version */
    rc = phobos_locate(pl_state->objs[0].oid, NULL,
                       pl_state->objs[0].version + 1, &hostname);
    assert_int_equal(rc, -ENOENT);

    /* UUID, bad version */
    rc = phobos_locate(NULL, pl_state->objs[0].uuid,
                       pl_state->objs[0].version + 1, &hostname);
    assert_int_equal(rc, -ENOENT);

    /* OID, UUID, bad version */
    rc = phobos_locate(pl_state->objs[0].oid, pl_state->objs[0].uuid,
                       pl_state->objs[0].version + 1, &hostname);
    assert_int_equal(rc, -ENOENT);
}

static void pl_hostname(const char *expected_hostname, void **state,
                        bool alive)
{
    struct phobos_locate_state *pl_state = (struct phobos_locate_state *)*state;
    char *hostname;
    int rc;

    /* oid */
    if (alive) {
        rc = phobos_locate(pl_state->objs[0].oid, NULL, 0, &hostname);
        assert_return_code(rc, -rc);
        assert_string_equal(expected_hostname, hostname);
        free(hostname);
    }

    /* oid, version */
    rc = phobos_locate(pl_state->objs[0].oid, NULL, pl_state->objs[0].version,
                       &hostname);
    assert_return_code(rc, -rc);
    assert_string_equal(expected_hostname, hostname);
    free(hostname);

    /* uuid */
    rc = phobos_locate(NULL, pl_state->objs[0].uuid, 0, &hostname);
    assert_return_code(rc, -rc);
    assert_string_equal(expected_hostname, hostname);
    free(hostname);

    /* uuid, version */
    rc = phobos_locate(NULL, pl_state->objs[0].uuid, pl_state->objs[0].version,
                       &hostname);
    assert_return_code(rc, -rc);
    assert_string_equal(expected_hostname, hostname);
    free(hostname);

    /* oid, uuid */
    rc = phobos_locate(pl_state->objs[0].oid, pl_state->objs[0].uuid, 0,
                       &hostname);
    assert_return_code(rc, -rc);
    assert_string_equal(expected_hostname, hostname);
    free(hostname);

    /* oid, uuid, version */
    rc = phobos_locate(pl_state->objs[0].oid, pl_state->objs[0].uuid,
                       pl_state->objs[0].version, &hostname);
    assert_return_code(rc, -rc);
    assert_string_equal(expected_hostname, hostname);
    free(hostname);
}

static void pl(void **state)
{
    struct phobos_locate_state *pl_state = (struct phobos_locate_state *)*state;
    struct object_info *obj = pl_state->objs;
    const char *myself_hostname = NULL;
    struct layout_info *layout;
    struct media_info *medium;
    struct pho_xfer_desc xfer;
    struct dss_filter filter;
    struct pho_id medium_id;
    char *hostname;
    int cnt;
    int rc;

    rc = phobos_locate(NULL, NULL, 1, &hostname);
    assert_int_equal(rc, -EINVAL);

    /* check ENOENT from object table */
    pl_enoent(state);

    /* locate local hostname in object table */
    myself_hostname = get_hostname();
    assert_non_null(myself_hostname);
    pl_hostname(myself_hostname, state, true);

    /* lock media from other owner */
    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                              "{\"DSS::EXT::oid\": \"%s\"}, "
                              "{\"DSS::EXT::uuid\": \"%s\"}, "
                              "{\"DSS::EXT::version\": \"%d\"}"
                          "]}",
                          obj->oid, obj->uuid, obj->version);
    assert_return_code(rc, -rc);
    rc = dss_layout_get(pl_state->dss, &filter, &layout, &cnt);
    dss_filter_free(&filter);
    assert_return_code(rc, -rc);
    assert_int_equal(cnt, 1);
    medium_id = layout->extents->media;
    dss_res_free(layout, cnt);
    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          "{\"DSS::MDA::family\": \"%s\"}, "
                          "{\"DSS::MDA::id\": \"%s\"}"
                          "]}",
                          rsc_family2str(medium_id.family),
                          medium_id.name);
    assert_return_code(rc, -rc);
    rc = dss_media_get(pl_state->dss, &filter, &medium, &cnt);
    dss_filter_free(&filter);
    assert_return_code(rc, -rc);
    assert_int_equal(cnt, 1);
    /* simulate lock on medium */
    rc = dss_lock(pl_state->dss, DSS_MEDIA, medium, cnt,
                  HOSTNAME ":lock_owner");
    assert_return_code(rc, -rc);
    /* locate with lock */
    pl_enoent(state);
    pl_hostname(HOSTNAME, state, true);


    /* move object to deprecated table */
    xfer.xd_objid = obj->oid;
    rc = phobos_delete(&xfer, 1);
    assert_return_code(rc, -rc);

    /* check ENOENT from deprecated table */
    pl_enoent(state);

    /* locate with lock in deprecated table */
    pl_hostname(HOSTNAME, state, false);
    /* free lock on medium */
    rc = dss_unlock(pl_state->dss, DSS_MEDIA, medium, cnt, NULL);
    dss_res_free(medium, cnt);
    assert_return_code(rc, -rc);
    /* locate without any lock in deprecated table */
    pl_hostname(myself_hostname, state, false);
}

static int pl_teardown(void **state)
{
    struct phobos_locate_state *pl_state = (struct phobos_locate_state *)*state;

    /* TODO: fully remove objects when phobos_remove will be ready to use */
    phobos_store_object_list_free(pl_state->objs, pl_state->n_objs);
    pl_state->objs = NULL;
    pl_state->n_objs = 0;
    return 0;
}

#define NB_ARGS 1
static const char *usage = "Take one argument the rsc_family to test, "
                           "\"dir\" or \"tape\"\n";
int main(int argc, char **argv)
{
    int family;

    if (argc != NB_ARGS + 1) {
        fprintf(stderr, "%s", usage);
        exit(EXIT_FAILURE);
    }

    family = str2rsc_family(argv[1]);

    switch (family) {
    case PHO_RSC_TAPE:
    case PHO_RSC_DIR:
        phobos_locate_state.rsc_family = family;
        break;
    default:
        fprintf(stderr, "%s", usage);
        exit(EXIT_FAILURE);
    }

    const struct CMUnitTest phobos_locate_cases[] = {
        cmocka_unit_test_setup_teardown(pl, pl_setup, pl_teardown),
    };

    return cmocka_run_group_tests(phobos_locate_cases, global_setup,
                                  global_teardown);
}
