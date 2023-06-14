/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2022 CEA/DAM.
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
 * \brief  Phobos Distributed State Service API for utilities.
 */
#ifndef _PHO_DSS_UTILS_H
#define _PHO_DSS_UTILS_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <libpq-fe.h>

/**
 * Execute a PSQL \p request, verify the result is as expected with \p tested
 * and put the result in \p res.
 *
 * \param conn[in]    The connection to the database
 * \param request[in] Request to execute
 * \param res[out]    Result holder of the request
 * \param tested[in]  The expected result of the request
 *
 * \return            0 on success, or the error as returned by PSQL
 */
int execute(PGconn *conn, GString *request, PGresult **res,
            ExecStatusType tested);

/**
 * Convert PostgreSQL status codes to meaningful errno values.
 * \param   res[in]         Failed query result descriptor
 * \return                  Negated errno value corresponding to the error
 */
int psql_state2errno(const PGresult *res);

#endif
