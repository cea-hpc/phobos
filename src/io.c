/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos I/O adapters.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_io.h"
#include "pho_common.h"
#include "pho_mapper.h"
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/types.h>
#include <attr/xattr.h>
#include <ctype.h> /* FIXME drop when all mapping functions are implemented */

/** build the full posix path from a data_loc structure */
static GString *pho_posix_path(const struct data_loc *loc)
{
    GString *p = NULL;

    switch (loc->extent.addr_type) {
    case PHO_ADDR_PATH:
    case PHO_ADDR_HASH1:
        p = g_string_new(loc->root_path->str);
        g_string_append_printf(p, "/%s", loc->extent.address.buff);
        return p;
    default:
        return NULL;
    }
}

struct posix_hdl {
    int      fd;
    GString *path;
};

/** create directory levels from <root>/<lvl1> to dirname(fullpath) */
static int posix_make_parent_of(const GString *root, const GString *fullpath)
{
    char *c, *tmp, *last;
    int rc;

    if (strncmp(root->str, fullpath->str, root->len) != 0)
        LOG_RETURN(-EINVAL, "error: path '%s' is not under '%s'", fullpath->str,
                   root->str);
    c = fullpath->str + root->len;
    /* in fullpath, '/' is expected after root path */
    if (*c == '/')
        c++;
    /* ...unless root path is already slash-terminated */
    else if (root->str[root->len - 1] != '/')
        LOG_RETURN(-EINVAL, "error: path '%s' is not under '%s'", fullpath->str,
                   root->str);

    /* copy to tokenize */
    tmp = strdup(fullpath->str);
    if (tmp == NULL)
        return -ENOMEM;
    /* report c offset in fullpath to tmp */
    c = tmp + (c - fullpath->str);

    /* remove final part of the path (filename) */
    last = strrchr(c, '/');
    if (last == NULL)
        GOTO(free_str, rc = -EINVAL);
    *last = '\0';

    while ((last = strchr(c, '/')) != NULL) {
        *last = '\0';
        if (mkdir(tmp, 0750) != 0 && errno != EEXIST)
            LOG_GOTO(free_str, rc = -errno, "mkdir(%s) failed", tmp);
        *last = '/';
        c = last+1;
    }
    if (mkdir(tmp, 0750) != 0 && errno != EEXIST)
        LOG_GOTO(free_str, rc = -errno, "mkdir(%s) failed", tmp);
    rc = 0;

free_str:
    free(tmp);
    return rc;
}

static int pho_posix_open(const struct data_loc *loc, int flags, void **hdl)
{
    struct posix_hdl *phdl;
    int rc;

    phdl = calloc(1, sizeof(*phdl));
    if (phdl == NULL)
        return -ENOMEM;

    phdl->path = pho_posix_path(loc);
    if (phdl->path == NULL)
        GOTO(free_hdl, rc = -EINVAL);

    if (flags & O_CREAT) {
        /* mkdir -p */
        rc = posix_make_parent_of(loc->root_path, phdl->path);
        if (rc)
            goto free_hdl;
    }

    phdl->fd = open(phdl->path->str, flags, 0640);
    if (phdl->fd < 0)
        LOG_GOTO(free_path, rc = -errno, "open(%s) for write failed",
                 phdl->path->str);

    *hdl = (void *)phdl;
    return 0;

free_path:
    if (phdl != NULL && phdl->path != NULL)
        g_string_free(phdl->path, TRUE);
free_hdl:
    free(phdl);
    return rc;
}

static int pho_posix_close(void *hdl, int io_flags)
{
    struct posix_hdl *phdl = (struct posix_hdl *)hdl;
    int rc = 0;

    if (io_flags & PHO_IO_SYNC_FILE) {
        if (fsync(phdl->fd) != 0)
            rc = -errno;
    }
    if (io_flags & PHO_IO_SYNC_FS) {
        /* TODO call syncfs(), if it exists */
        /* else, sync all filesystems... */
        sync();
    }

    if (io_flags & PHO_IO_NO_REUSE) {
        rc = -posix_fadvise(phdl->fd, 0, 0,
                            POSIX_FADV_DONTNEED | POSIX_FADV_NOREUSE);
    }

    if (close(phdl->fd) != 0 && rc == 0) /* keep the first reported error */
        rc = -errno;

    phdl->fd = -1;
    g_string_free(phdl->path, TRUE);
    phdl->path = NULL;

    return rc;
}

static int pho_posix_sync(const struct data_loc *loc, int io_flags)
{
    if (io_flags & PHO_IO_SYNC_FS) {
        /* TODO call syncfs(), if it exists */
        /* else, sync all filesystems... */
        sync();
    }
    /* XXX allow SYNC_FILE? sync entry or any parent? */
    return 0;
}

#define LTFS_SYNC_ATTR_NAME "user.ltfs.sync"

static int pho_ltfs_sync(const struct data_loc *loc, int io_flags)
{
    int one = 1;

    if (io_flags & PHO_IO_SYNC_FS) {
        /* flush the LTFS partition to tape */
        if (setxattr(loc->root_path->str, LTFS_SYNC_ATTR_NAME,
                     (void *)&one, sizeof(one), 0) != 0)
            LOG_RETURN(-errno, "failed to set LTFS special xattr "
                       LTFS_SYNC_ATTR_NAME);
    }
    /* XXX allow SYNC_FILE? sync entry or any parent? */
    return 0;
}

static ssize_t pho_posix_sendfile(int tgt_fd, int src_fd, off_t *src_offset,
                                  size_t count, const char *way)
{
    ssize_t rw;
    off_t offsave = ((src_offset != NULL) ? *src_offset : 0);

    rw = sendfile(tgt_fd, src_fd, src_offset, count);

    if (rw < 0)
        return -errno;
    else if (rw < count) {
        log("Warning: incomplete sendfile %llu %s out of %llu",
            (unsigned long long)rw, way, (unsigned long long)count);
        if (src_offset != NULL) {
            /* check offset value */
            if (*src_offset != offsave + rw)
                LOG_RETURN(-EIO, "inconsistent src_offset value "
                           "(%llu != %llu + %llu)",
                           (unsigned long long)*src_offset,
                           (unsigned long long)offsave,
                           (unsigned long long)rw);
            else
                /* copy missing data */
                return pho_posix_sendfile(tgt_fd, src_fd, src_offset,
                                          count - rw, way);
        } else {
            offsave = rw;
            return pho_posix_sendfile(tgt_fd, src_fd, &offsave,
                                      count - rw, way);
        }
    } else if (rw != count)
        LOG_RETURN(-EIO, "inconsistent %s count %llu > %llu", way,
                   (unsigned long long)rw, (unsigned long long)count);
    /* rw == count */
    return 0; /* all data read/written */
}

static ssize_t pho_posix_sendfile_r(int tgt_fd, void *src_hdl,
                                    off_t *src_offset, size_t count)
{
    struct posix_hdl *phdl = (struct posix_hdl *)src_hdl;

    return  pho_posix_sendfile(tgt_fd, phdl->fd, src_offset, count,
                               "read");
}

static ssize_t pho_posix_sendfile_w(void *tgt_hdl, int src_fd,
                                    off_t *src_offset, size_t count)
{
    struct posix_hdl *phdl = (struct posix_hdl *)tgt_hdl;

    return  pho_posix_sendfile(phdl->fd, src_fd, src_offset, count,
                               "write");
}

/* let the backend select the xattr namespace */
#define POSIX_XATTR_PREFIX "user."

static int pho_posix_fsetxattr(void *hdl, const char *name, const void *value,
                               size_t size, int flags)
{
    struct posix_hdl *phdl = (struct posix_hdl *)hdl;
    char *tmp_name;
    int  rc = 0,
         len;

    len = strlen(POSIX_XATTR_PREFIX) + strlen(name);

    tmp_name = calloc(1, len + 1);
    if (tmp_name == NULL)
        return -errno;
    len = snprintf(tmp_name, len + 1, "%s%s", POSIX_XATTR_PREFIX, name);

    if (fsetxattr(phdl->fd, tmp_name, value, size, flags) != 0)
        rc = -errno;

    free(tmp_name);
    return rc;
}

static int pho_posix_fstat(void *hdl, struct stat *st)
{
    struct posix_hdl *phdl = (struct posix_hdl *)hdl;

    return (fstat(phdl->fd, st) != 0) ? -errno : 0;
}

static int pho_posix_remove(const struct data_loc *loc)
{
    int rc = 0;
    GString *path = pho_posix_path(loc);

    if (path == NULL)
        return -EINVAL;
    if (!gstring_empty(path)
        && (unlink(path->str) != 0))
        rc = -errno;

    g_string_free(path, TRUE);
    return rc;
}

/** FIXME (test only) replace by real call */
static int pho_mapper_clean_path(const char *obj_id, const char *ext_tag,
                                 char *dst_path, size_t dst_size)
{
    char *c;

    strncpy(dst_path, obj_id, dst_size);

    for (c = dst_path; *c != '\0'; c++)
        if (*c == '.' || !isprint(*c) || isspace(*c))
            *c = '_';

    if (ext_tag != NULL) {
        dst_path += strlen(obj_id);
        dst_size -= strlen(obj_id);
        snprintf(dst_path, dst_size, ".%s", ext_tag);
    }

    return 0;
}

/** allocate the desired path length, and call the path mapper */
static int pho_set_path(const char *id, const char *tag, struct pho_buff *addr)
{
    int rc;

    addr->size = strlen(id) + (tag ? strlen(tag) : 0) + 1;
    /* don't exceed PATH_MAX in any case */
    if (addr->size > PATH_MAX)
        addr->size = PATH_MAX;

    addr->buff = calloc(1, addr->size);
    if (addr->buff == NULL)
        return -ENOMEM;

    rc = pho_mapper_clean_path(id, tag, addr->buff, addr->size);
    if (rc) {
        free(addr->buff);
        addr->buff = NULL;
        addr->size = 0;
    }
    return rc;
}

/** allocate the desired path length, and call the hash-based mapper */
static int pho_set_hash1(const char *id, const char *tag, struct pho_buff *addr)
{
    int rc;

    /* portable everywhere... even on windows */
    addr->size = NAME_MAX + 1;
    addr->buff = calloc(1, addr->size);
    if (addr->buff == NULL)
        return -ENOMEM;

    rc = pho_mapper_extent_resolve(id, tag, addr->buff, addr->size);
    if (rc) {
        free(addr->buff);
        addr->buff = NULL;
        addr->size = 0;
    }
    return rc;
}

/** POSIX adapter */
static const struct io_adapter posix_adapter = {
    .open       = pho_posix_open,
    .close      = pho_posix_close,
    .sync       = pho_posix_sync,
    .sendfile_r = pho_posix_sendfile_r,
    .sendfile_w = pho_posix_sendfile_w,
    .fsetxattr  = pho_posix_fsetxattr,
    .fstat      = pho_posix_fstat,
    .remove     = pho_posix_remove
};

/** retrieve IO functions for the given filesystem and addressing type */
int get_io_adapter(enum fs_type fstype, enum address_type addrtype,
                   struct io_adapter *ioa)
{
    switch (fstype) {
    case PHO_FS_LTFS:
        *ioa = posix_adapter;
        ioa->sync = pho_ltfs_sync;
        break;
    case PHO_FS_POSIX:
        *ioa = posix_adapter;
        break;
    default:
        return -EINVAL;
    }

    switch (addrtype) {
    case PHO_ADDR_PATH:
        ioa->id2addr = pho_set_path;
        break;
    case PHO_ADDR_HASH1:
        ioa->id2addr = pho_set_hash1;
        break;
    default:
        return -EINVAL;
    }

    return 0;
}
