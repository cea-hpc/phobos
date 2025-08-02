/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2025 CEA/DAM.
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
 * \brief  Implementation of the generic locate for RAID like layouts
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>

#include "pho_common.h"
#include "pho_dss.h"
#include "pho_dss_wrapper.h"
#include "pho_type_utils.h"
#include "raid_common.h"

#define hashtable_foreach(hashtable, key, value) \
    GHashTableIter iter; \
    g_hash_table_iter_init(&iter, (hashtable)); \
    while (g_hash_table_iter_next(&iter, (key), (value)))

struct host_capabilities {
    /** Elements of type struct dev_info. The elements are not owned by this
     * array. They are references to elements returned by a DSS query.
     */
    GPtrArray *devices;
    /** There is one element in this array per extent in the object to locate.
     * If accessible_extents[i] is true, the host represented by this structure
     * has access to the i-th extent.
     */
    bool *accessible_extents;
};

struct extent_location {
    /** This is a reference to the extent owned by the layout structure. */
    struct extent *extent;
    /** Medium on which the extent is written. */
    struct media_info *medium;
    /** Hostname of the node that possesses this extent. If NULL, this extent is
     * not locked.
     */
    char *hostname;
};

static void host_capabilities_fini(gpointer data)
{
    struct host_capabilities *host = data;

    g_ptr_array_unref(host->devices);
    free(host->accessible_extents);
    free(host);
}

static GHashTable *setup_available_hosts(struct dev_info *devices, size_t count,
                                         size_t ext_count,
                                         const char *focus_host)
{
    GHashTable *hosts = g_hash_table_new_full(g_str_hash, g_str_equal,
                                              NULL, host_capabilities_fini);
    struct host_capabilities *host_cap;
    size_t i;

    pho_assert(hosts, "Failed to allocate hash table");

    /* Setup focus host in case it doesn't have devices */
    host_cap = xmalloc(sizeof(*host_cap));
    host_cap->devices = g_ptr_array_new();
    host_cap->accessible_extents =
        xcalloc(ext_count, sizeof(*host_cap->accessible_extents));
    g_hash_table_insert(hosts, (gpointer)focus_host, host_cap);

    for (i = 0; i < count; i++) {
        struct dev_info *device = &devices[i];

        host_cap = g_hash_table_lookup(hosts, device->host);
        if (!host_cap) {
            host_cap = xmalloc(sizeof(*host_cap));
            host_cap->devices = g_ptr_array_new();
            host_cap->accessible_extents =
                xcalloc(ext_count, sizeof(*host_cap->accessible_extents));

            g_hash_table_insert(hosts, device->host, host_cap);
        }

        g_ptr_array_add(host_cap->devices, device);
    }

    return hosts;
}

static void extent_location_fini(gpointer data)
{
    struct extent_location *loc = data;

    if (!loc)
        return;

    free(loc->hostname);
    media_info_free(loc->medium);
    free(loc);
}

static GPtrArray *setup_extent_location(struct layout_info *layout)
{
    GPtrArray *extents;
    int i;

    extents = g_ptr_array_new_full(layout->ext_count, extent_location_fini);

    for (i = 0; i < layout->ext_count; i++) {
        struct extent_location *loc =
            xcalloc(1, sizeof(struct extent_location));

        g_ptr_array_add(extents, loc);
        loc->extent = &layout->extents[i];
    }

    return extents;
}

static void remove_extent_location(GPtrArray *extents, int index)
{
    extent_location_fini(extents->pdata[index]);
    extents->pdata[index] = NULL;
}

static int locate_all_extents(struct dss_handle *dss,
                              struct layout_info *layout,
                              GPtrArray *extents,
                              size_t extents_per_split)
{
    int rc;
    int i;
    int j;

    for (i = 0; i < extents->len / extents_per_split; i++) {
        bool one_locate_succeeded = false;

        for (j = 0; j < extents_per_split; j++) {
            struct extent_location *loc;
            struct pho_id *medium_id;
            size_t ext_index;

            ext_index = i * extents_per_split + j;
            loc = extents->pdata[ext_index];
            medium_id = &layout->extents[ext_index].media;

            rc = dss_medium_locate(dss, medium_id, &loc->hostname,
                                   &loc->medium);
            if (rc) {
                pho_warn("Error when trying to locate medium "
                         "(family %s, name %s, library %s) of extent %lu : %s",
                         rsc_family2str(medium_id->family),
                         medium_id->name, medium_id->library,
                         ext_index, strerror(-rc));

                remove_extent_location(extents, ext_index);
                continue;
            }

            one_locate_succeeded = true;
        }

        if (!one_locate_succeeded)
            LOG_GOTO(cleanup, rc = -ENODEV,
                     "DSS locate failed for every extent of the split %d", i);

    }

    return 0;

cleanup:
    for (i = i * extents_per_split + j - 1; i >= 0; i--)
        remove_extent_location(extents, i);

    return rc;
}

static size_t count_compatible_devices(GPtrArray *devices,
                                       struct extent_location *loc)
{
    const char *model = loc->medium->rsc.model;
    size_t count = 0;
    int i;

    for (i = 0; i < devices->len; i++) {
        struct dev_info *device = devices->pdata[i];
        bool compatible;
        int rc;

        if (strcmp(device->rsc.id.library, loc->medium->rsc.id.library))
            continue;

        if (!device->rsc.model) {
            /* devices without model are always compatible */
            count++;
            continue;
        }

        rc = tape_drive_compat_models(model, device->rsc.model, &compatible);
        if (rc) {
            pho_error(rc,
                      "Failed to determine compatibility between "
                      "drive '%s' and tape '%s' for host '%s'",
                      device->rsc.model, model, loc->hostname);
            continue;
        }

        if (compatible)
            count++;
    }

    return count;
}

static bool has_compatible_devices(GPtrArray *devices,
                                   struct extent_location *loc)
{
    const char *model = loc->medium->rsc.model;
    int i;

    for (i = 0; i < devices->len; i++) {
        struct dev_info *device = devices->pdata[i];
        bool compatible;
        int rc;

        if (strcmp(device->rsc.id.library, loc->medium->rsc.id.library))
            continue;

        if (!device->rsc.model)
            /* devices without model are always compatible */
            return true;

        rc = tape_drive_compat_models(model, device->rsc.model, &compatible);
        if (rc) {
            pho_error(rc,
                      "Failed to determine compatibility between "
                      "drive '%s' and tape '%s' for host '%s'",
                      device->rsc.model, model, loc->hostname);
            continue;
        }

        if (compatible)
            return true;
    }

    return false;
}

static void set_host_extent_accessibility(GHashTable *hosts, GPtrArray *extents)
{
    int i;

    for (i = 0; i < extents->len; i++) {
        struct extent_location *loc = extents->pdata[i];
        struct host_capabilities *host;

        if (!loc)
            continue;

        if (loc->hostname) {
            /* Since the host have a lock, it must contain at least one device
             * compatible.
             */
            host = g_hash_table_lookup(hosts, loc->hostname);
            assert(host);

            if (!has_compatible_devices(host->devices, loc)) {
                pho_error(0, "Medium ('%s', '%s') is locked by '%s' but this "
                             "host does not have a compatible device",
                          loc->medium->rsc.id.library,
                          loc->medium->rsc.id.name,
                          loc->hostname);

                host->accessible_extents[i] = false;
            } else {
                host->accessible_extents[i] = true;
            }
        } else {
            /* for all hosts, find if they have one compatible device */
            gpointer key, value;

            hashtable_foreach(hosts, &key, &value) {
                host = value;
                host->accessible_extents[i] =
                    has_compatible_devices(host->devices, loc);
            }
        }
    }
}

static void filter_inaccessible_extents(GHashTable *hosts, GPtrArray *extents)
{
    int i;

    for (i = 0; i < extents->len; i++) {
        struct extent_location *loc = extents->pdata[i];
        bool accessible = false;
        gpointer key, value;

        if (!loc)
            continue;

        hashtable_foreach(hosts, &key, &value) {
            struct host_capabilities *host = value;

            if (host->accessible_extents[i])
                accessible = true;
        }

        if (!accessible)
            remove_extent_location(extents, i);
    }
}

static bool unaccessible_split(GPtrArray *extents, size_t extents_per_split)
{
    int i;

    for (i = 0; i < extents->len / extents_per_split; i++) {
        size_t nb_accessible = 0;
        int j;

        for (j = 0; j < extents_per_split; j++) {
            if (extents->pdata[i * extents_per_split + j])
                nb_accessible++;
        }

        if (nb_accessible == 0) {
            pho_error(0, "Split '%d' is not accessible", i);
            return true;
        }
    }

    return false;
}

static void filter_host_with_partial_access(GHashTable *hosts,
                                            GPtrArray *extents,
                                            size_t n_data_extents,
                                            size_t n_parity_extents)
{
    size_t extents_per_split = n_data_extents + n_parity_extents;
    GPtrArray *to_remove = g_ptr_array_new();
    struct host_capabilities *host;
    gpointer key, value;
    char *hostname;
    int i;

    hashtable_foreach(hosts, &key, &value) {
        hostname = key;
        host = value;

        for (i = 0; i < extents->len / extents_per_split; i++) {
            size_t nb_accessible = 0;
            int j;

            for (j = 0; j < extents_per_split; j++) {
                size_t ext_index = i * extents_per_split + j;
                struct extent_location *loc;

                loc = extents->pdata[ext_index];
                if (!loc)
                    continue;

                /* XXX This is very restrictive. We expect at least
                 * n_data_extents devices available for this one extent. In
                 * practice, most use cases will meet this criteria.
                 */
                if (host->accessible_extents[ext_index] &&
                    count_compatible_devices(host->devices, loc) >=
                        n_data_extents)
                    nb_accessible++;
            }

            if (nb_accessible < n_data_extents) {
                g_ptr_array_add(to_remove, hostname);
                break;
            }
        }
    }

    for (i = 0; i < to_remove->len; i++) {
        char *hostname = to_remove->pdata[i];

        g_hash_table_remove(hosts, hostname);
    }

    g_ptr_array_unref(to_remove);
}

static char *find_best_host(GHashTable *hosts, GPtrArray *extents,
                            size_t n_data_extents, size_t n_parity_extents,
                            const char *focus_host)
{
    size_t extents_per_split = n_data_extents + n_parity_extents;
    gpointer key, value;
    struct {
        const char *hostname;
        size_t nb_locks;
    } best_host = {
        .hostname = NULL,
        .nb_locks = 0,
    };

    hashtable_foreach(hosts, &key, &value) {
        char *hostname = key;
        size_t nb_locks = 0;
        int i;
        int j;

        for (i = 0; i < extents->len / extents_per_split; i++) {
            size_t split_locks = 0;

            for (j = 0; j < extents_per_split; j++) {
                size_t ext_index = i * extents_per_split + j;
                struct extent_location *loc;

                loc = extents->pdata[ext_index];

                if (!loc || !loc->hostname)
                    continue;

                if (!strcmp(hostname, loc->hostname))
                    split_locks++;
            }

            nb_locks += (split_locks > n_data_extents) ?
                n_data_extents : split_locks;
        }

        if (!best_host.hostname ||
            (nb_locks > best_host.nb_locks) ||
            /* In case of equality, focus_host wins */
            (nb_locks == best_host.nb_locks && !strcmp(focus_host, hostname))) {

            best_host.hostname = hostname;
            best_host.nb_locks = nb_locks;
        }
    }

    return xstrdup_safe(best_host.hostname);
}

static void cleanup_locks(struct dss_handle *dss,
                          struct pho_id **medium_locked,
                          int nb_extents)
{
    bool warned = false;
    int i;

    for (i = 0; i < nb_extents; i++) {
        struct media_info medium;
        int rc;

        if (!medium_locked[i])
            continue;

        if (!warned) {
            /* only display the warning if at least one lock was taken */
            pho_warn("locate: could not reserve enough locks after locate. "
                     "Unlocking reserved locks.");
            warned = true;
        }

        medium.rsc.id = *medium_locked[i];
        rc = dss_unlock(dss, DSS_MEDIA, &medium, 1, false);
        if (rc == -ENOLCK || rc == -EACCES)
            pho_warn("locate: failed to unlock reserved lock for ('%s', '%s'). "
                     "Lock was modified by someone else: %s",
                     medium.rsc.id.library, medium.rsc.id.name,
                     strerror(-rc));
        else if (rc)
            pho_warn("locate: failed to unlock reserved lock for ('%s', '%s') "
                     ": %s",
                     medium.rsc.id.library, medium.rsc.id.name,
                     strerror(-rc));
    }
}

/* XXX we do not check that the extents that are locked have a compatible device
 * on the selected host.
 */
static int lock_extents(struct dss_handle *dss,
                        GHashTable *hosts,
                        GPtrArray *extents,
                        int *nb_locks_per_split,
                        const char *hostname,
                        size_t n_data_extents,
                        size_t n_parity_extents)
{
    size_t extents_per_split = n_data_extents + n_parity_extents;
    struct pho_id *medium_locked[extents->len];
    struct host_capabilities *host;
    int nb_new_locks = 0;
    int i, j;

    host = g_hash_table_lookup(hosts, hostname);
    assert(host);

    for (i = 0; i < extents->len / extents_per_split; i++) {
        if (nb_locks_per_split[i] >= n_data_extents)
            continue;

        for (j = 0; j < extents_per_split; j++) {
            size_t ext_index = i * extents_per_split + j;
            struct extent_location *loc;
            struct media_info medium;
            int rc;

            loc = extents->pdata[ext_index];
            if (!loc)
                continue;

            medium.rsc.id = loc->medium->rsc.id;

            /* Check the host has a compatible device to read the extent. */
            if (!has_compatible_devices(host->devices, loc)) {
                pho_warn("Host %s has no device able to read medium %s in "
                         "library %s", hostname, loc->medium->rsc.id.name,
                         loc->medium->rsc.id.library);
                continue;
            }
            rc = dss_lock_hostname(dss, DSS_MEDIA, &medium, 1, hostname);
            if (rc == -EEXIST) {
                /* somebody else took the lock */
                continue;
            } else if (rc) {
                pho_warn("locate: failed to reserve lock on medium ('%s', "
                         "'%s') for host '%s': %s",
                         medium.rsc.id.library, medium.rsc.id.name, hostname,
                         strerror(-rc));
                continue;
            }

            nb_new_locks++;
            nb_locks_per_split[i]++;
            /* used for later cleanup in case of error */
            medium_locked[ext_index] = &loc->medium->rsc.id;
            if (nb_locks_per_split[i] >= n_data_extents)
                break;
        }

        if (nb_locks_per_split[i] < n_data_extents) {
            cleanup_locks(dss, medium_locked, extents->len);
            LOG_RETURN(-EAGAIN, "locate: not enough locks where taken");
        }
    }

    return nb_new_locks;
}

static int reserve_locks(struct dss_handle *dss, GHashTable *hosts,
                         GPtrArray *extents, const char *hostname,
                         size_t n_data_extents, size_t n_parity_extents)
{
    size_t extents_per_split = n_data_extents + n_parity_extents;
    size_t max_nb_extents = n_data_extents * extents->len / extents_per_split;
    int nb_locks_per_split[max_nb_extents];
    int i, j;

    memset(nb_locks_per_split, 0, sizeof(nb_locks_per_split));
    for (i = 0; i < extents->len / extents_per_split; i++) {
        for (j = 0; j < extents_per_split; j++) {
            size_t ext_index = i * extents_per_split + j;
            struct extent_location *loc;

            loc = extents->pdata[ext_index];
            if (loc && loc->hostname && !strcmp(loc->hostname, hostname)) {
                /* already locked by the proper host */

                int rc;

                rc = dss_lock_refresh(dss, DSS_MEDIA, loc->medium, 1, true);
                if (rc)
                    pho_debug("locate: failed to update locate timestamp for"
                              FMT_PHO_ID, PHO_ID(loc->medium->rsc.id));
                nb_locks_per_split[i]++;
            }
        }
    }

    return lock_extents(dss, hosts, extents, nb_locks_per_split, hostname,
                      n_data_extents, n_parity_extents);
}

int raid_locate(struct dss_handle *dss, struct layout_info *layout,
                size_t n_data_extents, size_t n_parity_extents,
                const char *focus_host, char **hostname,
                int *nb_new_locks)
{
    struct dev_info *devices;
    enum rsc_family family;
    GPtrArray *extents; /* struct extent_location */
    GHashTable *hosts; /* key: hostname, value: struct host_capabilities */
    int n_devices;
    int rc;

    if (!focus_host) {
        focus_host = get_hostname();
        if (!focus_host)
            LOG_RETURN(-EADDRNOTAVAIL, "Unable to get self hostname");
    }

    family = layout->extents[0].media.family;
    rc = dss_get_usable_devices(dss, family, NULL, &devices, &n_devices);
    if (rc)
        return rc;

    hosts = setup_available_hosts(devices, n_devices, layout->ext_count,
                                  focus_host);
    extents = setup_extent_location(layout);

    rc = locate_all_extents(dss, layout, extents,
                            n_data_extents + n_parity_extents);
    if (rc)
        GOTO(clean, rc);

    set_host_extent_accessibility(hosts, extents);
    filter_inaccessible_extents(hosts, extents);
    if (unaccessible_split(extents, n_data_extents + n_parity_extents))
        GOTO(clean, rc = -EAGAIN);

    filter_host_with_partial_access(hosts, extents, n_data_extents,
                                    n_parity_extents);
    if (g_hash_table_size(hosts) == 0)
        /* No host has access to this object */
        GOTO(clean, rc = -EAGAIN);

    *hostname = find_best_host(hosts, extents, n_data_extents, n_parity_extents,
                               focus_host);
    if (!*hostname)
        GOTO(clean, rc = -EAGAIN);

    *nb_new_locks = reserve_locks(dss, hosts, extents, *hostname,
                                 n_data_extents, n_parity_extents);
    if (*nb_new_locks < 0)
        rc = *nb_new_locks;

clean:
    g_ptr_array_free(extents, true);
    g_hash_table_destroy(hosts);
    dss_res_free(devices, n_devices);

    return rc;
}
