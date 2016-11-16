/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
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
