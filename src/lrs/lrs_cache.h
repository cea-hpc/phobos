/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2024 CEA/DAM.
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
 * \brief LRS Medium Cache prototypes
 */
#ifndef _PHO_LRS_CACHE_H
#define _PHO_LRS_CACHE_H

#include "pho_cache.h"
#include "pho_types.h"

int lrs_cache_setup(enum rsc_family family);

void lrs_cache_cleanup(enum rsc_family family);

struct media_info *lrs_medium_acquire(const struct pho_id *id);

void lrs_medium_release(struct media_info *medium);

struct media_info *lrs_medium_update(struct pho_id *id);

struct media_info *lrs_medium_insert(struct media_info *medium);

void lrs_media_cache_dump(enum rsc_family family);

#endif
