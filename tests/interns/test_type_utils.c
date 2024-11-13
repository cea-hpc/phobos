/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2022 CEA/DAM.
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
 * \brief test type utils
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_test_utils.h"
#include "pho_types.h"
#include "pho_type_utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

static const char *const T_AB[] = {"a", "b"};
static const char *const T_AC[] = {"a", "c"};
static const char *const T_BA[] = {"b", "a"};
static const char *const T_ABC[] = {"a", "b", "c"};
static const char *const T_CBA[] = {"c", "b", "a"};

static void test_no_string(void)
{
    struct string_array strings = NO_STRING;

    assert(strings.strings == NULL);
    assert(strings.count == 0);
}

static void test_string_array_various(void)
{
    struct string_array string_array_ab;
    struct string_array string_array_ab2;
    struct string_array string_array_ab3;
    struct string_array string_array_ba;
    struct string_array string_array_ac;
    struct string_array string_array_abc;
    struct string_array string_array_cba;
    struct string_array string_array_none = NO_STRING;

    /* Static allocation */
    string_array_ab.strings = (char **)T_AB;
    string_array_ab.count = 2;

    /* Dynamic allocations */
    string_array_init(&string_array_ab2, (char **)T_AB, 2);
    assert(string_array_ab2.strings != NULL);
    assert(string_array_ab2.count == 2);
    string_array_dup(&string_array_ab3, &string_array_ab2);
    assert(string_array_ab3.strings != NULL);
    assert(string_array_ab3.count == 2);

    string_array_ba.strings = (char **)T_BA;
    string_array_ba.count = 2;

    string_array_ac.strings = (char **)T_AC;
    string_array_ac.count = 2;

    string_array_abc.strings = (char **)T_ABC;
    string_array_abc.count = 3;

    string_array_cba.strings = (char **)T_CBA;
    string_array_cba.count = 3;

    /* Equality */
    assert(string_array_eq(&string_array_ab, &string_array_ab));
    assert(string_array_eq(&string_array_ab, &string_array_ab2));
    assert(string_array_eq(&string_array_ab2, &string_array_ab));
    assert(string_array_eq(&string_array_ab, &string_array_ab3));
    assert(string_array_eq(&string_array_ab2, &string_array_ab3));
    assert(!string_array_eq(&string_array_ab, &string_array_ba));
    assert(!string_array_eq(&string_array_ab, &string_array_ac));
    assert(!string_array_eq(&string_array_ab, &string_array_abc));
    assert(!string_array_eq(&string_array_ab, &string_array_none));

    /* Containment */
    assert(string_array_in(&string_array_abc, &string_array_ab));
    assert(string_array_in(&string_array_cba, &string_array_ab));
    assert(string_array_in(&string_array_ab, &string_array_ab));
    assert(string_array_in(&string_array_ab, &string_array_ba));
    assert(!string_array_in(&string_array_ac, &string_array_ab));
    assert(!string_array_in(&string_array_none, &string_array_ab));
    assert(string_array_in(&string_array_ab, &string_array_none));
    assert(string_array_in(&string_array_none, &string_array_none));

    /* Free */
    string_array_free(&string_array_ab2);
    string_array_free(&string_array_ab3);

    /* Don't segfault on double free */
    string_array_free(&string_array_ab2);
}

static void test_string_array_dup(void)
{
    struct string_array string_array_src;
    struct string_array string_array_dst;

    string_array_src.strings = (char **)T_AB;
    string_array_src.count = 2;

    /* Should not segfault */
    string_array_dup(NULL, NULL);
    string_array_dup(NULL, &string_array_src);

    /* string_array_dst should be equal to NO_STRING */
    string_array_dup(&string_array_dst, NULL);
    assert(string_array_eq(&string_array_dst, &NO_STRING));

    /* Standard dup */
    string_array_dup(&string_array_dst, &string_array_src);
    assert(string_array_eq(&string_array_dst, &string_array_dst));
    assert(!string_array_eq(&string_array_dst, &NO_STRING));

    string_array_free(&string_array_dst);
}

static void test_str2string_array(void)
{
    struct string_array string_array_new = {};
    struct string_array string_array_abc = {};
    char *string_array_as_string = "";

    /* empty string */
    str2string_array(string_array_as_string, &string_array_new);
    assert(string_array_eq(&string_array_abc, &string_array_new));

    /* 3 string_array */
    string_array_abc.strings = (char **)T_ABC;
    string_array_abc.count = 3;

    string_array_as_string = "a,b,c";
    str2string_array(string_array_as_string, &string_array_new);
    assert(string_array_eq(&string_array_abc, &string_array_new));

    string_array_free(&string_array_new);
}

int main(int argc, char **argv)
{
    test_env_initialize();
    test_no_string();
    test_string_array_various();
    test_string_array_dup();
    test_str2string_array();

    return EXIT_SUCCESS;
}
