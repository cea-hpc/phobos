/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos Object Store implementation
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "phobos_store.h"
#include "pho_common.h"
#include "pho_types.h"
#include "pho_attrs.h"
#include "pho_extents.h"
#include "pho_lrs.h"
#include "pho_io.h"
#include <sys/types.h>
#include <attr/xattr.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define PHO_ATTR_BACKUP_JSON_FLAGS (JSON_COMPACT | JSON_SORT_KEYS)

#define PHO_EA_ID_NAME  "id"
#define PHO_EA_UMD_NAME "user_md"
#define PHO_EA_NFO_NAME "ext_info"

/** attach metadata information to an extent */
static int extent_store_md(void *hdl, const struct io_adapter *ioa,
                           const char *id, const struct pho_attrs *md,
                           const struct layout_descr *lay,
                           struct data_loc *loc)
{
    int rc;
    GString *str = g_string_new("");

    /* store entry id */
    rc = ioa_fsetxattr(ioa, hdl, PHO_EA_ID_NAME, id, strlen(id) + 1,
                              XATTR_CREATE);
    if (rc)
        goto out_free;

    rc = pho_attrs_to_json(md, str, PHO_ATTR_BACKUP_JSON_FLAGS);
    if (rc)
        goto out_free;

    if (!gstring_empty(str)) {
        /* @TODO take flags into account */
        rc = ioa_fsetxattr(ioa, hdl, PHO_EA_UMD_NAME, str->str,
                                  str->len + 1, XATTR_CREATE);
        if (rc)
            goto out_free;
    }

    /* v00: the file has a single extent so we don't have to link it to
     * other extents. Just save basic layout and extent information. */
#if 0
    rc = storage_info_to_json(lay, &loc->extent, str,
                              PHO_ATTR_BACKUP_JSON_FLAGS);
    if (rc)
        goto out_free;

    if (!gstring_empty(str)) {
        rc = ioa_fsetxattr(ioa, hdl, PHO_EA_NFO, str, str->len + 1,
                                  XATTR_CREATE);
        if (rc)
            goto out_free;
    }
#endif
    rc = 0;

out_free:
    g_string_free(str, TRUE);
    return rc;
}

struct src_info {
    int         fd;
    struct stat st;
};

static int copy_standard_w(const struct src_info *src, void *tgt_hdl,
                           const struct io_adapter *ioa, size_t size)
{
    ssize_t      r, w, done;
    char        *io_buff = NULL;
    size_t       io_size;
    int          rc;
    struct stat  st;

    /* compute the optimal IO size */
    rc = ioa_fstat(ioa, tgt_hdl, &st);
    if (rc)
        LOG_RETURN(rc, "Failed to stat target file");

    io_size = max(src->st.st_blksize, st.st_blksize);

    if (size < io_size)
        io_size = size;

    rc = posix_memalign((void **)&io_buff, getpagesize(), io_size);
    if (rc)
        return -rc;

    done = 0;
    while ((r = read(src->fd, io_buff, io_size)) > 0) {
        w = 0;
        while (w < r) {
            if (ioa->ioa_write != NULL)
                rc = ioa_write(ioa, tgt_hdl, io_buff + w, r - w);
            else if (ioa->ioa_pwrite != NULL)
                rc = ioa_pwrite(ioa, tgt_hdl, io_buff + w, r - w,
                                       done + w);
            else
                rc = -EOPNOTSUPP;

            if (rc < 0)
                LOG_GOTO(out_free, rc = -errno, "write failed");

            w += rc;
        }
        done += w;
    }
    if (r < 0)
        rc = -errno;
    else
        rc = 0;

out_free:
    free(io_buff);
    return rc;
}

/** copy data from the src_fd to the given extent */
static int write_extents(const struct src_info *src, const char *obj_id,
                         const struct pho_attrs *md,
                         const struct layout_descr *lay,
                         struct data_loc *loc, int flags)
{
    struct io_adapter    ioa;
    char                 tag[PHO_LAYOUT_TAG_MAX] = "";
    void                *hdl = NULL;
    off_t                offset = 0;
    int                  rc;

    /* get vector of functions to access the media */
    rc = get_io_adapter(loc->extent.fs_type, loc->extent.addr_type, &ioa);
    if (rc)
        return rc;

    if (!io_adapter_is_valid(&ioa))
        LOG_RETURN(-EINVAL, "Invalid I/O adapter, check implementation!");

    /* build extent tag from layout description */
    rc = layout2tag(lay, loc->extent.layout_idx, tag);
    if (rc)
        return rc;

    /* fill address field in extent info */
    rc = ioa_id2addr(&ioa, obj_id, tag[0] ? tag : NULL,
                            &loc->extent.address);
    if (rc)
        return rc;

    /* open the extent for writing */
    /* TODO take flags into account */
    rc = ioa_open(&ioa, loc, O_CREAT|O_WRONLY|O_EXCL, &hdl);
    if (rc)
        LOG_RETURN(rc, "failed to open target extent");

    /* store metadata in the extent, to be able to rebuild the database
     * if it is accidentally lost */
    rc = extent_store_md(hdl, &ioa, obj_id, md, lay, loc);
    if (rc)
        LOG_GOTO(clean_ext, rc, "failed to attach MD to the extent");

    /* First try using sendfile to copy the data and fall back to regular
     * write if not available. ioa_is_valid() has already checked
     * that at least one method is available. */
    rc = ioa_sendfile_w(&ioa, hdl, src->fd, &offset, src->st.st_size);
    if (rc == -EOPNOTSUPP)
        rc = copy_standard_w(src, hdl, &ioa, src->st.st_size);

    if (rc)
        LOG_GOTO(clean_ext, rc, "I/O failed");

    /* flush the data after single put */
    rc = ioa_close(&ioa, hdl, PHO_IO_SYNC_FILE);
    if (rc)
        pho_error(rc, "failed to sync extent data");

    return rc;

clean_ext:
    /* close & cleanup the extent on error */
    ioa_close(&ioa, hdl, 0);
    ioa_remove(&ioa, loc);
    ioa_sync(&ioa, loc, PHO_IO_SYNC_FS);
    return rc;
}

static int open_noatime(const char *path, int flags)
{
    int fd;

    fd = open(path, flags | O_NOATIME);
    /* not allowed to open with NOATIME arg, try without */
    if (fd < 0 && errno == EPERM)
        fd = open(path, flags & ~O_NOATIME);

    return fd;
}

/** Put a file to the object store
 * \param obj_id    unique arbitrary string to identify the object
 * \param src_file  file to put to the store
 * \param flags     behavior flags
 * \param md        user attribute set
 */
int phobos_put(const char *obj_id, const char *src_file, int flags,
               const struct pho_attrs *md)
{
    /* the only layout type we can handle for now */
    struct layout_descr simple_layout = {.type = PHO_LYT_SIMPLE};
    struct data_loc     write_loc = {0};
    struct src_info     info = {0};
    int                 rc;

    /* get the size of the source file and check its availability */
    info.fd = open_noatime(src_file, O_RDONLY);
    if (info.fd < 0)
        LOG_RETURN(rc = -errno, "open(%s) failed", src_file);

    if (fstat(info.fd, &info.st) != 0)
        LOG_RETURN(rc = -errno, "fstat(%s) failed", src_file);

    /* store object info in the DB (transient state) + check if it already
     * exists */
    /** @TODO   rc = obj_put_start(obj_id, md, flags_obj2md(flags)); */
    rc = 0;
    if (rc)
        LOG_GOTO(close_src, rc, "obj_put_start(%s) failed", obj_id);

    /* get storage resource to write the object */
    rc = lrs_write_intent(info.st.st_size, &simple_layout, &write_loc);
    if (rc)
        LOG_GOTO(out_clean_obj, rc, "failed to get storage resource to write "
                 "%zu bytes", info.st.st_size);

    /* set extent info in DB (transient state) */
    /** @TODO    rc = extent_put_start(obj_id, &simple_layout, &write_loc,
                             flags_obj2md(flags)); */
    if (rc)
        LOG_GOTO(out_lrs_end, rc, "couln't save extents info");

    /* write data to the media */
    rc = write_extents(&info, obj_id, md, &simple_layout, &write_loc,
                       flags);
    if (rc)
        LOG_GOTO(out_clean_db_ext, rc, "failed to write extents");

    close(info.fd);

    /* complete DB info (object status & extents) */
    /** @TODO   rc = obj_put_done(obj_id, &simple_layout, &write_loc); */
    if (rc)
        LOG_GOTO(out_clean_ext, rc, "obj_put_done(%s) failed", obj_id);

    /* release storage resources + update device/media info */
    lrs_done(&write_loc);
    /* don't care about the error here, the object has been saved successfully
     * and the LRS error should have been logged by lower layers.
     */
    /** @TODO release write_loc structure memory */

    pho_info("put complete: %s -> %s", src_file, obj_id);
    return 0;

    /* cleaning after error cases */
out_clean_ext:
/** @TODO    clean_extents(obj_id, &simple_layout, &write_loc); */
    ;
out_clean_db_ext:
/** @TODO    extent_put_abort(obj_id, &simple_layout, &write_loc); */
    ;
out_lrs_end:
    /* release resource reservations */
    lrs_done(&write_loc);
out_clean_obj:
    /** @TODO    obj_put_abort(obj_id); */
    ;
close_src:
    close(info.fd);
    return rc;
}
