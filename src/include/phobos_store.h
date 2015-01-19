/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos Object Store interface
 */
#ifndef _PHO_STORE_H
#define _PHO_STORE_H

#include "pho_attrs.h"

/** put/get flags */
/* put: replace the object if it already exists
 * get: replace the target file if it already exists */
#define PHO_OBJ_REPLACE (1 << 0)

/** put a file to the object store */
int phobos_put(const char *obj_id, const char *src_file, int flags,
               const struct pho_attrs *md);

/** retrieve a file from the object store */
int phobos_get(const char *obj_id, const char *tgt_file, int flags);

/** query metadata of the object store */
/* TODO int phobos_query(criteria, &obj_list); */
/* TODO int phobos_del(); */

#endif
