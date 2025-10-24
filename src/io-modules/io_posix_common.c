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
 * \brief  Phobos I/O POSIX common adapter functions.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "io_posix_common.h"
#include "pho_attrs.h"
#include "pho_common.h"
#include "pho_io.h"
#include "pho_mapper.h"
#include "pho_cfg.h"

#include <attr/xattr.h>
#include <attr/attributes.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/sendfile.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <unistd.h>

#define MAX_NULL_WRITE_TRY 10
#define MAX_NULL_READ_TRY 10

/**
 * Return a new null initialized posix_io_ctx.
 *
 * To free this io_ctx, call pho_posix_close.
 */
static struct posix_io_ctx *alloc_posix_io_ctx(void)
{
    struct posix_io_ctx *io_ctx;

    io_ctx = xmalloc(sizeof(struct posix_io_ctx));
    io_ctx->fd = -1;
    io_ctx->fpath = NULL;

    return io_ctx;
}

/** build the full posix path from a pho_ext_loc structure */
static char *pho_posix_fullpath(const struct pho_ext_loc *loc)
{
    char *p;

    switch (loc->addr_type) {
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
static int pho_posix_make_parent_of(const char *root, const char *fullpath)
{
    const char *c;
    int root_len;
    char *last;
    char *tmp;
    int rc;

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
    tmp = xstrdup(fullpath);

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
        c = last + 1;
    }

    if (mkdir(tmp, 0750) != 0 && errno != EEXIST)
        LOG_GOTO(free_str, rc = -errno, "mkdir(%s) failed", tmp);

    rc = 0;

free_str:
    free(tmp);
    return rc;
}

/** allocate the desired path length, and call the path mapper */
int build_addr_path(const char *extent_key, const char *extent_desc,
                    struct pho_buff *addr)
{
    int rc;

    addr->size = strlen(extent_desc) + strlen(extent_key) + 1;
    /* don't exceed PATH_MAX in any case */
    if (addr->size > PATH_MAX)
        addr->size = PATH_MAX;

    addr->buff = xcalloc(1, addr->size);

    rc = pho_mapper_clean_path(extent_key, extent_desc, addr->buff, addr->size);
    if (rc) {
        free(addr->buff);
        addr->buff = NULL;
        addr->size = 0;
    }
    return rc;
}

/** allocate the desired path length, and call the hash-based mapper */
static int build_addr_hash1(const char *extent_key, const char *extent_desc,
                            struct pho_buff *addr)
{
    int rc;

    /* portable everywhere... even on windows */
    addr->size = NAME_MAX + 1;
    addr->buff = xcalloc(1, addr->size);

    rc = pho_mapper_hash1(extent_key, extent_desc, addr->buff, addr->size);
    if (rc) {
        free(addr->buff);
        addr->buff = NULL;
        addr->size = 0;
    }
    return rc;
}

/** set address field for a POSIX extent */
static int pho_posix_set_addr(const char *extent_key, const char *extent_desc,
                              enum address_type addrtype, struct pho_buff *addr)
{
    switch (addrtype) {
    case PHO_ADDR_PATH:
        return build_addr_path(extent_key, extent_desc, addr);
    case PHO_ADDR_HASH1:
        return build_addr_hash1(extent_key, extent_desc, addr);
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
char *full_xattr_name(const char *name)
{
    char *tmp_name;
    int len;

    len = asprintf(&tmp_name, "%s%s", POSIX_XATTR_PREFIX, name);
    if (len == -1)
        return NULL;

    return tmp_name;
}


/** set an extended attribute (or remove it if value is NULL) */
static int pho_setxattr(const char *path, int fd, const char *name,
                        const char *value, int flags)
{
    char *tmp_name;
    int rc = 0;

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
 * \param[in]   path    Full path to the extent.
 * \param[in]   fd      File descriptor, if specified uses it over the path
 *                      to get xattrs.
 * \param[in]   name    Name of the extended attribute without the "user."
 *                      prefix.
 * \param[out]  value   This buffer is allocated by the call if the attribute
 *                      exists. It must be freed by the caller. It is set to
 *                      NULL if the attribute doesn't exist.
 *
 * \return              0 on success,
 *                      -errno on failure.
 */
int pho_getxattr(const char *path, int fd, const char *name, char **value)
{
    char *tmp_name;
    char *buff;
    int rc;

    ENTRY;

    if (fd < 0 && path == NULL)
        return -EINVAL;

    if (value == NULL)
        return -EINVAL;

    *value = NULL;

    if (name == NULL || name[0] == '\0')
        return -EINVAL;

    tmp_name = full_xattr_name(name);
    if (tmp_name == NULL)
        return -ENOMEM;

    buff = xcalloc(1, ATTR_MAX_VALUELEN);

    if (fd < 0)
        rc = getxattr(path, tmp_name, buff, ATTR_MAX_VALUELEN);
    else
        rc = fgetxattr(fd, tmp_name, buff, ATTR_MAX_VALUELEN);

    if (rc <= 0) {
        if (errno == ENODATA || rc == 0)
            GOTO(free_buff, rc = 0);

        LOG_GOTO(free_buff, rc = -errno, "getxattr failed");
    }

    pho_debug("'%s' = '%s'", tmp_name, buff);

    *value = xstrndup(buff, rc);
    rc = 0;

free_buff:
    free(buff);
    free(tmp_name);
    return rc;
}

struct md_iter_sx {
    const char *mis_path;
    int mis_flags;
    int mis_fd;
};

static int setxattr_cb(const char *key, const char *value, void *udata)
{
    struct md_iter_sx *arg = (struct md_iter_sx *)udata;

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
    struct md_iter_sx args;

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
    struct pho_attrs *mig_attrs;
    const char *mig_path;
    int mig_fd;
};

static int getxattr_cb(const char *key, const char *value, void *udata)
{
    struct md_iter_gx *arg = (struct md_iter_gx *)udata;
    char *tmp_val;
    int rc;

    rc = pho_getxattr(arg->mig_path, arg->mig_fd, key, &tmp_val);
    if (rc != 0)
        return rc;

    pho_attr_set(arg->mig_attrs, key, tmp_val);

    return 0;
}

static int pho_posix_md_get(const char *path, int fd, struct pho_attrs *attrs)
{
    struct md_iter_gx args;
    int rc;

    ENTRY;

    args.mig_path = path;
    args.mig_attrs = attrs;
    args.mig_fd = fd;

    rc = pho_attrs_foreach(attrs, getxattr_cb, &args);
    if (rc != 0)
        pho_attrs_free(attrs);

    return rc;
}

int pho_posix_get(const char *extent_desc, struct pho_io_descr *iod)
{
    bool already_opened = (iod->iod_ctx != NULL);
    struct posix_io_ctx *io_ctx;
    int rc2 = 0;
    int rc = 0;

    ENTRY;

    if (!already_opened) {
        rc = pho_posix_open(extent_desc, iod, false);
        if (rc || iod->iod_flags & PHO_IO_MD_ONLY)
            return rc;
    }

    io_ctx = (struct posix_io_ctx *) iod->iod_ctx;

    /** If size is not stored in the DB, use the extent size */
    if (iod->iod_size == 0) {
        struct stat st;

        if (fstat(io_ctx->fd, &st) != 0)
            LOG_GOTO(clean, rc = -errno, "failed to stat %s", io_ctx->fpath);

        pho_warn("Extent size is not set in DB: using physical extent size: "
                 "%ju bytes", st.st_size);
        iod->iod_size = st.st_size;
    }

    /* read the extent */
    rc = pho_posix_sendfile(iod->iod_fd, io_ctx->fd, iod->iod_size);
    if (rc)
        goto clean;

    if (iod->iod_flags & PHO_IO_NO_REUSE) {
        /* release source file from system cache */
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

    if (!already_opened) {
        rc2 = pho_posix_close(iod);
        if (rc2 && rc == 0) /* keep the first reported error */
            rc = rc2;
    }

    return rc;
}

int pho_posix_del(struct pho_io_descr *iod)
{
    char *path;
    int rc = 0;

    ENTRY;

    if (iod->iod_loc->extent->address.buff == NULL)
        LOG_RETURN(-EINVAL, "Object has no address stored in database");

    path = pho_posix_fullpath(iod->iod_loc);
    if (path == NULL)
        return -EINVAL;

    if (unlink(path) != 0)
        rc = -errno;

    free(path);
    return rc;
}

static int pho_posix_open_put(struct pho_io_descr *iod)
{
    struct posix_io_ctx *io_ctx;
    bool file_created = false;
    bool file_existed = true;
    int flags;
    int rc;

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

    io_ctx->fd = open(io_ctx->fpath, flags | O_WRONLY, 0660);
    if (io_ctx->fd < 0 && errno == ENOENT) {
        file_existed = false;
        io_ctx->fd = open(io_ctx->fpath, flags | O_CREAT | O_WRONLY, 0660);
    }
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
        if (unlink(io_ctx->fpath)) {
            if (!rc) /* keep the first reported error */
                rc = -errno;

            pho_warn("Failed to clean extent '%s': %s", io_ctx->fpath,
                     strerror(-rc));
        }
    }

    /* error cleaning by closing io_ctx */
    pho_posix_close(iod);
    return rc;
}

static int pho_posix_open_get(struct pho_io_descr *iod)
{
    struct posix_io_ctx *io_ctx;
    int rc;

    io_ctx = iod->iod_ctx;

    /* get entry MD, if requested */
    rc = pho_posix_md_get(io_ctx->fpath, iod->iod_fd, &iod->iod_attrs);
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
int pho_posix_open(const char *extent_desc, struct pho_io_descr *iod,
                   bool is_put)
{
    struct extent *extent = iod->iod_loc->extent;
    struct posix_io_ctx *io_ctx;
    int rc = 0;

    ENTRY;

    /* generate entry address, if it is not already set */
    if (!is_ext_addr_set(iod->iod_loc)) {
        if (!is_put)
            LOG_RETURN(-EINVAL, "Object has no address stored in database");

        rc = pho_posix_set_addr(extent->uuid, extent_desc,
                                iod->iod_loc->addr_type,
                                &iod->iod_loc->extent->address);
        if (rc)
            return rc;
    }

    /* allocate io_ctx */
    io_ctx = alloc_posix_io_ctx();
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

static int path_from_fd(int fd, char **path)
{
    struct stat stat;
    char *fd_path;
    int rc;

    rc = asprintf(&fd_path, "/proc/self/fd/%d", fd);
    if (rc == -1)
        return -errno;

    rc = lstat(fd_path, &stat);
    if (rc == -1)
        GOTO(free_fd_path, rc = -errno);

    *path = xmalloc(stat.st_size + 1);

    rc = readlink(fd_path, *path, stat.st_size);
    if (rc == -1)
        GOTO(free_path, rc = -errno);

    (*path)[rc] = '\0';
    rc = 0;
    goto free_fd_path;

free_path:
    free(*path);
free_fd_path:
    free(fd_path);

    return rc;
}

int pho_posix_iod_from_fd(struct pho_io_descr *iod, int fd)
{
    struct posix_io_ctx *io_ctx;
    int rc;

    iod->iod_flags = 0;
    iod->iod_size = 0;
    iod->iod_loc = NULL;
    iod->iod_fd = -1;

    io_ctx = alloc_posix_io_ctx();
    iod->iod_ctx = io_ctx;

    io_ctx->fd = fd;
    rc = path_from_fd(fd, &io_ctx->fpath);
    if (rc)
        return rc;

    return 0;
}

int pho_posix_set_md(const char *extent_desc,
                     struct pho_io_descr *iod)
{
    struct posix_io_ctx *io_ctx = iod->iod_ctx;
    int rc = 0;

    if (io_ctx == NULL || io_ctx->fd == -1) {
        /**
         * Call pho_posix_open after setting the flag to PHO_IO_MD_ONLY
         * to set the xattr without actually opening the file, meaning
         * we don't need a 'pho_posix_close' statement.
         */
        iod->iod_flags = PHO_IO_MD_ONLY;
        rc = pho_posix_open(extent_desc, iod, true);
    } else {
        rc = pho_posix_md_fset(io_ctx->fd, &iod->iod_attrs, iod->iod_flags);
    }

    return rc;
}

int pho_posix_write(struct pho_io_descr *iod, const void *buf, size_t count)
{
    struct posix_io_ctx *io_ctx;
    size_t written_size = 0;
    int nb_null_try = 0;
    int rc = 0;

    io_ctx = iod->iod_ctx;

    /* write count bytes by taking care of partial write */
    while (written_size < count) {
        ssize_t nb_written_bytes;

        nb_written_bytes = write(io_ctx->fd, buf + written_size,
                                 count - written_size);
        if (nb_written_bytes < 0)
            LOG_RETURN(rc = -errno, "Failed to write into %s", io_ctx->fpath);

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

ssize_t pho_posix_read(struct pho_io_descr *iod, void *buf, size_t count)
{
    struct posix_io_ctx *io_ctx;
    ssize_t nb_read_bytes = 0;
    int nb_null_try = 0;

    io_ctx = iod->iod_ctx;

    while (count > 0) {
        ssize_t rc;

        rc = read(io_ctx->fd, buf, count);
        if (rc < 0)
            LOG_RETURN(nb_read_bytes = -errno, "Failed to read from '%s'",
                       io_ctx->fpath);

        if (rc == 0) {
            pho_verb("Read of zero byte from '%s', %zu are still missing",
                     io_ctx->fpath, count);
            ++nb_null_try;
            if (nb_null_try > MAX_NULL_READ_TRY) {
                pho_info("Too many reads of zero byte");
                break;
            }
        }

        nb_read_bytes += rc;
        count -= rc;
    }

    return nb_read_bytes;
}

/**
 * Closing iod->iod_ctx->fd and in-depth freeing of the iod->iod_ctx .
 */
int pho_posix_close(struct pho_io_descr *iod)
{
    struct posix_io_ctx *io_ctx;
    int rc = 0;

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

ssize_t pho_posix_preferred_io_size(struct pho_io_descr *iod)
{
    struct posix_io_ctx *io_ctx;
    struct statfs sfs;

    io_ctx = iod->iod_ctx;
    if (!io_ctx || io_ctx->fd < 0)
        return -EINVAL;

    if (fstatfs(io_ctx->fd, &sfs) != 0)
        return -errno;

    pho_debug("prefered I/O size %ld", sfs.f_bsize);
    return sfs.f_bsize;
}

int pho_get_common_xattrs_from_extent(struct pho_io_descr *iod,
                                      struct layout_info *lyt_info,
                                      struct extent *extent_to_insert,
                                      struct object_info *obj_info)
{
    const char *tmp_extent_offset;
    const char *tmp_object_size;
    const char *tmp_object_uuid;
    const char *tmp_layout_name;
    const char *tmp_user_md;
    const char *tmp_version;
    struct module_desc mod;
    const char *tmp_copy;
    struct pho_attrs md;
    int object_size;
    char *filename;
    char *tmp_uuid;
    char *tmp_oid;
    int version;
    char *uuid;
    char *oid;
    int rc;

    filename = xstrdup(iod->iod_loc->extent->address.buff);

    tmp_oid = filename;
    tmp_uuid = strrchr(filename, '.');
    if (tmp_uuid == NULL)
        LOG_GOTO(free_filename, rc = -EINVAL,
                 "Failed to read uuid from filename '%s'", filename);

    *tmp_uuid = 0;
    tmp_uuid += 1;
    if (strlen(tmp_uuid) != (UUID_LEN - 1))
        LOG_GOTO(free_filename, rc = -EINVAL,
                 "Uuid is not of correct length in filename '%s': expected '%d', length found '%lu'",
                 filename, (UUID_LEN - 1), strlen(tmp_uuid));

    oid = xstrdup(tmp_oid);
    uuid = xstrdup(tmp_uuid);

    lyt_info->oid = oid;
    obj_info->oid = oid;
    extent_to_insert->uuid = uuid;

    md.attr_set = NULL;
    pho_attr_set(&md, PHO_EA_OBJECT_UUID_NAME, NULL);
    pho_attr_set(&md, PHO_EA_OBJECT_SIZE_NAME, NULL);
    pho_attr_set(&md, PHO_EA_VERSION_NAME, NULL);
    pho_attr_set(&md, PHO_EA_LAYOUT_NAME, NULL);
    pho_attr_set(&md, PHO_EA_UMD_NAME, NULL);
    pho_attr_set(&md, PHO_EA_MD5_NAME, NULL);
    pho_attr_set(&md, PHO_EA_XXH128_NAME, NULL);
    pho_attr_set(&md, PHO_EA_EXTENT_OFFSET_NAME, NULL);
    pho_attr_set(&md, PHO_EA_COPY_NAME, NULL);

    rc = pho_posix_md_get(NULL, iod->iod_fd, &md);
    if (rc)
        LOG_GOTO(free_oid_uuid, rc,
                 "Failed to read extended attributes of file '%s'", filename);

    tmp_object_size = pho_attr_get(&md, PHO_EA_OBJECT_SIZE_NAME);
    if (tmp_object_size == NULL)
        LOG_GOTO(free_oid_uuid, rc = -EINVAL,
                 "Failed to retrieve object size of file '%s'", filename);

    object_size = str2int64(tmp_object_size);
    if (object_size < 0)
        LOG_GOTO(free_oid_uuid, rc = -EINVAL,
                 "Invalid object size found on '%s': '%d'",
                 filename, object_size);

    tmp_version = pho_attr_get(&md, PHO_EA_VERSION_NAME);
    if (tmp_version == NULL)
        LOG_GOTO(free_oid_uuid, rc = -EINVAL,
                 "Failed to retrieve object version of file '%s'", filename);

    version = str2int64(tmp_version);
    if (version <= 0)
        LOG_GOTO(free_oid_uuid, rc = -EINVAL,
                 "Invalid object version found on '%s': '%d'",
                 filename, version);

    tmp_user_md = pho_attr_get(&md, PHO_EA_UMD_NAME);
    if (tmp_user_md == NULL)
        LOG_GOTO(free_oid_uuid, rc = -EINVAL,
                 "Failed to retrieve object uuid of file '%s'", filename);

    tmp_object_uuid = pho_attr_get(&md, PHO_EA_OBJECT_UUID_NAME);
    if (tmp_object_uuid == NULL)
        LOG_GOTO(free_oid_uuid, rc = -EINVAL,
                 "Failed to retrieve object uuid of file '%s'", filename);

    tmp_layout_name = pho_attr_get(&md, PHO_EA_LAYOUT_NAME);
    if (tmp_layout_name == NULL)
        LOG_GOTO(free_oid_uuid, rc = -EINVAL,
                 "Failed to retrieve layout name of file '%s'",
                 iod->iod_loc->extent->address.buff);

    tmp_extent_offset = pho_attr_get(&md, PHO_EA_EXTENT_OFFSET_NAME);
    if (tmp_extent_offset == NULL)
        LOG_GOTO(free_oid_uuid, rc = -EINVAL,
                 "Failed to retrieve layout name of file '%s'",
                 iod->iod_loc->extent->address.buff);

    extent_to_insert->offset = str2int64(tmp_extent_offset);
    if (extent_to_insert->offset < 0)
        LOG_GOTO(free_oid_uuid, rc = -EINVAL,
                 "Invalid extent offset found on '%s': '%ld'",
                 filename, extent_to_insert->offset);

    tmp_copy = pho_attr_get(&md, PHO_EA_COPY_NAME);
    if (tmp_copy == NULL) {
        rc = get_cfg_default_copy_name(&tmp_copy);
        if (rc)
            LOG_GOTO(free_oid_uuid, rc = -EINVAL,
                     "Failed to retrieve copy name of file '%s'",
                     filename);
    }

    obj_info->size = object_size;

    lyt_info->version = version;
    obj_info->version = version;

    lyt_info->uuid = xstrdup(tmp_object_uuid);
    obj_info->uuid = xstrdup(tmp_object_uuid);

    lyt_info->copy_name = xstrdup(tmp_copy);

    mod.mod_name = xstrdup(tmp_layout_name);

    mod.mod_major = 0;
    mod.mod_minor = 2;
    pho_attr_remove(&md, PHO_EA_LAYOUT_NAME);

    obj_info->user_md = xstrdup(tmp_user_md);

    pho_attrs_remove_null(&md);

    mod.mod_attrs = md;
    lyt_info->layout_desc = mod;

    free(filename);

    return 0;

free_oid_uuid:
    free(oid);
    free(uuid);

free_filename:
    free(filename);

    return rc;
}

ssize_t pho_posix_size(struct pho_io_descr *iod)
{
    struct posix_io_ctx *io_ctx;
    struct stat statbuf;

    ENTRY;

    io_ctx = iod->iod_ctx;
    if (!io_ctx || io_ctx->fd < 0)
        return -EINVAL;

    if (fstat(io_ctx->fd, &statbuf) != 0)
        return -errno;

    return statbuf.st_size;
}
