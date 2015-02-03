/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
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
#define PHO_MAPPER_PREFIX_LENGTH    (sizeof("aa/bb/aabbccdd_") - 1)


/**
 * Build extent path as a cleaned and truncated version of obj_id + ext_tag.
 */
int pho_mapper_clean_path(const char *obj_id, const char *ext_tag,
                          char *dst_path, size_t dst_size);
/**
 * Build extent path using a hash, according to obj_id and ext_tag.
 * See mapper.c for more explanations about how the path is generated.
 */
int pho_mapper_hash1(const char *obj_id, const char *ext_tag, char *dst_path,
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
