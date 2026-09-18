/*
 *  All rights reserved (c) 2014-2026 CEA/DAM.
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
 * \brief  Phobos admin header for media rebuild
 */

#ifndef _PHO_ADMIN_REBUILD_H
#define _PHO_ADMIN_REBUILD_H

#include "phobos_admin.h"
#include "pho_types.h"
#include "pho_type_utils.h"

#include <glib.h>

struct rebuild_extent {
    struct layout_info *layout;
    struct extent *extent_to_rebuild; /**< Extent to rebuild */
    GPtrArray *avail_extents;         /**< Extents available to rebuild from */
    int n_data_extents;               /**< Number of extents needed to rebuild
                                        */
    struct pho_id *media_to_read;     /**< Selected media to read from */
};

struct group_key {
    int n_media;
    struct pho_id media[];
};

struct rebuild_scheduler {
    struct group_key *curr_media; /**< Currents media being used */
    GPtrArray *curr_extents;      /**< Extents being rebuilt */
    GHashTable *pending_groups;   /**< Remaining groups to rebuild */
    GArray *extents_to_rebuild;   /**< All the extents to rebuild */
    GHashTable *frequency;        /**< Frequency of occurrence of each media */
};

static inline guint group_key_hash(gconstpointer data)
{
    const struct group_key *key = data;
    guint hash = key->n_media;

    for (guint i = 0; i < key->n_media; i++)
        hash ^= g_pho_id_hash(&key->media[i]);

    return hash;
}

static inline gboolean group_key_equal(gconstpointer a, gconstpointer b)
{
    const struct group_key *ka = a;
    const struct group_key *kb = b;

    if (ka->n_media != kb->n_media)
        return false;

    for (guint i = 0; i < ka->n_media; i++)
        if (!pho_id_equal(&ka->media[i], &kb->media[i]))
            return false;

    return true;
}

/**
 * Rebuild all the extents of a copy on \p med.
 *
 * @param[in] item    the item to rebuild
 *
 * @return 0 on success, negated errno on failure
 */
int rebuild_copy(struct rebuild_extent *rebuild_extent);

/**
 * Retrieve the extents to rebuild for each copy and computes the frequency
 * of occurrence of all the media we can use to rebuild that extent.
 *
 * @param[in]     med       The medium to rebuild
 * @param[in]     layouts   All the layouts on \p med
 * @param[in]     n_layout  The number of layouts
 * @param[in/out] sched     Rebuild scheduler
 * @param[in/out] skipped   Boolean if an extents to rebuild has been skipped
 *
 * @return 0 on success, negated errno on failure
 */
int collect_rebuild_extents_and_frequency(struct pho_id *med,
                                          struct layout_info *layouts,
                                          int n_layout,
                                          struct rebuild_scheduler *sched,
                                          bool *skipped);

/**
 * Group rebuild extents by the media needed to rebuild them.
 *
 * If the number of available extents is equal the n_data_extents, all
 * available media are used as the group key. Otherwise, we choose the more
 * frequent media.
 *
 * Groups are appended to \p groups.
 *
 * @param[in/out] sched  rebuild scheduler containing collected items
 */
void group_extents(struct rebuild_scheduler *sched);

/**
 * Sort each pending rebuild group by extent creation time.
 *
 * The sort is done inside every pending group so older extents
 * are rebuilt before newer extents within that group.
 *
 * @param[in] groups  pending groups
 */
void sort_extents_by_creation_time(struct rebuild_scheduler *sched);

/**
 * Allocate and initialize a rebuild scheduler.
 *
 * @return a new scheduler to free with rebuild_scheduler_free()
 */
struct rebuild_scheduler *rebuild_scheduler_new(void);

/**
 * Free a rebuild scheduler and all groups/items owned by it.
 *
 * @param[in] sched  scheduler returned by rebuild_scheduler_new()
 */
void rebuild_scheduler_free(struct rebuild_scheduler *sched);

/**
 * Select the next group of rebuild items to process.
 *
 * The scheduler tries to minimize media changes, keep useful media mounted for
 * future groups, and restart from a component edge when no current media can be
 * reused. The returned array remains owned by \p sched and is valid until the
 * next call to rebuild_scheduler_next() or rebuild_scheduler_free().
 *
 * @param[in/out] sched  rebuild scheduler
 * @param[out]    out    selected group items when true is returned
 *
 * @return true when a group was selected, false when no pending group remains
 */
bool rebuild_scheduler_next(struct rebuild_scheduler *sched, GPtrArray **out);

#endif
