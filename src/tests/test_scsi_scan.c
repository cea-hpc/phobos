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
#define _GNU_SOURCE
#include "pho_test_utils.h"
#include "../ldm/scsi_api.h"
#include "pho_ldm.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>

#define ASSERT_RC(stmt)             \
    do {                            \
        int rc;                     \
        rc = stmt;                  \
        if (rc) {                   \
            pho_error(rc, #stmt);   \
            exit(EXIT_FAILURE);     \
        }                           \
    } while (0)

static void test_lib_scan(void)
{
    struct lib_adapter lib = {0};
    json_t            *lib_data, *data_entry;
    char              *json_str;
    size_t             index;

    ASSERT_RC(get_lib_adapter(PHO_LIB_SCSI, &lib));
    ASSERT_RC(ldm_lib_open(&lib, "/dev/changer"));
    ASSERT_RC(ldm_lib_scan(&lib, &lib_data));

    if (!json_array_size(lib_data)) {
        pho_error(-EINVAL, "ldm_lib_scan returned an empty array");
        exit(EXIT_FAILURE);
    }

    /* Iterate on lib element and perform basic checks */
    json_array_foreach(lib_data, index, data_entry) {
        if (!json_object_get(data_entry, "type")) {
            pho_error(-EINVAL, "Missing \"type\" key from json in lib_scan_cb");
            exit(EXIT_FAILURE);
        }
    }

    json_str = json_dumps(lib_data, JSON_INDENT(2));
    printf("JSON: %s\n", json_str);
    free(json_str);
    json_decref(lib_data);

    ASSERT_RC(ldm_lib_close(&lib));
}


int main(int argc, char **argv)
{
    test_env_initialize();
    test_lib_scan();

    exit(EXIT_SUCCESS);
}
