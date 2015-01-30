/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */

/**
 * \brief  Test logging API
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>
#include "pho_common.h"
#include "pho_test_utils.h"


static bool recv_dbg;
static bool recv_vrb;
static bool recv_nfo;
static bool recv_err;


static int test1(void *hint)
{
    enum pho_log_level i;

    for (i = PHO_LOG_DISABLED; i <= PHO_LOG_DEBUG; i++) {
        pho_log_level_set(i);
        pho_dbg("TEST DEBUG");
        pho_vrb("TEST VERBOSE");
        pho_nfo("TEST INFO");
        pho_err(-EINVAL, "TEST ERROR");
    }
    return 0;
}

static void test2_cb(const struct pho_logrec *rec)
{
    switch (rec->plr_level) {
    case PHO_LOG_DEBUG:
        recv_dbg = true;
        break;
    case PHO_LOG_VERB:
        recv_vrb = true;
        break;
    case PHO_LOG_INFO:
        recv_nfo = true;
        break;
    case PHO_LOG_ERROR:
        recv_err = true;
        break;
    case PHO_LOG_DISABLED:
        /* Make sure they notice we got something */
        recv_dbg = recv_vrb = recv_nfo = recv_err = true;
        break;
    }
}

static void pretest_flags_reset(void)
{
    recv_dbg = recv_vrb = recv_nfo = recv_err = false;
}

static bool posttest_flags_test(enum pho_log_level level)
{
    switch (level) {
    case PHO_LOG_DEBUG:
        return recv_dbg && recv_vrb && recv_nfo && recv_err;
    case PHO_LOG_VERB:
        return !recv_dbg && recv_vrb && recv_nfo && recv_err;
    case PHO_LOG_INFO:
        return !recv_dbg && !recv_vrb && recv_nfo && recv_err;
    case PHO_LOG_ERROR:
        return !recv_dbg && !recv_vrb && !recv_nfo && recv_err;
    case PHO_LOG_DISABLED:
        return !recv_dbg && !recv_vrb && !recv_nfo && !recv_err;
    default:
        return false;
    }
}

static int test2(void *hint)
{
    enum pho_log_level i;
    bool            is_valid;

    pho_log_callback_set(test2_cb);
    for (i = PHO_LOG_DISABLED; i <= PHO_LOG_DEBUG; i++) {
        pretest_flags_reset();
        pho_log_level_set(i);
        pho_dbg("TEST DEBUG");
        pho_vrb("TEST VERBOSE");
        pho_nfo("TEST INFO");
        pho_err(-EINVAL, "TEST ERROR");
        is_valid = posttest_flags_test(i);
        if (!is_valid)
            return -EINVAL;
    }
    return 0;
}

static int test3(void *hint)
{
    pho_log_level_set(PHO_LOG_INFO);

    errno = ESHUTDOWN;
    pho_nfo("test");
    if (errno != ESHUTDOWN)
        return -EINVAL;

    /* Works with zero too? */
    errno = 0;
    pho_nfo("test");
    if (errno != 0)
        return -EINVAL;

    return 0;
}

int main(int ac, char **av)
{
    run_test("Test 1: exercise default callback on all log levels",
             test1, NULL, PHO_TEST_SUCCESS);

    run_test("Test 2: register custom callback",
             test2, NULL, PHO_TEST_SUCCESS);

    run_test("Test 3: emitting logs should not alter errno",
             test3, NULL, PHO_TEST_SUCCESS);

    printf("MAPPER: All tests succeeded\n");
    return 0;
}
