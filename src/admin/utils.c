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
 * \brief  Phobos admin source file for utilities
 */

#include "utils.h"

int get_extents_from_medium(struct admin_handle *adm,
                            const struct pho_id *source,
                            struct extent **extents, int *count, bool no_orphan)
{
    struct dss_filter filter;
    int rc;

    if (no_orphan)
        rc = dss_filter_build(&filter,
                              "{\"$AND\": ["
                              "  {\"DSS::EXT::medium_family\": \"%s\"},"
                              "  {\"DSS::EXT::medium_id\": \"%s\"},"
                              "  {\"DSS::EXT::medium_library\": \"%s\"},"
                              "  {\"$NOR\": [{\"DSS::EXT::state\": \"%s\"}]}"
                              "]}", rsc_family2str(source->family),
                              source->name, source->library,
                              extent_state2str(PHO_EXT_ST_ORPHAN));
    else
        rc = dss_filter_build(&filter,
                              "{\"$AND\": ["
                              "  {\"DSS::EXT::medium_family\": \"%s\"},"
                              "  {\"DSS::EXT::medium_id\": \"%s\"},"
                              "  {\"DSS::EXT::medium_library\": \"%s\"}"
                              "]}", rsc_family2str(source->family),
                              source->name, source->library);
    if (rc)
        LOG_RETURN(rc, "Failed to build filter for extent retrieval");

    rc = dss_extent_get(&adm->dss, &filter, extents, count, NULL);
    dss_filter_free(&filter);
    if (rc)
        LOG_RETURN(rc, "Failed to retrieve "FMT_PHO_ID" extents",
                   PHO_ID(*source));

    return rc;
}
