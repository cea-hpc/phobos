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
 * \brief  Phobos scsi_tape serial/path mapping.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_ldm.h"
#include "pho_module_loader.h"
#include "pho_type_utils.h"
#include "slist.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <net/if.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PLUGIN_NAME     "scsi_tape"
#define PLUGIN_MAJOR    0
#define PLUGIN_MINOR    1

static struct module_desc DEV_ADAPTER_SCSI_TAPE_MODULE_DESC = {
    .mod_name  = PLUGIN_NAME,
    .mod_major = PLUGIN_MAJOR,
    .mod_minor = PLUGIN_MINOR,
};

/* Driver name to access /sys/class tree of scsi tape */
#define DRIVER_NAME "scsi_tape"

/* Driver name to access /sys/class tree of scsi generic devices */
#define SG_DRIVER_NAME "scsi_generic"

/* Maximum serial size (including trailing zero) */
#define MAX_SERIAL  48

/* Maximum model size (including trailing zero) */
#define MAX_MODEL  33

/* Maximum number of drives supported */
#define LDM_MAX_DRIVES  256

/* sysfs attribute for vital product data (VPD) page 0x80,
 * includes the Serial Number.
 */
#define SYS_DEV_PAGE80   "device/vpd_pg80"
/* name of device model attribute under /sys/class/scsi_tape/stX */
#define SYS_DEV_MODEL "device/model"
/* name of the pointer to the SCSI generic device under
 * /sys/class/scsi_tape/stX
 */
#define SYS_DEV_GENERIC "device/generic"

/* In-memory map to associate drive serial and device name.
 *
 * The first time it is used, this ldm adapter lists all 'st'
 * devices on the system and loads them in an internal cache
 * to remember the st<->sg device association, and their related
 * information (serial number and model).
 * Calls to scsi_tape_dev_query() and scsi_tape_dev_lookup()
 * relies on this list.
 * scsi_tape_dev_query() accepts both 'st' and 'sg' devices
 * as argument (the function checks the two fields).
 */
struct drive_map_entry {
    char    serial[MAX_SERIAL];  /* e.g. 1013005381 - as a string */
    char    model[MAX_MODEL];    /* e.g. ULT3580-TD6 */
    char    st_devname[IFNAMSIZ];   /* e.g. st1 */
    char    sg_devname[IFNAMSIZ];   /* e.g. sg5 */
};

/**
 * List of available drives.
 * Protected by phobos_context()->ldm_dev_scsi_tape_lock.
 */
static struct slist_entry *drive_cache;

/** Build the path of a sysfs attribute of a device listed under the given
 * device class, e.g. /sys/class/scsi_tape/st0/device/model.
 */
static void build_class_sys_path(const char *class_name, const char *name,
                                 const char *attr, char *dst_path, size_t n)
{
    snprintf(dst_path, n, "/sys/class/%s/%s/%s", class_name, name, attr);
    dst_path[n - 1] = '\0';
}

static void build_sys_path(const char *name, const char *attr, char *dst_path,
                           size_t n)
{
    build_class_sys_path(DRIVER_NAME, name, attr, dst_path, n);
}

/** Read the given attribute for the given device name */
static int read_device_attr(const char *st_devname, const char *attrname,
                            char *info, size_t info_size)
{
    char    spath[PATH_MAX];
    ssize_t nread;
    int     fd;
    int     rc = 0;

    build_sys_path(st_devname, attrname, spath, sizeof(spath));

    fd = open(spath, O_RDONLY);
    if (fd < 0)
        LOG_RETURN(rc = -errno, "Cannot open '%s'", spath);

    memset(info, 0, info_size);
    nread = read(fd, info, info_size - 1);
    if (nread <= 0)
        LOG_GOTO(out_close, rc = -errno, "Cannot read %s in '%s'", attrname,
                 spath);
    info[nread] = '\0';

    /* rstrip stupid '\n' and spaces */
    rstrip(info);

    pho_debug("Device '%s': %s='%s'", st_devname, attrname, info);

out_close:
    close(fd);
    return rc;
}

/**
 * Read serial number from SCSI INQUIRY response page x80
 * (Unit Serial Number Inquiry Page).
 *
 * @param class_name sysfs class the device is listed under, "scsi_tape"
 *                   for a st name or "scsi_generic" for a sg name.
 * @param dev_name   SCSI device name as it appears under
 *                   /sys/class/<class_name>, e.g. "st0" or "sg3".
 * @param attrname   Path of a page80 pseudo-file in sysclass (e.g. vpd_pg80),
 *                   relative to /sys/class/<class_name>/<dev_name>.
 * @param info       Output string, NUL-terminated on success.
 * @param info_size  Size of the output buffer, including the NUL terminator.
 *
 * @return 0 on success, -errno on error.
 */
static int read_page80_serial(const char *class_name, const char *dev_name,
                              const char *attrname,
                              char *info, size_t info_size)
{
#define SCSI_PAGE80_HEADER_SIZE 4
/* max value of "length" (encoded on 1 byte) */
#define SCSI_PAGE80_PAGE_MAX    255
    static const int BUFF_SIZE = SCSI_PAGE80_HEADER_SIZE + SCSI_PAGE80_PAGE_MAX;

    char     spath[PATH_MAX];
    char    *buffer;
    ssize_t  nread;
    int      len;
    int      fd;
    int      i;
    int      rc = 0;

    /* According to tape drive SCSI references, the page 80 looks like:
     * +------+----------------------------------------+
     * | Byte |            Contents (bits 7 to 0)      |
     * +------+----------------------------------------+
     * |   0  | periph. qualifier |    device type     |
     * |   1  |           Page code (0x80)             |
     * |   2  |             reserved                   |
     * |   3  |   Page length (length of the S/N)      |
     * +------+----------------------------------------+
     * |   4  |           Serial number                |
     * |  ... |       (left-padded with zeros)         |
     */

    /* Allocate a temporary buffer to read the page 0x80. */
    buffer = xcalloc(1, BUFF_SIZE);

    build_class_sys_path(class_name, dev_name, attrname, spath,
                         sizeof(spath));

    fd = open(spath, O_RDONLY);
    if (fd < 0) {
        free(buffer);
        LOG_RETURN(rc = -errno, "Cannot open '%s'", spath);
    }

    nread = read(fd, buffer, BUFF_SIZE);
    if (nread < 0)
        LOG_GOTO(out_close, rc = -errno, "Cannot read %s in '%s'", attrname,
                 spath);

    if (nread < SCSI_PAGE80_HEADER_SIZE)
        LOG_GOTO(out_close, rc = -EINTR, "Incomplete page header in '%s'",
                 spath);

    if (buffer[1] != (char)0x80)
        /* the given path was not at vpd_pg80 */
        LOG_GOTO(out_close, rc = -EINVAL, "Invalid page code %#hhx != 0x80",
                 buffer[1]);

    len = (unsigned char)buffer[3];

    /* Make sure we read all the serial number.
     * Short read is not expected in that case.
     */
    if (nread < len + SCSI_PAGE80_HEADER_SIZE)
        LOG_GOTO(out_close, rc = -EINTR, "Invalid page size %zd < %d", nread,
                 len + SCSI_PAGE80_HEADER_SIZE);

    /* skip leading zeros */
    for (i = 0; i < len && buffer[i+SCSI_PAGE80_HEADER_SIZE] == '\0'; i++)
        ;

    if (info_size < (size_t)(len - i) + 1)
        LOG_GOTO(out_close, rc = -ENOBUFS, "Target buffer too small");

    /* copy the serial number */
    memcpy(info, buffer + SCSI_PAGE80_HEADER_SIZE + i, len - i);
    info[len - i] = '\0';

    pho_debug("Device '%s': %s='%s'", dev_name, attrname, info);

out_close:
    free(buffer);
    close(fd);
    return rc;
}

/**
 * Check that the device behind the given sysfs class node (e.g. "st0" under
 * scsi_tape, "sg3" under scsi_generic) still reports the given serial number
 * through its VPD page 0x80.
 *
 * This is used to detect stale cache entries after a SCSI re-enumeration:
 * device minor numbers are assigned on a lowest-free basis, so a node name
 * may still exist after a re-enumeration while belonging to another drive
 * that took the freed minor. Only the unit serial number proves the
 * identity of the device behind a given node name.
 *
 * The kernel caches the VPD pages at device scan time and serves them from
 * memory, so this is a plain sysfs read: no SCSI command is issued to the
 * drive and a hung device cannot delay the caller.
 *
 * @param class_name  sysfs class of the node ("scsi_tape" or
 *                    "scsi_generic").
 * @param dev_name    name of the node within the class (e.g. "st0", "sg3").
 * @param serial      expected unit serial number.
 *
 * @return true if the node exists and reports the expected serial number.
 */
static bool node_matches_serial(const char *class_name, const char *dev_name,
                                const char *serial)
{
    char cur_serial[MAX_SERIAL];
    int rc;

    rc = read_page80_serial(class_name, dev_name, SYS_DEV_PAGE80, cur_serial,
                            sizeof(cur_serial));
    if (rc) {
        pho_error(rc, "Cannot read VPD serial of '%s' under %s, "
                  "considering the cached entry as stale", dev_name,
                  class_name);
        return false;
    }

    return strcmp(cur_serial, serial) == 0;
}

/** Indicate if the given name is a valid sg device */
static inline bool is_sg_device(const char *dev_name)
{
    char suffix;
    int  idx;
    int  res;

    res = sscanf(dev_name, "sg%d%c", &idx, &suffix);
    return res == 1;
}

/** Retrieve the scsi generic device corresponding to 'st_devname' */
static int read_scsi_generic(const char *st_devname, char *sg_devname,
                             size_t sg_size)
{
    char  spath[PATH_MAX];
    char  link[PATH_MAX];
    char *sg_name;
    int   rc = 0;

    build_sys_path(st_devname, SYS_DEV_GENERIC, spath, sizeof(spath));

    rc = readlink(spath, link, sizeof(link));
    if (rc < 0)
        LOG_RETURN(rc = -errno, "Cannot read symlink '%s'", spath);

    link[rc < sizeof(link) ? rc : sizeof(link) - 1] = '\0';

    /* link is supposed to end with '/sgN' */
    sg_name = strrchr(link, '/');
    if (sg_name == NULL)
        sg_name = link;
    else
        sg_name++;

    if (!is_sg_device(sg_name))
        LOG_RETURN(rc = -EINVAL, "'%s' is not a valid sg device", link);

    if (strnlen(sg_name, sg_size) >= sg_size)
        LOG_RETURN(rc = -EINVAL, "'%s' is a too long sg device string to be "
                   "stored in %zu characters of the sg_devname",
                   sg_name, sg_size);

    strcpy(sg_devname, sg_name);

    pho_debug("Device '%s': SG='%s'", st_devname, sg_name);
    return 0;
}

/**
 * Load the information about a given st device into the local cache.
 * @param st_devname  Name of a 'st' device ('sg' doesn't provide
 *                    the required information (serial and model)).
 * @return 0 on success, -errno on failure.
 */
static int cache_load_from_name(const char *st_devname)
{
    struct drive_map_entry   *dinfo;
    size_t                    namelen = strlen(st_devname);
    int                       rc = 0;

    /* ensure final '\0' fits in the target buffer */
    if (namelen + 1 > IFNAMSIZ)
        LOG_RETURN(-ENOBUFS, "Device name '%s' exceeds expected size %d",
                   st_devname, IFNAMSIZ);

    dinfo = xcalloc(1, sizeof(*dinfo));

    memcpy(dinfo->st_devname, st_devname, namelen + 1);

    rc = read_page80_serial(DRIVER_NAME, st_devname, SYS_DEV_PAGE80,
                            dinfo->serial, sizeof(dinfo->serial));
    if (rc)
        goto err_free;

    rc = read_device_attr(st_devname, SYS_DEV_MODEL, dinfo->model,
                          sizeof(dinfo->model));
    if (rc)
        goto err_free;

    /* Read SCSI generic device name
     * (LTFS 2.4 needs path to sg device)
     */
    rc = read_scsi_generic(st_devname, dinfo->sg_devname,
                           sizeof(dinfo->sg_devname));
    if (rc)
        goto err_free;

    pho_debug("Added device ST=/dev/%s SG=/dev/%s with serial '%s'",
              dinfo->st_devname, dinfo->sg_devname, dinfo->serial);
    drive_cache = list_prepend(drive_cache, dinfo);

    return 0;

err_free:
    free(dinfo);
    return rc;
}

static bool match_st(const void *item, const void *user_data)
{
    const struct drive_map_entry *drive = item;
    const char *name = user_data;

    assert(drive != NULL);
    assert(name != NULL);

    return !strcmp(drive->st_devname, name);
}

static bool match_sg(const void *item, const void *user_data)
{
    const struct drive_map_entry *drive = item;
    const char *name = user_data;

    assert(drive != NULL);
    assert(name != NULL);

    return !strcmp(drive->sg_devname, name);
}

static bool match_serial(const void *item, const void *user_data)
{
    const struct drive_map_entry *drive = item;
    const char *serial = user_data;

    assert(drive != NULL);
    assert(serial != NULL);

    return !strcmp(drive->serial, serial);
}

static void build_sys_class_path(char *path, size_t path_size, const char *name)
{
    snprintf(path, path_size, "/sys/class/%s", name);
    path[path_size - 1] = '\0';
}

/**
 * Indicate if the given device name is a SCSI tape (st) device.
 */
static inline bool is_st_device(const char *dev_name)
{
    char suffix;
    int  idx;
    int  res;

    res = sscanf(dev_name, "st%d%c", &idx, &suffix);
    return res == 1;
}

__attribute__((destructor)) static void scsi_tape_map_free(void)
{
    if (drive_cache == NULL)
        return;

    list_free_all(drive_cache, free);
    drive_cache = NULL;
}

/**
 * readdir helper with return code.
 * This helps distinguishing readdir errors from end of directory,
 * and preserves previous value of errno.
 */
static int readdir_rc(DIR *dir, struct dirent **ent)
{
    int save_errno = errno;

    /* If  the  end  of  the directory stream is reached, NULL is returned and
     * errno is not changed. If an error occurs, NULL is returned  and  errno
     * is set appropriately.
     */
    errno = 0;
    *ent = readdir(dir);
    if (errno != 0)
        return -errno;

    errno = save_errno;
    return 0;
}

/**
 * Loads the list of 'st' devices on the system into an internal cache.
 * @return 0 on success, -errno on failure.
 */
static int scsi_tape_map_load(void)
{
    char             sys_path[PATH_MAX];
    DIR             *dir;
    struct dirent   *entry;
    int              count = 0;
    int              rc;

    /* Is this a refresh? */
    if (drive_cache != NULL)
        scsi_tape_map_free();

    build_sys_class_path(sys_path, sizeof(sys_path), DRIVER_NAME);
    pho_debug("Listing devices at '%s' to populate cache", sys_path);

    dir = opendir(sys_path);
    if (dir == NULL)
        LOG_RETURN(rc = -errno, "Cannot opendir(%s) to list devices", sys_path);

    do {
        rc = readdir_rc(dir, &entry);
        if (rc)
            LOG_GOTO(out_close, -rc, "Error while iterating over directory");

        if (entry == NULL)
            /* end of directory */
            break;

        if (!is_st_device(entry->d_name))
            continue;

        rc = cache_load_from_name(entry->d_name);
        if (rc)
            LOG_GOTO(out_close, rc, "Error while loading entry '%s'",
                     entry->d_name);

        pho_debug("Loaded device '%s' successfully", entry->d_name);
        count++;
    } while (1);

    pho_debug("Loaded %d devices for driver %s", count, DRIVER_NAME);

out_close:
    closedir(dir);
    if (rc && drive_cache)
        scsi_tape_map_free();
    return rc;
}

/**
 * Copy information about a drive identified by serial or device path.
 *
 * Readers share the cache lock. A missing or stale entry triggers one reload
 * under the write lock, followed by the same path and serial checks. Copy the
 * entry before unlocking so a concurrent reload cannot invalidate the result.
 *
 * @param id         Serial number if by_serial is true, device path otherwise.
 * @param by_serial  Whether to search by serial instead of st/sg device name.
 * @param info       Copy of the validated cache entry on success.
 *
 * @return 0 on success, -errno on failure.
 */
static int scsi_tape_dev_info(const char *id, bool by_serial,
                              struct drive_map_entry *info)
{
    pthread_rwlock_t *lock = &phobos_context()->ldm_dev_scsi_tape_lock;
    const struct drive_map_entry *dme;
    const char *name = id;
    bool refreshed = false;
    char path[PATH_MAX];
    int rc;

    if (!by_serial) {
        name = strrchr(id, '/');
        name = name ? name + 1 : id;
    }

    if (strlen(name) >= (by_serial ? MAX_SERIAL : IFNAMSIZ))
        LOG_RETURN(-ENAMETOOLONG, "Device identifier '%s' is too long", name);

    rc = pthread_rwlock_rdlock(lock);
    if (rc)
        return -rc;

retry:
    if (by_serial) {
        dme = list_find(drive_cache, name, match_serial);
    } else {
        dme = list_find(drive_cache, name, match_st);
        if (!dme)
            dme = list_find(drive_cache, name, match_sg);
    }

    if (dme) {
        const char *dev_name = by_serial ? dme->sg_devname : name;
        const char *class_name = is_st_device(dev_name) ? DRIVER_NAME
                                                       : SG_DRIVER_NAME;
        const char *dev_path = id;

        if (by_serial) {
            /* LTFS 2.4 needs the sg node. Its minor number is assigned
             * independently of the st node, so verify the sg identity.
             */
            snprintf(path, sizeof(path), "/dev/%s", dev_name);
            dev_path = path;
        }

        if (access(dev_path, F_OK) == 0 &&
            node_matches_serial(class_name, dev_name, dme->serial)) {
            *info = *dme;
            rc = 0;
            goto out_unlock;
        }
    }

    if (refreshed) {
        rc = -ENOENT;
        goto out_unlock;
    }

    pthread_rwlock_unlock(lock);
    rc = pthread_rwlock_wrlock(lock);
    if (rc)
        return -rc;

    pho_debug("Device '%s' missing or stale in cache: reloading...", id);
    rc = scsi_tape_map_load();
    if (rc)
        goto out_unlock;

    refreshed = true;
    goto retry;

out_unlock:
    pthread_rwlock_unlock(lock);
    return rc;
}

/** Return the sg device path of the drive with the given serial number. */
static int scsi_tape_dev_lookup(const char *serial, char *path,
                                size_t path_size)
{
    struct drive_map_entry info;
    int rc;

    ENTRY;

    rc = scsi_tape_dev_info(serial, true, &info);
    if (rc)
        return rc;

    rc = snprintf(path, path_size, "/dev/%s", info.sg_devname);
    if (rc >= path_size)
        LOG_RETURN(-ENOBUFS, "Target buffer too small for device path");

    return 0;
}

/**
 * Returns information about the drive with the given path (this can be a 'st'
 * or 'sg' device).
 */
static int scsi_tape_dev_query(const char *dev_path, struct ldm_dev_state *lds)
{
    struct drive_map_entry info;
    int rc;

    ENTRY;

    if (access(dev_path, F_OK))
        LOG_RETURN(-errno, "Cannot access '%s'", dev_path);

    rc = scsi_tape_dev_info(dev_path, false, &info);
    if (rc)
        return rc;

    /* Free any preexisting serial and model */
    free(lds->lds_serial);
    free(lds->lds_model);

    memset(lds, 0, sizeof(*lds));
    lds->lds_family = PHO_RSC_TAPE;
    lds->lds_model = xstrdup(info.model);
    lds->lds_serial = xstrdup(info.serial);

    return 0;
}

/** Exported dev adapter */
struct pho_dev_adapter_module_ops DEV_ADAPTER_SCSI_TAPE_OPS = {
    .dev_lookup = scsi_tape_dev_lookup,
    .dev_query  = scsi_tape_dev_query,
    .dev_load   = NULL, /** @TODO to be implemented */
    .dev_eject  = NULL, /** @TODO to be implemented */
};

/** Dev adapter module registration entry point */
int pho_module_register(void *module, void *context)
{
    struct dev_adapter_module *self = (struct dev_adapter_module *) module;

    phobos_module_context_set(context);

    self->desc = DEV_ADAPTER_SCSI_TAPE_MODULE_DESC;
    self->ops = &DEV_ADAPTER_SCSI_TAPE_OPS;

    pthread_rwlock_init(&phobos_context()->ldm_dev_scsi_tape_lock, NULL);

    return 0;
}
