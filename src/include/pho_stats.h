/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2025 CEA/DAM.
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
 * \brief   Phobos metrology framework.
 */
#ifndef _PHO_STATS_H
#define _PHO_STATS_H

#include <stdint.h>
#include <jansson.h>

struct pho_stat;

enum pho_stat_type {
    PHO_STAT_COUNTER, /**< Stat is a Counter (cumulative). */
    PHO_STAT_GAUGE    /**< Stat is a Gauge (variable). */
};

/**
 * Create and register a new metric.
 * \param[in]   type        type of metric: counter or gauge
 * \param[in]   namespace   designate the group/layer of the metric
 * \param[in]   name        name of the metric
 * \param[in]   tags        comma-separated list of key=value
 *
 * \return      Pointer to the created stat structure.
 */
struct pho_stat *pho_stat_create(enum pho_stat_type type,
                                 const char *namespace,
                                 const char *name,
                                 const char *tags);

/**
 * Unregister, release a stat and nil its pointer.
 *
 * \param[in] Pointer to the stat pointer to be unregistered.
 */
void pho_stat_destroy(struct pho_stat **stat);

/**
 * Increment a metric (counter or gauge).
 *
 * \param[in] stat  Stat created by pho_stat_create().
 *                  Can be either a counter or gauge.
 * \param[in] val   Positive integer
 */
void pho_stat_incr(struct pho_stat *stat, uint64_t val);

/**
 * Set the value of a gauge.
 *
 * \param[in] stat  Gauge created by pho_stat_create()
 * \param[in] val   Positive integer
 */
void pho_stat_set(struct pho_stat *stat, uint64_t val);

/**
 *  Get the value from a stat.
 *  \retval UINT64_MAX on error.
 */
uint64_t pho_stat_get(struct pho_stat *stat);

/**
 * Initialize a stats iterator with optional namespace, name and tags.
 * \param[in] namespace Filter on the namespace name. Optional, can be NULL.
 * \param[in] name      Filter on the metric name. Optional, can be NULL.
 * \param[in] tag_set   A list of tag of coma-separated filters in the format
 *                      "tag=value,tag=value,...".
 * \return  An iterator on success, NULL on error.
 */
struct pho_stat_iter *pho_stat_iter_init(const char *namespace,
                                         const char *name,
                                         const char *tag_set);
/**
 * Get the next metric from the iterator.
 */
struct pho_stat *pho_stat_iter_next(struct pho_stat_iter *iter);

/**
 * Close an iterator
 */
void pho_stat_iter_close(struct pho_stat_iter *iter);

/**
 * Dumps stats as JSON. Apply the same filters as pho_stat_iter_init().
 *
 * \param[in] namespace Filter on the namespace name. Optional, can be NULL.
 * \param[in] name      Filter on the metric name. Optional, can be NULL.
 * \param[in] tag_set   A list of tag of coma-separated filters in the format
 *                      "tag=value,tag=value,...".
 * \return  A newly allocated json on success, NULL on error.
 */
json_t *pho_stats_dump_json(const char *ns_filter, const char *name_filter,
                            const char *tag_set);

#endif
