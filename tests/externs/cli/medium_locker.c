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
 * \brief  binary to lock/unlock a medium
 */

/* phobos stuff */
#include "dss_lock.h"
#include "pho_dss.h"

/* standard stuff */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

static void usage_exit(void)
{
    printf("usage: lock/unlock dir/tape medium_name/all lock_hostname pid\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    struct media_info *medium;
    struct dss_filter filter;
    struct pho_id medium_id;
    struct dss_handle dss;
    int pid;
    int cnt;
    int rc;

    /* check params */
    if (argc != 6 ||
        (strcmp(argv[1], "lock") && strcmp(argv[1], "unlock")) ||
        (strcmp(argv[2], "dir") && strcmp(argv[2], "tape")))
        usage_exit();

    /* init medium_id */
    if (!strcmp(argv[2], "dir"))
        medium_id.family = PHO_RSC_DIR;
    else
        medium_id.family = PHO_RSC_TAPE;

    /* init dss */
    setenv("PHOBOS_DSS_connect_string", "dbname=phobos host=localhost "
                                        "user=phobos password=phobos", 1);

    rc = dss_init(&dss);
    if (rc)
        exit(EXIT_FAILURE);

    if (strcmp(argv[3], "all")) {
        pho_id_name_set(&medium_id, argv[3]);

        /* get medium info */
        rc = dss_filter_build(&filter,
                              "{\"$AND\": ["
                                "{\"DSS::MDA::family\": \"%s\"}, "
                                "{\"DSS::MDA::id\": \"%s\"}"
                              "]}",
                              rsc_family2str(medium_id.family),
                              medium_id.name);
        if (rc)
            LOG_GOTO(clean, rc, "Error while building filter");

        rc = dss_media_get(&dss, &filter, &medium, &cnt);
        dss_filter_free(&filter);
        if (rc)
            LOG_GOTO(clean, rc, "Error while getting medium from dss");

        if (cnt > 1)
            LOG_GOTO(clean_medium, rc = -EINVAL,
                    "Error: multiple media found when targeting unique medium");
    } else {
        rc = dss_media_get(&dss, NULL, &medium, &cnt);
        if (rc)
            LOG_GOTO(clean, rc, "Error on getting medium from dss");
    }

    if (cnt == 0)
        LOG_GOTO(clean_medium, rc = -EINVAL, "Error: no medium found");

    pid = (int) strtoll(argv[5], NULL, 10);
    if (errno == EINVAL || errno == ERANGE)
        LOG_GOTO(clean_medium, rc = -errno,
                 "Conversion error occurred: %d\n", errno);

    if (!strcmp(argv[1], "lock"))
        rc = _dss_lock(&dss, DSS_MEDIA, medium, cnt, argv[4], pid);
    else
        rc = _dss_unlock(&dss, DSS_MEDIA, medium, cnt, argv[4], pid);

clean_medium:
    dss_res_free(medium, cnt);
clean:
    dss_fini(&dss);
    return rc;
}
