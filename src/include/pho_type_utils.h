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
int storage_info_to_json(const struct layout_descr *layout,
                         const struct extent *extent,
                         GString *str, int json_flags);

/** parse "phobos drive query" JSON output */
int device_state_from_json(const char *str, struct dev_state *dev_st);

/** convert a piece of layout to an extent tag
 * @param tag this buffer must be at least PHO_LAYOUT_TAG_MAX
 */
int layout2tag(const struct layout_descr *layout,
               unsigned int layout_idx, char *tag);

#endif
