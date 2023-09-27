/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2023 CEA/DAM.
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
 * \brief  Phobos Distributed State Service API for logging
 */
#ifndef _PHO_DSS_LOGS_H
#define _PHO_DSS_LOGS_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <libpq-fe.h>
#include <sys/types.h>
#include <unistd.h>

#include "pho_dss.h"
#include "pho_type_utils.h"

#define DSS_LOGS_SELECT_QUERY \
    "SELECT family, device, medium, errno, cause, message, time FROM logs"

enum log_query_idx {
    DSS_EMIT_LOG,
    DSS_DELETE_LOGS,
};

static const char * const log_query[] = {
    [DSS_EMIT_LOG]    = "INSERT INTO logs "
                        "(family, device, medium, errno, cause, message) "
                        "VALUES ('%s', '%s', '%s', %d, '%s', '%s');",
    [DSS_DELETE_LOGS] = "DELETE FROM logs",
};

int dss_logs_from_pg_row(struct dss_handle *handle, void *item,
                         PGresult *res, int row_num);

void dss_logs_result_free(void *item);

#endif
