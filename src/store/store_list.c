/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2020 CEA/DAM.
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
 * \brief  Phobos Object Store implementation of non data transfer API calls
 */

#include "phobos_store.h"
#include "pho_cfg.h"
#include "pho_dss.h"
#include <glib.h>

int phobos_store_object_list(const char *pattern, const char **metadata,
                             int n_metadata, bool deprecated,
                             struct object_info **objs, int *n_objs)
{
    struct dss_filter filter;
    struct dss_handle dss;
    int rc;

    rc = pho_cfg_init_local(NULL);
    if (rc && rc != -EALREADY)
        return rc;

    rc = dss_init(&dss);
    if (rc != 0)
        return rc;

    if (n_metadata) {
        GString *metadata_str;
        int i;

        metadata_str = g_string_new(NULL);

        for (i = 0; i < n_metadata; ++i)
            g_string_append_printf(metadata_str,
                                   "{\"$KVINJSON\": "
                                   "  {\"DSS::OBJ::user_md\": \"%s\"}}%s",
                                   metadata[i],
                                   (i + 1 != n_metadata) ? "," : "");

        rc = dss_filter_build(&filter,
                              "{\"$AND\": ["
                              "  {\"$REGEXP\": {\"DSS::OBJ::oid\": \"%s\"}},"
                              "  %s"
                              "]}",
                              pattern, metadata_str->str);
        g_string_free(metadata_str, TRUE);
    } else {
        rc = dss_filter_build(&filter,
                              "{\"$REGEXP\": {\"DSS::OBJ::oid\": \"%s\"}}",
                              pattern);
    }

    if (rc)
        GOTO(err, rc);

    if (deprecated)
        rc = dss_deprecated_object_get(&dss, &filter, objs, n_objs);
    else
        rc = dss_object_get(&dss, &filter, objs, n_objs);
    if (rc)
        pho_error(rc, "Cannot fetch objects");

    dss_filter_free(&filter);

err:
    dss_fini(&dss);

    return rc;
}

void phobos_store_object_list_free(struct object_info *objs, int n_objs)
{
    dss_res_free(objs, n_objs);
}
