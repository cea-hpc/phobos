/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos Local Resource Scheduler (LRS)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_lrs.h"
#include "pho_common.h"
#include <string.h>

#ifdef _TEST
#include <stdlib.h>

/** Default mount point for tests */
#define TEST_DEFAULT_MNT        "/tmp/tape0"
#define TEST_DEFAULT_ADDR_TYPE  PHO_ADDR_HASH1

/** @FIXME TEST ONLY */
static const char *get_test_root(void)
{
    char *var;

    var = getenv("PHO_TEST_MNT");
    return (var != NULL ? var : TEST_DEFAULT_MNT);
}


/** @FIXME TEST ONLY */
static void set_test_loc(struct data_loc *loc, size_t size)
{
    char *var;

    loc->root_path = g_string_new(get_test_root());

    loc->extent.layout_idx = 0;
    loc->extent.size = size;
    loc->extent.media.type = PHO_MED_TAPE;
    strncpy(loc->extent.media.id_u.label, "L00001",
            sizeof(loc->extent.media.id_u.label));
    loc->extent.fs_type = PHO_FS_POSIX;

    var = getenv("PHO_TEST_ADDR_TYPE");
    if (var == NULL)
        loc->extent.addr_type = TEST_DEFAULT_ADDR_TYPE;
    else if (!strcasecmp(var, "path"))
        loc->extent.addr_type = PHO_ADDR_PATH;
    else if (!strcasecmp(var, "hash"))
        loc->extent.addr_type = PHO_ADDR_HASH1;
    else {
        pho_warn("unsupported address type '%s': using default", var);
        loc->extent.addr_type = TEST_DEFAULT_ADDR_TYPE;
    }

    loc->extent.address = PHO_BUFF_NULL;
}
#endif

int lrs_write_intent(size_t size, const struct layout_descr *layout,
                     struct data_loc *loc)
{
#ifdef _TEST
    set_test_loc(loc, size);
    return 0;
#endif
    return -ENOTSUP;
}

int lrs_read_intent(const struct layout_descr *layout, struct data_loc *loc)
{
#ifdef _TEST
    loc->root_path = g_string_new(get_test_root());
    return 0;
#endif
    return -ENOTSUP;
}

int lrs_done(struct data_loc *loc)
{
#ifdef _TEST
    loc->root_path = g_string_new(get_test_root());
#endif
    return -ENOTSUP;
}
