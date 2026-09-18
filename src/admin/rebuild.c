/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
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
 * \brief  Phobos admin source file for media rebuild
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "phobos_store.h"

#include "pho_common.h"
#include "pho_type_utils.h"
#include "pho_types.h"
#include "pho_layout.h"

#include "rebuild.h"

int rebuild_copy(struct rebuild_extent *rebuild_extent)
{
    struct pho_xfer_target target = {0};
    struct pho_xfer_desc xfer = {0};
    int *extents_idx;
    int rc = 0;

    target.xt_objid = rebuild_extent->layout->oid;
    target.xt_objuuid = rebuild_extent->layout->uuid;
    target.xt_version = rebuild_extent->layout->version;

    xfer.xd_ntargets = 1;
    xfer.xd_targets = &target;
    xfer.xd_params.rebuild.put.copy_name = rebuild_extent->layout->copy_name;
    xfer.xd_params.rebuild.get.copy_name = rebuild_extent->layout->copy_name;
    xfer.xd_params.rebuild.get.scope = DSS_OBJ_ALL;

    xfer.xd_params.rebuild.n_extents = 1;

    extents_idx = xmalloc(sizeof(*extents_idx));
    extents_idx[0] = rebuild_extent->extent_to_rebuild->layout_idx;
    xfer.xd_params.rebuild.extents_idx = extents_idx;

    if (rebuild_extent->media_to_read)
        xfer.xd_params.rebuild.media_read =
            pho_id_dup(rebuild_extent->media_to_read);

    rc = phobos_copy_rebuild(&xfer, 1);
    if (rc)
        pho_error(rc, "Failed to rebuild copy");

    pho_xfer_desc_clean(&xfer);

    return rc;
}

static int rebuild_extent_init(struct rebuild_extent *rebuild_extent,
                               struct layout_info *layout,
                               struct extent *extent_to_rebuild)
{
    int n_parity_extents;
    int rc;

    memset(rebuild_extent, 0, sizeof(*rebuild_extent));

    rebuild_extent->layout = layout;
    rebuild_extent->extent_to_rebuild = extent_to_rebuild;

    rc = layout_get_replica_info(layout, &rebuild_extent->n_data_extents,
                                 &n_parity_extents);
    if (rc)
        return rc;

    rebuild_extent->avail_extents =
        layout_get_extents_to_rebuild_from(layout, extent_to_rebuild);

    if (rebuild_extent->avail_extents == NULL)
        return -EINVAL;

    return 0;
}

static void compute_frequency(GPtrArray *avail_extents, GHashTable *frequency)
{
    int freq;

    for (int i = 0; i < avail_extents->len; i++) {
            struct extent *extent =
                (struct extent *) g_ptr_array_index(avail_extents, i);

            freq = GPOINTER_TO_INT(g_hash_table_lookup(frequency,
                                                       &extent->media));
            g_hash_table_replace(frequency, pho_id_dup(&extent->media),
                                 GINT_TO_POINTER(freq + 1));
    }
}

int collect_rebuild_extents_and_frequency(struct pho_id *med,
                                          struct layout_info *layouts,
                                          int n_layout,
                                          struct rebuild_scheduler *sched,
                                          bool *skipped)
{
    int rc;

    for (int i = 0; i < n_layout; i++) {
        struct layout_info *layout = &layouts[i];

        for (int j = 0; j < layout->ext_count; j++) {
            struct extent *extent = &layout->extents[j];
            struct rebuild_extent rebuild_extent;

            if (!pho_id_equal(med, &extent->media))
                continue;

            rc = rebuild_extent_init(&rebuild_extent, layout, extent);
            if (rc)
                return rc;

            if (rebuild_extent.avail_extents->len <
                rebuild_extent.n_data_extents) {
                *skipped = true;
                pho_error(-ENODATA, "Cannot rebuild extent of object '%s' on "
                          FMT_PHO_ID": not enough extents to read from, "
                          "skipped", layout->oid, PHO_ID(*med));
                g_ptr_array_free(rebuild_extent.avail_extents, true);
                continue;
            }

            compute_frequency(rebuild_extent.avail_extents, sched->frequency);
            g_array_append_val(sched->extents_to_rebuild, rebuild_extent);
        }
    }

    return 0;
}

static struct extent *choose_media_with_max_frequency(
                                          GHashTable *frequency,
                                          struct rebuild_extent *rebuild_extent)
{
    struct extent *best = NULL;
    int max_freq = 0;

    for (int i = 0; i < rebuild_extent->avail_extents->len; i++) {
        struct extent *extent =
            g_ptr_array_index(rebuild_extent->avail_extents, i);
        struct pho_id *media = &extent->media;
        int freq;

        freq = GPOINTER_TO_INT(g_hash_table_lookup(frequency, media));
        if (freq > max_freq) {
            max_freq = freq;
            best = extent;
        }
    }

    return best;
}

/* The media are always from the same family and library */
static int pho_id_name_cmp(const void *a, const void *b)
{
    const struct pho_id *id_a = a;
    const struct pho_id *id_b = b;

    return strcmp(id_a->name, id_b->name);
}

static struct group_key *new_group_key(struct extent **extents,
                                       int n_extents)
{
    struct group_key *key;

    key = xmalloc(sizeof(*key) + n_extents * sizeof(*key->media));
    key->n_media = n_extents;

    for (int i = 0; i < n_extents; i++)
        pho_id_copy(&key->media[i], &extents[i]->media);

    qsort(key->media, key->n_media, sizeof(*key->media), pho_id_name_cmp);

    return key;
}

static void append_extent_to_group(GHashTable *groups, struct group_key *key,
                                   struct rebuild_extent *rebuild_extent)
{
    GPtrArray *group_rebuild_extent;

    group_rebuild_extent = g_hash_table_lookup(groups, key);
    if (group_rebuild_extent == NULL) {
        group_rebuild_extent = g_ptr_array_new();
        g_hash_table_insert(groups, key, group_rebuild_extent);
    } else {
        free(key);
    }

    g_ptr_array_add(group_rebuild_extent, rebuild_extent);
}

void group_extents(struct rebuild_scheduler *sched)
{
    for (int i = 0; i < sched->extents_to_rebuild->len; i++) {
        struct rebuild_extent *rebuild_extent =
            &g_array_index(sched->extents_to_rebuild, struct rebuild_extent, i);
        struct extent **extents = NULL;
        struct group_key *key;
        int n_extents;

        /* If the number of extents required for the rebuild is equal to the
         * number of available extents, then we take them all. This is always
         * the case for RAID4, and sometimes for RAID1 when only one replica is
         * available. Otherwise, in the other RAID1 cases, we choose the extent
         * that is present on the most frequently used medium among all of them.
         *
         * XXX: This will need to be modified if new layouts are added in the
         *      future.
         */
        if (rebuild_extent->n_data_extents ==
            rebuild_extent->avail_extents->len) {
            extents = (struct extent **) rebuild_extent->avail_extents->pdata;
            n_extents = rebuild_extent->avail_extents->len;
        } else {
            struct extent *extent;

            extent = choose_media_with_max_frequency(sched->frequency,
                                                     rebuild_extent);
            n_extents = 1;
            extents = &extent;
            /* XXX: For now, we are only in this case for RAID1, so we only
             * need one read medium.
             */
            rebuild_extent->media_to_read = &extent->media;
        }

        key = new_group_key(extents, n_extents);

        append_extent_to_group(sched->pending_groups, key, rebuild_extent);
    }
}

static gint cmp_timeval(gconstpointer _a, gconstpointer _b)
{
    const struct rebuild_extent *a = *((struct rebuild_extent **) _a);
    const struct rebuild_extent *b = *((struct rebuild_extent **) _b);

    if (a->extent_to_rebuild->creation_time.tv_sec <
        b->extent_to_rebuild->creation_time.tv_sec)
        return -1;

    if (a->extent_to_rebuild->creation_time.tv_sec >
        b->extent_to_rebuild->creation_time.tv_sec)
        return 1;

    if (a->extent_to_rebuild->creation_time.tv_usec <
        b->extent_to_rebuild->creation_time.tv_usec)
        return -1;

    if (a->extent_to_rebuild->creation_time.tv_usec >
        b->extent_to_rebuild->creation_time.tv_usec)
        return 1;

    return 0;
}

void sort_extents_by_creation_time(struct rebuild_scheduler *sched)
{
    GHashTableIter iter;
    gpointer key;
    gpointer value;

    g_hash_table_iter_init(&iter, sched->pending_groups);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        GPtrArray *array = value;

        g_ptr_array_sort(array, cmp_timeval);
    }
}

static bool key_has_media(struct group_key *key, struct pho_id *medium)
{
    for (int i = 0; i < key->n_media; i++) {
        if (pho_id_equal(&key->media[i], medium))
            return true;
    }

    return false;
}

static int count_required_already_mounted(struct rebuild_scheduler *sched,
                                          struct group_key *candidate)
{
    int count = 0;

    if (sched->curr_media == NULL)
        return count;

    for (int i = 0; i < candidate->n_media; i++) {
        if (key_has_media(sched->curr_media, &candidate->media[i]))
            count++;
    }

    return count;
}

static int candidate_cost(struct rebuild_scheduler *sched,
                          struct group_key *candidate)
{
    int already_mounted = count_required_already_mounted(sched, candidate);

    if (sched->curr_media == NULL)
        return candidate->n_media;

    /* The transition cost counts both unmounts and mounts. A reused medium
     * appears once in the current group and once in the candidate group, so
     * remove both operations from the cost.
     */
    return sched->curr_media->n_media + candidate->n_media -
           2 * already_mounted;
}

/* Number of pending groups that still need this tape. */
static int future_score_candidate(struct rebuild_scheduler *sched,
                                  struct group_key *candidate)
{
    GHashTableIter iter;
    gpointer value_ptr;
    gpointer key_ptr;
    int score = 0;

    g_hash_table_iter_init(&iter, sched->pending_groups);

    while (g_hash_table_iter_next(&iter, &key_ptr, &value_ptr)) {
        struct group_key *key = key_ptr;

        /* Skip the potential candidate */
        if (key == candidate)
            continue;

        for (int i = 0; i < candidate->n_media; i++) {
            if (key_has_media(key, &candidate->media[i]))
                score++;
        }
    }

    return score;
}

static int candidate_weight(GPtrArray *items)
{
    return items->len;
}

static GHashTable *build_media_usage(GHashTable *pending)
{
    GHashTable *usage = g_hash_table_new(g_pho_id_hash, g_pho_id_equal);
    GHashTableIter iter;
    gpointer value_ptr;
    gpointer key_ptr;

    g_hash_table_iter_init(&iter, pending);

    while (g_hash_table_iter_next(&iter, &key_ptr, &value_ptr)) {
        struct group_key *key = key_ptr;

        for (int i = 0; i < key->n_media; i++) {
            int old = GPOINTER_TO_INT(g_hash_table_lookup(usage,
                                                          &key->media[i]));
            g_hash_table_replace(usage, &key->media[i],
                                 GINT_TO_POINTER(old + 1));
        }
    }

    return usage;
}

/**
 * Connectivity score of a group among pending groups.
 *
 * Groups with a low overlap_degree are usually located at the edge of a
 * connected component. Groups with a high overlap_degree are usually more
 * central.
 *
 * We use this value when starting a new component: starting from an edge
 * avoids starting in the middle of a chain and then having to come back later.
 */
static int group_overlap_degree(GHashTable *usage, struct group_key *key)
{
    int degree = 0;

    for (int i = 0; i < key->n_media; i++) {
        int count = GPOINTER_TO_INT(g_hash_table_lookup(usage, &key->media[i]));

        if (count > 0)
            /* Remove the media from key */
            degree += count - 1;
    }

    return degree;
}

struct group_candidate {
    struct group_key *key;
    GPtrArray *items;
    int cost;
    int future_score;
    int degree;
    int weight;
};

static void free_current_group(struct rebuild_scheduler *sched)
{
    free(sched->curr_media);
    if (sched->curr_extents)
        g_ptr_array_free(sched->curr_extents, true);

    sched->curr_media = NULL;
    sched->curr_extents = NULL;
}

static void select_group(struct rebuild_scheduler *sched,
                         struct group_key *key, GPtrArray *extents_to_rebuild,
                         GPtrArray **out)
{
    g_hash_table_steal(sched->pending_groups, key);
    free_current_group(sched);

    sched->curr_media = key;
    sched->curr_extents = extents_to_rebuild;
    *out = extents_to_rebuild;
}

static bool has_non_isolated_group(struct rebuild_scheduler *sched,
                                   GHashTable *usage)
{
    GHashTableIter iter;
    gpointer value_ptr;
    gpointer key_ptr;

    /* Check if there is atleast one group linked with the others */
    g_hash_table_iter_init(&iter, sched->pending_groups);

    while (g_hash_table_iter_next(&iter, &key_ptr, &value_ptr)) {
        struct group_key *key = key_ptr;

        if (group_overlap_degree(usage, key) > 0)
            return true;
    }

    return false;
}

static void init_candidate(struct rebuild_scheduler *sched, GHashTable *usage,
                           struct group_candidate *candidate,
                           struct group_key *key, GPtrArray *items)
{
    candidate->key = key;
    candidate->items = items;
    candidate->cost = candidate_cost(sched, key);
    candidate->future_score = future_score_candidate(sched, key);
    candidate->degree = group_overlap_degree(usage, key);
    candidate->weight = candidate_weight(items);
}

static bool better_start_group(const struct group_candidate *candidate,
                               const struct group_candidate *best)
{
    /* A candidate is better when:
     * - no group has been selected yet
     * - it has a lower degree, meaning fewer pending neighbours and therefore
     *   a better edge from which to start a connected component
     * - it has the same degree but contains more rebuild items, so equal
     *   scheduling choices process the largest group first.
     */
    return best->key == NULL ||
           candidate->degree < best->degree ||
           (candidate->degree == best->degree &&
            candidate->weight > best->weight);
}

static bool better_next_group(const struct group_candidate *candidate,
                              const struct group_candidate *best)
{
    /* A candidate is better when:
     * - no group has been selected yet;
     * - it has a lower immediate transition cost
     * - it has the same cost but its media appear in more pending groups
     * - it has the same cost and future score but a lower degree, meaning it is
     *   closer to an edge
     * - all scheduling metrics are tied but it contains more rebuild items.
     */
    return best->key == NULL ||
           candidate->cost < best->cost ||
           (candidate->cost == best->cost &&
            candidate->future_score > best->future_score) ||
           (candidate->cost == best->cost &&
            candidate->future_score == best->future_score &&
            candidate->degree < best->degree) ||
           (candidate->cost == best->cost &&
            candidate->future_score == best->future_score &&
            candidate->degree == best->degree &&
            candidate->weight > best->weight);
}

static struct group_candidate find_best_group(struct rebuild_scheduler *sched,
                                              bool start_group)
{
    struct group_candidate best = {0};
    bool has_non_isolated;
    GHashTableIter iter;
    gpointer value_ptr;
    GHashTable *usage;
    gpointer key_ptr;

    usage = build_media_usage(sched->pending_groups);
    has_non_isolated = start_group && has_non_isolated_group(sched, usage);

    g_hash_table_iter_init(&iter, sched->pending_groups);

    while (g_hash_table_iter_next(&iter, &key_ptr, &value_ptr)) {
        struct group_candidate candidate;
        struct group_key *key = key_ptr;
        GPtrArray *items = value_ptr;

        init_candidate(sched, usage, &candidate, key, items);

        /* If a linked group exist, don't start by an isolated group */
        if (has_non_isolated && candidate.degree == 0)
            continue;

        if ((start_group && better_start_group(&candidate, &best)) ||
            (!start_group && better_next_group(&candidate, &best)))
            best = candidate;
    }

    g_hash_table_destroy(usage);

    return best;
}

static bool choose_start_group(struct rebuild_scheduler *sched, GPtrArray **out)
{
    struct group_candidate best = find_best_group(sched, true);

    if (best.key == NULL)
        return false;

    select_group(sched, best.key, best.items, out);

    return true;
}

static bool choose_next_group(struct rebuild_scheduler *sched,
                              GPtrArray **out)
{
    struct group_candidate best = find_best_group(sched, false);

    if (best.key == NULL)
        return false;

    if (sched->curr_media &&
        count_required_already_mounted(sched, best.key) == 0)
        return choose_start_group(sched, out);

    select_group(sched, best.key, best.items, out);

    return true;
}

bool rebuild_scheduler_next(struct rebuild_scheduler *sched, GPtrArray **out)
{
    if (g_hash_table_size(sched->pending_groups) == 0)
        return false;

    if (!sched->curr_media)
        return choose_start_group(sched, out);
    else
        return choose_next_group(sched, out);
}

static void rebuild_extent_clear(void *data)
{
    struct rebuild_extent *rebuild_extent = data;

    g_ptr_array_free(rebuild_extent->avail_extents, true);
}

static void _g_ptr_array_free(void *data)
{
    GPtrArray *array = data;

    g_ptr_array_free(array, true);
}

struct rebuild_scheduler *rebuild_scheduler_new(void)
{
    struct rebuild_scheduler *sched;

    sched = xmalloc(sizeof(*sched));

    sched->frequency = g_hash_table_new_full(g_pho_id_hash, g_pho_id_equal,
                                             free, NULL);
    sched->extents_to_rebuild = g_array_new(FALSE, FALSE,
                                            sizeof(struct rebuild_extent));
    g_array_set_clear_func(sched->extents_to_rebuild, rebuild_extent_clear);

    sched->pending_groups = g_hash_table_new_full(group_key_hash,
                                                  group_key_equal,
                                                  free, _g_ptr_array_free);
    sched->curr_media = NULL;
    sched->curr_extents = NULL;

    return sched;
}

void rebuild_scheduler_free(struct rebuild_scheduler *sched)
{
    g_hash_table_destroy(sched->frequency);
    g_hash_table_destroy(sched->pending_groups);
    free_current_group(sched);
    g_array_free(sched->extents_to_rebuild, true);

    free(sched);
}
