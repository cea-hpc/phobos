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
 * \brief  Device/Media health management
 */

#ifndef _PHO_HEALTH_H
#define _PHO_HEALTH_H

#include <stddef.h>

struct media_info;
struct lrs_dev;
enum device_load_state;

/**
 * Increase \p medium's health by one without exceeding the maximum health
 * limit.
 *
 * \return new medium's health
 */
size_t increase_medium_health(struct media_info *medium);

/**
 * Decrease \p medium's health. Once the health reaches 0, the medium is set to
 * failed in the DSS and its lock is released.
 *
 * \return new medium's health
 */
size_t decrease_medium_health(struct lrs_dev *dev, struct media_info *medium);

/**
 * Increase \p device's health by one without exceeding the maximum health
 * limit.
 *
 * \return new device's health
 */
size_t increase_device_health(struct lrs_dev *device);

/**
 * Decrease \p device's health. Once the health reaches 0, the device is set to
 * failed locally. The device is still not failed in the DSS and the LRS still
 * holds the lock.
 *
 * \return new device's health
 */
size_t decrease_device_health(struct lrs_dev *device);

/** Return the maximum health of a device or medium from the configuration */
size_t max_health(void);

#endif
