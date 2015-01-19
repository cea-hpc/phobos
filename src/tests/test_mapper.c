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

static int test13(void)
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
    int rc;

    /* Test 0: Simple name */
    rc = test_build_path("test0", "p0");
    if (rc < 0)
        return 1;

    /* Test 1: No tag (empty) */
    rc = test_build_path("test1", "");
    if (rc < 0)
        return 1;

    /* Test 2: No tag (null) */
    rc = test_build_path("test2", NULL);
    if (rc < 0)
        return 1;

    /* Test 3: No name (empty), SHOULD FAIL */
    rc = test_build_path("", "p3");
    if (rc == 0)
        return 1;

    /* Test 4: No name (null), SHOULD FAIL */
    rc = test_build_path(NULL, "p4");
    if (rc == 0)
        return 1;

    /* Test 5: Long tag, SHOULD FAIL */
    rc = test_build_path("test5", _240_TIMES_A);
    if (rc == 0)
        return 1;

    /* Test 6a: Non-printable chars in name */
    rc = test_build_path("tes\x07t6a", "p6a");
    if (rc < 0)
        return 1;

    rc = test_build_path("test6b\x07", "p6b");
    if (rc < 0)
        return 1;

    rc = test_build_path("\x07test6c", "p6c");
    if (rc < 0)
        return 1;

    /* Test 6b: Non-printable chars in tag */
    rc = test_build_path("test6d", "\x07p6d");
    if (rc < 0)
        return 1;

    rc = test_build_path("test6e", "p\x07""6e");
    if (rc < 0)
        return 1;

    rc = test_build_path("test6f", "p6f\x07");
    if (rc < 0)
        return 1;

    /* Test 7a: Annoying shell specials chars */
    rc = test_build_path("te<st7a", "p7a");
    if (rc < 0)
        return 1;

    /* Test 7b: clean multiple chars from name */
    rc = test_build_path("te<<<<<<{{[[[st7b", "p7b");
    if (rc < 0)
        return 1;

    /* Test 7c: name ending with '.' */
    rc = test_build_path("test7c.", "p7c");
    if (rc < 0)
        return 1;

    /* Test 8a: clean tag */
    rc = test_build_path("test8a", "p|8a");
    if (rc < 0)
        return 1;

    /* Test 8b: clean chars from beginning of tag */
    rc = test_build_path("test8b", "<<{p8b");
    if (rc < 0)
        return 1;

    /* Test 8c: tag starting with '.' */
    rc = test_build_path("test8c", ".p8c");
    if (rc < 0)
        return 1;

    /* Test 9: Long (truncated) name, no tag (empty) */
    rc = test_build_path(_240_TIMES_A"9", "");
    if (rc < 0)
        return 1;

    /* Test 10: Long (truncated) name, no tag (NULL) */
    rc = test_build_path(_240_TIMES_A"10", NULL);
    if (rc < 0)
        return 1;

    /* Test 11: Long (truncated) name */
    rc = test_build_path(_240_TIMES_A _240_TIMES_A"11", "p11");
    if (rc < 0)
        return 1;

    /* Test 12: long (truncated) name, long (invalid tag), SHOULD FAIL */
    rc = test_build_path(_240_TIMES_A"12", _240_TIMES_A"12");
    if (rc == 0)
        return 1;

    /* Test 13: make sure fields do not collide unexpectedly */
    rc = test13();
    if (rc < 0)
        return 1;

    printf("MAPPER: All tests succeeded\n");
    return 0;
}
