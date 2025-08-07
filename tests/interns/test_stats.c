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
                PHO_STAT_COUNTER, "int", "counter", "increment=1");
    assert_non_null(stat);

    pho_stat_incr(stat, 5);
    assert_int_equal(pho_stat_get(stat), 5);

    // increment again
    pho_stat_incr(stat, 42);
    assert_int_equal(pho_stat_get(stat), 47);

    pho_stat_destroy(&stat);
}

static void test_int_gauge(void **state)
{
    struct pho_stat *stat = pho_stat_create(
                PHO_STAT_GAUGE, "int", "gauge", "set=1");
    assert_non_null(stat);

    pho_stat_set(stat, 10);
    assert_int_equal(pho_stat_get(stat), 10);

    // set again
    pho_stat_set(stat, 2736);
    assert_int_equal(pho_stat_get(stat), 2736);

    // gauges can also be incremented
    pho_stat_incr(stat, 12);
    assert_int_equal(pho_stat_get(stat), 2748);

    pho_stat_destroy(&stat);
}

/**
 * Test creating stats with different sizes.
 * Test the tag matching, as well as the namespace matching.
 */
static void test_iterators(void **state)
{
    struct pho_stat *stat1 = pho_stat_create(PHO_STAT_GAUGE, "ns1", "stat1",
                                             "");
    struct pho_stat *stat2 = pho_stat_create(PHO_STAT_COUNTER, "ns1", "stat2",
                                             "tag1=value1");
    struct pho_stat *stat3 = pho_stat_create(PHO_STAT_GAUGE, "ns2", "stat3",
                                             "tag1=value1,tag2=value2");
    struct pho_stat *stat4 = pho_stat_create(PHO_STAT_COUNTER, "ns2", "stat4",
                                             "tag1=value2,tag2=value2");
    struct pho_stat *stat5 = pho_stat_create(PHO_STAT_COUNTER, "ns2", "stat5",
                                         "tag1=value3,tag2=value4,tag3=value5");

    assert_non_null(stat1);
    assert_non_null(stat2);
    assert_non_null(stat3);
    assert_non_null(stat4);
    assert_non_null(stat5);

    /* Test tag matching and namespace matching using iterators */
    struct pho_stat_iter *iter;

    /* Test empty tag list */
    pho_debug("Test iterator with empty tag list");
    iter = pho_stat_iter_init("ns1", "stat1", NULL);
    assert_non_null(iter);
    assert_ptr_equal(pho_stat_iter_next(iter), stat1);

    /* make sure pho_stat_iter_next return no other match */
    assert_ptr_equal(pho_stat_iter_next(iter), NULL);
    pho_stat_iter_close(iter);

    /* Test single tag and no namespace.
     * Note this expects stat creation order to be preserved.
     */
    pho_debug("Test iterator with single tag and no namespace");
    iter = pho_stat_iter_init(NULL, NULL, "tag1=value1");
    assert_non_null(iter);
    assert_ptr_equal(pho_stat_iter_next(iter), stat2);
    assert_ptr_equal(pho_stat_iter_next(iter), stat3);
    pho_stat_iter_close(iter);

    /* Same test with a namespace */
    pho_debug("Test iterator with namespace");
    iter = pho_stat_iter_init("ns1", NULL, "tag1=value1");
    assert_non_null(iter);
    assert_ptr_equal(pho_stat_iter_next(iter), stat2);
    assert_ptr_equal(pho_stat_iter_next(iter), NULL);
    pho_stat_iter_close(iter);

    /* Same test with different case  */
    pho_debug("Test iterator case insensitivity");
    iter = pho_stat_iter_init("Ns1", "", "Tag1=Value1");
    assert_non_null(iter);
    assert_ptr_equal(pho_stat_iter_next(iter), stat2);
    assert_ptr_equal(pho_stat_iter_next(iter), NULL);
    pho_stat_iter_close(iter);

    /* Test multiple tags */
    pho_debug("Test iterator with multiple tags");
    iter = pho_stat_iter_init(NULL, NULL, "tag1=value1,tag2=value2");
    assert_non_null(iter);
    assert_ptr_equal(pho_stat_iter_next(iter), stat3);
    assert_ptr_equal(pho_stat_iter_next(iter), NULL);
    pho_stat_iter_close(iter);

    /* Test insensitive matching of name*/
    pho_debug("Test iterator insensitive matching of name");
    iter = pho_stat_iter_init("ns1", "Stat1", NULL);
    assert_non_null(iter);
    assert_ptr_equal(pho_stat_iter_next(iter), stat1);
    assert_ptr_equal(pho_stat_iter_next(iter), NULL);
    pho_stat_iter_close(iter);

    pho_stat_destroy(&stat1);
    pho_stat_destroy(&stat2);
    pho_stat_destroy(&stat3);
    pho_stat_destroy(&stat4);
    pho_stat_destroy(&stat5);
}

int main(void)
{
    const struct CMUnitTest pho_stats_test[] = {
        cmocka_unit_test(test_int_counter),
        cmocka_unit_test(test_int_gauge),
        cmocka_unit_test(test_iterators),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(pho_stats_test, NULL, NULL);
}
