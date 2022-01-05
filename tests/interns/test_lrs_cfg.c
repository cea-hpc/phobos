#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lrs_cfg.h"
#include "pho_types.h"

#include <cmocka.h>

static void gcttv_valid_multiple_tokens(void **state)
{
    struct timespec res;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_LRS_sync_time_threshold",
                "dir=0,disk=1,tape=1000003", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_time_threshold_value(PHO_RSC_DIR, &res);
    assert_int_equal(rc, -rc);
    assert_int_equal(res.tv_sec, 0);
    assert_int_equal(res.tv_nsec, 0);

    rc = get_cfg_time_threshold_value(PHO_RSC_DISK, &res);
    assert_int_equal(rc, -rc);
    assert_int_equal(res.tv_sec, 0);
    assert_int_equal(res.tv_nsec, 1000000);

    rc = get_cfg_time_threshold_value(PHO_RSC_TAPE, &res);
    assert_int_equal(rc, -rc);
    assert_int_equal(res.tv_sec, 1000);
    assert_int_equal(res.tv_nsec, 3000000);
}

static void gcttv_valid_sole_token(void **state)
{
    struct timespec res;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_LRS_sync_time_threshold", "dir=1", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_time_threshold_value(PHO_RSC_DIR, &res);
    assert_int_equal(rc, -rc);
    assert_int_equal(res.tv_sec, 0);
    assert_int_equal(res.tv_nsec, 1000000);

    rc = get_cfg_time_threshold_value(PHO_RSC_TAPE, &res);
    assert_int_equal(rc, -EINVAL);
}

static void gcttv_valid_no_token(void **state)
{
    struct timespec res;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_LRS_sync_time_threshold", "", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_time_threshold_value(PHO_RSC_DIR, &res);
    assert_int_equal(rc, -EINVAL);

    rc = get_cfg_time_threshold_value(PHO_RSC_TAPE, &res);
    assert_int_equal(rc, -EINVAL);
}

static void gcttv_invalid_strings(void **state)
{
    struct timespec res;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_LRS_sync_time_threshold",
                "dir=60p,disk=inval,tape=", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_time_threshold_value(PHO_RSC_DIR, &res);
    assert_int_equal(rc, -EINVAL);

    rc = get_cfg_time_threshold_value(PHO_RSC_DISK, &res);
    assert_int_equal(rc, -EINVAL);

    rc = get_cfg_time_threshold_value(PHO_RSC_TAPE, &res);
    assert_int_equal(rc, -EINVAL);
}

static void gcttv_invalid_numbers(void **state)
{
    struct timespec res;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_LRS_sync_time_threshold",
                "dir=-1,tape=20000000000000000000", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_time_threshold_value(PHO_RSC_DIR, &res);
    assert_int_equal(rc, -ERANGE);

    rc = get_cfg_time_threshold_value(PHO_RSC_TAPE, &res);
    assert_int_equal(rc, -ERANGE);
}

int main(void)
{
    const struct CMUnitTest get_time_threshold_test_cases[] = {
        cmocka_unit_test(gcttv_valid_multiple_tokens),
        cmocka_unit_test(gcttv_valid_sole_token),
        cmocka_unit_test(gcttv_valid_no_token),
        cmocka_unit_test(gcttv_invalid_strings),
        cmocka_unit_test(gcttv_invalid_numbers),
    };

    return cmocka_run_group_tests(get_time_threshold_test_cases, NULL, NULL);
}

