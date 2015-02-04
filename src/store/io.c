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
    char *c, *tmp, *last;
    int rc;
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

/** sendfile wrapper */
static int pho_posix_sendfile(int tgt_fd, int src_fd, off_t *src_offset,
                              size_t count)
{
    ssize_t rw;
    off_t offsave = *src_offset;
    ENTRY;

    rw = sendfile(tgt_fd, src_fd, src_offset, count);

    /* @TODO fallback to read/write */

    if (rw < 0)
        return -errno;
    else if (rw < count) {
        pho_verb("sendfile returned after copying %llu bytes. %llu bytes left.",
                 (unsigned long long)rw, (unsigned long long)(count - rw));
        /* check offset value */
        if (*src_offset != offsave + rw)
            LOG_RETURN(-EIO, "inconsistent src_offset value "
                       "(%llu != %llu + %llu)",
                       (unsigned long long)*src_offset,
                       (unsigned long long)offsave,
                       (unsigned long long)rw);
        else
            /* copy missing data */
            return pho_posix_sendfile(tgt_fd, src_fd, src_offset, count - rw);
    } else if (rw != count)
        LOG_RETURN(-EIO, "inconsistent byte count %llu > %llu",
                   (unsigned long long)rw, (unsigned long long)count);
    /* rw == count */
    return 0; /* all data read/written */
}

static int pho_flags2open(int pho_io_flags)
{
    /* no replace => O_EXCL */
    if (!(pho_io_flags & PHO_IO_REPLACE))
        return O_EXCL;
    return 0;
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

    len = strlen(POSIX_XATTR_PREFIX) + strlen(name);

    tmp_name = calloc(1, len + 1);
    if (tmp_name == NULL)
        return NULL;

    snprintf(tmp_name, len + 1, "%s%s", POSIX_XATTR_PREFIX, name);
    return tmp_name;
}


/** set an extended attribute (or remove it if value is NULL) */
static int pho_setxattr(const char *path, int fd, const char *name,
                        const char *value, int flags)
{
    char *tmp_name;
    int   rc = 0,
          len;
    ENTRY;

    if (name == NULL || name[0] == '\0')
        return -EINVAL;

    len = strlen(POSIX_XATTR_PREFIX) + strlen(name);

    tmp_name = full_xattr_name(name);
    if (tmp_name == NULL)
        return -ENOMEM;

    if (value != NULL) {
        if (fd != -1)
            rc = fsetxattr(fd, tmp_name, value, strlen(value)+1, flags);
        else
            rc = setxattr(path, tmp_name, value, strlen(value)+1, flags);

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
        else
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



/** Set entry metadata as extended attributes.
 * fd or path must be specified.
 */
static int _pho_posix_set_md(const char *path, int fd, const char **md_keys,
                            const char **md_values, int md_count,
                            int pho_io_flags)
{
    int i, rc, flags = 0;
    ENTRY;

    if (pho_io_flags & PHO_IO_REPLACE)
        flags = 0;
    else /* pure create: fails if the attribute already exists */
        flags |= XATTR_CREATE;

    for (i = 0; i < md_count; i++) {
        rc = pho_setxattr(path, fd, md_keys[i], md_values[i], flags);
        if (rc)
            return rc;
    }
    return 0;
}

#define pho_posix_set_md_by_fd(_fd, args...) \
            _pho_posix_set_md(NULL, (_fd), args)
#define pho_posix_set_md_by_path(_p, args...) \
            _pho_posix_set_md((_p), -1, args)

static int pho_posix_get_md(const char *path, const char **md_keys,
                            char **md_values, int md_count)
{
    int i, rc;
    ENTRY;

    for (i = 0; i < md_count; i++) {
        rc = pho_getxattr(path, md_keys[i], &md_values[i]);
        if (rc != 0) {
            int j;

            /* free previously allocated values */
            for (j = 0; j < i; j++) {
                free(md_values[j]);
                md_values[j] = NULL;
            }
            return rc;
        }
    }
    return 0;
}

static int pho_posix_put(const char *id, const char *tag,
                   int src_fd, off_t src_off, size_t size, struct data_loc *loc,
                   const char **md_keys, const char **md_values,
                   int md_count, int pho_io_flags,
                   io_callback_t io_cb, void *user_data)
{
    int      rc, tgt_fd, flags;
    GString *fpath;
    ENTRY;

    /* XXX asynchronous PUT is not supported for now */
    if (io_cb != NULL)
        return -ENOTSUP;

    /* generate entry address, if it is not already set */
    if (loc->extent.address.buff == NULL) {
        rc = pho_posix_set_addr(id, tag, loc->extent.addr_type,
                                &loc->extent.address);
        if (rc)
            return rc;
    }

    fpath = pho_posix_fullpath(loc);
    if (fpath == NULL)
        return -EINVAL;

    /* if the call is MD_ONLY, it is expected the entry exists. */
    if (pho_io_flags & PHO_IO_MD_ONLY) {
        rc = pho_posix_set_md_by_path(fpath->str, md_keys, md_values, md_count,
                                      /* propagate SYNC options */
                                      pho_io_flags);
        goto free_path;
    }

    /* mkdir -p */
    rc = pho_posix_make_parent_of(loc->root_path, fpath);
    if (rc)
        goto free_path;

    flags = pho_flags2open(pho_io_flags);

    tgt_fd = open(fpath->str, flags | O_CREAT | O_WRONLY, 0640);
    if (tgt_fd < 0)
        LOG_GOTO(free_path, rc = -errno, "open(%s) for write failed",
                 fpath->str);

    /* set metadata */
    rc = pho_posix_set_md_by_fd(tgt_fd, md_keys, md_values, md_count,
                                /* don't propagate SYNC options,
                                 * just replace option */
                                pho_io_flags & PHO_IO_REPLACE);
    if (rc)
        goto close_tgt;

    /* write data */
    rc = pho_posix_sendfile(tgt_fd, src_fd, &src_off, size);
    if (rc)
        goto close_tgt;

    /* flush data */
    if (pho_io_flags & PHO_IO_SYNC_FILE)
        if (fsync(tgt_fd) != 0)
            LOG_GOTO(close_tgt, rc = -errno, "fsync failed");

    if (pho_io_flags & PHO_IO_NO_REUSE) {
        rc = -posix_fadvise(tgt_fd, 0, 0,
                            POSIX_FADV_DONTNEED | POSIX_FADV_NOREUSE);
        if (rc != 0) {
            pho_warn("posix_fadvise failed: %s (%d)", strerror(-rc), rc);
            /* ignore */
            rc = 0;
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
                   int tgt_fd, size_t size, struct data_loc *loc,
                   const char **md_keys, char **md_values,
                   int md_count, int pho_io_flags,
                   io_callback_t io_cb, void *user_data)
{
    int      rc, src_fd, i;
    GString *fpath;
    off_t src_off = 0; /* always read the whole extent */
    ENTRY;

    /* XXX asynchronous PUT is not supported for now */
    if (io_cb != NULL)
        return -ENOTSUP;

    /* generate entry address, if it is not already set */
    if (loc->extent.address.buff == NULL) {
        pho_warn("Object has no address stored in database"
                 " (generating it from object id)");
        rc = pho_posix_set_addr(id, tag, loc->extent.addr_type,
                                &loc->extent.address);
        if (rc)
            return rc;
    }

    fpath = pho_posix_fullpath(loc);
    if (fpath == NULL)
        return -EINVAL;

    /* get entry MD, if requested */
    rc = pho_posix_get_md(fpath->str, md_keys, md_values, md_count);
    if (rc != 0 || (pho_io_flags & PHO_IO_MD_ONLY))
        goto free_path;

    /* open the extent */
    src_fd = open(fpath->str, O_RDONLY);
    if (src_fd < 0)
        LOG_GOTO(free_attrs, rc = -errno, "open(%s) for read failed",
                 fpath->str);

    /** If size is not stored in the DB, use the extent size */
    if (size == 0) {
        struct stat st;

        if (fstat(src_fd, &st) != 0)
            LOG_GOTO(free_attrs, rc = -errno, "failed to stat %s", fpath->str);

        pho_warn("Extent size is not set in DB: using physical extent size: "
                 "%llu bytes", st.st_size);
        size = st.st_size;
    }

    /* read the extent */
    rc = pho_posix_sendfile(tgt_fd, src_fd, &src_off, size);
    if (rc)
        goto close_src;

    if (pho_io_flags & PHO_IO_NO_REUSE) {
        /*  release source file from system cache */
        rc = -posix_fadvise(src_fd, 0, 0,
                            POSIX_FADV_DONTNEED | POSIX_FADV_NOREUSE);
        if (rc != 0) {
            pho_warn("posix_fadvise failed: %s (%d)", strerror(-rc), rc);
            /* ignore */
            rc = 0;
        }
    }

close_src:
    if (close(src_fd) != 0)
        /* we could read the data, just warn */
        pho_warn("Failed to close source file: %s (%d)", strerror(errno),
                 errno);
free_attrs:
    if (rc != 0)
        for (i = 0; i < md_count; i++) {
            free(md_values[i]);
            md_values[i] = NULL;
        }
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
        return -EINVAL;
    }

    return 0;
}
