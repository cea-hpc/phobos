#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lrs_cfg.h"
#include "pho_types.h"

#include <cmocka.h>

#define ASSERT_VALID_GET_TIME(rc, res, sec, nsec) \
do {                                              \
    assert_int_equal(rc, -rc);                    \
    assert_int_equal(res.tv_sec, sec);            \
    assert_int_equal(res.tv_nsec, nsec);          \
} while (0)

static void gcttv_valid_multiple_tokens(void **state)
{
    struct timespec res;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_LRS_sync_time_threshold",
                "dir=0,disk=1,tape=1000003", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_time_threshold_value(PHO_RSC_DIR, &res);
    ASSERT_VALID_GET_TIME(rc, res, 0, 0);

    rc = get_cfg_time_threshold_value(PHO_RSC_DISK, &res);
    ASSERT_VALID_GET_TIME(rc, res, 0, 1000000);

    rc = get_cfg_time_threshold_value(PHO_RSC_TAPE, &res);
    ASSERT_VALID_GET_TIME(rc, res, 1000, 3000000);
}

static void gcttv_valid_sole_token(void **state)
{
    struct timespec res;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_LRS_sync_time_threshold", "dir=1", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_time_threshold_value(PHO_RSC_DIR, &res);
    ASSERT_VALID_GET_TIME(rc, res, 0, 1000000);

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

#define ASSERT_VALID_GET_NB_REQ(rc, res, val) \
do {                                          \
    assert_int_equal(rc, -rc);                \
    assert_int_equal(res, val);               \
} while (0)

static void gcntv_valid_multiple_tokens(void **state)
{
    unsigned int res;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_LRS_sync_nb_req_threshold", "dir=1,tape=20", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_nb_req_threshold_value(PHO_RSC_DIR, &res);
    ASSERT_VALID_GET_NB_REQ(rc, res, 1);

    rc = get_cfg_nb_req_threshold_value(PHO_RSC_TAPE, &res);
    ASSERT_VALID_GET_NB_REQ(rc, res, 20);
}

static void gcntv_valid_sole_token(void **state)
{
    unsigned int res;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_LRS_sync_nb_req_threshold", "dir=10", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_nb_req_threshold_value(PHO_RSC_DIR, &res);
    ASSERT_VALID_GET_NB_REQ(rc, res, 10);

    rc = get_cfg_nb_req_threshold_value(PHO_RSC_TAPE, &res);
    assert_int_equal(rc, -EINVAL);
}

static void gcntv_valid_no_token(void **state)
{
    unsigned int res;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_LRS_sync_nb_req_threshold", "", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_nb_req_threshold_value(PHO_RSC_DIR, &res);
    assert_int_equal(rc, -EINVAL);

    rc = get_cfg_nb_req_threshold_value(PHO_RSC_TAPE, &res);
    assert_int_equal(rc, -EINVAL);
}

static void gcntv_invalid_strings(void **state)
{
    unsigned int res;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_LRS_sync_nb_req_threshold",
                "dir=60p,disk=inval,tape=", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_nb_req_threshold_value(PHO_RSC_DIR, &res);
    assert_int_equal(rc, -EINVAL);

    rc = get_cfg_nb_req_threshold_value(PHO_RSC_DISK, &res);
    assert_int_equal(rc, -EINVAL);

    rc = get_cfg_nb_req_threshold_value(PHO_RSC_TAPE, &res);
    assert_int_equal(rc, -EINVAL);
}

static void gcntv_invalid_numbers(void **state)
{
    unsigned int res;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_LRS_sync_nb_req_threshold",
                "dir=-1,disk=0,tape=10000000000", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_nb_req_threshold_value(PHO_RSC_DIR, &res);
    assert_int_equal(rc, -ERANGE);

    rc = get_cfg_nb_req_threshold_value(PHO_RSC_DISK, &res);
    assert_int_equal(rc, -ERANGE);

    rc = get_cfg_nb_req_threshold_value(PHO_RSC_TAPE, &res);
    assert_int_equal(rc, -ERANGE);
}

#define ASSERT_VALID_GET_WRITTEN_SIZE(rc, res, val) \
do {                                                \
    assert_int_equal(rc, -rc);                      \
    assert_int_equal(res, val);                     \
} while (0)

static void gcwtv_valid_multiple_tokens(void **state)
{
    unsigned long res;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_LRS_sync_written_size_threshold", "dir=1,tape=20", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_written_size_threshold_value(PHO_RSC_DIR, &res);
    ASSERT_VALID_GET_NB_REQ(rc, res, 1024);

    rc = get_cfg_written_size_threshold_value(PHO_RSC_TAPE, &res);
    ASSERT_VALID_GET_NB_REQ(rc, res, 20 * 1024);
}

static void gcwtv_valid_sole_token(void **state)
{
    unsigned long res;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_LRS_sync_written_size_threshold", "dir=10", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_written_size_threshold_value(PHO_RSC_DIR, &res);
    ASSERT_VALID_GET_NB_REQ(rc, res, 10 * 1024);

    rc = get_cfg_written_size_threshold_value(PHO_RSC_TAPE, &res);
    assert_int_equal(rc, -EINVAL);
}

static void gcwtv_valid_no_token(void **state)
{
    unsigned long res;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_LRS_sync_written_size_threshold", "", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_written_size_threshold_value(PHO_RSC_DIR, &res);
    assert_int_equal(rc, -EINVAL);

    rc = get_cfg_written_size_threshold_value(PHO_RSC_TAPE, &res);
    assert_int_equal(rc, -EINVAL);
}

static void gcwtv_invalid_strings(void **state)
{
    unsigned long res;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_LRS_sync_written_size_threshold",
                "dir=60p,disk=inval,tape=", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_written_size_threshold_value(PHO_RSC_DIR, &res);
    assert_int_equal(rc, -EINVAL);

    rc = get_cfg_written_size_threshold_value(PHO_RSC_DISK, &res);
    assert_int_equal(rc, -EINVAL);

    rc = get_cfg_written_size_threshold_value(PHO_RSC_TAPE, &res);
    assert_int_equal(rc, -EINVAL);
}

static void gcwtv_invalid_numbers(void **state)
{
    unsigned long res;
    int rc;

    (void)state;

    rc = setenv("PHOBOS_LRS_sync_written_size_threshold",
                "dir=-1,disk=0,tape=20000000000000000", 1);
    assert_int_equal(rc, -rc);

    rc = get_cfg_written_size_threshold_value(PHO_RSC_DIR, &res);
    assert_int_equal(rc, -ERANGE);

    rc = get_cfg_written_size_threshold_value(PHO_RSC_DISK, &res);
    assert_int_equal(rc, -ERANGE);

    rc = get_cfg_written_size_threshold_value(PHO_RSC_TAPE, &res);
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

    const struct CMUnitTest get_nb_req_threshold_test_cases[] = {
        cmocka_unit_test(gcntv_valid_multiple_tokens),
        cmocka_unit_test(gcntv_valid_sole_token),
        cmocka_unit_test(gcntv_valid_no_token),
        cmocka_unit_test(gcntv_invalid_strings),
        cmocka_unit_test(gcntv_invalid_numbers),
    };

    const struct CMUnitTest get_wsize_threshold_test_cases[] = {
        cmocka_unit_test(gcwtv_valid_multiple_tokens),
        cmocka_unit_test(gcwtv_valid_sole_token),
        cmocka_unit_test(gcwtv_valid_no_token),
        cmocka_unit_test(gcwtv_invalid_strings),
        cmocka_unit_test(gcwtv_invalid_numbers),
    };

    return cmocka_run_group_tests(get_time_threshold_test_cases, NULL, NULL)
        + cmocka_run_group_tests(get_nb_req_threshold_test_cases, NULL, NULL)
        + cmocka_run_group_tests(get_wsize_threshold_test_cases, NULL, NULL);
}

