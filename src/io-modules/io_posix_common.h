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
 * \brief  Phobos I/O POSIX common adapter functions header.
 */
#ifndef _PHO_IO_POSIX_COMMON_H
#define _PHO_IO_POSIX_COMMON_H

#include "pho_io.h"
#include "pho_types.h"

struct posix_io_ctx {
    char *fpath;
    int fd;
};

int pho_posix_get(const char *extent_key, const char *extent_desc,
                  struct pho_io_descr *iod);

int pho_posix_del(struct pho_io_descr *iod);

int pho_posix_open(const char *extent_key, const char *extent_desc,
                   struct pho_io_descr *iod, bool is_put);

int pho_posix_write(struct pho_io_descr *iod, const void *buf, size_t count);

int pho_posix_close(struct pho_io_descr *iod);

ssize_t pho_posix_preferred_io_size(struct pho_io_descr *iod);

int build_addr_path(const char *extent_key, const char *extent_desc,
                    struct pho_buff *addr);

int build_addr_hash1(const char *extent_key, const char *extent_desc,
                    struct pho_buff *addr);

int pho_posix_set_addr(const char *extent_key, const char *extent_desc,
                              enum address_type addrtype,
                              struct pho_buff *addr);

char *full_xattr_name(const char *name);

#endif
