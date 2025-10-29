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
 * \brief  Phobos Distributed State Service API for utilities.
 */
#ifndef _PHO_DSS_LOCK_H
#define _PHO_DSS_LOCK_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <sys/types.h>

#include "pho_dss.h"
#include "pho_type_utils.h"

/** Specific intern functions used for testing. */
int _dss_lock(struct dss_handle *handle, enum dss_type type,
              const void *item_list, int item_cnt, const char *lock_hostname,
              int lock_pid, bool is_early, struct timeval *last_locate);

int _dss_lock_refresh(struct dss_handle *handle, enum dss_type type,
                      const void *item_list, int item_cnt,
                      const char *lock_hostname, int lock_owner, bool locate);

int _dss_unlock(struct dss_handle *handle, enum dss_type type,
                const void *item_list, int item_cnt, const char *lock_hostname,
                int lock_owner);

#endif
