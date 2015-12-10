/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */

/**
 * \brief  Test lintape devname / serial mapping API
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

#define DEV_PREFIX_SZ   strlen("/dev/")

static int test_unit(void *hint)
{
    struct ldm_dev_state lds;
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

static int test_name_serial_match(void *hint)
{
    char  *name_ref = hint;
    struct dev_adapter deva;
    struct ldm_dev_state lds;
    char   path[128];
    int    rc;

    rc = get_dev_adapter(PHO_DEV_TAPE, &deva);
    if (rc)
        return rc;

    rc = ldm_dev_query(&deva, name_ref, &lds);
    if (rc)
        return rc;

    rc = ldm_dev_lookup(&deva, lds.lds_serial, path, sizeof(path));
    if (rc)
        return rc;

    pho_debug("Reverse mapped serial '%s' to '%s'", lds.lds_serial, path);
    return strcmp(path + DEV_PREFIX_SZ, name_ref) == 0 ? 0 : -EINVAL;
}

static bool device_exists(int dev_index)
{
    struct stat st;
    char        dev_path[PATH_MAX];
    int         rc;

    snprintf(dev_path, sizeof(dev_path) - 1, "/dev/IBMtape%d", dev_index);
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
        snprintf(dev_name, sizeof(dev_name) - 1, "IBMtape%d", i);
        snprintf(test_name, sizeof(test_name) - 1,
                 "Test %da: get serial for drive %s", i, dev_name);
        run_test(test_name, test_unit, dev_name, PHO_TEST_SUCCESS);
    }

    for (i = 0; i < TEST_MAX_DRIVES; i++) {
        if (!device_exists(i))
            break;
        snprintf(dev_name, sizeof(dev_name) - 1, "IBMtape%d", i);
        snprintf(test_name, sizeof(test_name) - 1,
                 "Test %dc: match name/serial for drive %s", i, dev_name);
        run_test(test_name, test_name_serial_match, dev_name, PHO_TEST_SUCCESS);
    }

    pho_info("LINTAPE MAPPER: All tests succeeded");
    exit(EXIT_SUCCESS);
}
