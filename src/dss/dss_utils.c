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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dss_utils.h"
#include "pho_common.h"

#include <errno.h>


struct sqlerr_map_item {
    const char *smi_prefix;  /**< SQL error code or class (prefix) */
    int         smi_errcode; /**< Corresponding negated errno code */
};

/**
 * Map errors from SQL to closest errno.
 * The list is traversed from top to bottom and stops at first match, so make
 * sure that new items are inserted in most-specific first order.
 * See: http://www.postgresql.org/docs/9.4/static/errcodes-appendix.html
 */
static const struct sqlerr_map_item sqlerr_map[] = {
    /* Class 00 - Succesful completion */
    {"00000", 0},
    /* Class 22 - Data exception */
    {"22", -EINVAL},
    /* Class 23 - Integrity constraint violation */
    {"23", -EEXIST},
    /* Class 42 - Syntax error or access rule violation */
    {"42", -EINVAL},
    /* Class 53 - Insufficient resources */
    {"53100", -ENOSPC},
    {"53200", -ENOMEM},
    {"53300", -EUSERS},
    {"53", -EIO},
    /* Class PH - Phobos custom errors */
    {"PHLK1", -ENOLCK},
    {"PHLK2", -EACCES},
    /* Catch all -- KEEP LAST -- */
    {"", -ECOMM}
};

int psql_state2errno(const PGresult *res)
{
    char *sqlstate = PQresultErrorField(res, PG_DIAG_SQLSTATE);
    int i;

    if (sqlstate == NULL)
        return 0;

    for (i = 0; i < ARRAY_SIZE(sqlerr_map); i++) {
        const char *pfx = sqlerr_map[i].smi_prefix;

        if (strncmp(pfx, sqlstate, strlen(pfx)) == 0)
            return sqlerr_map[i].smi_errcode;
    }

    /* sqlerr_map must contain a catch-all entry */
    UNREACHED();
}
