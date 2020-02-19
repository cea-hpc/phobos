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
 * \brief  Phobos Local Device Manager: device calls for inplace directories.
 *
 * Implement device primitives for a directory.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_ldm.h"
#include "pho_common.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

static int dir_lookup(const char *dev_id, char *dev_path, size_t path_size)
{
    /* identifier for directories consists of <host>:<path> */
    const char *sep = strchr(dev_id, ':');
    ENTRY;

    if (!sep)
        return -EINVAL;

    strncpy(dev_path, sep + 1, path_size - 1);
    dev_path[path_size - 1] = '\0';
    return 0;
}

static int dir_query(const char *dev_path, struct ldm_dev_state *lds)
{
    char     hostname[HOST_NAME_MAX];
    char    *dot;
    char    *id = NULL;
    char    *real;
    int      rc = 0;
    ENTRY;

    lds->lds_family = PHO_DEV_DIR;
    lds->lds_model = NULL;

    real = realpath(dev_path, NULL);
    if (!real)
        LOG_RETURN(-errno, "Could not resolve path '%s'", dev_path);

    if (gethostname(hostname, HOST_NAME_MAX))
        LOG_GOTO(out_free, rc = -EADDRNOTAVAIL, "Failed to get host name");

    /* truncate to short host name */
    dot = strchr(hostname, '.');
    if (dot)
        *dot = '\0';

    /* dir id is set to <host>:<real-path> */
    if (asprintf(&id, "%s:%s", hostname, real) == -1 || id == NULL)
        LOG_GOTO(out_free, rc = -ENOMEM, "String allocation failed");

    lds->lds_serial = id;
    lds->lds_loaded = true; /* always online */

out_free:
    free(real);
    return rc;
}

struct dev_adapter dev_adapter_dir = {
    .dev_lookup = dir_lookup,
    .dev_query  = dir_query,
    .dev_load   = NULL,
    .dev_eject  = NULL,
};

