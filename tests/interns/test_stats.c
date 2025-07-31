/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2025 CEA/DAM.
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
 * \brief  Test stats API
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "pho_stats.h"
#include "pho_test_utils.h"

static void test_int_counter(void **state)
{
    struct pho_stat *stat = pho_stat_create(
                PHO_STAT_COUNTER, "int", "counter", "increment");
    assert_non_null(stat);

    pho_stat_incr(stat, 5);
    assert_int_equal(pho_stat_get(stat), 5);

    // increment again
    pho_stat_incr(stat, 42);
    assert_int_equal(pho_stat_get(stat), 47);

    pho_stat_free(stat);
}

static void test_int_gauge(void **state)
{
    struct pho_stat *stat = pho_stat_create(
                PHO_STAT_GAUGE, "int", "gauge", "set");
    assert_non_null(stat);

    pho_stat_set(stat, 10);
    assert_int_equal(pho_stat_get(stat), 10);

    // set again
    pho_stat_set(stat, 2736);
    assert_int_equal(pho_stat_get(stat), 2736);

    // gauges can also be incremented
    pho_stat_incr(stat, 12);
    assert_int_equal(pho_stat_get(stat), 2748);

    pho_stat_free(stat);
}


int main(void)
{
    const struct CMUnitTest pho_stats_test[] = {
        cmocka_unit_test(test_int_counter),
        cmocka_unit_test(test_int_gauge)
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(pho_stats_test, NULL, NULL);
}
