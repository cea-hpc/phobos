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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "health.h"
#include "lrs_device.h"

/* XXX for now, this is hard coded at 5 */
#define MAX_HEALTH 5

size_t increase_medium_health(struct media_info *medium)
{
    if (medium->health < MAX_HEALTH)
        medium->health++;

    return medium->health;
}

size_t decrease_medium_health(struct lrs_dev *dev, struct media_info *medium)
{
    if (medium->health == 0)
        return 0;

    medium->health--;
    if (medium->health == 0)
        fail_release_medium(dev, medium);

    return medium->health;
}

size_t increase_device_health(struct lrs_dev *device)
{
    if (device->ld_dss_dev_info->health < MAX_HEALTH)
        device->ld_dss_dev_info->health++;

    return device->ld_dss_dev_info->health;
}

size_t decrease_device_health(struct lrs_dev *device)
{
    if (device->ld_dss_dev_info->health == 0)
        return 0;

    device->ld_dss_dev_info->health--;
    if (device->ld_dss_dev_info->health == 0) {
        MUTEX_LOCK(&device->ld_mutex);
        device->ld_op_status = PHO_DEV_OP_ST_FAILED;
        MUTEX_UNLOCK(&device->ld_mutex);
    }

    return device->ld_dss_dev_info->health;
}
