/*
 *  All rights reserved (c) 2014-2024 CEA/DAM.
 *
 *  This file is part of Phobos.
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
 * \brief Phobos Distributed State Service API for specific database updates.
 */

#include <errno.h>
#include <glib.h>
#include <libpq-fe.h>

#include "pho_common.h"
#include "pho_dss.h"
#include "dss_utils.h"

int dss_update_extent_migrate(struct dss_handle *handle, const char *old_uuid,
                              const char *new_uuid)
{
    GString *request = g_string_new("BEGIN;");
    PGresult *res;
    int rc = 0;

    g_string_append_printf(request,
        "UPDATE layout SET extent_uuid = '%s' WHERE extent_uuid = '%s';"
        "UPDATE extent SET state = 'orphan' WHERE extent_uuid = '%s';"
        "UPDATE extent SET state = 'sync' WHERE extent_uuid = '%s';",
        new_uuid, old_uuid, old_uuid, new_uuid);

    rc = execute_and_commit_or_rollback(handle->dh_conn, request, &res,
                                        PGRES_COMMAND_OK);
    g_string_free(request, true);
    return rc;
}
