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
 * \brief  Tests for phobos_locate store API function
 */

/* phobos stuff */
#include "dss_lock.h"
#include "phobos_store.h"
#include "phobos_admin.h"
#include "pho_common.h" /* get_hostname */
#include "pho_dss.h"
#include "pho_cfg.h"
#include "pho_ldm.h"
#include "pho_test_xfer_utils.h"
#include "pho_test_utils.h"
#include "test_setup.h"

/* standard stuff */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <glib.h>
#include <time.h>

/* cmocka stuff */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

static struct phobos_locate_state {
    struct dss_handle  *dss;
    enum rsc_family     rsc_family;
    struct object_info *objs;
    int n_objs;
    struct copy_info *copies;
    int n_copy;
} phobos_locate_state;

#define HOSTNAME "hostname"

/* global setup connect to the DSS */
static int global_setup(void **state)
{
    int rc;

    rc = global_setup_dss((void **)&phobos_locate_state.dss);
    if (rc)
        return rc;

    *state = &phobos_locate_state;

    return 0;
}

static int global_teardown(void **state)
{
    struct phobos_locate_state *pl_state = (struct phobos_locate_state *)*state;

    return global_teardown_dss((void **)&pl_state->dss);
}

static int local_setup(void **state, char *oid)
{
    struct phobos_locate_state *pl_state = (struct phobos_locate_state *)*state;
    struct pho_xfer_target target = {0};
    struct pho_xfer_desc xfer = {0};
    struct pho_list_filters filters = {0};
    int rc;

    /* open the xfer path and properly set xfer values */
    xfer.xd_targets = &target;
    xfer_desc_open_path(&xfer, "/etc/hosts", PHO_XFER_OP_PUT, 0);
    xfer.xd_params.put.family = pl_state->rsc_family;
    xfer.xd_targets->xt_objid = oid;
    xfer.xd_targets->xt_attrs.attr_set = NULL;

    /* put the object and close the descriptor */
    rc = phobos_put(&xfer, 1, NULL, NULL);
    xfer_close_fd(xfer.xd_targets);
    assert_return_code(rc, -rc);
    assert_return_code(xfer.xd_rc, -xfer.xd_rc);
    pho_xfer_desc_clean(&xfer);

    /* get object info */
    filters.res = (const char **) &oid;
    filters.n_res = 1;
    rc = phobos_store_object_list(&filters, DSS_OBJ_ALIVE,
                                  &pl_state->objs, &pl_state->n_objs, NULL);
    assert_return_code(rc, -rc);
    assert_int_equal(pl_state->n_objs, 1);
    assert_string_equal(pl_state->objs[0].oid, oid);

    /* get copy_name */
    filters.uuid = pl_state->objs[0].uuid;
    filters.version = 1;
    filters.status_filter = DSS_STATUS_FILTER_ALL;
    rc = phobos_store_copy_list(&filters, DSS_OBJ_ALIVE,
                                &pl_state->copies, &pl_state->n_copy, NULL);
    assert_return_code(rc, -rc);
    assert_int_equal(pl_state->n_copy, 1);
    assert_string_equal(pl_state->copies[0].object_uuid,
                        pl_state->objs[0].uuid);

    return 0;
}

static int local_teardown(void **state)
{
    struct phobos_locate_state *pl_state = (struct phobos_locate_state *)*state;

    /* TODO: fully remove objects when phobos_remove will be ready to use */
    phobos_store_object_list_free(pl_state->objs, pl_state->n_objs);
    pl_state->objs = NULL;
    pl_state->n_objs = 0;

    phobos_store_copy_list_free(pl_state->copies, pl_state->n_copy);
    pl_state->copies = NULL;
    pl_state->n_copy = 0;

    return 0;
}

/**
 * Retrieve media containing extents of an object from DSS
 * @param[in]  hdl      valid connection handle
 * @param[in]  obj      object information
 * @param[out] media    list of retrieved media, to be freed with dss_res_free()
 * @param[out] cnt      number of media retrieved in the list
 *
 * @return 0 on success, -errno on failure
 *                       -EINVAL if \p obj doesn't exist in the DSS
 */
static int media_of_object(struct dss_handle *hdl, struct object_info *obj,
                           struct media_info **media, int *cnt)
{
    struct layout_info *layout;
    struct dss_filter filter;
    struct pho_id medium_id;
    GString *filter_str;
    int rc;
    int i;

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                            "{\"DSS::OBJ::oid\": \"%s\"}, "
                            "{\"DSS::OBJ::uuid\": \"%s\"}, "
                            "{\"DSS::OBJ::version\": \"%d\"}"
                          "]}",
                          obj->oid, obj->uuid, obj->version);
    if (rc)
        LOG_RETURN(rc, "Cannot build filter");

    rc = dss_full_layout_get(hdl, &filter, NULL, &layout, cnt, NULL);
    dss_filter_free(&filter);
    if (rc)
        LOG_RETURN(rc, "Failed to retrieve layout of object '%s'", obj->oid);

    if (*cnt == 0) {
        dss_res_free(layout, 0);
        LOG_RETURN(-EINVAL, "No extent found for object '%s'", obj->oid);
    }

    filter_str = g_string_new(NULL);
    if (*cnt > 1)
        g_string_append_printf(filter_str, "{\"OR\": [");

    for (i = 0; i < *cnt; ++i) {
        medium_id = layout[i].extents->media;
        g_string_append_printf(filter_str,
                               "{\"$AND\": ["
                                 "{\"DSS::MDA::family\": \"%s\"}, "
                                 "{\"DSS::MDA::id\": \"%s\"}, "
                                 "{\"DSS::MDA::library\": \"%s\"}"
                               "]}%s",
                               rsc_family2str(medium_id.family),
                               medium_id.name, medium_id.library,
                               i + 1 < *cnt ? "," : "");
    }

    if (*cnt > 1)
        g_string_append_printf(filter_str, "]}");

    dss_res_free(layout, *cnt);

    rc = dss_filter_build(&filter, "%s", filter_str->str);
    g_string_free(filter_str, true);
    if (rc)
        LOG_RETURN(rc, "Cannot build filter");

    rc = dss_media_get(hdl, &filter, media, cnt, NULL);
    dss_filter_free(&filter);

    return rc;
}

static void lock_medium(struct phobos_locate_state *pl_state,
                        struct media_info **medium, const char *hostname,
                        int *cnt)
{
    struct object_info *obj = pl_state->objs;
    int rc;

    rc = media_of_object(pl_state->dss, obj, medium, cnt);
    assert_return_code(rc, -rc);
    assert_int_not_equal(*cnt, 0);

    /* XXX: first ensure we can lock the media by unlocking them. This is
     * necessary because the LRS locked the media we want to get at that point,
     * and doesn't have time to unlock them before we request the lock.
     */
    dss_unlock(pl_state->dss, DSS_MEDIA, *medium, *cnt, true);

    /* simulate lock on medium */
    rc = _dss_lock(pl_state->dss, DSS_MEDIA, *medium, *cnt, hostname, 1337,
                   true, NULL);
    assert_return_code(rc, -rc);
}

static void unlock_medium(struct phobos_locate_state *pl_state,
                          struct media_info *medium, int cnt)
{
    int rc;

    rc = dss_unlock(pl_state->dss, DSS_MEDIA, medium, cnt, true);
    dss_res_free(medium, cnt);
    assert_return_code(rc, -rc);
}

/*********************/
/* pl: phobos_locate */
/*********************/
#define BAD_OID "bad_oid_to_locate"
#define BAD_UUID "bad_uuid_to_locate"
#define BAD_COPY "bad_copy_to_locate"
static int pl_setup(void **state)
{
    char *oid_pl = "oid_pl";

    return local_setup(state, oid_pl);
}

static void pl_enoent(void **state)
{
    struct phobos_locate_state *pl_state = (struct phobos_locate_state *)*state;
    int nb_new_lock;
    char *hostname;
    int rc;

    /* bad OID */
    rc = phobos_locate(BAD_OID, NULL, 0, NULL, NULL, &hostname, &nb_new_lock);
    assert_int_equal(rc, -ENOENT);

    /* bad UUID */
    rc = phobos_locate(NULL, BAD_UUID, 0, NULL, NULL, &hostname, &nb_new_lock);
    assert_int_equal(rc, -ENOENT);

    /* OID, bad UUID */
    rc = phobos_locate(pl_state->objs[0].oid, BAD_UUID, 0, NULL, NULL,
                       &hostname, &nb_new_lock);
    assert_int_equal(rc, -ENOENT);

    /* bad OID, UUID */
    rc = phobos_locate(BAD_OID, pl_state->objs[0].uuid, 0, NULL, NULL,
                       &hostname, &nb_new_lock);
    assert_int_equal(rc, -ENOENT);

    /* OID, bad version */
    rc = phobos_locate(pl_state->objs[0].oid, NULL,
                       pl_state->objs[0].version + 1, NULL, NULL, &hostname,
                       &nb_new_lock);
    assert_int_equal(rc, -ENOENT);

    /* UUID, bad version */
    rc = phobos_locate(NULL, pl_state->objs[0].uuid,
                       pl_state->objs[0].version + 1, NULL, NULL, &hostname,
                       &nb_new_lock);
    assert_int_equal(rc, -ENOENT);

    /* OID, UUID, bad version */
    rc = phobos_locate(pl_state->objs[0].oid, pl_state->objs[0].uuid,
                       pl_state->objs[0].version + 1, NULL, NULL, &hostname,
                       &nb_new_lock);
    assert_int_equal(rc, -ENOENT);

    /* OID, bad copy_name */
    rc = phobos_locate(pl_state->objs[0].oid, NULL, 0, NULL, BAD_COPY,
                       &hostname, &nb_new_lock);
    assert_int_equal(rc, -ENOENT);
}

static void pl_hostname(const char *expected_hostname, const char *focus_host,
                        void **state, bool alive)
{
    struct phobos_locate_state *pl_state = (struct phobos_locate_state *)*state;
    int nb_new_lock;
    char *hostname;
    int rc;

    /* oid */
    if (alive) {
        rc = phobos_locate(pl_state->objs[0].oid, NULL, 0, focus_host, NULL,
                           &hostname, &nb_new_lock);
        assert_return_code(rc, -rc);
        assert_non_null(hostname);
        assert_string_equal(expected_hostname, hostname);
        free(hostname);
    }

    /* oid, version */
    rc = phobos_locate(pl_state->objs[0].oid, NULL, pl_state->objs[0].version,
                       focus_host, NULL, &hostname, &nb_new_lock);
    assert_return_code(rc, -rc);
    assert_non_null(hostname);
    assert_string_equal(expected_hostname, hostname);
    free(hostname);

    /* uuid */
    rc = phobos_locate(NULL, pl_state->objs[0].uuid, 0, focus_host, NULL,
                       &hostname, &nb_new_lock);
    assert_return_code(rc, -rc);
    assert_non_null(hostname);
    assert_string_equal(expected_hostname, hostname);
    free(hostname);

    /* uuid, version */
    rc = phobos_locate(NULL, pl_state->objs[0].uuid, pl_state->objs[0].version,
                       focus_host, NULL, &hostname, &nb_new_lock);
    assert_return_code(rc, -rc);
    assert_non_null(hostname);
    assert_string_equal(expected_hostname, hostname);
    free(hostname);

    /* oid, uuid */
    rc = phobos_locate(pl_state->objs[0].oid, pl_state->objs[0].uuid, 0,
                       focus_host, NULL, &hostname, &nb_new_lock);
    assert_return_code(rc, -rc);
    assert_non_null(hostname);
    assert_string_equal(expected_hostname, hostname);
    free(hostname);

    /* oid, uuid, version */
    rc = phobos_locate(pl_state->objs[0].oid, pl_state->objs[0].uuid,
                       pl_state->objs[0].version, focus_host, NULL, &hostname,
                       &nb_new_lock);
    assert_return_code(rc, -rc);
    assert_non_null(hostname);
    assert_string_equal(expected_hostname, hostname);
    free(hostname);

    /* oid, uuid, copy name */
    pho_debug("%s", pl_state->copies[0].copy_name);
    rc = phobos_locate(pl_state->objs[0].oid, pl_state->objs[0].uuid, 0,
                       focus_host, pl_state->copies[0].copy_name, &hostname,
                       &nb_new_lock);
    assert_return_code(rc, -rc);
    assert_non_null(hostname);
    assert_string_equal(expected_hostname, hostname);
    free(hostname);
}

static void pl(void **state)
{
    struct phobos_locate_state *pl_state = (struct phobos_locate_state *)*state;
    struct object_info *obj = pl_state->objs;
    struct pho_xfer_target target = {0};
    const char *myself_hostname = NULL;
    struct pho_xfer_desc xfer = { 0 };
    struct media_info *medium;
    int nb_new_lock;
    char *hostname;
    int cnt;
    int rc;

    rc = phobos_locate(NULL, NULL, 1, NULL, NULL, &hostname, &nb_new_lock);
    assert_int_equal(rc, -EINVAL);

    /* check ENOENT from object table */
    pl_enoent(state);

    /* locate local hostname in object table */
    myself_hostname = get_hostname();
    assert_non_null(myself_hostname);
    pl_hostname(myself_hostname, NULL, state, true);
    pl_hostname(myself_hostname, myself_hostname, state, true);

    /* lock media from other owner */
    lock_medium(pl_state, &medium, HOSTNAME, &cnt);

    /* locate with lock */
    pl_enoent(state);
    pl_hostname(HOSTNAME, NULL, state, true);
    pl_hostname(HOSTNAME, myself_hostname, state, true);
    pl_hostname(HOSTNAME, HOSTNAME, state, true);

    /* move object to deprecated table */
    xfer.xd_targets = &target;
    xfer.xd_ntargets = 1;
    xfer.xd_targets->xt_objid = obj->oid;
    rc = phobos_delete(&xfer, 1);
    pho_xfer_desc_clean(&xfer);
    assert_return_code(rc, -rc);

    /* check ENOENT from deprecated table */
    pl_enoent(state);

    /* locate with lock in deprecated table */
    pl_hostname(HOSTNAME, NULL, state, false);
    pl_hostname(HOSTNAME, myself_hostname, state, false);
    pl_hostname(HOSTNAME, HOSTNAME, state, false);
    /* free lock on medium */
    unlock_medium(pl_state, medium, cnt);
    /* locate without any lock in deprecated table */
    if (pl_state->rsc_family == PHO_RSC_DIR) {
        rc = phobos_locate(pl_state->objs[0].oid, NULL,
                           pl_state->objs[0].version, myself_hostname, NULL,
                           &hostname, &nb_new_lock);
        assert_int_equal(rc, -ENODEV);
    } else {
        pl_hostname(myself_hostname, NULL, state, false);
        pl_hostname(myself_hostname, myself_hostname, state, false);
    }
}

/************************************/
/* pgl: phobos_get with locate flag */
/************************************/
static int pgl_setup(void **state)
{
    char *oid_pgl = "oid_pgl";

    return local_setup(state, oid_pgl);
}

static void assert_get_hostname(struct pho_xfer_desc xfer,
                                const char *hostname, int expected)
{
    int rc = phobos_get(&xfer, 1, NULL, NULL);

    assert_int_equal(rc, expected);
    if (expected)
        assert_string_equal(xfer.xd_params.get.node_name, hostname);
    else
        assert_null(xfer.xd_params.get.node_name);
    free(xfer.xd_params.get.node_name);
    free(xfer.xd_targets->xt_objuuid);
}

static void pgl_scenario(struct pho_xfer_desc xfer, struct object_info *obj,
                         const char *hostname, int expected)
{
    /* good OID*/
    xfer.xd_targets->xt_objid = obj->oid;
    xfer.xd_targets->xt_objuuid = NULL;
    xfer.xd_targets->xt_version = 0;
    assert_get_hostname(xfer, hostname, expected);

    /* good OID, good VERSION */
    xfer.xd_targets->xt_objid = obj->oid;
    xfer.xd_targets->xt_objuuid = NULL;
    xfer.xd_targets->xt_version = obj->version;
    assert_get_hostname(xfer, hostname, expected);

    /* good OID, good UUID, good VERSION */
    xfer.xd_targets->xt_objid = obj->oid;
    xfer.xd_targets->xt_objuuid = obj->uuid;
    assert_get_hostname(xfer, hostname, expected);
}

static void pgl(void **state)
{
    struct phobos_locate_state *pl_state = (struct phobos_locate_state *)*state;
    struct object_info *obj = pl_state->objs;
    struct pho_xfer_target target = {0};
    struct pho_xfer_desc xfer = { 0 };
    struct media_info *medium;
    const char *myself = NULL;
    int cnt;

    /* Setup hostnames */
    myself = get_hostname();
    assert_non_null(myself);

    /* Open xfer descriptor */
    xfer.xd_targets = &target;
    xfer_desc_open_path(&xfer, "/etc/hosts", PHO_XFER_OP_GET,
                        PHO_XFER_OBJ_REPLACE | PHO_XFER_OBJ_BEST_HOST);

    /* Check we can get file when it is locked on local node */
    lock_medium(pl_state, &medium, myself, &cnt);
    pgl_scenario(xfer, obj, myself, 0);

    /* Another call to lock medium would replace the media_info structure, so
     * we first unlock/free it
     */
    unlock_medium(pl_state, medium, cnt);

    /**
     * Lock the medium with a hostname and try getting the object.
     * Since the medium is locked by an other hostname, we can't retrieve the
     * object, as we don't own the lock, the get/locate should return
     * -EREMOTE.
     */
    lock_medium(pl_state, &medium, HOSTNAME, &cnt);
    pgl_scenario(xfer, obj, HOSTNAME, -EREMOTE);

    /* Unlock the medium */
    unlock_medium(pl_state, medium, cnt);

    /* Close xfer descriptor */
    xfer_close_fd(xfer.xd_targets);
}

#define NB_ARGS 1
static const char *usage = "Take one argument the rsc_family to test, "
                           "\"dir\" or \"tape\"\n";

int main(int argc, char **argv)
{
    int family;

    pho_context_init();
    atexit(pho_context_fini);

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
        cmocka_unit_test_setup_teardown(pl, pl_setup, local_teardown),
        cmocka_unit_test_setup_teardown(pgl, pgl_setup, local_teardown),
    };

    return cmocka_run_group_tests(phobos_locate_cases, global_setup,
                                  global_teardown);
}
