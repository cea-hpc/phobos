/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Handling of internal types.
 */
#ifndef _PHO_TYPE_UTILS_H
#define _PHO_TYPE_UTILS_H

#include "pho_types.h"

/** dump basic storage information as JSON to be attached to data objects. */
int storage_info_to_json(const struct layout_info *layout,
                         GString *str, int json_flags);

/** parse "phobos drive query" JSON output */
int device_state_from_json(const char *str, struct dev_state *dev_st);

/** convert a piece of layout to an extent tag
 * @param tag this buffer must be at least PHO_LAYOUT_TAG_MAX
 */
int layout2tag(const struct layout_info *layout,
               unsigned int layout_idx, char *tag);

/** duplicate a dev_info structure */
struct dev_info *dev_info_dup(const struct dev_info *dev);

/** free a dev_info structure */
void dev_info_free(struct dev_info *dev);

/** duplicate a media_info structure */
struct media_info *media_info_dup(const struct media_info *mda);

/** free a media_info structure */
void media_info_free(struct media_info *mda);

#endif
