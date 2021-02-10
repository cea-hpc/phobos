/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2017 CEA/DAM.
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
 * \brief  Phobos extent mapping interface
 */

#ifndef _PHO_MAPPER_H
#define _PHO_MAPPER_H

#include <unistd.h>
#include <ctype.h>


/**
 * Length of the automatically generated prefix.
 */
#define PHO_MAPPER_PREFIX_LENGTH    (sizeof("aa/bb/") - 1)


/**
 * Build extent path as a cleaned version of ext_desc + ext_key where ext_desc
 * can be truncated if too long.
 */
int pho_mapper_clean_path(const char *ext_key, const char *ext_desc,
                          char *dst_path, size_t dst_size);
/**
 * Build extent path using a hash, according to ext_key and ext_desc.
 * See mapper.c for more explanations about how the path is generated.
 */
int pho_mapper_hash1(const char *ext_key, const char *ext_desc, char *dst_path,
                     size_t dst_size);

/**
 * Check for valid characters of phobos mapped path components
 */
static inline int pho_mapper_chr_valid(int c)
{
    /* Exclude invisible characters */
    if (isspace(c) || !isprint(c))
        return 0;

    /* Exclude annoying chars (shell specials) */
    switch (c) {
    case '`':
    case '#':
    case '$':
    case '*':
    case '?':
    case '!':
    case '|':
    case '.':
    case ';':
    case '&':
    case '<':
    case '>':
    case '[':
    case ']':
    case '{':
    case '}':
    case '\'':
    case '"':
    case '\\':
    case '/':
        return 0;

    default:
        break;
    }

    return 1;
}

#endif
