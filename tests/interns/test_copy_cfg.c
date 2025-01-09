#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pho_cfg.h"
#include "pho_common.h"

#include <cmocka.h>

static void gpo_valid_multiple_tokens(void **state)
{
    char **res = NULL;
    size_t count;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_COPY_get_preferred_order", "fast,cache", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_preferred_order(&res, &count);
    assert_int_equal(rc, -rc);
    assert_int_equal(count, 2);
    assert_string_equal(res[0], "fast");
    assert_string_equal(res[1], "cache");

    free(res[0]);
    free(res[1]);
    free(res);
}

static void gpo_valid_one_token(void **state)
{
    char **res = NULL;
    size_t count;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_COPY_get_preferred_order", "fast", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_preferred_order(&res, &count);
    assert_int_equal(rc, -rc);
    assert_int_equal(count, 1);
    assert_string_equal(res[0], "fast");

    free(res[0]);
    free(res);
}

static void gpo_valid_no_token(void **state)
{
    char **res = NULL;
    size_t count;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_COPY_get_preferred_order", "", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_preferred_order(&res, &count);
    assert_int_equal(rc, -EINVAL);

    free(res);
}

static void gpo_not_set(void **state)
{
    char **res = NULL;
    size_t count;
    int rc;

    (void)state;

    rc = get_cfg_preferred_order(&res, &count);
    assert_int_equal(rc, -ENODATA);
}

static void gpo_invalid(void **state)
{
    char **res = NULL;
    size_t count;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_COPY_get_preferred_order", ",", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_preferred_order(&res, &count);
    assert_int_equal(rc, -EINVAL);

    free(res);
}

int main(void)
{
    const struct CMUnitTest get_preferred_order_test_cases[] = {
        cmocka_unit_test(gpo_not_set),
        cmocka_unit_test(gpo_valid_multiple_tokens),
        cmocka_unit_test(gpo_valid_one_token),
        cmocka_unit_test(gpo_valid_no_token),
        cmocka_unit_test(gpo_invalid),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(get_preferred_order_test_cases, NULL, NULL);
}
