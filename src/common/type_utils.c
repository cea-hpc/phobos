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
 * \brief  Handling of layout and extent structures.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_type_utils.h"
#include "pho_common.h"
#include <errno.h>
#include <jansson.h>
#include <assert.h>


static inline char *strdup_safe(const char *str)
{
    if (str == NULL)
        return NULL;

    return strdup(str);
}

struct dev_info *dev_info_dup(const struct dev_info *dev)
{
    struct dev_info *dev_out;

    dev_out = malloc(sizeof(*dev_out));
    if (!dev_out)
        return NULL;

    dev_out->family = dev->family;
    dev_out->model = strdup_safe(dev->model);
    dev_out->path = strdup_safe(dev->path);
    dev_out->host = strdup_safe(dev->host);
    dev_out->serial = strdup_safe(dev->serial);
    dev_out->adm_status = dev->adm_status;

    return dev_out;
}

void dev_info_free(struct dev_info *dev)
{
    if (!dev)
        return;
    free(dev->model);
    free(dev->path);
    free(dev->host);
    free(dev->serial);
    free(dev);
}

struct media_info *media_info_dup(const struct media_info *media)
{
    struct media_info *media_out;

    media_out = malloc(sizeof(*media_out));
    if (!media_out)
        return NULL;

    media_out->id = media->id;
    media_out->addr_type = media->addr_type;
    media_out->model = strdup_safe(media->model);
    media_out->adm_status = media->adm_status;
    media_out->fs = media->fs;
    media_out->stats = media->stats;

    return media_out;
}

void media_info_free(struct media_info *mda)
{
    if (!mda)
        return;
    free(mda->model);
    free(mda);
}
