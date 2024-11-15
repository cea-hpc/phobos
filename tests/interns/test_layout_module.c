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
 * \brief  Test layout loading
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#include "pho_layout.h"

#include <cmocka.h>

static void le_valid_module(void **data)
{
    struct pho_xfer_target target = {0};
    struct pho_encoder encoder = {0};
    struct pho_xfer_desc xfer = {0};
    int rc;

    target.xt_objid = "oid";
    target.xt_size = 0;
    xfer.xd_ntargets = 1;
    xfer.xd_targets = &target;
    xfer.xd_params.put.layout_name = "raid1";
    xfer.xd_params.put.lyt_params.attr_set = NULL;

    rc = layout_encode(&encoder, &xfer);
    assert_return_code(rc, -rc);
    layout_destroy(&encoder);
}

static void le_invalid_module(void **data)
{
    struct pho_xfer_target target = {0};
    struct pho_encoder encoder = {0};
    struct pho_xfer_desc xfer = {0};
    int rc;

    target.xt_objid = "oid";
    xfer.xd_ntargets = 1;
    xfer.xd_targets = &target;
    xfer.xd_params.put.layout_name = "unknown";

    rc = layout_encode(&encoder, &xfer);
    assert_int_equal(rc, -EINVAL);
}

static void le_invalid_layout_io_size(void **data)
{
    struct pho_xfer_target target = {0};
    struct pho_encoder encoder = {0};
    struct pho_xfer_desc xfer = {0};
    int rc;

    target.xt_objid = "oid";
    target.xt_size = 0;
    xfer.xd_ntargets = 1;
    xfer.xd_targets = &target;
    xfer.xd_params.put.layout_name = "raid1";
    xfer.xd_params.put.lyt_params.attr_set = NULL;

    rc = setenv("PHOBOS_IO_io_block_size", "-1", 1);
    assert_int_equal(rc, 0);

    rc = layout_encode(&encoder, &xfer);
    assert_int_equal(rc, -EINVAL);

    rc = setenv("PHOBOS_IO_io_block_size", "bla", 1);
    assert_int_equal(rc, 0);

    rc = layout_encode(&encoder, &xfer);
    assert_int_equal(rc, -EINVAL);

    /* integer beyond 64bits (highest 64bits ~ 18*10^18) */
    rc = setenv("PHOBOS_IO_io_block_size", "19446744073709551615", 1);
    assert_int_equal(rc, 0);

    rc = layout_encode(&encoder, &xfer);
    assert_int_equal(rc, -EINVAL);
}

static void le_valid_layout_io_size(void **data)
{
    struct pho_xfer_target target = {0};
    struct pho_encoder encoder = {0};
    struct pho_xfer_desc xfer = {0};
    int rc;

    target.xt_objid = "oid";
    target.xt_size = 0;
    xfer.xd_ntargets = 1;
    xfer.xd_targets = &target;
    xfer.xd_params.put.layout_name = "raid1";
    xfer.xd_params.put.lyt_params.attr_set = NULL;

    /* 0 is allowed in cfg */
    rc = setenv("PHOBOS_IO_io_block_size", "0", 1);

    rc = layout_encode(&encoder, &xfer);
    assert_int_equal(rc, 0);
    assert_int_equal(encoder.io_block_size, 0);
    layout_destroy(&encoder);

    /* other positive value is allowed */
    rc = setenv("PHOBOS_IO_io_block_size", "1024", 1);

    rc = layout_encode(&encoder, &xfer);
    assert_int_equal(rc, 0);
    assert_int_equal(encoder.io_block_size, 1024);
    layout_destroy(&encoder);
}



int main(void)
{
    const struct CMUnitTest layout_module_tests[] = {
        cmocka_unit_test(le_valid_module),
        cmocka_unit_test(le_invalid_module),
        cmocka_unit_test(le_invalid_layout_io_size),
        cmocka_unit_test(le_valid_layout_io_size),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(layout_module_tests, NULL, NULL);
}
