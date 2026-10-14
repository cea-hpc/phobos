/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2026 CEA/DAM.
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
 * \brief  Tests for dss_media_gc_stats_compute
 */

#include "test_setup.h"
#include "pho_dss.h"
#include "pho_type_utils.h"
#include "pho_dss_wrapper.h"

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

static struct dss_handle *DSS;

static struct pho_id M1 = {
    .family = PHO_RSC_DIR, .name = "/tmp/pho_gc_dir1", .library = "legacy",
};
static struct pho_id M2 = {
    .family = PHO_RSC_DIR, .name = "/tmp/pho_gc_dir2", .library = "otherlib",
};
static struct pho_id M3 = {
    .family = PHO_RSC_TAPE, .name = "TAPE0001", .library = "legacy",
};

static void fill_medium_info(struct media_info *medium_info,
                             const struct pho_id *id)
{
    memset(medium_info, 0, sizeof(*medium_info));

    medium_info->rsc.id = *id;
    medium_info->rsc.model = id->family == PHO_RSC_DIR ? "dir" : "LTO6";
    medium_info->rsc.adm_status = PHO_RSC_ADM_ST_UNLOCKED;
    medium_info->addr_type = PHO_ADDR_HASH1;
    medium_info->fs.type = PHO_FS_POSIX;
    medium_info->fs.status = PHO_FS_STATUS_USED;
    medium_info->fs.label[0] = '\0';
    medium_info->flags.put = true;
    medium_info->flags.get = true;
    medium_info->flags.delete = true;
}

static int insert_medium(const struct pho_id *id)
{
    struct media_info medium_info;

    fill_medium_info(&medium_info, id);

    return dss_media_insert(DSS, &medium_info, 1);
}

static int insert_object(const char *oid, const char *uuid, int version)
{
    struct object_info obj = {
        .oid = oid, .uuid = (char *)uuid, .version = version,
        .user_md = "{}",
    };

    return dss_object_insert(DSS, &obj, 1, DSS_SET_FULL_INSERT);
}

static int insert_deprecated_object(const char *oid, const char *uuid,
                                    int version)
{
    struct object_info obj = {
        .oid = oid, .uuid = (char *)uuid, .version = version,
        .user_md = "{}",
    };
    int rc;

    rc = insert_object(oid, uuid, version);
    if (rc)
        return rc;

    return dss_move_object_to_deprecated(DSS, &obj, 1);
}

static int insert_extent(const char *uuid, enum extent_state state,
                         ssize_t size, const struct pho_id *media)
{
    struct extent ext = {
        .uuid = (char *)uuid, .state = state, .size = size, .media = *media,
        .address = { .buff = "pho_gc_addr" },
    };

    return dss_extent_insert(DSS, &ext, 1, DSS_SET_INSERT);
}

static int link_extent(const char *object_uuid, int version,
                       const char *extent_uuid, int layout_idx)
{
    struct extent ext = {
        .uuid = (char *)extent_uuid, .layout_idx = layout_idx,
    };
    struct layout_info lyt = {
        .uuid = (char *)object_uuid, .version = version,
        .layout_desc = { .mod_name = "simple", .mod_major = 0,
                         .mod_minor = 1 },
        .extents = &ext, .ext_count = 1, .copy_name = "source",
    };

    return dss_layout_insert(DSS, &lyt, 1);
}

static int dmgcs_setup(void **state)
{
    int rc;

    (void) state;

    rc = global_setup_dss_with_dbinit((void **)&DSS);
    assert_return_code(rc, -rc);

    /* dss_media_update normalizes the path of dir media: they must exist */
    mkdir(M1.name, 0755);
    mkdir(M2.name, 0755);

    /* the media under test: two dir media in different libraries, an empty
     * tape
     */
    rc = insert_medium(&M1);
    assert_return_code(rc, -rc);
    rc = insert_medium(&M2);
    assert_return_code(rc, -rc);
    rc = insert_medium(&M3);
    assert_return_code(rc, -rc);

    /* version 2 of the shared object is deprecated, version 1 stays live */
    rc = insert_deprecated_object("oid-shared", "uuid-shared", 1);
    assert_return_code(rc, -rc);
    rc = insert_object("oid-shared", "uuid-shared", 2);
    assert_return_code(rc, -rc);

    rc = insert_object("oid-live", "uuid-live", 1);
    assert_return_code(rc, -rc);

    rc = insert_deprecated_object("oid-dep", "uuid-dep", 1);
    assert_return_code(rc, -rc);

    /*
     * Extents of M1:
     * - e1: sync, referenced by the layout of a live object version
     * - e2: sync, referenced by the layout of a deprecated object version
     * - e3: sync, referenced by the layout of the deprecated version of
     *   the shared object
     * - e4: sync, without any layout reference
     * - e5: orphan, without any layout reference
     * - e6: orphan, referenced by a live object version
     * - e7: pending
     * - e8: sync, with a negative size, without any layout reference
     */
    rc = insert_extent("e1", PHO_EXT_ST_SYNC, 100, &M1);
    assert_return_code(rc, -rc);
    rc = link_extent("uuid-live", 1, "e1", 0);
    assert_return_code(rc, -rc);

    rc = insert_extent("e2", PHO_EXT_ST_SYNC, 200, &M1);
    assert_return_code(rc, -rc);
    rc = link_extent("uuid-dep", 1, "e2", 0);
    assert_return_code(rc, -rc);

    rc = insert_extent("e3", PHO_EXT_ST_SYNC, 300, &M1);
    assert_return_code(rc, -rc);
    rc = link_extent("uuid-shared", 1, "e3", 0);
    assert_return_code(rc, -rc);

    rc = insert_extent("e4", PHO_EXT_ST_SYNC, 400, &M1);
    assert_return_code(rc, -rc);

    rc = insert_extent("e5", PHO_EXT_ST_ORPHAN, 500, &M1);
    assert_return_code(rc, -rc);

    rc = insert_extent("e6", PHO_EXT_ST_ORPHAN, 50, &M1);
    assert_return_code(rc, -rc);
    rc = link_extent("uuid-live", 1, "e6", 1);
    assert_return_code(rc, -rc);

    rc = insert_extent("e7", PHO_EXT_ST_PENDING, 70, &M1);
    assert_return_code(rc, -rc);

    rc = insert_extent("e8", PHO_EXT_ST_SYNC, -5, &M1);
    assert_return_code(rc, -rc);

    /* a deprecated extent on another medium of another library */
    rc = insert_extent("e9", PHO_EXT_ST_SYNC, 10, &M2);
    assert_return_code(rc, -rc);
    rc = link_extent("uuid-dep", 1, "e9", 1);
    assert_return_code(rc, -rc);

    /* an orphan extent on the tape medium of another family */
    rc = insert_extent("e10", PHO_EXT_ST_ORPHAN, 100, &M3);
    assert_return_code(rc, -rc);

    return 0;
}

static int dmgcs_teardown(void **state)
{
    int rc;

    (void) state;

    rc = global_teardown_dss_with_dbdrop((void **)&DSS);
    if (rc)
        return rc;

    rmdir(M1.name);
    rmdir(M2.name);

    return 0;
}

static void assert_gc(const struct media_info *medium,
                      long long nb_orphan, long long logc_orphan,
                      long long nb_deprecated, long long logc_deprecated)
{
    assert_int_equal(medium->stats.nb_extent_orphan, nb_orphan);
    assert_int_equal(medium->stats.logc_spc_orphan, logc_orphan);
    assert_int_equal(medium->stats.nb_extent_deprecated, nb_deprecated);
    assert_int_equal(medium->stats.logc_spc_deprecated, logc_deprecated);
}

/* retrieve the stored media_info of the media with the given id */
static struct media_info *get_stored_medium(const char *name, int *count)
{
    struct dss_filter filter;
    struct media_info *media;
    int rc;

    rc = dss_filter_build(&filter, "{\"DSS::MDA::id\": \"%s\"}", name);
    assert_return_code(rc, -rc);

    rc = dss_media_get(DSS, &filter, &media, count, NULL);
    dss_filter_free(&filter);
    assert_return_code(rc, -rc);

    return media;
}

static void dmgcs_refresh_family(void **state)
{
    struct media_info *media;
    int media_count;
    int rc;

    (void) state;

    /* no media given: all the media of the dir family are refreshed */
    rc = dss_media_gc_stats_compute(DSS, PHO_RSC_DIR, NULL, NULL, 0);
    assert_int_equal(rc, 2);

    /* e5+e6 are orphan (550 bytes), e2+e3 are deprecated (500 bytes) */
    media = get_stored_medium(M1.name, &media_count);
    assert_int_equal(media_count, 1);
    assert_gc(&media[0], 2, 550, 2, 500);
    dss_res_free(media, media_count);

    /* e9 is deprecated only (10 bytes) */
    media = get_stored_medium(M2.name, &media_count);
    assert_int_equal(media_count, 1);
    assert_gc(&media[0], 0, 0, 1, 10);
    dss_res_free(media, media_count);

    /* the tape medium of another family is not refreshed */
    media = get_stored_medium(M3.name, &media_count);
    assert_int_equal(media_count, 1);
    assert_gc(&media[0], 0, 0, 0, 0);
    dss_res_free(media, media_count);
}

static void dmgcs_refresh_library_scope(void **state)
{
    struct media_info *media;
    int media_count;
    int rc;

    (void) state;

    /* no media given: only the media of the given library are refreshed */
    rc = dss_media_gc_stats_compute(DSS, PHO_RSC_DIR, "otherlib", NULL, 0);
    assert_int_equal(rc, 1);

    /* e9 is deprecated only (10 bytes) */
    media = get_stored_medium(M2.name, &media_count);
    assert_int_equal(media_count, 1);
    assert_gc(&media[0], 0, 0, 1, 10);
    dss_res_free(media, media_count);

    /* M1, of another library, is not refreshed */
    media = get_stored_medium(M1.name, &media_count);
    assert_int_equal(media_count, 1);
    assert_gc(&media[0], 0, 0, 0, 0);
    dss_res_free(media, media_count);

    /* an unknown library matches no medium */
    rc = dss_media_gc_stats_compute(DSS, PHO_RSC_DIR, "nosuchlib", NULL, 0);
    assert_int_equal(rc, 0);
}

static void dmgcs_refresh_given_media(void **state)
{
    struct pho_id targets[3];
    struct media_info *media;
    int media_count;
    int rc;

    (void) state;

    targets[0] = M1;
    targets[1] = M2;
    targets[2] = M3;

    /* all the given media are refreshed, whatever their family */
    rc = dss_media_gc_stats_compute(DSS, PHO_RSC_DIR, NULL, targets, 3);
    assert_int_equal(rc, 3);

    media = get_stored_medium(M1.name, &media_count);
    assert_int_equal(media_count, 1);
    assert_gc(&media[0], 2, 550, 2, 500);
    dss_res_free(media, media_count);

    media = get_stored_medium(M2.name, &media_count);
    assert_int_equal(media_count, 1);
    assert_gc(&media[0], 0, 0, 1, 10);
    dss_res_free(media, media_count);

    /* the tape medium holds e10, an orphan extent */
    media = get_stored_medium(M3.name, &media_count);
    assert_int_equal(media_count, 1);
    assert_gc(&media[0], 1, 100, 0, 0);
    dss_res_free(media, media_count);
}

static void dmgcs_refresh_subset(void **state)
{
    struct pho_id targets[1];
    struct media_info *media;
    int media_count;
    int rc;

    (void) state;

    targets[0] = M2;

    rc = dss_media_gc_stats_compute(DSS, PHO_RSC_DIR, NULL, targets, 1);
    assert_int_equal(rc, 1);

    media = get_stored_medium(M2.name, &media_count);
    assert_int_equal(media_count, 1);
    assert_gc(&media[0], 0, 0, 1, 10);
    dss_res_free(media, media_count);

    /* M1 is not part of the given media: its counters are still null */
    media = get_stored_medium(M1.name, &media_count);
    assert_int_equal(media_count, 1);
    assert_gc(&media[0], 0, 0, 0, 0);
    dss_res_free(media, media_count);
}

static void dmgcs_refresh_unknown_medium(void **state)
{
    struct pho_id unknown = {
        .family = PHO_RSC_DIR, .name = "/nonexistent", .library = "legacy",
    };
    struct pho_id targets[2];
    struct media_info *media;
    int media_count;
    int rc;

    (void) state;

    targets[0] = M2;
    targets[1] = unknown;

    /* only the existing medium is refreshed, the returned count reflects it */
    rc = dss_media_gc_stats_compute(DSS, PHO_RSC_DIR, NULL, targets, 2);
    assert_int_equal(rc, 1);

    media = get_stored_medium(M2.name, &media_count);
    assert_int_equal(media_count, 1);
    assert_gc(&media[0], 0, 0, 1, 10);
    dss_res_free(media, media_count);
}

static void dmgcs_refresh_preserves_live_counters(void **state)
{
    struct media_info *media;
    struct media_info upd;
    int media_count;
    int rc;

    (void) state;

    /* an ordinary media update before the refresh */
    media = get_stored_medium(M1.name, &media_count);
    assert_int_equal(media_count, 1);
    upd = media[0];
    upd.stats.logc_spc_used = 42;
    dss_res_free(media, media_count);

    rc = dss_media_update(DSS, &upd, &upd, 1, LOGC_SPC_USED);
    assert_return_code(rc, -rc);

    rc = dss_media_gc_stats_compute(DSS, PHO_RSC_DIR, NULL, NULL, 0);
    assert_int_equal(rc, 2);

    /* the GC counters are published, the live counter survived both the
     * update and the refresh
     */
    media = get_stored_medium(M1.name, &media_count);
    assert_int_equal(media_count, 1);
    assert_gc(&media[0], 2, 550, 2, 500);
    assert_int_equal(media[0].stats.logc_spc_used, 42);
    dss_res_free(media, media_count);
}

static void dmgcs_refresh_idempotent(void **state)
{
    struct media_info *media;
    int media_count;
    int rc;

    (void) state;

    rc = dss_media_gc_stats_compute(DSS, PHO_RSC_DIR, NULL, NULL, 0);
    assert_int_equal(rc, 2);

    /* a second refresh replaces the counters, it does not add to them */
    rc = dss_media_gc_stats_compute(DSS, PHO_RSC_DIR, NULL, NULL, 0);
    assert_int_equal(rc, 2);

    media = get_stored_medium(M1.name, &media_count);
    assert_int_equal(media_count, 1);
    assert_gc(&media[0], 2, 550, 2, 500);
    dss_res_free(media, media_count);
}

static void dmgcs_refresh_invalid_args(void **state)
{
    struct pho_id targets[1];
    int rc;

    (void) state;

    /* a media count without the corresponding list */
    rc = dss_media_gc_stats_compute(DSS, PHO_RSC_DIR, NULL, NULL, 1);
    assert_int_equal(rc, -EINVAL);

    /* a negative media count */
    rc = dss_media_gc_stats_compute(DSS, PHO_RSC_DIR, NULL, targets, -1);
    assert_int_equal(rc, -EINVAL);
}

int main(void)
{
    const struct CMUnitTest dmgcs_cases[] = {
        cmocka_unit_test_setup_teardown(dmgcs_refresh_family, dmgcs_setup,
                                        dmgcs_teardown),
        cmocka_unit_test_setup_teardown(dmgcs_refresh_library_scope,
                                        dmgcs_setup, dmgcs_teardown),
        cmocka_unit_test_setup_teardown(dmgcs_refresh_given_media, dmgcs_setup,
                                        dmgcs_teardown),
        cmocka_unit_test_setup_teardown(dmgcs_refresh_subset, dmgcs_setup,
                                        dmgcs_teardown),
        cmocka_unit_test_setup_teardown(dmgcs_refresh_unknown_medium,
                                        dmgcs_setup, dmgcs_teardown),
        cmocka_unit_test_setup_teardown(dmgcs_refresh_preserves_live_counters,
                                        dmgcs_setup, dmgcs_teardown),
        cmocka_unit_test_setup_teardown(dmgcs_refresh_idempotent, dmgcs_setup,
                                        dmgcs_teardown),
        cmocka_unit_test_setup_teardown(dmgcs_refresh_invalid_args, dmgcs_setup,
                                        dmgcs_teardown),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(dmgcs_cases, NULL, NULL);
}
