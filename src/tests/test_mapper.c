/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */

/**
 * \brief  Test mapper API
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_mapper.h"
#include "pho_test_utils.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

/* Long string, 240 * 'a' */
#define _240_TIMES_A    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"\
                        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"\
                        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"\
                        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"\
                        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"

static inline int is_prefix_chr_valid(int c)
{
    switch (tolower(c)) {
    case '/':
    case '_':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
          return 1;
    default:
          break;
    }
    return 0;
}

static int is_path_valid(const char *path)
{
    size_t  path_len = strlen(path);
    int     dots = 0;
    int     i;

    if (path_len < 15 || path_len > NAME_MAX)
        return 0;

    /* Path are of the form: '5f/e7/5fe739a2_<obj>[.<tag>]'
     * Check the first (hashed) part first */
    for (i = 0; i < PHO_MAPPER_PREFIX_LENGTH; i++) {
        if (!is_prefix_chr_valid(path[i]))
            return 0;
    }

    for (i = 6; i < path_len; i++) {
        /* hackish check, no more that one '.' is allowed */
        if (path[i] == '.' && dots++)
            return 0;

        if (path[i] != '.' && !pho_mapper_chr_valid(path[i]))
            return 0;
    }

    return 1;
}

#define SAFE_STR(o) ((o) != NULL ? (o) : "null")

static int test_build_path(const char *obj, const char *tag)
{
    char    buff[NAME_MAX + 1];
    size_t  buff_len = sizeof(buff);
    int     rc;

    rc = pho_mapper_extent_resolve(obj, tag, buff, buff_len);
    if (rc)
        return rc;

    printf("MAPPER: o='%s' t='%s': '%s'\n", SAFE_STR(obj), SAFE_STR(tag), buff);

    if (!is_path_valid(buff)) {
        fprintf(stderr, "[ERROR] Invalid path crafted: '%s'\n", buff);
        return -EINVAL;
    }

    return 0;
}

static int test14(void *hint)
{
    return pho_mapper_extent_resolve("a", "b", NULL, 0);
}

static int test15(void *hint)
{
    return pho_mapper_extent_resolve("a", "b", NULL, NAME_MAX + 1);
}

static int test16(void *hint)
{
    char    buff[2];

    return pho_mapper_extent_resolve("a", "b", buff, sizeof(buff));
}

static int test0(void *hint)
{
    return test_build_path("test", "p1");
}

static int test1(void *hint)
{
    return test_build_path("test", "");
}

static int test2(void *hint)
{
    return test_build_path("test", NULL);
}

static int test3(void *hint)
{
    return test_build_path("", "p1");
}

static int test4(void *hint)
{
    return test_build_path(NULL, "p1");
}

static int test5(void *hint)
{
    return test_build_path("test", _240_TIMES_A);
}

static int test6a(void *hint)
{
    return test_build_path("\x07test", "p1");
}

static int test6b(void *hint)
{
    return test_build_path("tes\x07t", "p1");
}

static int test6c(void *hint)
{
    return test_build_path("test\x07", "p1");
}

static int test6d(void *hint)
{
    return test_build_path("test", "\x07p1");
}

static int test6e(void *hint)
{
    return test_build_path("test", "p\x07z");
}

static int test6f(void *hint)
{
    return test_build_path("test", "p1\x07");
}

static int test7a(void *hint)
{
    return test_build_path("te<st", "p1");
}

static int test7b(void *hint)
{
    return test_build_path("te<<<<<<{{[[[st", "p1");
}

static int test7c(void *hint)
{
    return test_build_path("test.", "p1");
}

static int test8a(void *hint)
{
    return test_build_path("test", "p|1");
}

static int test8b(void *hint)
{
    return test_build_path("test", "<<{p1");
}

static int test8c(void *hint)
{
    return test_build_path("test", ".p1");
}

static int test9(void *hint)
{
    return test_build_path(_240_TIMES_A, "");
}

static int test10(void *hint)
{
    return test_build_path(_240_TIMES_A, NULL);
}

static int test11(void *hint)
{
    return test_build_path(_240_TIMES_A _240_TIMES_A, "p11");
}

static int test12(void *hint)
{
    return test_build_path(_240_TIMES_A, _240_TIMES_A);
}

static int test13(void *hint)
{
    char    buff1[NAME_MAX + 1];
    char    buff2[NAME_MAX + 1];
    int     rc;

    rc = pho_mapper_extent_resolve("a", "bc", buff1, sizeof(buff1));
    if (rc)
        return rc;

    rc = pho_mapper_extent_resolve("ab", "c", buff2, sizeof(buff2));
    if (rc)
        return rc;

    if (strcmp(buff1, buff2) == 0)
        return -EINVAL;

    return 0;
}

int main(int argc, char **argv)
{
    run_test("Test 0: Simple name crafting",
             test0, NULL, PHO_TEST_SUCCESS);

    run_test("Test 1: No tag (empty)",
             test1, NULL, PHO_TEST_SUCCESS);

    run_test("Test 2: No tag (null)",
             test2, NULL, PHO_TEST_SUCCESS);

    run_test("Test 3: No name (empty) (INVALID)",
             test3, NULL, PHO_TEST_FAILURE);

    run_test("Test 4: No name (null) (INVALID)",
             test4, NULL, PHO_TEST_FAILURE);

    run_test("Test 5: Long tag (INVALID)",
             test5, NULL, PHO_TEST_FAILURE);

    run_test("Test 6a: Non-printable chars in name (beginning)",
             test6a, NULL, PHO_TEST_SUCCESS);

    run_test("Test 6b: Non-printable chars in name (middle)",
             test6b, NULL, PHO_TEST_SUCCESS);

    run_test("Test 6c: Non-printable chars in name (end)",
             test6c, NULL, PHO_TEST_SUCCESS);

    run_test("Test 6d: Non-printable chars in tag (beginning)",
             test6d, NULL, PHO_TEST_SUCCESS);

    run_test("Test 6e: Non-printable chars in tag (middle)",
             test6e, NULL, PHO_TEST_SUCCESS);

    run_test("Test 6f: Non-printable chars in tag (end)",
             test6f, NULL, PHO_TEST_SUCCESS);

    run_test("Test 7a: Annoying shell specials chars",
             test7a, NULL, PHO_TEST_SUCCESS);

    run_test("Test 7b: clean multiple chars from name",
             test7b, NULL, PHO_TEST_SUCCESS);

    run_test("Test 7c: name ending with '.' separator",
             test7c, NULL, PHO_TEST_SUCCESS);

    run_test("Test 8a: clean special chars from middle of tag",
             test8a, NULL, PHO_TEST_SUCCESS);

    run_test("Test 8b clean chars from beginning of tag",
             test8b, NULL, PHO_TEST_SUCCESS);

    run_test("Test 8c: clean tag starting with '.' separator",
             test8c, NULL, PHO_TEST_SUCCESS);

    run_test("Test 9: Long (truncated) name, no tag (empty)",
             test9, NULL, PHO_TEST_SUCCESS);

    run_test("Test 10: Long (truncated) name, no tag (NULL)",
             test10, NULL, PHO_TEST_SUCCESS);

    run_test("Test 11: Long (truncated) name",
             test11, NULL, PHO_TEST_SUCCESS);

    run_test("Test 12: long (truncated) name, long (invalid tag)",
             test12, NULL, PHO_TEST_FAILURE);

    run_test("Test 13: make sure fields do not collide unexpectedly",
             test13, NULL, PHO_TEST_SUCCESS);

    run_test("Test 14: pass in NULL/0 destination buffer",
             test14, NULL, PHO_TEST_FAILURE);

    run_test("Test 15: pass in NULL/<length> destination buffer",
             test15, NULL, PHO_TEST_FAILURE);

    run_test("Test 16: pass in small destination buffer",
             test16, NULL, PHO_TEST_FAILURE);

    printf("MAPPER: All tests succeeded\n");
    return 0;
}
