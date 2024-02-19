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
 * \brief  Tests for phobos_admin_medium_locate function
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "pho_common.h"
#include "pho_cache.h"

struct test_cache_env {
    size_t nb_build;
    size_t nb_destroy;
    char *new_build_value;
};

struct test_state {
    struct pho_cache *cache;
    struct test_cache_env env;
};

static struct key_value *test_cache_build(const void *_key, void *_env)
{
    struct test_cache_env *env = _env;
    const char *key = _key;
    const char *value;

    if (env->new_build_value)
        value = env->new_build_value;
    else
        value = key;

    env->nb_build++;
    return key_value_alloc((void *)key, (void *)value, strlen(value) + 1);
}

static struct key_value *test_cache_value2kv(void *key, void *_value)
{
    char *value = _value;

    return key_value_alloc(key, value, strlen(value) + 1);
}

static void test_cache_destroy(struct key_value *kv, void *_env)
{
    struct test_cache_env *env = _env;

    env->nb_destroy++;
    free(kv);
}

struct pho_cache_operations test_cache_operations = {
    .pco_hash     = g_str_hash,
    .pco_equal    = g_str_equal,
    .pco_build    = test_cache_build,
    .pco_value2kv = test_cache_value2kv,
    .pco_destroy  = test_cache_destroy,
};

static int test_setup(void **_state)
{
    struct test_state *state;

    state = xcalloc(1, sizeof(*state));
    state->cache = pho_cache_init("test_cache", &test_cache_operations,
                                  &state->env);
    *_state = state;

    return 0;
}

static int test_cleanup(void **_state)
{
    struct test_state *state = *_state;

    pho_cache_destroy(state->cache);
    free(state);

    return 0;
}

static int subtest_teardown(void **_state)
{
    struct test_state *state = *_state;

    memset(&state->env, 0, sizeof(state->env));

    return 0;
}

static void pho_cache_acquire_release(void **_state)
{
    struct test_state *state = *_state;
    char *value;

    value = pho_cache_acquire(state->cache, "test");
    assert_string_equal("test", value);
    assert_int_equal(state->env.nb_build, 1);

    pho_cache_release(state->cache, value);
    assert_int_equal(state->env.nb_destroy, 1);
}

static void pho_cache_2_acquire_release(void **_state)
{
    struct test_state *state = *_state;
    char *value1;
    char *value2;

    value1 = pho_cache_acquire(state->cache, "test");
    assert_string_equal("test", value1);
    assert_int_equal(state->env.nb_build, 1);

    value2 = pho_cache_acquire(state->cache, "test");
    assert_string_equal("test", value2);
    assert_int_equal(state->env.nb_build, 1);
    assert_ptr_equal(value1, value2);

    pho_cache_release(state->cache, value2);
    assert_int_equal(state->env.nb_destroy, 0);

    pho_cache_release(state->cache, value1);
    assert_int_equal(state->env.nb_destroy, 1);
}

static void pho_cache_insert_new_value(void **_state)
{
    struct test_state *state = *_state;
    char *value1;
    char *value2;

    value1 = pho_cache_acquire(state->cache, "key");
    assert_string_equal(value1, "key");

    value2 = pho_cache_insert(state->cache, "key", "new_value");
    assert_string_equal(value1, "key");
    assert_string_equal(value2, "new_value");

    pho_cache_release(state->cache, value1);
    assert_int_equal(state->env.nb_destroy, 1);

    pho_cache_release(state->cache, value2);
    assert_int_equal(state->env.nb_destroy, 2);
}

static void pho_cache_update_value(void **_state)
{
    struct test_state *state = *_state;
    char *value1;
    char *value2;

    value1 = pho_cache_acquire(state->cache, "test");
    assert_string_equal(value1, "test");
    assert_int_equal(state->env.nb_build, 1);

    state->env.new_build_value = "new_value";
    value2 = pho_cache_update(state->cache, "test");
    assert_ptr_not_equal(value1, value2);
    assert_int_equal(state->env.nb_build, 2);

    pho_cache_release(state->cache, value1);
    assert_int_equal(state->env.nb_destroy, 1);

    pho_cache_release(state->cache, value2);
    assert_int_equal(state->env.nb_destroy, 2);
}

int main(void)
{
    const struct CMUnitTest pho_cache_test[] = {
        cmocka_unit_test_teardown(pho_cache_acquire_release,  subtest_teardown),
        cmocka_unit_test_teardown(pho_cache_2_acquire_release,
                                  subtest_teardown),
        cmocka_unit_test_teardown(pho_cache_insert_new_value, subtest_teardown),
        cmocka_unit_test_teardown(pho_cache_update_value,     subtest_teardown),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(pho_cache_test, test_setup, test_cleanup);
}
