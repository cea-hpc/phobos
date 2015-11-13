/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
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

/* Maximum number of drives supported */
#define LDM_MAX_DRIVES  256


/* In-memory map to associate drive serial and device name */
struct drive_map_entry {
    char    dme_serial[MAX_SERIAL];  /* 1013005381 - as a string */
    char    dme_devname[IFNAMSIZ];   /* IBMTape0 */
};

/* Singly-linked list of structures that describe available drives */
static GSList *drive_cache;


static void build_sys_serial_path(const char *name, char *dst_path, size_t n)
{
    snprintf(dst_path, n, "/sys/class/%s/%s/serial_num", DRIVER_NAME, name);
    dst_path[n - 1] = '\0';
}


static int cache_load_from_name(const char *devname)
{
    struct drive_map_entry   *dinfo;
    char                      spath[PATH_MAX];
    char                      serial[MAX_SERIAL];
    size_t                    namelen = strlen(devname);
    ssize_t                   nread;
    int                       fd;
    int                       rc = 0;

    assert(namelen < IFNAMSIZ);
    build_sys_serial_path(devname, spath, sizeof(spath));

    fd = open(spath, O_RDONLY);
    if (fd < 0) {
        /* Ignore ENOENT (see lintape_map_load) */
        if (errno == ENOENT)
            return 0;
        LOG_RETURN(rc = -errno, "Cannot open '%s'", spath);
    }

    memset(serial, 0, sizeof(serial));
    nread = read(fd, serial, sizeof(serial) - 1);
    if (nread <= 0)
        LOG_GOTO(out_close, rc = -errno, "Cannot read serial at '%s'", spath);

    /* rstrip stupid \n */
    if (serial[nread - 1] == '\n')
        serial[nread - 1] = '\0';

    dinfo = calloc(1, sizeof(*dinfo));
    if (dinfo == NULL)
        LOG_GOTO(out_close, rc = -ENOMEM, "Cannot allocate cache node for '%s'",
                 spath);

    memcpy(dinfo->dme_serial, serial, nread);
    memcpy(dinfo->dme_devname, devname, namelen);

    drive_cache = g_slist_prepend(drive_cache, dinfo);

out_close:
    close(fd);
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

int lintape_dev_rlookup(const char *name, char *serial, size_t serial_size)
{
    GSList  *element;

    if (strlen(name) >= IFNAMSIZ)
        LOG_RETURN(-ENAMETOOLONG, "Device name '%s' > %d char long",
                   name, IFNAMSIZ - 1);

    if (drive_cache == NULL) {
        pho_debug("No information available in cache: loading...");
        lintape_map_load();
    }

    element = g_slist_find_custom(drive_cache, name, _find_by_name_cb);
    if (element != NULL) {
        struct drive_map_entry  *dme = element->data;

        pho_debug("Found serial '%s' for device %s", dme->dme_serial, name);
        snprintf(serial, serial_size, "%s", dme->dme_serial);
        return 0;
    }

    return -ENOENT;
}

int lintape_dev_lookup(const char *serial, char *path, size_t path_size)
{
    GSList  *element;

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

int lintape_map_load(void)
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

void lintape_map_free(void)
{
    pho_debug("Freeing device serial cache");
    g_slist_free_full(drive_cache, free);
    drive_cache = NULL;
}
