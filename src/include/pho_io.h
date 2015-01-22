/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos backend adapters
 */
#ifndef _PHO_IO_H
#define _PHO_IO_H

#include "pho_types.h"
#include <sys/stat.h> /* for struct stat */
#include <unistd.h> /* for ssize_t */

#define PHO_IO_SYNC_FILE (1 << 0)
#define PHO_IO_SYNC_FS   (1 << 1)
#define PHO_IO_NO_REUSE  (1 << 2)

struct io_adapter {
    /** convert  object id + extent tag to an address in the media */
    int (*id2addr)(const char *id, const char *tag, struct pho_buff *addr);
    int (*open)(const struct data_loc *loc, int flags, void **hdl);
    int (*close)(void *hdl, int io_flags);
    int (*sync)(const struct data_loc *loc, int io_flags);
    /** sendfile from the media to a fd */
    ssize_t (*sendfile_r)(int tgt_fd, void *src_hdl, off_t *src_offset,
                          size_t count);
    /** sendfile from a fd to the media */
    ssize_t (*sendfile_w)(void *tgt_hdl, int src_fd, off_t *src_offset,
                          size_t count);
    ssize_t (*pread)(void *hdl, void *buf, size_t count, off_t offset);
    ssize_t (*pwrite)(void *hdl, const void *buf, size_t count, off_t offset);

    ssize_t (*read)(void *hdl, void *buf, size_t count);
    ssize_t (*write)(void *hdl, const void *buf, size_t count);

    /* Note: name is without xattr prefix (let the backend choose it) */
    int (*fsetxattr)(void *hdl, const char *name, const void *value,
                     size_t size, int flags);
    int (*fstat)(void *hdl, struct stat *st);
    int (*remove)(const struct data_loc *loc);
};

/** retrieve IO functions for the given filesystem and addressing type */
int get_io_adapter(enum fs_type fstype, enum address_type addrtype,
                   struct io_adapter *ioa);

#endif
