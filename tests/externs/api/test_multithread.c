/*
 * All rights reserved (c) 2014-2025 CEA/DAM.
 *
 * This file is part of Phobos.
 *
 * Phobos is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * Phobos is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Phobos. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \brief Test some multi-threaded puts
 */

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "phobos_store.h"

void *exec_put(void *_arg)
{
    char **argv = (char **)_arg;
    char *objid = argv[1];
    char *path = argv[0];

    struct pho_xfer_target xtgt = {
        .xt_fd = open(path, O_RDONLY),
        .xt_objid = objid,
    };
    struct pho_xfer_desc xfer = {
        .xd_op = PHO_XFER_OP_PUT,
        .xd_flags = 0,
        .xd_ntargets = 1,
        .xd_targets = &xtgt,
        .xd_params.put.family = PHO_RSC_DIR,
    };
    struct stat st;

    assert(fstat(xtgt.xt_fd, &st) == 0);
    xtgt.xt_size = st.st_size;

    assert(phobos_init() == 0);
    assert(phobos_put(&xfer, 1, NULL, NULL) == 0);
    pho_xfer_desc_clean(&xfer);
    phobos_fini();

    return NULL;
}

int main(int argc, char **argv)
{
    int nb_workers = argc / 2;
    pthread_t *thrs;
    int i;

    if (argc % 2 != 1) {
        fprintf(stderr, "usage: %s <file> <id> [...]\n", argv[0]);
        fprintf(stderr, "       <file>: file to put to Phobos\n");
        fprintf(stderr, "       <id>: id of new Phobos object for <file>\n");
        fprintf(stderr, "example: %s file_1 id_1 file_2 id_2\n", argv[0]);
        return 1;
    }

    thrs = malloc(nb_workers * sizeof(*thrs));
    if (thrs == NULL)
        return 1;

    for (i = 0; i < nb_workers; ++i) {
        int rc;

        rc = pthread_create(thrs + i, NULL, exec_put,
                            argv + (i * 2 + 1));
        assert(rc == 0);
    }

    for (i = 0; i < nb_workers; ++i) {
        int rc;

        rc = pthread_join(thrs[i], NULL);
        assert(rc == 0);
    }

    free(thrs);

    return 0;
}

/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
