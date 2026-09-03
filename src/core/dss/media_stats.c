/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2026 CEA/DAM.
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
 * \brief  Media statistics resource file of Phobos's Distributed State Service.
 */


#include "pho_type_utils.h"

#include "dss_utils.h"
#include "media_stats.h"
#include "media.h"

static int media_stats_select_query(GString **conditions, int n_conditions,
                                    GString *request, struct dss_sort *sort)
{
    g_string_append(request,
                    "SELECT family, id, library, adm_status, fs_status, stats "
                    "FROM media");

    if (n_conditions == 1)
        g_string_append(request, conditions[0]->str);
    else if (n_conditions >= 2)
        return -ENOTSUP;

    g_string_append(request, ";");

    return 0;
}

static int media_stats_from_pg_row(struct dss_handle *handle,
                                   void *void_media_stats, PGresult *res,
                                   int row_num)
{
    struct media_enriched_stats *stats = void_media_stats;
    int rc;

    stats->rsc.id.family  = str2rsc_family(PQgetvalue(res, row_num, 0));
    pho_id_name_set(&stats->rsc.id, PQgetvalue(res, row_num, 1),
                    get_str_value(res, row_num, 2));
    stats->rsc.adm_status = str2rsc_adm_status(PQgetvalue(res, row_num, 3));
    stats->status         = str2fs_status(PQgetvalue(res, row_num, 4));

    rc = dss_media_stats_decode(&stats->stats, PQgetvalue(res, row_num, 5));
    if (rc) {
        pho_error(rc, "dss_media stats decode error");
        return rc;
    }

    return rc;
}

static void media_stats_result_free(void *void_media_stats)
{
    (void) void_media_stats;
}

const struct dss_resource_ops media_stats_ops = {
    .insert_query = NULL,
    .update_query = NULL,
    .select_query = media_stats_select_query,
    .delete_query = NULL,
    .create       = media_stats_from_pg_row,
    .free         = media_stats_result_free,
    .size         = sizeof(struct media_enriched_stats),
};
