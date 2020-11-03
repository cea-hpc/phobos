/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2018 CEA/DAM.
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
 * \brief test type utils
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "phobos_store.h"
#include "pho_test_utils.h"
#include "pho_types.h"
#include "pho_type_utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

static const char *const T_AB[] = {"a", "b"};
static const char *const T_AC[] = {"a", "c"};
static const char *const T_BA[] = {"b", "a"};
static const char *const T_ABC[] = {"a", "b", "c"};
static const char *const T_CBA[] = {"c", "b", "a"};

static void test_no_tags(void)
{
    struct tags tags = NO_TAGS;

    assert(tags.tags == NULL);
    assert(tags.n_tags == 0);
}

static void test_tags_various(void)
{
    struct tags tags_ab;
    struct tags tags_ab2;
    struct tags tags_ab3;
    struct tags tags_ba;
    struct tags tags_ac;
    struct tags tags_abc;
    struct tags tags_cba;
    struct tags tags_none = NO_TAGS;

    /* Static allocation */
    tags_ab.tags = (char **)T_AB;
    tags_ab.n_tags = 2;

    /* Dynamic allocations */
    assert(!tags_init(&tags_ab2, (char **)T_AB, 2));
    assert(tags_ab2.tags != NULL);
    assert(tags_ab2.n_tags == 2);
    assert(!tags_dup(&tags_ab3, &tags_ab2));
    assert(tags_ab3.tags != NULL);
    assert(tags_ab3.n_tags == 2);

    tags_ba.tags = (char **)T_BA;
    tags_ba.n_tags = 2;

    tags_ac.tags = (char **)T_AC;
    tags_ac.n_tags = 2;

    tags_abc.tags = (char **)T_ABC;
    tags_abc.n_tags = 3;

    tags_cba.tags = (char **)T_CBA;
    tags_cba.n_tags = 3;

    /* Equality */
    assert(tags_eq(&tags_ab, &tags_ab));
    assert(tags_eq(&tags_ab, &tags_ab2));
    assert(tags_eq(&tags_ab2, &tags_ab));
    assert(tags_eq(&tags_ab, &tags_ab3));
    assert(tags_eq(&tags_ab2, &tags_ab3));
    assert(!tags_eq(&tags_ab, &tags_ba));
    assert(!tags_eq(&tags_ab, &tags_ac));
    assert(!tags_eq(&tags_ab, &tags_abc));
    assert(!tags_eq(&tags_ab, &tags_none));

    /* Containment */
    assert(tags_in(&tags_abc, &tags_ab));
    assert(tags_in(&tags_cba, &tags_ab));
    assert(tags_in(&tags_ab, &tags_ab));
    assert(tags_in(&tags_ab, &tags_ba));
    assert(!tags_in(&tags_ac, &tags_ab));
    assert(!tags_in(&tags_none, &tags_ab));
    assert(tags_in(&tags_ab, &tags_none));
    assert(tags_in(&tags_none, &tags_none));

    /* Free */
    tags_free(&tags_ab2);
    tags_free(&tags_ab3);

    /* Don't segfault on double free */
    tags_free(&tags_ab2);
}

static void test_tags_dup(void)
{
    struct tags tags_src;
    struct tags tags_dst;

    tags_src.tags = (char **)T_AB;
    tags_src.n_tags = 2;

    /* Should not segfault */
    assert(!tags_dup(NULL, NULL));
    assert(!tags_dup(NULL, &tags_src));

    /* tags_dst should be equal to NO_TAGS */
    assert(!tags_dup(&tags_dst, NULL));
    assert(tags_eq(&tags_dst, &NO_TAGS));

    /* Standard dup */
    assert(!tags_dup(&tags_dst, &tags_src));
    assert(tags_eq(&tags_dst, &tags_dst));
    assert(!tags_eq(&tags_dst, &NO_TAGS));

    tags_free(&tags_dst);
}

static void test_str2tags(void)
{
    struct tags tags_new = {};
    struct tags tags_abc = {};
    char *tags_as_string = "";

    /* empty string */
    assert(str2tags(tags_as_string, &tags_new) == 0);
    assert(tags_eq(&tags_abc, &tags_new));

    /* 3 tags */
    tags_abc.tags = (char **)T_ABC;
    tags_abc.n_tags = 3;

    tags_as_string = "a,b,c";
    assert(str2tags(tags_as_string, &tags_new) == 0);
    assert(tags_eq(&tags_abc, &tags_new));
}

static void test_fill_put_params(void)
{
    static struct pho_xfer_desc empty_xfer;
    struct pho_xfer_desc xfer;
    char *alias_name_full = "full-test";
    char *alias_name_no_family = "empty-family-test";
    char *alias_name_no_layout = "empty-layout-test";
    char *alias_name_no_tags = "empty-tag-test";
    char *pre_existing_tag[1];

    pre_existing_tag[0] = "bar-tag";

    /* set family to invalid */
    empty_xfer.xd_params.put.family = PHO_RSC_INVAL;

    /* test default values */
    xfer = empty_xfer;

    assert(fill_put_params(&xfer) == 0);

    assert(strcmp(xfer.xd_params.put.layout_name, "simple") == 0);
    assert(xfer.xd_params.put.family == PHO_RSC_TAPE);
    assert(xfer.xd_params.put.tags.n_tags == 0);

    /* test full alias */
    xfer = empty_xfer;
    xfer.xd_params.put.alias = alias_name_full;

    assert(fill_put_params(&xfer) == 0);

    assert(strcmp(xfer.xd_params.put.layout_name, "raid1") == 0);
    assert(xfer.xd_params.put.family == PHO_RSC_DIR);
    assert(xfer.xd_params.put.tags.n_tags == 1);
    assert(strcmp(xfer.xd_params.put.tags.tags[0], "foo-tag") == 0);

    /* test alias without family */
    xfer = empty_xfer;
    xfer.xd_params.put.alias = alias_name_no_family;

    assert(fill_put_params(&xfer) == 0);

    assert(strcmp(xfer.xd_params.put.layout_name, "raid1") == 0);
    assert(xfer.xd_params.put.family == PHO_RSC_TAPE);
    assert(xfer.xd_params.put.tags.n_tags == 1);
    assert(strcmp(xfer.xd_params.put.tags.tags[0], "foo-tag") == 0);

    /* test alias without layout */
    xfer = empty_xfer;
    xfer.xd_params.put.alias = alias_name_no_layout;

    assert(fill_put_params(&xfer) == 0);

    assert(strcmp(xfer.xd_params.put.layout_name, "simple") == 0);
    assert(xfer.xd_params.put.family == PHO_RSC_DIR);
    assert(xfer.xd_params.put.tags.n_tags == 1);
    assert(strcmp(xfer.xd_params.put.tags.tags[0], "foo-tag") == 0);

    /* test alias without tags */
    xfer = empty_xfer;
    xfer.xd_params.put.alias = alias_name_no_tags;

    assert(fill_put_params(&xfer) == 0);

    assert(strcmp(xfer.xd_params.put.layout_name, "raid1") == 0);
    assert(xfer.xd_params.put.family == PHO_RSC_DIR);
    assert(xfer.xd_params.put.tags.n_tags == 0);

    /* test additional parameters */
    xfer = empty_xfer;
    xfer.xd_params.put.alias = alias_name_full;
    xfer.xd_params.put.family = PHO_RSC_TAPE;
    xfer.xd_params.put.layout_name = "simple";
    tags_init(&xfer.xd_params.put.tags, (char **)pre_existing_tag, 1);

    assert(fill_put_params(&xfer) == 0);

    assert(xfer.xd_params.put.family == PHO_RSC_TAPE);
    assert(strcmp(xfer.xd_params.put.layout_name, "simple") == 0);
    assert(xfer.xd_params.put.tags.n_tags == 2);
    assert(strcmp(xfer.xd_params.put.tags.tags[0], "bar-tag") == 0);
    assert(strcmp(xfer.xd_params.put.tags.tags[1], "foo-tag") == 0);

}

int main(int argc, char **argv)
{
    load_config(argv[0]);

    test_env_initialize();
    test_no_tags();
    test_tags_various();
    test_tags_dup();
    test_str2tags();
    test_fill_put_params();

    return EXIT_SUCCESS;
}
