/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2024 CEA/DAM.
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
 * \brief  test profile functionality of object store
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "phobos_store.h"
#include "pho_attrs.h"
#include "pho_cfg.h"
#include "pho_type_utils.h"
#include "store_profile.h"
#include "pho_common.h"

#include <assert.h>
#include <libgen.h>
#include <stdlib.h>

static void test_fill_put_params(void)
{
    static struct pho_xfer_desc empty_xfer;
    struct pho_xfer_desc xfer;
    char *profile_name_full = "full-test";
    char *profile_name_no_family = "empty-family-test";
    char *profile_name_no_layout = "empty-layout-test";
    char *profile_name_no_tags = "empty-tag-test";
    char *profile_name_no_library = "empty-lib-test";
    char *pre_existing_tag[1];

    pre_existing_tag[0] = "new-tag";

    /* set family to invalid */
    empty_xfer.xd_params.put.family = PHO_RSC_INVAL;

    /* test default values */
    xfer = empty_xfer;
    assert(fill_put_params(&xfer) == 0);

    assert(strcmp(xfer.xd_params.put.layout_name, "raid1") == 0);
    assert(strcmp(pho_attr_get(&xfer.xd_params.put.lyt_params, "repl_count"),
                  "1") == 0);
    assert(xfer.xd_params.put.family == PHO_RSC_TAPE);
    assert(xfer.xd_params.put.tags.count == 0);

    string_array_free(&xfer.xd_params.put.tags);

    /* test full profile */
    xfer = empty_xfer;
    xfer.xd_params.put.profile = profile_name_full;

    assert(fill_put_params(&xfer) == 0);

    assert(strcmp(xfer.xd_params.put.layout_name, "raid1") == 0);
    assert(xfer.xd_params.put.family == PHO_RSC_DIR);
    assert(xfer.xd_params.put.tags.count == 2);
    assert(strcmp(xfer.xd_params.put.tags.strings[0], "foo-tag") == 0);
    assert(strcmp(xfer.xd_params.put.tags.strings[1], "bar-tag") == 0);
    assert(strcmp(xfer.xd_params.put.library, "legacy") == 0);

    string_array_free(&xfer.xd_params.put.tags);

    /* test profile without family */
    xfer = empty_xfer;
    xfer.xd_params.put.profile = profile_name_no_family;

    assert(fill_put_params(&xfer) == 0);

    assert(strcmp(xfer.xd_params.put.layout_name, "raid1") == 0);
    assert(xfer.xd_params.put.family == PHO_RSC_TAPE);
    assert(xfer.xd_params.put.tags.count == 1);
    assert(strcmp(xfer.xd_params.put.tags.strings[0], "foo-tag") == 0);
    assert(strcmp(xfer.xd_params.put.library, "legacy") == 0);

    string_array_free(&xfer.xd_params.put.tags);

    /* test profile without layout */
    xfer = empty_xfer;
    xfer.xd_params.put.profile = profile_name_no_layout;

    assert(fill_put_params(&xfer) == 0);

    assert(strcmp(xfer.xd_params.put.layout_name, "raid1") == 0);
    assert(xfer.xd_params.put.family == PHO_RSC_DIR);
    assert(xfer.xd_params.put.tags.count == 1);
    assert(strcmp(xfer.xd_params.put.tags.strings[0], "foo-tag") == 0);
    assert(strcmp(xfer.xd_params.put.library, "legacy") == 0);

    string_array_free(&xfer.xd_params.put.tags);

    /* test profile without tags */
    xfer = empty_xfer;
    xfer.xd_params.put.profile = profile_name_no_tags;

    assert(fill_put_params(&xfer) == 0);

    assert(strcmp(xfer.xd_params.put.layout_name, "raid1") == 0);
    assert(xfer.xd_params.put.family == PHO_RSC_DIR);
    assert(xfer.xd_params.put.tags.count == 0);
    assert(strcmp(xfer.xd_params.put.library, "legacy") == 0);

    string_array_free(&xfer.xd_params.put.tags);

    /* test additional parameters */
    xfer = empty_xfer;
    xfer.xd_params.put.profile = profile_name_full;
    xfer.xd_params.put.family = PHO_RSC_TAPE;
    xfer.xd_params.put.layout_name = "raid1";
    string_array_init(&xfer.xd_params.put.tags, (char **)pre_existing_tag, 1);

    assert(fill_put_params(&xfer) == 0);

    assert(xfer.xd_params.put.family == PHO_RSC_TAPE);
    assert(strcmp(xfer.xd_params.put.layout_name, "raid1") == 0);
    assert(pho_attrs_is_empty(&xfer.xd_params.put.lyt_params));
    assert(xfer.xd_params.put.tags.count == 3);
    assert(strcmp(xfer.xd_params.put.tags.strings[0],
                  pre_existing_tag[0]) == 0);
    assert(strcmp(xfer.xd_params.put.tags.strings[1], "foo-tag") == 0);
    assert(strcmp(xfer.xd_params.put.tags.strings[2], "bar-tag") == 0);
    assert(strcmp(xfer.xd_params.put.library, "legacy") == 0);

    string_array_free(&xfer.xd_params.put.tags);

    /* test profile without library */
    xfer = empty_xfer;
    xfer.xd_params.put.profile = profile_name_no_library;

    assert(fill_put_params(&xfer) == 0);

    assert(strcmp(xfer.xd_params.put.layout_name, "raid1") == 0);
    assert(xfer.xd_params.put.family == PHO_RSC_DIR);
    assert(xfer.xd_params.put.tags.count == 1);
    assert(strcmp(xfer.xd_params.put.tags.strings[0], "foo-tag") == 0);
    assert(xfer.xd_params.put.library == NULL);

    string_array_free(&xfer.xd_params.put.tags);
}

static void load_config(void)
{
    int rc;

    rc = pho_cfg_init_local("../phobos.conf");
    atexit(pho_cfg_local_fini);
    if (rc != 0 && rc != -EALREADY)
        exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    pho_context_init();
    atexit(pho_context_fini);

    load_config();
    test_fill_put_params();

    return EXIT_SUCCESS;
}
