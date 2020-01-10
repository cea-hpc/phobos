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
 * \brief  Test scsi_tape devname / serial mapping API
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_ldm.h"
#include "pho_common.h"
#include "pho_test_utils.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <net/if.h>

#define TEST_MAX_DRIVES 32


static int test_unit(void *hint)
{
    struct ldm_dev_state lds = {0};
    struct dev_adapter deva;
    char *dev_name = hint;
    int rc;

    rc = get_dev_adapter(PHO_DEV_TAPE, &deva);
    if (rc)
        return rc;

    rc = ldm_dev_query(&deva, dev_name, &lds);
    if (rc == 0)
        pho_info("Mapped '%s' to '%s' (model: '%s')", dev_name, lds.lds_serial,
                 lds.lds_model);

    return rc;
}

/**
 * Retrieve path to sg device, given the path to an st device
 */
static int st_to_sg_path(const char *st_dev, char *sg_dev, size_t sg_size)
{
    char generic[1024];
    char *link_path;
    const char *c;
    int  rc;

    /* get the 'st' part from the path */
    c = strrchr(st_dev, '/');
    if (c == NULL)
        c = st_dev;
    else
        c++;

    if (asprintf(&link_path, "/sys/class/scsi_tape/%s/device/generic", c) < 0)
        LOG_RETURN(-ENOMEM, "Failed to allocate memory");

    rc = readlink(link_path, generic, sizeof(generic));
    free(link_path);
    if (rc < 0) {
        rc = -errno;
        LOG_RETURN(rc, "Failed to read link");
    }
    generic[rc] = '\0';

    c = strrchr(generic, '/');
    if (c == NULL)
        c = generic;
    else
        c++;

    snprintf(sg_dev, sg_size, "/dev/%s", c);
    return 0;
}


static int test_name_serial_match(void *hint)
{
    char  *st_path = hint;
    char  sg_path[PATH_MAX];
    struct dev_adapter deva;
    struct ldm_dev_state lds = {0};
    char   path[128];
    int    rc;

    rc = get_dev_adapter(PHO_DEV_TAPE, &deva);
    if (rc)
        return rc;

    /* get the matching sg name */
    rc = st_to_sg_path(st_path, sg_path, sizeof(sg_path));
    if (rc)
        return rc;

    rc = ldm_dev_query(&deva, st_path, &lds);
    if (rc)
        return rc;

    rc = ldm_dev_lookup(&deva, lds.lds_serial, path, sizeof(path));
    if (rc)
        return rc;

    pho_debug("Reverse mapped serial '%s' to '%s'", lds.lds_serial, path);
    ldm_dev_state_fini(&lds);
    return strcmp(path, sg_path) == 0 ? 0 : -EINVAL;
}

static bool device_exists(int dev_index)
{
    struct stat st;
    char        dev_path[PATH_MAX];
    int         rc;

    snprintf(dev_path, sizeof(dev_path) - 1, "/dev/st%d", dev_index);
    rc = stat(dev_path, &st);
    pho_info("Accessing %s: %s", dev_path, rc ? strerror(errno) : "OK");
    return rc == 0;
}

int main(int argc, char **argv)
{
    char test_name[128];
    char dev_name[IFNAMSIZ];
    int  i;

    test_env_initialize();

    for (i = 0; i < TEST_MAX_DRIVES; i++) {
        if (!device_exists(i))
            break;
        snprintf(dev_name, sizeof(dev_name) - 1, "/dev/st%d", i);
        snprintf(test_name, sizeof(test_name) - 1,
                 "Test %da: get serial for drive %s", i, dev_name);
        run_test(test_name, test_unit, dev_name, PHO_TEST_SUCCESS);
    }

    for (i = 0; i < TEST_MAX_DRIVES; i++) {
        if (!device_exists(i))
            break;
        snprintf(dev_name, sizeof(dev_name) - 1, "/dev/st%d", i);
        snprintf(test_name, sizeof(test_name) - 1,
                 "Test %dc: match name/serial for drive %s", i, dev_name);
        run_test(test_name, test_name_serial_match, dev_name, PHO_TEST_SUCCESS);
    }

    pho_info("LINTAPE MAPPER: All tests succeeded");
    exit(EXIT_SUCCESS);
}
