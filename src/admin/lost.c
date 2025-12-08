/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2025 CEA/DAM.
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
 * \brief  Phobos admin source file for removal of lost media
 */

#include "pho_types.h"

#include "import.h"
#include "lost.h"
#include "utils.h"

static int get_layout_from_extent(struct dss_handle *dss,
                                  struct extent *extent,
                                  struct layout_info **layout)
{
    struct dss_filter filter;
    int layout_count;
    int rc;

    rc = dss_filter_build(&filter, "{\"DSS::LYT::extent_uuid\": \"%s\"}",
                          extent->uuid);
    if (rc)
        LOG_RETURN(rc, "Failed to build filter for layout retrieval");

    rc = dss_layout_get(dss, &filter, layout, &layout_count);
    dss_filter_free(&filter);

    /* /!\ This works if there is no deduplication on the target extent. If the
     * same extent is used by multiple layouts (because of deduplication), this
     * assert will fail.
     */
    assert(layout_count == 1);

    return rc;
}

int delete_media_and_extents(struct admin_handle *handle,
                             struct media_info *media_list,
                             int media_count)
{
    int rc = 0;
    int rc2;
    int i;
    int j;

    for (i = 0; i < media_count; i++) {
        struct media_info *medium = &media_list[i];
        struct extent *extents = NULL;
        int extent_count = 0;

        rc2 = get_extents_from_medium(handle, &medium->rsc.id, &extents,
                                      &extent_count, false);
        if (rc2) {
            pho_error(rc2, "Failed to get extents of medium "FMT_PHO_ID,
                      PHO_ID(medium->rsc.id));
            goto set_rc_and_continue;
        }

        for (j = 0; j < extent_count; j++) {
            struct layout_info *layout;
            struct copy_info copy;

            rc2 = get_layout_from_extent(&handle->dss, &extents[j], &layout);
            if (rc2) {
                pho_error(rc2,
                          "Failed to get layout associated with extent '%s'",
                          extents[j].uuid);
                goto free_layout;
            }

            copy.object_uuid = layout->uuid;
            copy.version = layout->version;
            copy.copy_name = layout->copy_name;

            rc2 = dss_layout_delete(&handle->dss, layout, 1);
            if (rc2) {
                pho_error(rc2,
                          "Failed to delete layout associated with copy '%s' of object '%s', version '%d'",
                          layout->copy_name, layout->oid, layout->version);
                goto free_layout;
            }

            rc2 = reconstruct_copy(handle, &copy);
            if (rc2)
                pho_error(rc2,
                          "Failed to update copy '%s' of object '%s'",
                          layout->copy_name, layout->oid);

free_layout:
            dss_res_free(layout, 1);
            if (rc2)
                goto set_rc_and_continue;
        }

        rc2 = dss_extent_delete(&handle->dss, extents, extent_count);
        if (rc2)
            pho_error(rc2,
                      "Failed to delete extents of medium "FMT_PHO_ID,
                      PHO_ID(medium->rsc.id));

set_rc_and_continue:
        dss_res_free(extents, extent_count);
        rc = (rc ? : rc2);
        if (rc2) {
            pho_error(rc2, "Cannot delete medium '"FMT_PHO_ID"', skipping it",
                      PHO_ID(medium->rsc.id));
            continue;
        }

        rc2 = dss_media_delete(&handle->dss, medium, 1);
        if (rc2) {
            pho_error(rc2, "Failed to delete medium "FMT_PHO_ID,
                      PHO_ID(medium->rsc.id));
            rc = (rc ? : rc2);
        }
    }

    return rc;
}
