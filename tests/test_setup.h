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
 * \brief  Cmocka unit test setup and teardown
 */

#ifndef _TEST_SETUP_H
#define _TEST_SETUP_H

/**
 * Setup phobos db, PHOBOS_DSS_connect_string environnement variable and set
 * state to DSS handle
 */
int global_setup_dss(void **state);

/**
 * Free DSS handle state, drop phobos db and unset PHOBOS_DSS_connect_string
 */
int global_teardown_dss(void **state);

/**
 * Setup phobos db, PHOBOS_DSS_connect_string environnement variable and set
 * state to admin handle without lrs connection
 */
int global_setup_admin_no_lrs(void **state);

/**
 * Free admin handle state, drop phobos db and unset PHOBOS_DSS_connect_string
 */
int global_teardown_admin(void **state);

#endif /* _TEST_SETUP_H */
