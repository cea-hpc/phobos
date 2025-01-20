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
 * \brief  data processor reader and writer from one posix FD
 */

#ifndef POSIX_LAYOUT_H
#define POSIX_LAYOUT_H

#include "pho_layout.h"

/**
 * Initialize the posix reader of an encoder
 *
 * @param[in,out] encoder    The encoder to init
 *
 * @return 0 on success, -errno on error.
 */
int set_posix_reader(struct pho_data_processor *encoder);

/**
 * Initialize the posix writer of a decoder
 *
 * @param[in,out] decoder    The decoder to init
 *
 * @return 0 on success, -errno on error.
 */
int set_posix_writer(struct pho_data_processor *decoder);

#endif /* POSIX_LAYOUT_H */
