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
 * \brief  Phobos I/O adapters.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_io.h"
#include "pho_attrs.h"
#include "pho_common.h"
#include "pho_mapper.h"
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/types.h>
#include <attr/xattr.h>
#include <attr/attributes.h>
#include <unistd.h>

#define MAX_NULL_WRITE_TRY 10

static int pho_posix_open(const char *id, const char *tag,
                          struct pho_io_descr *iod, bool is_put);
static int pho_posix_close(struct pho_io_descr *iod);

struct posix_io_ctx {
    int   fd;
    char *fpath;
};


/**
 * Return a new null initialized posix_io_ctx.
 *
 * To free this io_ctx, call pho_posix_close.
 */
static struct posix_io_ctx *alloc_posix_io_ctx(void)
{
    struct posix_io_ctx *io_ctx;

    io_ctx = malloc(sizeof(struct posix_io_ctx));
    if (io_ctx) {
        io_ctx->fd = -1;
        io_ctx->fpath = NULL;
    }

    return io_ctx;
}

/** build the full posix path from a pho_ext_loc structure */
static char *pho_posix_fullpath(const struct pho_ext_loc *loc)
{
    char *p;

    switch (loc->extent->addr_type) {
    case PHO_ADDR_PATH:
    case PHO_ADDR_HASH1:
        if (loc->extent->address.buff == NULL)
            return NULL;
        if (asprintf(&p, "%s/%s", loc->root_path,
                     loc->extent->address.buff) < 0)
            return NULL;
        return p;
    default:
        return NULL;
    }
}

/** create directory levels from <root>/<lvl1> to dirname(fullpath) */
static int pho_posix_make_parent_of(const char *root,
                                    const char *fullpath)
{
    const char *c;
    char       *tmp, *last;
    int         rc;
    int         root_len;
    ENTRY;

    root_len = strlen(root);
    if (strncmp(root, fullpath, root_len) != 0)
        LOG_RETURN(-EINVAL, "Path '%s' is not under '%s'", fullpath, root);

    c = fullpath + root_len;
    /* in fullpath, '/' is expected after root path */
    if (*c == '/')
        c++;
    /* ...unless root path is already slash-terminated */
    else if (root[root_len - 1] != '/')
        LOG_RETURN(-EINVAL, "Path '%s' is not under '%s'", fullpath, root);

    /* copy to tokenize */
    tmp = strdup(fullpath);
    if (tmp == NULL)
        return -ENOMEM;

    /* report c offset in fullpath to tmp */
    c = tmp + (c - fullpath);

    /* remove final part of the path (filename) */
    last = strrchr(c, '/');
    if (last == NULL)
        GOTO(free_str, rc = 0); /* Ok, nothing to do */

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

/** allocate the desired path length, and call the path mapper */
static int build_addr_path(const char *id, const char *tag,
                           struct pho_buff *addr)
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
static int build_addr_hash1(const char *id, const char *tag,
                            struct pho_buff *addr)
{
    int rc;

    /* portable everywhere... even on windows */
    addr->size = NAME_MAX + 1;
    addr->buff = calloc(1, addr->size);
    if (addr->buff == NULL)
        return -ENOMEM;

    rc = pho_mapper_hash1(id, tag, addr->buff, addr->size);
    if (rc) {
        free(addr->buff);
        addr->buff = NULL;
        addr->size = 0;
    }
    return rc;
}

/** set address field for a POSIX extent */
static int pho_posix_set_addr(const char *id, const char *tag,
                              enum address_type addrtype,
                              struct pho_buff *addr)
{
    switch (addrtype) {
    case PHO_ADDR_PATH:
        return build_addr_path(id, tag, addr);
    case PHO_ADDR_HASH1:
        return build_addr_hash1(id, tag, addr);
    default:
        return -EINVAL;
    }
}

/**
 * Sendfile wrapper
 * @TODO fallback to (p)read/(p)write
 *
 * @TODO: for raid1 layout, multiple destinations will be written from one
 * source, this function will not be adapted then. Anyway, we should not rely on
 * seekable FDs and avoid fiddling with offsets in input.
 */
static int pho_posix_sendfile(int tgt_fd, int src_fd, size_t count)
{
    ssize_t rw = 0;
    ENTRY;

    while (count > 0) {
        rw = sendfile(tgt_fd, src_fd, NULL, count);
        if (rw < 0)
            LOG_RETURN(-errno, "sendfile failure");

        if (count && rw == 0)
            LOG_RETURN(-ENOBUFS,
                       "sendfile failure, reached source fd eof too soon");

        pho_debug("sendfile returned after copying %zd bytes. %zd bytes left",
                  rw, count - rw);

        count -= rw;
    }

    return 0;
}

static int pho_flags2open(enum pho_io_flags io_flags)
{
    int flags = 0;

    /* no replace => O_EXCL */
    if (!(io_flags & PHO_IO_REPLACE))
        flags |= O_EXCL;

    return flags;
}

/* let the backend select the xattr namespace */
#define POSIX_XATTR_PREFIX "user."

/** Build full xattr name "user.<name>".
 * The returned pointer must be released after
 * usage.
 */
static char *full_xattr_name(const char *name)
{
    int   len;
    char *tmp_name;

    len = asprintf(&tmp_name, "%s%s", POSIX_XATTR_PREFIX, name);
    if (len == -1)
        return NULL;

    return tmp_name;
}


/** set an extended attribute (or remove it if value is NULL) */
static int pho_setxattr(const char *path, int fd, const char *name,
                        const char *value, int flags)
{
    char    *tmp_name;
    int      rc = 0;
    ENTRY;

    if (name == NULL || name[0] == '\0')
        return -EINVAL;

    tmp_name = full_xattr_name(name);
    if (tmp_name == NULL)
        return -ENOMEM;

    if (value != NULL) {
        if (fd != -1)
            rc = fsetxattr(fd, tmp_name, value, strlen(value) + 1, flags);
        else
            rc = setxattr(path, tmp_name, value, strlen(value) + 1, flags);

        if (rc != 0)
            LOG_GOTO(free_tmp, rc = -errno, "setxattr failed");

    } else if (flags & XATTR_REPLACE) {
        /* remove the previous attribute */
        if (fd != -1)
            rc = fremovexattr(fd, tmp_name);
        else
            rc = removexattr(path, tmp_name);

        if (rc != 0) {
            if (errno == ENODATA)
                rc = 0;
            else
                LOG_GOTO(free_tmp, rc = -errno, "removexattr failed");
        }
    } /* else : noop */

free_tmp:
    free(tmp_name);
    return rc;
}

/** Get a user extended attribute.
 * \param[in] path full path to the extent.
 * \param[in] name name of the extended attribute without the "user." prefix.
 * \param[out] value This buffer is allocated by the call if the attribute
 *                   exists. It must be freed by the caller. It is set to NULL
 *                   if the attribute doesn't exist.
 */
static int pho_getxattr(const char *path, const char *name, char **value)
{
    char *tmp_name;
    char *buff;
    int   rc;
    ENTRY;

    if (value == NULL)
        return -EINVAL;

    *value = NULL;

    if (name == NULL || name[0] == '\0')
        return -EINVAL;

    tmp_name = full_xattr_name(name);
    if (tmp_name == NULL)
        return -ENOMEM;

    buff = calloc(1, ATTR_MAX_VALUELEN);
    if (buff == NULL)
        GOTO(free_tmp, rc = -ENOMEM);

    rc = getxattr(path, tmp_name, buff, ATTR_MAX_VALUELEN);
    if (rc <= 0) {
        if (errno == ENODATA || rc == 0)
            GOTO(free_buff, rc = 0);

        LOG_GOTO(free_buff, rc = -errno, "getxattr failed");
    }
    pho_debug("%s=%s", tmp_name, buff);

    *value = strndup(buff, rc);
    rc = 0;

free_buff:
    free(buff);
free_tmp:
    free(tmp_name);
    return rc;
}

struct md_iter_sx {
    const char  *mis_path;
    int          mis_fd;
    int          mis_flags;
};

static int setxattr_cb(const char *key, const char *value, void *udata)
{
    struct md_iter_sx  *arg = (struct md_iter_sx *)udata;

    return pho_setxattr(arg->mis_path, arg->mis_fd, key, value,
                        arg->mis_flags);
}

/**
 * Set entry metadata as extended attributes.
 * Either fd or path must be specified.
 */
static int _pho_posix_md_set(const char *path, int fd,
                             const struct pho_attrs *attrs,
                             enum pho_io_flags flags)
{
    struct md_iter_sx  args;
    ENTRY;

    /* Specify one and only one of path or fd */
    assert((path == NULL) != (fd == -1));

    args.mis_path  = path;
    args.mis_fd    = fd;
    /* pure create: fails if the attribute already exists */
    args.mis_flags = (flags & PHO_IO_REPLACE) ? 0 : XATTR_CREATE;

    return pho_attrs_foreach(attrs, setxattr_cb, &args);
}

static inline int pho_posix_md_fset(int fd, const struct pho_attrs *attrs,
                                    enum pho_io_flags flags)
{
    return _pho_posix_md_set(NULL, fd, attrs, flags);
}

static inline int pho_posix_md_set(const char *path,
                                   const struct pho_attrs *attrs,
                                   enum pho_io_flags flags)
{
    return _pho_posix_md_set(path, -1, attrs, flags);
}


struct md_iter_gx {
    const char          *mig_path;
    struct pho_attrs    *mig_attrs;
};

static int getxattr_cb(const char *key, const char *value, void *udata)
{
    struct md_iter_gx   *arg = (struct md_iter_gx *)udata;
    char                *tmp_val;
    int                  rc;

    rc = pho_getxattr(arg->mig_path, key, &tmp_val);
    if (rc != 0)
        return rc;

    rc = pho_attr_set(arg->mig_attrs, key, tmp_val);
    if (rc != 0)
        return rc;

    return 0;
}

static int pho_posix_md_get(const char *path, struct pho_attrs *attrs)
{
    struct md_iter_gx   args;
    int                 rc;
    ENTRY;

    args.mig_path  = path;
    args.mig_attrs = attrs;

    rc = pho_attrs_foreach(attrs, getxattr_cb, &args);
    if (rc != 0)
        pho_attrs_free(attrs);

    return rc;
}

static int pho_posix_put(const char *id, const char *tag,
                         struct pho_io_descr *iod)
{
    int                  rc = 0, rc2 = 0;
    struct posix_io_ctx *io_ctx;

    ENTRY;

    /* open */
    rc = pho_posix_open(id, tag, iod, true);
    if (rc || iod->iod_flags & PHO_IO_MD_ONLY)
        return rc;

    io_ctx = iod->iod_ctx;

    /* write data */
    rc = pho_posix_sendfile(io_ctx->fd, iod->iod_fd, iod->iod_size);
    if (rc)
        goto clean;

    /* flush data */
    if (iod->iod_flags & PHO_IO_SYNC_FILE)
        if (fsync(io_ctx->fd) != 0)
            LOG_GOTO(clean, rc = -errno, "fsync failed");

    if (iod->iod_flags & PHO_IO_NO_REUSE) {
        rc = posix_fadvise(io_ctx->fd, 0, 0,
                           POSIX_FADV_DONTNEED | POSIX_FADV_NOREUSE);
        if (rc) {
            pho_warn("posix_fadvise failed: %s (%d)", strerror(rc), rc);
            rc = 0; /* ignore error */
        }
    }

clean:
    /* unlink if failure occurs */
    if (rc != 0) {
        assert(io_ctx->fpath);
        if (unlink(io_ctx->fpath))
            if (!rc) /* keep the first reported error */
                rc = -errno;
            pho_warn("Failed to clean extent '%s': %s", io_ctx->fpath,
                     strerror(errno));
    }

    rc2 = pho_posix_close(iod);
    if (rc2 && rc == 0) /* keep the first reported error */
        rc = rc2;

    return rc;
}

static int pho_posix_get(const char *id, const char *tag,
                         struct pho_io_descr *iod)
{
    int                  rc = 0, rc2 = 0;
    struct posix_io_ctx *io_ctx;

    ENTRY;

    rc = pho_posix_open(id, tag, iod, false);
    if (rc || iod->iod_flags & PHO_IO_MD_ONLY)
        return rc;

    io_ctx = (struct posix_io_ctx *) iod->iod_ctx;

    /** If size is not stored in the DB, use the extent size */
    if (iod->iod_size == 0) {
        struct stat st;

        if (fstat(io_ctx->fd, &st) != 0)
            LOG_GOTO(clean, rc = -errno, "failed to stat %s",
                                              io_ctx->fpath);

        pho_warn("Extent size is not set in DB: using physical extent size: "
                 "%ju bytes", st.st_size);
        iod->iod_size = st.st_size;
    }

    /* read the extent */
    rc = pho_posix_sendfile(iod->iod_fd, io_ctx->fd, iod->iod_size);
    if (rc)
        goto clean;

    if (iod->iod_flags & PHO_IO_NO_REUSE) {
        /*  release source file from system cache */
        rc = posix_fadvise(io_ctx->fd, 0, 0,
                            POSIX_FADV_DONTNEED | POSIX_FADV_NOREUSE);
        if (rc) {
            pho_warn("posix_fadvise failed: %s (%d)", strerror(rc), rc);
            rc = 0; /* ignore error */
        }
    }

clean:
    if (rc != 0)
        pho_attrs_free(&iod->iod_attrs);

    rc2 = pho_posix_close(iod);
    if (rc2 && rc == 0) /* keep the first reported error */
        rc = rc2;

    return rc;
}

static int pho_posix_medium_sync(const char *root_path)
{
    int rc = 0;
    int fd;

    ENTRY;

    fd = open(root_path, O_RDONLY);
    if (fd == -1)
        return -errno;

    if (syncfs(fd))
        rc = -errno;

    if (close(fd) && !rc)
        return -errno;

    return rc;
}

#define LTFS_SYNC_ATTR_NAME "user.ltfs.sync"

static int pho_ltfs_sync(const char *root_path)
{
    int one = 1;
    ENTRY;

    /* flush the LTFS partition to tape */
    if (setxattr(root_path, LTFS_SYNC_ATTR_NAME, (void *)&one,
                 sizeof(one), 0) != 0)
        LOG_RETURN(-errno, "failed to set LTFS special xattr "
                   LTFS_SYNC_ATTR_NAME);

    return 0;
}

static int pho_posix_del(const char *id, const char *tag,
                         struct pho_ext_loc *loc)
{
    int   rc = 0;
    char *path;
    ENTRY;

    if (loc->extent->address.buff == NULL) {
        pho_warn("Object has no address stored in database"
                 " (generating it from object id)");
        rc = pho_posix_set_addr(id, tag, loc->extent->addr_type,
                                &loc->extent->address);
        if (rc)
            return rc;
    }

    path = pho_posix_fullpath(loc);
    if (path == NULL)
        return -EINVAL;

    if (unlink(path) != 0)
        rc = -errno;

    free(path);
    return rc;
}

static int pho_posix_open_put(struct pho_io_descr *iod)
{
    int                  rc;
    int                  flags;
    bool                 file_existed;
    bool                 file_created = false;
    struct posix_io_ctx *io_ctx;

    io_ctx = iod->iod_ctx;

    /* if the call is MD_ONLY, it is expected that the entry exists. */
    if (iod->iod_flags & PHO_IO_MD_ONLY) {
        /* pho_io_flags are passed in to propagate SYNC options */
        rc = pho_posix_md_set(io_ctx->fpath, &iod->iod_attrs,
                              iod->iod_flags);
        goto free_io_ctx;
    }

    /* mkdir -p */
    rc = pho_posix_make_parent_of(iod->iod_loc->root_path, io_ctx->fpath);
    if (rc)
        goto free_io_ctx;

    /* build posix flags */
    flags = pho_flags2open(iod->iod_flags);

    /* testing pre-existing file to know if we need to clean it on error */
    rc = access(io_ctx->fpath, F_OK);
    if (rc) {
        rc = -errno;
        if (rc != -ENOENT)
            goto free_io_ctx;

        file_existed = false;
    } else {
        file_existed = true;
    }

    io_ctx->fd = open(io_ctx->fpath, flags | O_CREAT | O_WRONLY, 0660);
    if (io_ctx->fd < 0)
        LOG_GOTO(free_io_ctx, rc = -errno, "open(%s) for write failed",
                 io_ctx->fpath);

    if (!file_existed)
        file_created = true;

    /* set metadata */
    /* Only propagate REPLACE option, if specified */
    rc = pho_posix_md_fset(io_ctx->fd, &iod->iod_attrs,
                           iod->iod_flags & PHO_IO_REPLACE);

    if (rc == 0) /* no error */
        return rc;

free_io_ctx:
    /* unlink if failure occurs */
    if (file_created) {
        assert(io_ctx->fpath);
        if (unlink(io_ctx->fpath))
            if (!rc) /* keep the first reported error */
                rc = -errno;
            pho_warn("Failed to clean extent '%s': %s", io_ctx->fpath,
                     strerror(errno));
    }

    /* error cleaning by closing io_ctx */
    pho_posix_close(iod);
    return rc;
}

static int pho_posix_open_get(struct pho_io_descr *iod)
{
    struct posix_io_ctx     *io_ctx;
    int rc;

    io_ctx = iod->iod_ctx;

    /* get entry MD, if requested */
    rc = pho_posix_md_get(io_ctx->fpath, &iod->iod_attrs);
    if (rc != 0 || (iod->iod_flags & PHO_IO_MD_ONLY))
        goto free_io_ctx;

    /* open the extent */
    io_ctx->fd = open(io_ctx->fpath, O_RDONLY);
    if (io_ctx->fd < 0) {
        rc = -errno;
        pho_attrs_free(&iod->iod_attrs);
        LOG_GOTO(free_io_ctx, rc, "open %s for read failed", io_ctx->fpath);
    }

    return 0;

free_io_ctx:
    /* cleaning */
    pho_posix_close(iod);
    return rc;
}

/**
 * If (iod->iod_flags & PHO_IO_MD_ONLY), we only get/set attr, no iod_ctx is
 * allocated and there is no need to close.
 */
static int pho_posix_open(const char *id, const char *tag,
                          struct pho_io_descr *iod, bool is_put)
{
    int                      rc = 0;
    struct posix_io_ctx     *io_ctx;

    ENTRY;

    /* generate entry address, if it is not already set */
    if (!is_ext_addr_set(iod->iod_loc)) {
        if (!is_put)
            pho_warn("Object has no address stored in database"
                     " (generating it from object id)");

        rc = pho_posix_set_addr(id, tag, iod->iod_loc->extent->addr_type,
                                &iod->iod_loc->extent->address);
        if (rc)
            return rc;
    }

    /* allocate io_ctx */
    io_ctx = alloc_posix_io_ctx();
    if (!io_ctx)
        return -ENOMEM;

    iod->iod_ctx = io_ctx;

    /* build full path */
    io_ctx->fpath = pho_posix_fullpath(iod->iod_loc);
    if (io_ctx->fpath == NULL) {
        rc = -EINVAL;
        pho_posix_close(iod);
        return rc;
    }

    pho_verb("extent location: '%s'", io_ctx->fpath);

    return is_put ? pho_posix_open_put(iod) : pho_posix_open_get(iod);
}

static int pho_posix_write(struct pho_io_descr *iod, const void *buf,
                           size_t count)
{
    int                  rc = 0;
    size_t               written_size = 0;
    int                  nb_null_try = 0;
    struct posix_io_ctx *io_ctx;

    io_ctx = iod->iod_ctx;

    /* write count bytes by taking care of partial write */
    while (written_size < count) {
        ssize_t nb_written_bytes;

        nb_written_bytes = write(io_ctx->fd, buf + written_size,
                                 count - written_size);
        if (nb_written_bytes < 0)
            LOG_RETURN(rc = -errno, "Failed to write into %s : %s",
                       io_ctx->fpath, strerror(errno));

        /* handle partial write */
        if (nb_written_bytes < count - written_size) {
            pho_warn("Incomplete write into '%s': %zu of %zu",
                     io_ctx->fpath, nb_written_bytes, count - written_size);
            if (nb_written_bytes == 0) {
                nb_null_try++;
                if (nb_null_try > MAX_NULL_WRITE_TRY)
                    LOG_RETURN(-EIO, "Too many writes of zero byte");
            }
        }

        written_size += nb_written_bytes;
    }

    return rc;
}

/**
 * Closing iod->iod_ctx->fd and in-depth freeing of the iod->iod_ctx .
 */
static int pho_posix_close(struct pho_io_descr *iod)
{
    int rc = 0;
    struct posix_io_ctx *io_ctx;

    io_ctx = iod->iod_ctx;

    if (!io_ctx)
        return 0;

    /* closing fd */
    if (io_ctx->fd >= 0) {
        if (close(io_ctx->fd)) {
            rc = -errno;
            pho_warn("Failed to close the file '%s': %s", io_ctx->fpath,
                     strerror(errno));
        }
    }

    /* free in-depth io_ctx */
    free(io_ctx->fpath);
    free(io_ctx);
    iod->iod_ctx = NULL;
    return rc;
}

/** POSIX adapter */
static const struct io_adapter posix_adapter = {
    .ioa_put            = pho_posix_put,
    .ioa_get            = pho_posix_get,
    .ioa_del            = pho_posix_del,
    .ioa_open           = pho_posix_open,
    .ioa_write          = pho_posix_write,
    .ioa_close          = pho_posix_close,
    .ioa_medium_sync    = pho_posix_medium_sync,
};

/** LTFS adapter with specialized flush function */
static const struct io_adapter ltfs_adapter = {
    .ioa_put            = pho_posix_put,
    .ioa_get            = pho_posix_get,
    .ioa_del            = pho_posix_del,
    .ioa_open           = pho_posix_open,
    .ioa_write          = pho_posix_write,
    .ioa_close          = pho_posix_close,
    .ioa_medium_sync    = pho_ltfs_sync,
};

/** retrieve IO functions for the given filesystem and addressing type */
int get_io_adapter(enum fs_type fstype, struct io_adapter *ioa)
{
    switch (fstype) {
    case PHO_FS_LTFS:
        *ioa = ltfs_adapter;
        break;
    case PHO_FS_POSIX:
        *ioa = posix_adapter;
        break;
    default:
        pho_error(-EINVAL, "Invalid FS type %#x", fstype);
        return -EINVAL;
    }

    return 0;
}
