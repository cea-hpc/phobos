/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2015 CEA/DAM. All Rights Reserved.
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

    strncpy(dev_path, sep + 1, path_size);

    return 0;
}

static int dir_query(const char *dev_path, struct ldm_dev_state *lds)
{
    char hostname[HOST_NAME_MAX];
    char *dot;
    char *id = NULL;
    char *real;
    ENTRY;

    lds->lds_family = PHO_DEV_DIR;
    lds->lds_model = NULL;

    real = realpath(dev_path, NULL);
    if (!real)
        LOG_RETURN(-errno, "Could not resolve path '%s'", dev_path);

    if (gethostname(hostname, HOST_NAME_MAX))
        LOG_RETURN(-errno, "Failed to get host name");

    /* truncate to short host name */
    dot = strchr(hostname, '.');
    if (dot)
        *dot = '\0';

    /* dir id is set to <host>:<real-path> */
    if (asprintf(&id, "%s:%s", hostname, real) == -1 || id == NULL) {
        free(real);
        LOG_RETURN(-errno, "String allocation failed");
    }

    lds->lds_serial = id;
    lds->lds_loaded = true; /* always online */

    return 0;
}

struct dev_adapter dev_adapter_dir = {
    .dev_lookup = dir_lookup,
    .dev_query  = dir_query,
    .dev_load   = NULL,
    .dev_eject  = NULL,
};

