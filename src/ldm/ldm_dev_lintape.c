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
 * \brief  Phobos Lintape serial/path mapping.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_ldm.h"
#include "pho_cfg.h"
#include "pho_type_utils.h"
#include "pho_common.h"

#include <stdio.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <assert.h>
#include <net/if.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Driver name; to access /sys/class tree */
#define DRIVER_NAME "lin_tape"

/* Maximum serial size (including trailing zero) */
#define MAX_SERIAL  48

/* Maximum model size (including trailing zero) */
#define MAX_MODEL  33

/* Maximum number of drives supported */
#define LDM_MAX_DRIVES  256

/* name of serial number attribute in /sys/class */
#define SYS_SERIAL_NUMBER   "serial_num"
/* name of device model attribute in /sys/class */
#define SYS_DEV_MODEL "device/model"


/* In-memory map to associate drive serial and device name */
struct drive_map_entry {
    char    dme_serial[MAX_SERIAL];  /* 1013005381 - as a string */
    char    dme_model[MAX_MODEL];    /* ULT3580-TD6 */
    char    dme_devname[IFNAMSIZ];   /* IBMTape0 */
};

/* Singly-linked list of structures that describe available drives */
static GSList *drive_cache;

static void build_sys_path(const char *name, const char *attr, char *dst_path,
                           size_t n)
{
    snprintf(dst_path, n, "/sys/class/%s/%s/%s", DRIVER_NAME, name, attr);
    dst_path[n - 1] = '\0';
}

/** Read the given attribute for the given device name */
static int read_device_attr(const char *devname, const char *attrname,
                            char *info, size_t info_size)
{
    char    spath[PATH_MAX];
    ssize_t nread;
    int     fd;
    int     rc = 0;

    build_sys_path(devname, attrname, spath, sizeof(spath));

    fd = open(spath, O_RDONLY);
    if (fd < 0)
        LOG_RETURN(rc = -errno, "Cannot open '%s'", spath);

    memset(info, 0, info_size);
    nread = read(fd, info, info_size - 1);
    if (nread <= 0)
        LOG_GOTO(out_close, rc = -errno, "Cannot read %s in '%s'", attrname,
                 spath);

    /* rstrip stupid '\n' and spaces */
    rstrip(info);

    pho_debug("Device '%s': %s='%s'", devname, attrname, info);

out_close:
    close(fd);
    return rc;
}

static int cache_load_from_name(const char *devname)
{
    struct drive_map_entry   *dinfo;
    size_t                    namelen = strlen(devname);
    int                       rc = 0;

    assert(namelen < IFNAMSIZ);

    dinfo = calloc(1, sizeof(*dinfo));
    if (dinfo == NULL)
        LOG_RETURN(-ENOMEM, "Cannot allocate cache node for '%s'", devname);

    memcpy(dinfo->dme_devname, devname, namelen);

    rc = read_device_attr(devname, SYS_SERIAL_NUMBER, dinfo->dme_serial,
                          sizeof(dinfo->dme_serial));
    if (rc)
        goto err_free;

    rc = read_device_attr(devname, SYS_DEV_MODEL, dinfo->dme_model,
                          sizeof(dinfo->dme_model));
    if (rc)
        goto err_free;

    drive_cache = g_slist_prepend(drive_cache, dinfo);

    return 0;

err_free:
    free(dinfo);
    return rc;
}

static gint _find_by_name_cb(gconstpointer a, gconstpointer b)
{
    const struct drive_map_entry *drive = a;
    const char              *name  = b;

    if (drive == NULL || name == NULL)
        return -1;

    return strcmp(drive->dme_devname, name);
}

static gint _find_by_serial_cb(gconstpointer a, gconstpointer b)
{
    const struct drive_map_entry *drive  = a;
    const char              *serial = b;

    if (drive == NULL || serial == NULL)
        return -1;

    return strcmp(drive->dme_serial, serial);
}

static void build_sys_class_path(char *path, size_t path_size, const char *name)
{
    snprintf(path, path_size, "/sys/class/%s", name);
    path[path_size - 1] = '\0';
}

/** TODO consider passing driver name to handle multiple models */
static inline bool is_device_valid(const char *dev_name)
{
    char norewind;
    int  idx;
    int  res;

    res = sscanf(dev_name, "IBMtape%d%c", &idx, &norewind);
    return res == 1;
}

static void lintape_map_free(void)
{
    pho_debug("Freeing device serial cache");
    g_slist_free_full(drive_cache, free);
    drive_cache = NULL;
}

static int lintape_map_load(void)
{
    char             sys_path[PATH_MAX];
    DIR             *dir;
    struct dirent    entry;
    struct dirent   *result;
    int              count = 0;
    int              rc;

    /* Is this a reload? */
    if (drive_cache != NULL)
        lintape_map_free();

    build_sys_class_path(sys_path, sizeof(sys_path), DRIVER_NAME);
    pho_debug("Listing devices at '%s' to populate cache", sys_path);

    dir = opendir(sys_path);
    if (dir == NULL)
        LOG_RETURN(rc = -errno, "Cannot opendir(%s) to list devices", sys_path);

    drive_cache = g_slist_alloc();

    for (;;) {
        rc = readdir_r(dir, &entry, &result);
        if (rc)
            LOG_GOTO(out_close, -rc, "Error while iterating over directory");

        if (result == NULL)
            break;

        if (!is_device_valid(entry.d_name)) {
            pho_debug("Ignoring device '%s'", entry.d_name);
            continue;
        }

        rc = cache_load_from_name(entry.d_name);
        if (rc)
            LOG_GOTO(out_close, rc, "Error while loading entry '%s'",
                     entry.d_name);

        pho_debug("Loaded device '%s' successfully", entry.d_name);
        count++;
    }

    pho_debug("Loaded %d devices for driver %s", count, DRIVER_NAME);

out_close:
    closedir(dir);
    if (rc && drive_cache)
        lintape_map_free();
    return rc;
}

static const struct drive_map_entry *lintape_dev_info(const char *name)
{
    GSList  *element;

    if (strlen(name) >= IFNAMSIZ) {
        pho_error(-ENAMETOOLONG, "Device name '%s' > %d char long",
                  name, IFNAMSIZ - 1);
        return NULL;
    }

    if (drive_cache == NULL) {
        pho_debug("No information available in cache: loading...");
        lintape_map_load();
    }

    element = g_slist_find_custom(drive_cache, name, _find_by_name_cb);
    if (element != NULL) {
        struct drive_map_entry  *dme = element->data;

        pho_debug("Found device '%s': serial='%s', model='%s',",
                  name, dme->dme_serial, dme->dme_model);
        return dme;
    }

    pho_info("Device '%s' not found in lintape device cache", name);
    return NULL;
}

static int lintape_dev_lookup(const char *serial, char *path,
                              size_t path_size)
{
    GSList  *element;
    ENTRY;

    if (strlen(serial) >= MAX_SERIAL)
        LOG_RETURN(-ENAMETOOLONG, "Device name '%s' > %d char long",
                   serial, MAX_SERIAL - 1);

    if (drive_cache == NULL) {
        pho_debug("No information available in cache: loading...");
        lintape_map_load();
    }

    element = g_slist_find_custom(drive_cache, serial, _find_by_serial_cb);
    if (element != NULL) {
        struct drive_map_entry  *dme = element->data;

        pho_debug("Found device at /dev/%s for '%s'", dme->dme_devname, serial);
        snprintf(path, path_size, "/dev/%s", dme->dme_devname);
        return 0;
    }

    return -ENOENT;
}

static int lintape_dev_query(const char *dev_path, struct ldm_dev_state *lds)
{
    const struct drive_map_entry    *dme;
    const char                      *dev_short;
    ENTRY;

    /* Make sure the device exists before we do any string manipulation
     * on its path. */
    if (access(dev_path, F_OK))
        LOG_RETURN(-errno, "Cannot access '%s'", dev_path);

    /* extract basename(device)*/
    dev_short = strrchr(dev_path, '/');
    if (dev_short == NULL)
        dev_short = dev_path;
    else
        dev_short++;

    /* get serial and model from driver mapping */
    dme = lintape_dev_info(dev_short);
    if (!dme)
        return -ENOENT;

    memset(lds, 0, sizeof(*lds));
    lds->lds_family = PHO_DEV_TAPE;
    lds->lds_model = strdup(dme->dme_model);
    lds->lds_serial = strdup(dme->dme_serial);

    /** TODO query drive for actual state */
    return 0;
}

struct dev_adapter dev_adapter_lintape = {
    .dev_lookup = lintape_dev_lookup,
    .dev_query  = lintape_dev_query,
    .dev_load   = NULL, /** @TODO to be implemented */
    .dev_eject  = NULL, /** @TODO to be implemented */
};


