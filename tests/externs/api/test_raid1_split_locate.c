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
 * \brief  Tests for raid1 layout locate function in split case
 *
 * This integration script tests the raid1 layout locate on a split case.
 * All medium must be different.
 */

/* phobos stuff */
#include "phobos_store.h"
#include "pho_common.h" /* get_hostname */
#include "pho_dss.h"
#include "../layout-modules/raid1.h"
#include "../dss/dss_lock.h"

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

#define WIN_HOST "winner_hostname"
#define WIN_HOST_BIS "winner_hostname_bis"

static struct raid1_split_locate_state {
    const char *local_hostname;
    struct dss_handle *dss;
    char *oid;
    int layout_cnt; /* only to free layout with dss_res_free */
    struct layout_info *layout;
    struct media_info **media; /* array of layout->ext_count media pointers */
    unsigned int repl_count;
    unsigned int split_count;
} rsl_state;

static int rsl_clean_all_media(void **state)
{
    struct raid1_split_locate_state *rsl_state =
        (struct raid1_split_locate_state *)*state;
    int rc;
    int i;

    /* clean each medium */
    for (i = 0; i < rsl_state->layout->ext_count; i++) {
        /* unlock */
        rc = dss_unlock(rsl_state->dss, DSS_MEDIA, rsl_state->media[i], 1,
                        true);
        if (rc && rc != -ENOLCK) {
            fprintf(stderr, "Error when cleaning all lock : %d, %s",
                    rc, strerror(rc));
            return -1;
        }

        /* admin unlock and get flag to true */
        rsl_state->media[i]->rsc.adm_status = PHO_RSC_ADM_ST_UNLOCKED;
        rsl_state->media[i]->flags.get = true;
        rc = dss_media_set(rsl_state->dss, rsl_state->media[i], 1,
                           DSS_SET_UPDATE, ADM_STATUS | GET_ACCESS);
        assert_return_code(rc, -rc);
    }

    return 0;
}

/* global setup connect to the DSS */
static int global_setup(void **state)
{
    struct dss_filter filter;
    int rc;
    int i;

    *state = NULL;

    /* get_hostname */
    rsl_state.local_hostname = get_hostname();
    if (!rsl_state.local_hostname) {
        fprintf(stderr, "Unable to get self hostname");
        return -1;
    }

    /* init DSS */
    rsl_state.dss = malloc(sizeof(*rsl_state.dss));
    if (!rsl_state.dss)
        return -1;

    setenv("PHOBOS_DSS_connect_string", "dbname=phobos host=localhost "
                                        "user=phobos password=phobos", 1);

    rc = dss_init(rsl_state.dss);
    if (rc)
        GOTO(clean, rc = -1);

    /* get layout */
    rc = dss_filter_build(&filter, "{\"DSS::EXT::oid\": \"%s\"}",
                          rsl_state.oid);
    if (rc)
        GOTO(clean_dss, rc = -1);

    rc = dss_layout_get(rsl_state.dss, &filter, &rsl_state.layout,
                        &rsl_state.layout_cnt);
    dss_filter_free(&filter);
    if (rc)
        GOTO(clean_dss, rc = -1);

    /* check layout is "raid1" one */
    if (strcmp(rsl_state.layout->layout_desc.mod_name, "raid1")) {
        fprintf(stderr, "layout is not \"raid1\"");
        GOTO(clean_layout, rc = -1);
    }

    /* get repl_count */
    rc = layout_repl_count(rsl_state.layout, &rsl_state.repl_count);
    if (rc) {
        fprintf(stderr, "Unable to get replica count from layout");
        GOTO(clean_layout, rc = -1);
    }

    /* check ext_count is multiple of repl_count and get split count */
    if (rsl_state.layout->ext_count % rsl_state.repl_count != 0) {
        fprintf(stderr, "ext_count is not a multiple of repl_count");
        GOTO(clean_layout, rc = -1);
    }

    rsl_state.split_count = rsl_state.layout->ext_count / rsl_state.repl_count;

    /* check we have at least two splits */
    if (rsl_state.split_count < 2) {
        fprintf(stderr,
                "raid1 split locate test needs at least two splits, found "
                "only %u: extent count %u, repl_count %u",
                rsl_state.split_count, rsl_state.layout->ext_count,
                rsl_state.repl_count);
        GOTO(clean_layout, rc = -1);
    }

    /* alloc media */
    rsl_state.media = calloc(rsl_state.layout->ext_count,
                             sizeof(*rsl_state.media));
    if (!rsl_state.media)
        GOTO(clean_layout, rc = -1);

    /* get media */
    for (i = 0; i < rsl_state.layout->ext_count; i++) {
        struct pho_id *medium_id = &rsl_state.layout->extents[i].media;
        int cnt;
        int j;

        rc = dss_filter_build(&filter,
                              "{\"$AND\": ["
                                  "{\"DSS::MDA::family\": \"%s\"}, "
                                  "{\"DSS::MDA::id\": \"%s\"}"
                              "]}",
                              rsc_family2str(medium_id->family),
                              medium_id->name);
        if (rc)
            GOTO(clean_media, rc = -1);

        rc = dss_media_get(rsl_state.dss, &filter, &rsl_state.media[i], &cnt);
        dss_filter_free(&filter);
        if (rc)
            GOTO(clean_media, rc = -1);

        if (cnt != 1) {
            dss_res_free(rsl_state.media[i], cnt);
            rsl_state.media[i] = NULL;
            GOTO(clean_media, rc = -1);
        }

        /* check all media are different */
        for (j = 0; j < i; j++) {
            if (!strcmp(rsl_state.media[i]->rsc.id.name,
                        rsl_state.media[j]->rsc.id.name)) {
                fprintf(stderr, "Two medium are identical: %s, %d, %d\n",
                        rsl_state.media[i]->rsc.id.name, j, i);
                GOTO(clean_media, rc = -1);
            }
        }

        /* check medium is admin unlocked */
        if (rsl_state.media[i]->rsc.adm_status != PHO_RSC_ADM_ST_UNLOCKED) {
            fprintf(stderr, "A medium is not PHO_RSC_ADM_ST_UNLOCKED (%s, %s)",
                    rsc_family2str(rsl_state.media[i]->rsc.id.family),
                    rsl_state.media[i]->rsc.id.name);
            GOTO(clean_media, rc = -1);
        }

        /* check medium get operation flag is "true" */
        if (!rsl_state.media[i]->flags.get) {
            fprintf(stderr, "A medium has not get operation flag (%s, %s)",
                    rsc_family2str(rsl_state.media[i]->rsc.id.family),
                    rsl_state.media[i]->rsc.id.name);
            GOTO(clean_media, rc = -1);
        }
    }

    *state = &rsl_state;
    rc = rsl_clean_all_media(state);
    if (rc)
        GOTO(clean_media, rc = -1);

    /* success */
    return 0;

clean_media:
    for (i = 0; i < rsl_state.layout->ext_count; i++)
        if (rsl_state.media[i])
            dss_res_free(rsl_state.media[i], 1);

    free(rsl_state.media);
clean_layout:
    dss_res_free(rsl_state.layout, rsl_state.layout_cnt);
clean_dss:
    dss_fini(rsl_state.dss);
clean:
    free(rsl_state.dss);
    return rc;
}

static int global_teardown(void **state)
{

    struct raid1_split_locate_state *rsl_state =
        (struct raid1_split_locate_state *)*state;
    int i;

    if (rsl_state) {
        /* clean media */
        for (i = 0; i < rsl_state->layout->ext_count; i++)
            dss_res_free(rsl_state->media[i], 1);

        free(rsl_state->media);

        /* clean layout */
        dss_res_free(rsl_state->layout, rsl_state->layout_cnt);

        /* clean dss */
        dss_fini(rsl_state->dss);
        free(rsl_state->dss);
    }

    unsetenv("PHOBOS_DSS_connect_string");

    return 0;
}

/*************************************************************************/
/* rsl_no_lock: raid1 split locate returns localhost if there is no lock */
/*************************************************************************/
static void rsl_no_lock(void **state)
{
    struct raid1_split_locate_state *rsl_state =
        (struct raid1_split_locate_state *)*state;
    const char *my_hostname;
    char *hostname;
    int rc;

    my_hostname = get_hostname();
    assert_non_null(my_hostname);

    /* focus_host set to NULL */
    rc = layout_raid1_locate(rsl_state->dss, rsl_state->layout, NULL,
                             &hostname);
    assert_return_code(rc, -rc);
    assert_non_null(hostname);
    assert_string_equal(my_hostname, hostname);
    free(hostname);

    /* focus_host set to my_hostname */
    rc = layout_raid1_locate(rsl_state->dss, rsl_state->layout, my_hostname,
                             &hostname);
    assert_return_code(rc, -rc);
    assert_non_null(hostname);
    assert_string_equal(my_hostname, hostname);
    free(hostname);
}

/**********************************************************/
/* rsl_one_lock: raid1 split locate returns locked medium */
/**********************************************************/
static void rsl_one_lock(void **state)
{
    struct raid1_split_locate_state *rsl_state =
        (struct raid1_split_locate_state *)*state;
    const char *my_hostname;
    char *hostname;
    int rc;
    int i;

    my_hostname = get_hostname();
    assert_non_null(my_hostname);

    /* test each extent */
    for (i = 0; i < rsl_state->layout->ext_count; i++) {
        int j, k;
        int pid;

        pid = getpid();

        /* get lock */
        rc = _dss_lock(rsl_state->dss, DSS_MEDIA, rsl_state->media[i], 1,
                       WIN_HOST, pid);
        assert_return_code(rc, -rc);

        /* check locate */
        rc = layout_raid1_locate(rsl_state->dss, rsl_state->layout, my_hostname,
                                 &hostname);
        assert_return_code(rc, -rc);
        assert_non_null(hostname);
        assert_string_equal(WIN_HOST, hostname);
        free(hostname);

        /* check focus host if my_hostname and WIN_HOST both have one lock */
        for (j = 0; j < rsl_state->repl_count; j++)
            for (k = 0; k < rsl_state->split_count; k++) {
                int medium_index = j + k * rsl_state->repl_count;

                if (medium_index == i)
                    continue;

                /* lock my_hostname */
                rc = _dss_lock(rsl_state->dss, DSS_MEDIA,
                               rsl_state->media[medium_index], 1, my_hostname,
                               pid);

                /* check WIN_HOST as focus_host */
                rc = layout_raid1_locate(rsl_state->dss, rsl_state->layout,
                                         WIN_HOST, &hostname);
                assert_return_code(rc, -rc);
                assert_non_null(hostname);
                assert_string_equal(WIN_HOST, hostname);
                free(hostname);

                /* check my_hostname as focus_host */
                rc = layout_raid1_locate(rsl_state->dss, rsl_state->layout,
                                         my_hostname, &hostname);
                assert_return_code(rc, -rc);
                assert_non_null(hostname);
                assert_string_equal(my_hostname, hostname);
                free(hostname);

                /* unlock my_hostname */
                rc = dss_unlock(rsl_state->dss, DSS_MEDIA,
                                rsl_state->media[medium_index], 1, true);
                assert_return_code(rc, -rc);
        }

        /* admin lock this medium and check my_hostname */
        rsl_state->media[i]->rsc.adm_status = PHO_RSC_ADM_ST_LOCKED;
        rc = dss_media_set(rsl_state->dss, rsl_state->media[i], 1,
                           DSS_SET_UPDATE, ADM_STATUS);
        assert_return_code(rc, -rc);

        /* check locate */
        rc = layout_raid1_locate(rsl_state->dss, rsl_state->layout, my_hostname,
                                 &hostname);
        assert_return_code(rc, -rc);
        assert_non_null(hostname);
        assert_string_equal(my_hostname, hostname);
        free(hostname);

        rsl_state->media[i]->rsc.adm_status = PHO_RSC_ADM_ST_UNLOCKED;
        rc = dss_media_set(rsl_state->dss, rsl_state->media[i], 1,
                           DSS_SET_UPDATE, ADM_STATUS);
        assert_return_code(rc, -rc);

        /* set operation get flag to false and check NULL hostname */
        rsl_state->media[i]->flags.get = false;
        rc = dss_media_set(rsl_state->dss, rsl_state->media[i], 1,
                           DSS_SET_UPDATE, GET_ACCESS);
        assert_return_code(rc, -rc);

        /* check locate */
        rc = layout_raid1_locate(rsl_state->dss, rsl_state->layout, my_hostname,
                                 &hostname);
        assert_return_code(rc, -rc);
        assert_non_null(hostname);
        assert_string_equal(my_hostname, hostname);
        free(hostname);

        rsl_state->media[i]->flags.get = true;
        rc = dss_media_set(rsl_state->dss, rsl_state->media[i], 1,
                           DSS_SET_UPDATE, GET_ACCESS);
        assert_return_code(rc, -rc);

        /* unlock */
        rc = dss_unlock(rsl_state->dss, DSS_MEDIA, rsl_state->media[i], 1,
                        true);
        assert_return_code(rc, -rc);
    }
}

/**
 * rsl_one_lock_one_not_avail:
 *
 * For one replica, medium of first split is locked by win_host, medium of
 * second split is not avail. If the second split is free on other replica:
 * locate returns win_host. If the second split is locked on other replica by
 * win_host_bis: locate will returns win_host_bis.
 */
static void rsl_one_lock_one_not_avail(void **state)
{
    LargestIntegralType ok_or_enolock[2] = {0, -ENOLCK};
    struct raid1_split_locate_state *rsl_state =
        (struct raid1_split_locate_state *)*state;
    const char *my_hostname;
    char *hostname;
    int rc;
    int i;

    my_hostname = get_hostname();
    assert_non_null(my_hostname);

    /* test each replica */
    for (i = 0; i < rsl_state->repl_count; i++) {
        int j;

        /* set lock on first split medium */
        rc = _dss_lock(rsl_state->dss, DSS_MEDIA, rsl_state->media[i], 1,
                       WIN_HOST, getpid());
        assert_return_code(rc, -rc);

        /* operation get flag to false on second split medium */
        rsl_state->media[i + rsl_state->repl_count]->flags.get = false;
        rc = dss_media_set(rsl_state->dss,
                           rsl_state->media[i + rsl_state->repl_count], 1,
                           DSS_SET_UPDATE, GET_ACCESS);
        assert_return_code(rc, -rc);

        /* check locate */
        rc = layout_raid1_locate(rsl_state->dss, rsl_state->layout, my_hostname,
                                 &hostname);
        assert_return_code(rc, -rc);
        assert_non_null(hostname);
        assert_string_equal(WIN_HOST, hostname);
        free(hostname);

        /* locked second split on other replica */
        for (j = 0; j < rsl_state->repl_count; j++) {
            if (j != i) {
                rc = _dss_lock(rsl_state->dss, DSS_MEDIA,
                               rsl_state->media[j + rsl_state->repl_count], 1,
                               WIN_HOST_BIS, getpid());
                assert_return_code(rc, -rc);
            }
        }

        /* check locate */
        rc = layout_raid1_locate(rsl_state->dss, rsl_state->layout, my_hostname,
                                 &hostname);
        assert_return_code(rc, -rc);
        assert_non_null(hostname);
        assert_string_equal(WIN_HOST_BIS, hostname);
        free(hostname);

        /* unlocked second split on other replica */
        for (j = 0; j < rsl_state->split_count; j++) {
            if (j != i) {
                rc = dss_unlock(rsl_state->dss, DSS_MEDIA,
                                rsl_state->media[j + rsl_state->repl_count], 1,
                                true);
                assert_in_set(rc, ok_or_enolock, 2);
            }
        }

        /* unlock first split on medium */
        rc = dss_unlock(rsl_state->dss, DSS_MEDIA, rsl_state->media[i], 1,
                        true);
        assert_in_set(rc, ok_or_enolock, 2);

        /* operation get flag to true */
        rsl_state->media[i + rsl_state->repl_count]->flags.get = true;
        rc = dss_media_set(rsl_state->dss,
                           rsl_state->media[i + rsl_state->repl_count], 1,
                           DSS_SET_UPDATE, GET_ACCESS);
        assert_return_code(rc, -rc);
    }
}

#define NB_ARGS 1
static const char *usage = "Usage: test_raid1_split_locate <oid_to_test>";
int main(int argc, char **argv)
{
    /* get oid from arg */
    if (argc != NB_ARGS + 1) {
        fprintf(stderr, "%s\n", usage);
        exit(EXIT_FAILURE);
    }

    rsl_state.oid = argv[1];

    const struct CMUnitTest raid1_split_locate_cases[] = {
        cmocka_unit_test(rsl_no_lock),
        cmocka_unit_test_teardown(rsl_one_lock, rsl_clean_all_media),
        cmocka_unit_test_teardown(rsl_one_lock_one_not_avail,
                                  rsl_clean_all_media),
    };

    return cmocka_run_group_tests(raid1_split_locate_cases, global_setup,
                                  global_teardown);
}
