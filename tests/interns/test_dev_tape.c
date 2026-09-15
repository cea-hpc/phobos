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
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

#define TEST_MAX_DRIVES 32


static int test_unit(void *hint)
{
    struct dev_adapter_module *deva;
    struct ldm_dev_state lds = {0};
    char *dev_name = hint;
    int rc;

    rc = get_dev_adapter(PHO_RSC_TAPE, &deva);
    if (rc)
        return rc;

    rc = ldm_dev_query(deva, dev_name, &lds);
    if (rc == 0)
        pho_info("Mapped '%s' to '%s' (model: '%s')", dev_name, lds.lds_serial,
                 lds.lds_model);

    ldm_dev_state_fini(&lds);

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
    generic[rc < sizeof(generic) ? rc : sizeof(generic)] = '\0';

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
    struct dev_adapter_module *deva;
    struct ldm_dev_state lds = {0};
    char sg_path[PATH_MAX];
    char *st_path = hint;
    char path[128];
    int rc;

    rc = get_dev_adapter(PHO_RSC_TAPE, &deva);
    if (rc)
        return rc;

    /* get the matching sg name */
    rc = st_to_sg_path(st_path, sg_path, sizeof(sg_path));
    if (rc)
        return rc;

    rc = ldm_dev_query(deva, st_path, &lds);
    if (rc)
        return rc;

    rc = ldm_dev_lookup(deva, lds.lds_serial, path, sizeof(path));
    if (rc) {
        ldm_dev_state_fini(&lds);
        return rc;
    }

    pho_debug("Reverse mapped serial '%s' to '%s'", lds.lds_serial, path);
    ldm_dev_state_fini(&lds);
    return strcmp(path, sg_path) == 0 ? 0 : -EINVAL;
}

static bool device_exists(int dev_index)
{
    char dev_path[PATH_MAX];
    struct stat st;
    int rc;

    snprintf(dev_path, sizeof(dev_path) - 1, "/dev/st%d", dev_index);
    rc = stat(dev_path, &st);
    pho_info("Accessing %s: %s", dev_path, rc ? strerror(errno) : "OK");
    return rc == 0;
}

/* ------------------------------------------------------------------------ *
 * Re-enumeration tests.
 *
 * SCSI device nodes are assigned the lowest free minor number at
 * enumeration time. When drives are re-enumerated (e.g. after a firmware
 * flash followed by a rescan, as done in the field), a drive may come back
 * under different st/sg names, and its old names may be taken over by
 * another drive. The test below forces such a name exchange between two
 * drives and checks that the ldm cache recovers, instead of silently
 * returning a node that now belongs to another drive.
 */

#define TEST_MAX_SERIAL   48
#define RETRY_MAX         60
#define RETRY_SLEEP_US    500000

/** Indicate whether the given name is a st device, e.g. "st3". */
static bool is_st_name(const char *name)
{
    char suffix;
    int  idx;

    return sscanf(name, "st%d%c", &idx, &suffix) == 1;
}

/**
 * Read the VPD page 0x80 unit serial of the given st device directly from
 * sysfs. This is the ground truth about which drive currently sits behind
 * a node name: it does not rely on the ldm cache under test.
 */
static int sysfs_read_st_serial(const char *st_name, char *serial,
                                size_t size)
{
    char          path[PATH_MAX];
    unsigned char buf[4 + 255];
    ssize_t       nread;
    int           len;
    int           i;
    int           fd;

    snprintf(path, sizeof(path), "/sys/class/scsi_tape/%s/device/vpd_pg80",
             st_name);

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -errno;

    nread = read(fd, buf, sizeof(buf));
    close(fd);
    if (nread < 0)
        return -errno;

    /* VPD page 0x80 layout: byte 0 is the peripheral qualifier and device
     * type, byte 1 the page code, byte 3 the length of the serial number,
     * which starts at byte 4 and may be left-padded with zeros.
     */
    if (nread < 4 || buf[1] != 0x80)
        return -EINVAL;

    len = buf[3];
    if (nread < len + 4)
        return -EINVAL;

    for (i = 0; i < len && buf[4 + i] == '\0'; i++)
        ;

    if (size < (size_t)(len - i) + 1)
        return -ENOBUFS;

    memcpy(serial, buf + 4 + i, len - i);
    serial[len - i] = '\0';
    return 0;
}

/**
 * Find the st name of the drive with the given serial by scanning
 * /sys/class/scsi_tape (ground truth, independent from the ldm cache).
 */
static int sysfs_find_st_by_serial(const char *serial, char *st_name,
                                   size_t size)
{
    char           cur_serial[TEST_MAX_SERIAL];
    struct dirent *ent;
    DIR           *dir;
    size_t         name_len;
    int            rc = -ENOENT;

    dir = opendir("/sys/class/scsi_tape");
    if (dir == NULL)
        return -errno;

    while ((ent = readdir(dir)) != NULL) {
        if (!is_st_name(ent->d_name))
            continue;

        if (sysfs_read_st_serial(ent->d_name, cur_serial,
                                 sizeof(cur_serial)) == 0
            && strcmp(cur_serial, serial) == 0) {
            name_len = strlen(ent->d_name);

            /* ensure final '\0' fits in the target buffer, as done in
             * cache_load_from_name() (ldm_dev_scsi_tape.c)
             */
            if (name_len + 1 > size) {
                rc = -ENOBUFS;
                break;
            }

            memcpy(st_name, ent->d_name, name_len + 1);
            rc = 0;
            break;
        }
    }

    closedir(dir);
    return rc;
}

/** Write a string to a sysfs attribute (device deletion, host rescan...). */
static int sysfs_write(const char *path, const char *data)
{
    int fd;
    int rc = 0;

    fd = open(path, O_WRONLY);
    if (fd < 0)
        return -errno;

    if (write(fd, data, strlen(data)) != (ssize_t)strlen(data))
        rc = -errno;

    close(fd);
    return rc;
}

/** SCSI address of a device, as used to trigger a targeted host rescan.
 * The address identifies the physical drive slot and survives
 * re-enumeration.
 */
struct test_scsi_addr {
    int host;
    int chan;
    int target;
    int lun;
};

/** Read the SCSI address "H:C:T:L" of the given st device. */
static int get_scsi_addr(const char *st_name, struct test_scsi_addr *addr)
{
    char    path[PATH_MAX];
    char    link[PATH_MAX];
    char   *base;
    ssize_t n;

    snprintf(path, sizeof(path), "/sys/class/scsi_tape/%s/device", st_name);

    n = readlink(path, link, sizeof(link) - 1);
    if (n < 0)
        return -errno;
    link[n] = '\0';

    /* the link ends with the address of the scsi device, e.g. "0:0:2:0" */
    base = strrchr(link, '/');
    if (base == NULL)
        return -EINVAL;
    base++;

    if (sscanf(base, "%d:%d:%d:%d", &addr->host, &addr->chan, &addr->target,
               &addr->lun) != 4)
        return -EINVAL;

    return 0;
}

/** Rescan the given device address on its SCSI host: the kernel re-probes
 * this exact target and re-creates the device if it had been deleted.
 */
static int rescan_addr(const struct test_scsi_addr *addr)
{
    char path[PATH_MAX];
    /* worst case of "%d %d %d" (35 chars) + final '\0' */
    char data[36];

    snprintf(path, sizeof(path), "/sys/class/scsi_host/host%d/scan",
             addr->host);
    snprintf(data, sizeof(data), "%d %d %d", addr->chan, addr->target,
             addr->lun);

    return sysfs_write(path, data);
}

/** Rescan every SCSI host of the machine ("- - -"): recovery path used
 * when a drive did not come back from a targeted rescan.
 */
static void rescan_all_hosts(void)
{
    char           path[PATH_MAX];
    struct dirent *ent;
    DIR           *dir;
    int            rc;

    dir = opendir("/sys/class/scsi_host");
    if (dir == NULL)
        return;

    while ((ent = readdir(dir)) != NULL) {
        snprintf(path, sizeof(path), "/sys/class/scsi_host/%s/scan",
                 ent->d_name);
        rc = sysfs_write(path, "- - -");
        if (rc)
            pho_error(rc, "Cannot rescan SCSI host '%s'", ent->d_name);
    }

    closedir(dir);
}

/** Delete the SCSI device behind the given st name: its st and sg minor
 * numbers are freed and can be taken by the next enumerated device.
 */
static int delete_drive(const char *st_name)
{
    char path[PATH_MAX];

    snprintf(path, sizeof(path), "/sys/class/scsi_tape/%s/device/delete",
             st_name);

    return sysfs_write(path, "1");
}

/** Wait until both serials are visible again under /sys/class/scsi_tape. */
static int wait_for_serials(const char *serial_a, const char *serial_b)
{
    char st_name[IFNAMSIZ];
    int  tries;

    for (tries = 0; tries < RETRY_MAX; tries++) {
        if (sysfs_find_st_by_serial(serial_a, st_name,
                                    sizeof(st_name)) == 0
            && sysfs_find_st_by_serial(serial_b, st_name,
                                       sizeof(st_name)) == 0)
            return 0;

        usleep(RETRY_SLEEP_US);
    }

    return -ETIMEDOUT;
}

/** Wait until the given device node exists in /dev: udev may need a short
 * delay to create it after the sysfs entry is back.
 */
static int wait_for_dev_node(const char *dev_path)
{
    int tries;

    for (tries = 0; tries < RETRY_MAX; tries++) {
        if (access(dev_path, F_OK) == 0)
            return 0;

        usleep(RETRY_SLEEP_US);
    }

    return -ETIMEDOUT;
}

/** Extract the sg index from an sg path, e.g. "/dev/sg5" -> 5, or -1. */
static int sg_path_index(const char *sg_path)
{
    int idx;

    if (sscanf(sg_path, "/dev/sg%d", &idx) != 1)
        return -1;

    return idx;
}

/** Count the tape drives visible in /dev, tolerating holes in the st
 * numbering.
 */
static int count_tape_drives(void)
{
    int count = 0;
    int i;

    for (i = 0; i < TEST_MAX_DRIVES; i++)
        if (device_exists(i))
            count++;

    return count;
}

/**
 * Force an exchange of sg node names between two drives and check that the
 * ldm cache recovers.
 *
 * The two drives are deleted then re-enumerated in an order that makes the
 * drive with the highest sg index take the lowest freed sg minor, i.e. the
 * other drive's name: after that, the cached sg name of each drive still
 * exists but belongs to the other drive. A lookup by serial must detect
 * the mismatch through the VPD unit serial of the node and resolve the
 * drive through its current node, instead of returning the stale cached
 * one.
 *
 * This reproduces the field renumbering scenario where a rescan brings
 * drives back in another order than their initial enumeration.
 */
static int test_sg_node_reassignment(void *hint)
{
    char serial_a[TEST_MAX_SERIAL], serial_b[TEST_MAX_SERIAL];
    char st_path_a[PATH_MAX], st_path_b[PATH_MAX];
    char st_name_a[IFNAMSIZ], st_name_b[IFNAMSIZ];
    char sg_path_a[PATH_MAX], sg_path_b[PATH_MAX];
    struct test_scsi_addr addr_a, addr_b;
    struct dev_adapter_module *deva;
    struct ldm_dev_state lds = {0};
    char cur_serial[TEST_MAX_SERIAL];
    char expected_sg[PATH_MAX];
    char cur_st[IFNAMSIZ];
    char path[PATH_MAX];
    int  i_a = -1;
    int  i_b = -1;
    int  rc;
    int  i;

    (void)hint;

    rc = get_dev_adapter(PHO_RSC_TAPE, &deva);
    if (rc)
        return rc;

    /* Pick the first two tape drives of the machine. */
    for (i = 0; i < TEST_MAX_DRIVES; i++) {
        if (!device_exists(i))
            continue;
        if (i_a < 0) {
            i_a = i;
            continue;
        }
        i_b = i;
        break;
    }
    if (i_a < 0 || i_b < 0)
        LOG_RETURN(-ENODEV, "this test requires two tape drives");

    snprintf(st_name_a, sizeof(st_name_a), "st%d", i_a);
    snprintf(st_name_b, sizeof(st_name_b), "st%d", i_b);
    snprintf(st_path_a, sizeof(st_path_a), "/dev/st%d", i_a);
    snprintf(st_path_b, sizeof(st_path_b), "/dev/st%d", i_b);

    /* Remember the physical address of each drive: it survives the
     * re-enumeration and allows targeted rescans of each drive.
     */
    rc = get_scsi_addr(st_name_a, &addr_a);
    if (rc)
        return rc;
    rc = get_scsi_addr(st_name_b, &addr_b);
    if (rc)
        return rc;

    /* Resolve both drives and populate the ldm cache with their current
     * st/sg names.
     */
    rc = ldm_dev_query(deva, st_path_a, &lds);
    if (rc)
        return rc;
    snprintf(serial_a, sizeof(serial_a), "%s", lds.lds_serial);
    ldm_dev_state_fini(&lds);

    rc = ldm_dev_query(deva, st_path_b, &lds);
    if (rc)
        return rc;
    snprintf(serial_b, sizeof(serial_b), "%s", lds.lds_serial);
    ldm_dev_state_fini(&lds);

    rc = st_to_sg_path(st_path_a, sg_path_a, sizeof(sg_path_a));
    if (rc)
        return rc;
    rc = st_to_sg_path(st_path_b, sg_path_b, sizeof(sg_path_b));
    if (rc)
        return rc;

    pho_info("Re-enumeration test with drives '%s' (%s -> %s) and '%s' "
             "(%s -> %s)", serial_a, st_name_a, sg_path_a, serial_b,
             st_name_b, sg_path_b);

    /* Delete both drives: their st and sg minor numbers are freed. */
    rc = delete_drive(st_name_a);
    if (rc)
        return rc;
    rc = delete_drive(st_name_b);
    if (rc)
        return rc;

    /* While both drives are gone, a lookup by serial must fail cleanly
     * instead of returning a cached path to a node that no longer exists.
     */
    rc = ldm_dev_lookup(deva, serial_a, path, sizeof(path));
    if (rc == 0) {
        rescan_all_hosts();
        wait_for_serials(serial_a, serial_b);
        LOG_RETURN(-EINVAL, "lookup of '%s' unexpectedly succeeded while "
                   "the drive is absent", serial_a);
    }

    /* Re-enumerate both drives with targeted host rescans. Enumerating
     * first the drive with the highest sg index makes it take the lowest
     * freed sg minor, i.e. the other drive's name: the sg names of the two
     * drives are deterministically exchanged.
     */
    if (sg_path_index(sg_path_b) > sg_path_index(sg_path_a)) {
        rc = rescan_addr(&addr_b);
        if (rc)
            return rc;
        rc = rescan_addr(&addr_a);
    } else {
        rc = rescan_addr(&addr_a);
        if (rc)
            return rc;
        rc = rescan_addr(&addr_b);
    }
    if (rc)
        return rc;

    rc = wait_for_serials(serial_a, serial_b);
    if (rc) {
        rescan_all_hosts();
        rc = wait_for_serials(serial_a, serial_b);
        if (rc)
            LOG_RETURN(rc, "drives did not come back after the rescan");
    }

    /* Ground truth: where does the drive with serial_a sit now? This is
     * read from sysfs only, not from the ldm cache under test.
     */
    rc = sysfs_find_st_by_serial(serial_a, cur_st, sizeof(cur_st));
    if (rc)
        LOG_RETURN(rc, "cannot locate drive '%s' after re-enumeration",
                   serial_a);

    snprintf(path, sizeof(path), "/dev/%s", cur_st);
    rc = st_to_sg_path(path, expected_sg, sizeof(expected_sg));
    if (rc)
        return rc;

    rc = wait_for_dev_node(expected_sg);
    if (rc)
        LOG_RETURN(rc, "'%s' did not appear in /dev", expected_sg);

    /* The name exchange must actually have happened, otherwise the stale
     * identity path of the lookup is not exercised at all.
     */
    if (strcmp(expected_sg, sg_path_a) == 0)
        LOG_RETURN(-ENODEV, "could not force a sg name exchange in this "
                   "environment: '%s' kept its sg name", serial_a);

    /* The actual check: the lookup by serial must return the current node
     * of the drive, not the stale cached one, which still exists but
     * belongs to the other drive.
     */
    rc = ldm_dev_lookup(deva, serial_a, path, sizeof(path));
    if (rc)
        LOG_RETURN(rc, "lookup of '%s' failed after re-enumeration",
                   serial_a);

    if (strcmp(path, expected_sg) != 0)
        LOG_RETURN(-EINVAL, "lookup of '%s' returned stale node '%s' "
                   "instead of '%s'", serial_a, path, expected_sg);

    pho_info("Lookup of '%s' recovered its current node '%s'", serial_a,
             path);

    /* The returned node must really serve the requested drive. */
    rc = ldm_dev_query(deva, path, &lds);
    if (rc)
        return rc;
    rc = strcmp(lds.lds_serial, serial_a);
    ldm_dev_state_fini(&lds);
    if (rc != 0)
        LOG_RETURN(-EINVAL, "node '%s' does not serve drive '%s'", path,
                   serial_a);

    /* Same check on the name-based query: if the old st name of the first
     * drive was taken over by the second drive, querying it must report
     * the serial of the second drive, not the stale cached serial.
     */
    rc = sysfs_read_st_serial(st_name_a, cur_serial, sizeof(cur_serial));
    if (rc == 0 && strcmp(cur_serial, serial_b) == 0) {
        rc = ldm_dev_query(deva, st_path_a, &lds);
        if (rc)
            return rc;
        rc = strcmp(lds.lds_serial, serial_b);
        ldm_dev_state_fini(&lds);
        if (rc != 0)
            LOG_RETURN(-EINVAL, "query of '%s' reported the stale serial of "
                       "'%s' instead of '%s'", st_path_a, serial_a, serial_b);

        pho_info("Query of reused node '%s' reports the current drive",
                 st_path_a);
    } else {
        pho_debug("st node '%s' was not taken over by the other drive, "
                  "skipping the query-side check", st_name_a);
    }

    /* Restore the original name assignment to leave the machine in its
     * initial state for the rest of the test suite: exchange the names
     * again, with the same rule applied to the current assignment
     * (enumerate first the drive which now holds the highest sg index, so
     * it takes back the lowest freed minor).
     */
    rc = sysfs_find_st_by_serial(serial_a, cur_st, sizeof(cur_st));
    if (rc)
        LOG_RETURN(rc, "cannot locate drive '%s' before restoring the "
                   "initial names", serial_a);
    rc = delete_drive(cur_st);
    if (rc)
        return rc;

    rc = sysfs_find_st_by_serial(serial_b, cur_st, sizeof(cur_st));
    if (rc)
        LOG_RETURN(rc, "cannot locate drive '%s' before restoring the "
                   "initial names", serial_b);
    rc = delete_drive(cur_st);
    if (rc)
        return rc;

    if (sg_path_index(sg_path_b) > sg_path_index(sg_path_a)) {
        /* B initially held the highest sg index, so A holds it now:
         * enumerate A first to give it the lowest minor back.
         */
        rc = rescan_addr(&addr_a);
        if (rc)
            return rc;
        rc = rescan_addr(&addr_b);
    } else {
        rc = rescan_addr(&addr_b);
        if (rc)
            return rc;
        rc = rescan_addr(&addr_a);
    }
    if (rc)
        return rc;

    rc = wait_for_serials(serial_a, serial_b);
    if (rc) {
        rescan_all_hosts();
        rc = wait_for_serials(serial_a, serial_b);
        if (rc)
            LOG_RETURN(rc, "drives did not come back after the restore "
                       "rescan");
    }

    /* Check the restoration, for information: both drives are back and
     * phobos resolves them by serial, so a different name assignment is
     * not an error, but it deserves a clear log.
     */
    rc = sysfs_find_st_by_serial(serial_a, cur_st, sizeof(cur_st));
    if (rc == 0) {
        snprintf(path, sizeof(path), "/dev/%s", cur_st);
        if (st_to_sg_path(path, expected_sg, sizeof(expected_sg)) == 0
            && strcmp(expected_sg, sg_path_a) != 0)
            pho_info("note: '%s' did not get its initial sg name back "
                     "(now '%s', was '%s')", serial_a, expected_sg,
                     sg_path_a);
    }

    return 0;
}

int main(int argc, char **argv)
{
    char dev_name[IFNAMSIZ];
    char test_name[128];
    int i;

    test_env_initialize();

    for (i = 0; i < TEST_MAX_DRIVES; i++) {
        if (!device_exists(i))
            break;
        snprintf(dev_name, sizeof(dev_name) - 1, "/dev/st%d", i);
        snprintf(test_name, sizeof(test_name) - 1,
                 "Test %da: get serial for drive %s", i, dev_name);
        pho_run_test(test_name, test_unit, dev_name, PHO_TEST_SUCCESS);
    }

    for (i = 0; i < TEST_MAX_DRIVES; i++) {
        if (!device_exists(i))
            break;
        snprintf(dev_name, sizeof(dev_name) - 1, "/dev/st%d", i);
        snprintf(test_name, sizeof(test_name) - 1,
                 "Test %dc: match name/serial for drive %s", i, dev_name);
        pho_run_test(test_name, test_name_serial_match, dev_name,
                     PHO_TEST_SUCCESS);
    }

    if (count_tape_drives() < 2) {
        pho_info("Skipping reassignment test: at least 2 tape drives "
                 "required");
    } else {
        pho_run_test("Test: recover from sg node reassignment",
                     test_sg_node_reassignment, NULL, PHO_TEST_SUCCESS);
    }

    pho_info("LINTAPE MAPPER: All tests succeeded");
    exit(EXIT_SUCCESS);
}
