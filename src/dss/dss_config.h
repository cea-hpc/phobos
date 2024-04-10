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
 * \brief  Configuration header of Phobos's Distributed State Service.
 */

#ifndef _PHO_DSS_CONFIG_H
#define _PHO_DSS_CONFIG_H

#include <stdbool.h>

/**
 * Parse Phobos's configuration file to initialize the list of supported tape
 * models.
 *
 * \return 0 if success, -EALREADY if list is already initialized already done
 *                       a negative error code otherwise
 */
int parse_supported_tape_models(void);

/**
 * Check if a given tape \p model is supported by Phobos.
 *
 * \param[in] model  The tape model to check
 *
 * \return true if the model is supported, false otherwise
 */
bool dss_tape_model_check(const char *model);

/**
 * Retrieve the connection string to the DSS from Phobos's configuration file.
 *
 * \return the connection string, default is "dbname=phobos host=localhost"
 */
const char *get_connection_string(void);

#endif
