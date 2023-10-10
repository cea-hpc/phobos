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
 * \brief  TLC library interface
 */

#ifndef _PHO_TLC_LIBRARY_H
#define _PHO_TLC_LIBRARY_H

#include "../ldm-modules/scsi_api.h"

struct status_array {
    struct element_status *items;
    int count;
    bool loaded;
};

struct lib_descriptor {
    /* file descriptor to SCSI lib device */
    int fd;

    /* Cache of library element address */
    struct mode_sense_info msi;
    bool msi_loaded;

    /* Cache of library element status */
    struct status_array arms;
    struct status_array slots;
    struct status_array impexp;
    struct status_array drives;
};

/**
 * Open the library and load current status
 *
 * @param[in,out] lib   Already allocated library to fill with current status
 * @param[in]     dev   Device path of the library
 *
 * @return              0 if success, else a negative error code
 */
int tlc_library_open(struct lib_descriptor *lib, const char *dev);

/**
 * Close and clean the library
 *
 * @param[in,out]   lib     Library to close and clean
 */
void tlc_library_close(struct lib_descriptor *lib);

#endif /* _PHO_TLC_LIBRARY_H */
