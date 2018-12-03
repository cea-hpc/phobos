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
 * \brief  Phobos Local Resource Scheduler (LRS)
 */
#ifndef _PHO_LRS_H
#define _PHO_LRS_H

#include "pho_types.h"


struct dss_handle;
struct device_descr;

enum lrs_operation {
    LRS_OP_NONE = 0,
    LRS_OP_READ,
    LRS_OP_WRITE,
    LRS_OP_FORMAT,
};

struct lrs_intent {
    struct dss_handle   *li_dss;
    struct dev_descr    *li_device;
    struct pho_ext_loc   li_location;
};

/**
 * Query to write a given amount of data with a given layout.
 * (future: several extents if the file is splitted, striped...)
 *
 * @param(in)     dss     Initialized DSS handle.
 * @param(in,out) intent  The intent descriptor to fill.
 * @param(in)     tags    Tags used to select a media to write on, the selected
 *                        media must have the specified tags.
 * @return 0 on success, -1 * posix error code on failure
 */
int lrs_write_prepare(struct dss_handle *dss, struct lrs_intent *intent,
                      const struct tags *tags);

/**
 * Query to read from a given set of media.
 * (future: several locations if the file is splitted, striped...
 *  Moreover, the object may have several locations and layouts if it is
 *  duplicated).
 *
 * @param(in)     dss     Initialized DSS handle.
 * @param(in,out) intent  The intent descriptor to fill.
 * @return 0 on success, -1 * posix error code on failure
 */
int lrs_read_prepare(struct dss_handle *dss, struct lrs_intent *intent);

/**
 * Notify LRS of the completion of a write, let it flush data and update media
 * statistics.
 *
 * @param(in)   intent     The intent descriptor of the completed I/O.
 * @param(in)   fragments  The number of successfully written fragments.
 * @param(in)   err_code   An optional error code to interpret failures.
 * @return 0 on success, -1 * posix error code on failure.
 */
int lrs_io_complete(struct lrs_intent *intent, int fragments, int err_code);

/**
 * Release resources allocated by a lrs_{r/w}_prepare() call.
 * @param(in)   intent  the intent descriptor filled by lrs_{r,w}_prepare.
 * @return 0 on success, -1 * posix error code on failure
 */
int lrs_resource_release(struct lrs_intent *intent);

/**
 * Identify medium-global error codes.
 * Typically useful to trigger custom procedures when a medium becomes
 * read-only.
 */
static inline bool is_media_global_error(int errcode)
{
    return errcode == -ENOSPC || errcode == -EROFS || errcode == -EDQUOT;
}

/**
 * Load and format a media to the given fs type.
 *
 * @param(in)   dss     Initialized DSS handle.
 * @param(in)   id      Media ID for the media to format.
 * @param(in)   fs      Filesystem type (only PHO_FS_LTFS for now).
 * @param(in)   unlock  Unlock tape if successfully formated.
 * @return 0 on success, negative error code on failure.
 */
int lrs_format(struct dss_handle *dss, const struct media_id *id,
               enum fs_type fs, bool unlock);
#endif
