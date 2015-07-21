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


/** build the full posix path from a data_loc structure */
static GString *pho_posix_fullpath(const struct data_loc *loc)
{
    GString *p = NULL;

    switch (loc->extent.addr_type) {
    case PHO_ADDR_PATH:
    case PHO_ADDR_HASH1:
        if (loc->extent.address.buff == NULL)
            return NULL;
        p = g_string_new(loc->root_path->str);
        g_string_append_printf(p, "/%s", loc->extent.address.buff);
        return p;
    default:
        return NULL;
    }
}

/** create directory levels from <root>/<lvl1> to dirname(fullpath) */
static int pho_posix_make_parent_of(const GString *root,
                                    const GString *fullpath)
{
    char    *c, *tmp, *last;
    int      rc;
    ENTRY;

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
 * @TODO fallback to (p)read/(p)write */
static int pho_posix_sendfile(int tgt_fd, int src_fd, off_t *src_offset,
                              size_t count)
{
    ssize_t rw = 0;
    off_t   offsave = *src_offset;
    ENTRY;

    while (count > 0) {
        rw = sendfile(tgt_fd, src_fd, src_offset, count);
        if (rw < 0)
            LOG_RETURN(-errno, "sendfile failure");

        pho_debug("sendfile returned after copying %zd bytes. %zd bytes left",
                  rw, count - rw);

        /* check offset value */
        if (*src_offset != offsave + rw)
            LOG_RETURN(-EIO, "inconsistent src_offset value (%jd != %jd + %zd",
                       (intmax_t)*src_offset, (intmax_t)offsave, rw);
        count -= rw;
    }

    return 0;
}

static int pho_flags2open(int pho_io_flags)
{
    int flags = 0;

    /* no replace => O_EXCL */
    if (!(pho_io_flags & PHO_IO_REPLACE))
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
            if (errno == ENOATTR)
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
        if (errno == ENOATTR || rc == 0)
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
                             const struct pho_attrs *attrs, int pho_io_flags)
{
    struct md_iter_sx  args;
    ENTRY;

    /* Specify one and only one of path or fd */
    assert((path == NULL) != (fd == -1));

    args.mis_path  = path;
    args.mis_fd    = fd;
    /* pure create: fails if the attribute already exists */
    args.mis_flags = (pho_io_flags & PHO_IO_REPLACE) ? 0 : XATTR_CREATE;

    return pho_attrs_foreach(attrs, setxattr_cb, &args);
}

static inline int pho_posix_md_fset(int fd, const struct pho_attrs *attrs,
                                    int flags)
{
    return _pho_posix_md_set(NULL, fd, attrs, flags);
}

static inline int pho_posix_md_set(const char *path,
                                   const struct pho_attrs *attrs, int flags)
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
                         struct pho_io_descr *iod,
                         io_callback_t io_cb, void *user_data)
{
    int      rc;
    int      tgt_fd;
    int      flags;
    GString *fpath;
    ENTRY;

    if (io_cb != NULL)
        LOG_RETURN(-ENOTSUP, "Asynchronous PUT operations not supported yet");

    /* generate entry address, if it is not already set */
    if (!is_data_loc_valid(iod->iod_loc)) {
        rc = pho_posix_set_addr(id, tag, iod->iod_loc->extent.addr_type,
                                &iod->iod_loc->extent.address);
        if (rc)
            return rc;
    }

    fpath = pho_posix_fullpath(iod->iod_loc);
    if (fpath == NULL)
        return -EINVAL;

    /* if the call is MD_ONLY, it is expected that the entry exists. */
    if (iod->iod_flags & PHO_IO_MD_ONLY) {
        /* pho_io_flags are passed in to propagate SYNC options */
        rc = pho_posix_md_set(fpath->str, &iod->iod_attrs, iod->iod_flags);
        goto free_path;
    }

    /* mkdir -p */
    rc = pho_posix_make_parent_of(iod->iod_loc->root_path, fpath);
    if (rc)
        goto free_path;

    flags = pho_flags2open(iod->iod_flags);
    tgt_fd = open(fpath->str, flags | O_CREAT | O_WRONLY, 0640);
    if (tgt_fd < 0)
        LOG_GOTO(free_path, rc = -errno, "open(%s) for write failed",
                 fpath->str);

    /* set metadata */
    /* Only propagate REPLACE option, if specified */
    rc = pho_posix_md_fset(tgt_fd, &iod->iod_attrs,
                           iod->iod_flags & PHO_IO_REPLACE);
    if (rc)
        goto close_tgt;

    /* write data */
    rc = pho_posix_sendfile(tgt_fd, iod->iod_fd, &iod->iod_off, iod->iod_size);
    if (rc)
        goto close_tgt;

    /* flush data */
    if (iod->iod_flags & PHO_IO_SYNC_FILE)
        if (fsync(tgt_fd) != 0)
            LOG_GOTO(close_tgt, rc = -errno, "fsync failed");

    if (iod->iod_flags & PHO_IO_NO_REUSE) {
        rc = posix_fadvise(tgt_fd, 0, 0,
                           POSIX_FADV_DONTNEED | POSIX_FADV_NOREUSE);
        if (rc) {
            pho_warn("posix_fadvise failed: %s (%d)", strerror(rc), rc);
            rc = 0; /* ignore error */
        }
    }

close_tgt:
    if (close(tgt_fd) && rc == 0) /* keep the first reported error */
        rc = -errno;
    /* clean the extent on failure */
    if (rc != 0 && unlink(fpath->str) != 0)
        pho_warn("failed to clean extent '%s': %s",
                 fpath, strerror(errno));

free_path:
    g_string_free(fpath, TRUE);
    return rc;
}

static int pho_posix_get(const char *id, const char *tag,
                         struct pho_io_descr *iod,
                         io_callback_t io_cb, void *user_data)
{
    int      rc;
    int      src_fd;
    GString *fpath;
    ENTRY;

    /* XXX asynchronous PUT is not supported for now */
    if (io_cb != NULL)
        return -ENOTSUP;

    /* Always read the whole extent */
    if (iod->iod_off != 0) {
        pho_warn("Partial get not supported, reading whole extent instead of"
                 " seeking to offset %lu", (unsigned int)iod->iod_off);
        iod->iod_off = 0;
    }

    /* generate entry address, if it is not already set */
    if (!is_data_loc_valid(iod->iod_loc)) {
        pho_warn("Object has no address stored in database"
                 " (generating it from object id)");
        rc = pho_posix_set_addr(id, tag, iod->iod_loc->extent.addr_type,
                                &iod->iod_loc->extent.address);
        if (rc != 0)
            return rc;
    }

    fpath = pho_posix_fullpath(iod->iod_loc);
    if (fpath == NULL)
        return -EINVAL;

    /* get entry MD, if requested */
    rc = pho_posix_md_get(fpath->str, &iod->iod_attrs);
    if (rc != 0 || (iod->iod_flags & PHO_IO_MD_ONLY))
        goto free_path;

    /* open the extent */
    src_fd = open(fpath->str, O_RDONLY);
    if (src_fd < 0)
        LOG_GOTO(free_attrs, rc = -errno, "open(%s) for read failed",
                 fpath->str);

    /** If size is not stored in the DB, use the extent size */
    if (iod->iod_size == 0) {
        struct stat st;

        if (fstat(src_fd, &st) != 0)
            LOG_GOTO(free_attrs, rc = -errno, "failed to stat %s", fpath->str);

        pho_warn("Extent size is not set in DB: using physical extent size: "
                 "%llu bytes", st.st_size);
        iod->iod_size = st.st_size;
    }

    /* read the extent */
    rc = pho_posix_sendfile(iod->iod_fd, src_fd, &iod->iod_off, iod->iod_size);
    if (rc)
        goto close_src;

    if (iod->iod_flags & PHO_IO_NO_REUSE) {
        /*  release source file from system cache */
        rc = posix_fadvise(src_fd, 0, 0,
                            POSIX_FADV_DONTNEED | POSIX_FADV_NOREUSE);
        if (rc) {
            pho_warn("posix_fadvise failed: %s (%d)", strerror(rc), rc);
            rc = 0; /* ignore error */
        }
    }

close_src:
    if (close(src_fd) != 0)
        /* we could read the data, just warn */
        pho_warn("Failed to close source file: %s (%d)", strerror(errno),
                 errno);
free_attrs:
    if (rc != 0)
        pho_attrs_free(&iod->iod_attrs);

free_path:
    g_string_free(fpath, TRUE);
    return rc;
}



static int pho_posix_sync(const struct data_loc *loc)
{
    ENTRY;
    sync();
    return 0;
}


#define LTFS_SYNC_ATTR_NAME "user.ltfs.sync"

static int pho_ltfs_sync(const struct data_loc *loc)
{
    int one = 1;
    ENTRY;

    /* flush the LTFS partition to tape */
    if (setxattr(loc->root_path->str, LTFS_SYNC_ATTR_NAME,
                 (void *)&one, sizeof(one), 0) != 0)
        LOG_RETURN(-errno, "failed to set LTFS special xattr "
                   LTFS_SYNC_ATTR_NAME);

    return 0;
}


static int pho_posix_del(const char *id, const char *tag, struct data_loc *loc)
{
    int rc = 0;
    GString *path;
    ENTRY;

    if (loc->extent.address.buff == NULL) {
        pho_warn("Object has no address stored in database"
                 " (generating it from object id)");
        rc = pho_posix_set_addr(id, tag, loc->extent.addr_type,
                                &loc->extent.address);
        if (rc)
            return rc;
    }

    path = pho_posix_fullpath(loc);
    if (path == NULL)
        return -EINVAL;

    if (!gstring_empty(path)
        && (unlink(path->str) != 0))
        rc = -errno;

    g_string_free(path, TRUE);
    return rc;
}

/** POSIX adapter */
static const struct io_adapter posix_adapter = {
    .ioa_put       = pho_posix_put,
    .ioa_get       = pho_posix_get,
    .ioa_del       = pho_posix_del,
    .ioa_flush     = pho_posix_sync,
};


bool io_adapter_is_valid(const struct io_adapter *ioa)
{
    if (ioa == NULL)
        return false;

    /* Does the IOA exposes the mandatory calls? */
    if (ioa->ioa_put == NULL    ||
        ioa->ioa_get == NULL   ||
        ioa->ioa_del == NULL)
        return false;

    return true;
}

/** retrieve IO functions for the given filesystem and addressing type */
int get_io_adapter(enum fs_type fstype, struct io_adapter *ioa)
{
    switch (fstype) {
    case PHO_FS_LTFS:
        *ioa = posix_adapter;
        ioa->ioa_flush = pho_ltfs_sync;
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
