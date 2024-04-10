/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2024 CEA/DAM.
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
 * \brief  Logs resource header of Phobos's Distributed State Service.
 */

#ifndef _PHO_DSS_LOGS_H
#define _PHO_DSS_LOGS_H

#include "resources.h"

/**
 * The "logs" operations structure.
 * Implements every function of the structure except "update_query".
 */
extern const struct dss_resource_ops logs_ops;

int dss_resource_health(struct dss_handle *dss,
                        const struct pho_id *medium_id,
                        enum dss_type resource, size_t max_health,
                        size_t *health);

#endif
