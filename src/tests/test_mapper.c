/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2017 CEA/DAM.
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

static int is_hash1_path_valid(const char *path)
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

static int is_clean_path_valid(const char *path)
{
    size_t  path_len = strlen(path);
    int     dots = 0;
    int     i;

    if (path_len < 1 || path_len > NAME_MAX)
        return 0;

    for (i = 0; i < path_len; i++) {
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

    /* poison it */
    memset(buff, '?', sizeof(buff));

    rc = pho_mapper_hash1(obj, tag, buff, buff_len);
    if (rc)
        return rc;

    pho_info("HASH1 MAPPER: o='%s', t='%s': '%s'",
             SAFE_STR(obj), SAFE_STR(tag), buff);

    if (!is_hash1_path_valid(buff)) {
        pho_error(EINVAL, "Invalid hash1 path crafted: '%s'", buff);
        return -EINVAL;
    }

    /* poison it */
    memset(buff, '?', sizeof(buff));

    rc = pho_mapper_clean_path(obj, tag, buff, buff_len);
    if (rc)
        return rc;

    pho_info("PATH MAPPER: o='%s' t='%s': '%s'",
             SAFE_STR(obj), SAFE_STR(tag), buff);

    if (!is_clean_path_valid(buff)) {
        pho_error(EINVAL, "Invalid clean path crafted: '%s'", buff);
        return -EINVAL;
    }

    return 0;
}


typedef int (*pho_hash_func_t)(const char *obj_id, const char *ext_tag,
                               char *dst_path, size_t dst_size);

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
    pho_hash_func_t func = (pho_hash_func_t)hint;

    rc = func("a", "bc", buff1, sizeof(buff1));
    if (rc)
        return rc;

    rc = func("ab", "c", buff2, sizeof(buff2));
    if (rc)
        return rc;

    if (strcmp(buff1, buff2) == 0)
        return -EINVAL;

    return 0;
}

static int test14(void *hint)
{
    pho_hash_func_t func = (pho_hash_func_t)hint;

    return func("a", "b", NULL, 0);
}

static int test15(void *hint)
{
    pho_hash_func_t func = (pho_hash_func_t)hint;

    return func("a", "b", NULL, NAME_MAX + 1);
}

static int test16(void *hint)
{
    char    buff[2];
    pho_hash_func_t func = (pho_hash_func_t)hint;

    return func("a", "b", buff, sizeof(buff));
}

static void string_of_char(char *s, int len, int max)
{
    int i;

    assert(len < max);

    for (i = 0; i < len; i++)
        /* use a pattern to make truncation visible */
        s[i] = 'a' + (i % 26);
    s[len] = '\0';

    /* add garbage afterward */
    if (len + 1 < max)
        s[len + 1] = 'Z';
}

/** test corner cases around NAME_MAX: 253 to 257 + various tags */
static int test17(void *hint)
{
    int   i, j, rc;
    char  buff[NAME_MAX+3];
    char *tag[] = {NULL, "a", "aa", "aaa"};

    for (i = NAME_MAX - 3; i <= NAME_MAX + 2; i++) {
        for (j = 0; j < sizeof(tag)/sizeof(*tag); j++) {
            string_of_char(buff, i, sizeof(buff));
            pho_info("strlen(obj_id)=%zu, tag=%s",
                     strlen(buff), SAFE_STR(tag[j]));
            rc = test_build_path(buff, tag[j]);
            if (rc)
                return rc;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    test_env_initialize();

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

    run_test("Test 13a: make sure fields do not collide unexpectedly (hash1)",
             test13, pho_mapper_hash1, PHO_TEST_SUCCESS);
    run_test("Test 13b: make sure fields do not collide unexpectedly (path)",
             test13, pho_mapper_clean_path, PHO_TEST_SUCCESS);

    run_test("Test 14a: pass in NULL/0 destination buffer (hash1)",
             test14, pho_mapper_hash1, PHO_TEST_FAILURE);
    run_test("Test 14b: pass in NULL/0 destination buffer (path)",
             test14, pho_mapper_clean_path, PHO_TEST_FAILURE);

    run_test("Test 15a: pass in NULL/<length> destination buffer (hash1)",
             test15, pho_mapper_hash1, PHO_TEST_FAILURE);
    run_test("Test 15b: pass in NULL/<length> destination buffer (path)",
             test15, pho_mapper_clean_path, PHO_TEST_FAILURE);

    run_test("Test 16a: pass in small destination buffer (hash1)",
             test16, pho_mapper_hash1, PHO_TEST_FAILURE);
    run_test("Test 16b: pass in small destination buffer (path)",
             test16, pho_mapper_clean_path, PHO_TEST_FAILURE);

    run_test("Test 17a: corner cases around NAME_MAX (hash1)",
             test17, pho_mapper_hash1, PHO_TEST_SUCCESS);
    run_test("Test 17b: orner cases around NAME_MAX (path)",
             test17, pho_mapper_clean_path, PHO_TEST_SUCCESS);


    pho_info("MAPPER: All tests succeeded");
    exit(EXIT_SUCCESS);
}
