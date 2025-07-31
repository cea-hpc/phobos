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
 * \brief  Management of Phobos metrics
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "pho_stats.h"
#include "pho_common.h"

struct pho_stat {
    enum pho_stat_type type;
    char *namespace;
    char *name;
    char *tags;

    _Atomic uint_least64_t value;
};

void pho_stat_free(struct pho_stat *stat)
{
    if (!stat)
        return;

    free(stat->namespace);
    free(stat->name);
    free(stat->tags);
    free(stat);
}

/** Allocate and initialize a new metric */
struct pho_stat *pho_stat_create(enum pho_stat_type type,
                                 const char *namespace,
                                 const char *name,
                                 const char *tags)
{
    struct pho_stat *stat;

    assert(namespace != NULL && name != NULL);

    stat = xmalloc(sizeof(struct pho_stat));
    stat->type = type;
    stat->namespace = xstrdup_safe(namespace);
    stat->name = xstrdup_safe(name);
    stat->tags = xstrdup_safe(tags);

    atomic_init(&stat->value, 0);

    /* TODO: register the metric */

    return stat;
}

/** Increments an integer type metric. */
void pho_stat_incr(struct pho_stat *stat, uint64_t val)
{
    atomic_fetch_add(&stat->value, val);
}

/** Sets the value of an integer type metric. */
void pho_stat_set(struct pho_stat *stat, uint64_t val)
{
    /* can't set a counter */
    assert(stat->type != PHO_STAT_COUNTER);

    atomic_store(&stat->value, val);
}

/**
 *  Get the value from a stat
 */
uint64_t pho_stat_get(struct pho_stat *stat)
{
    return atomic_load(&stat->value);
}
