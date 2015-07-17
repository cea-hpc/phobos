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
#include "pho_type_utils.h"
#include "pho_lrs.h"
#include "pho_dss.h"
#include "pho_io.h"
#include "pho_cfg.h"

#include <sys/types.h>
#include <attr/xattr.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define PHO_ATTR_BACKUP_JSON_FLAGS (JSON_COMPACT | JSON_SORT_KEYS)

#define PHO_EA_ID_NAME      "id"
#define PHO_EA_UMD_NAME     "user_md"
#define PHO_EA_EXT_NAME     "ext_info"

/** fill an array with metadata blobs to be stored on the media */
static int build_extent_md(const char *id, const struct pho_attrs *md,
                           const struct layout_descr *lay, struct data_loc *loc,
                           struct pho_attrs *dst_md)
{
    GString *str;
    int      rc;

    rc = pho_attr_set(dst_md, PHO_EA_ID_NAME, id);
    if (rc)
        return rc;

    str = g_string_new("");
    rc = pho_attrs_to_json(md, str, PHO_ATTR_BACKUP_JSON_FLAGS);
    if (rc != 0)
        goto free_values;

    if (!gstring_empty(str)) {
        rc = pho_attr_set(dst_md, PHO_EA_UMD_NAME, str->str);
        if (rc != 0)
            goto free_values;
    }

    /* v00: the file has a single extent so we don't have to link it to
     * other extents. Just save basic layout and extent information. */
#if 0
    rc = storage_info_to_json(lay, &loc->extent, str,
                              PHO_ATTR_BACKUP_JSON_FLAGS);
    if (rc != 0)
        goto free_values;
#endif

free_values:
    if (rc != 0)
        pho_attrs_free(dst_md);

    g_string_free(str, TRUE);
    return rc;
}

struct src_info {
    int         fd;
    struct stat st;
};

static inline int obj2io_flags(int flags)
{
    if (flags & PHO_OBJ_REPLACE)
        return PHO_IO_REPLACE;
    else
        return 0;
}


/** copy data from the source fd to the extent identified by loc */
static int write_extents(const struct src_info *src, const char *obj_id,
                         const struct pho_attrs *md,
                         const struct layout_descr *lay,
                         struct data_loc *loc, int flags)
{
    char                tag[PHO_LAYOUT_TAG_MAX] = "";
    struct io_adapter   ioa;
    struct pho_io_descr iod;
    int                 rc;

    /* Get ready for upcoming NULL checks */
    memset(&iod, 0, sizeof(iod));

    /* get vector of functions to access the media */
    rc = get_io_adapter(loc->extent.fs_type, &ioa);
    if (rc)
        return rc;

    if (!io_adapter_is_valid(&ioa))
        LOG_RETURN(-EINVAL, "Invalid I/O adapter, check implementation!");

    /* build extent tag from layout description */
    rc = layout2tag(lay, loc->extent.layout_idx, tag);
    if (rc)
        return rc;

    /* single PUT: flush the data to disk */
    iod.iod_flags = obj2io_flags(flags) | PHO_IO_SYNC_FILE | PHO_IO_NO_REUSE;
    iod.iod_fd    = src->fd;
    iod.iod_off   = 0;
    iod.iod_size  = src->st.st_size;
    iod.iod_loc   = loc;

    /* Prepare the attributes to be saved */
    rc = build_extent_md(obj_id, md, lay, loc, &iod.iod_attrs);
    if (rc)
        return rc;

    /* write the extent */
    rc = ioa_put(&ioa, obj_id, tag[0] ? tag : NULL, &iod, NULL, NULL);
    if (rc)
        pho_error(rc, "PUT failed");

    pho_attrs_free(&iod.iod_attrs);
    return rc;
}

/** copy data from the extent to the given fd */
static int read_extents(int fd, const char *obj_id,
                        const struct layout_descr *layout,
                        struct data_loc *loc, int flags)
{
    char                 tag[PHO_LAYOUT_TAG_MAX] = "";
    struct io_adapter    ioa;
    struct pho_io_descr  iod;
    const char          *name;
    int                  rc;

    memset(&iod, 0, sizeof(iod));

    /* get vector of functions to access the media */
    rc = get_io_adapter(loc->extent.fs_type, &ioa);
    if (rc)
        return rc;

    /* build extent tag from layout description */
    rc = layout2tag(layout, loc->extent.layout_idx, tag);
    if (rc)
        return rc;

    iod.iod_flags = obj2io_flags(flags) | PHO_IO_NO_REUSE;
    iod.iod_fd    = fd;
    iod.iod_off   = 0;
    iod.iod_size  = loc->extent.size;
    iod.iod_loc   = loc;

    rc = pho_attr_set(&iod.iod_attrs, PHO_EA_ID_NAME, "");
    if (rc)
        return rc;

    /* read the extent */
    rc = ioa_get(&ioa, obj_id, tag[0] ? tag : NULL, &iod, NULL, NULL);
    if (rc) {
        pho_error(rc, "GET failed");
        return rc;
    }

    /* check ID from media */
    name = pho_attr_get(&iod.iod_attrs, PHO_EA_ID_NAME);
    if (name == NULL)
        LOG_RETURN(rc = -EIO, "Couldn't find 'id' metadata on media");

    if (strcmp(obj_id, name))
        LOG_GOTO(free_values, rc = -EIO, "Inconsistent 'id' stored on media: "
                 "'%s'", name);

    if (fsync(fd) != 0)
        LOG_GOTO(free_values, rc = -errno, "fsync failed on target");

free_values:
    pho_attrs_free(&iod.iod_attrs);
    return rc;
}


/** try to open a file with O_NOATIME flag.
 * Perform a standard open if it doesn't succeed.
 */
static int open_noatime(const char *path, int flags)
{
    int fd;

    fd = open(path, flags | O_NOATIME);
    /* not allowed to open with NOATIME arg, try without */
    if (fd < 0 && errno == EPERM)
        fd = open(path, flags & ~O_NOATIME);

    return fd;
}

static int store_init(void **dss_hdl)
{
    int         rc;
    const char *str;

    rc = pho_cfg_init_local(NULL);
    if (rc)
        return rc;

    rc = pho_cfg_get(PHO_CFG_DSS_connect_string, &str);
    if (rc)
        return rc;

    return dss_init(str, dss_hdl);

    /* FUTURE: return pho_cfg_set_thread_conn(dss_hdl); */
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
    void               *dss = NULL;

    /* load configuration and get dss handle */
    rc = store_init(&dss);
    if (rc)
        LOG_RETURN(rc, "initialization failed");

    /* get the size of the source file and check its availability */
    info.fd = open_noatime(src_file, O_RDONLY);
    if (info.fd < 0)
        LOG_GOTO(disconn, rc = -errno, "open(%s) failed", src_file);

    if (fstat(info.fd, &info.st) != 0)
        LOG_GOTO(disconn, rc = -errno, "fstat(%s) failed", src_file);

    /* store object info in the DB (transient state) + check if it already
     * exists */
    /** @TODO   rc = obj_put_start(obj_id, md, flags_obj2md(flags)); */
    rc = 0;
    if (rc)
        LOG_GOTO(close_src, rc, "obj_put_start(%s) failed", obj_id);

    /* get storage resource to write the object */
    rc = lrs_write_intent(dss, info.st.st_size, &simple_layout, &write_loc);
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
    lrs_done(dss, &write_loc);
    /* don't care about the error here, the object has been saved successfully
     * and the LRS error should have been logged by lower layers.
     */
    /** @TODO release write_loc structure memory */

    pho_info("put complete: '%s' -> obj_id:%s", src_file, obj_id);
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
    lrs_done(dss, &write_loc);
out_clean_obj:
    /** @TODO    obj_put_abort(obj_id); */
    ;
close_src:
    close(info.fd);
disconn:
    if (dss)
        dss_fini(dss);
    return rc;
}

/** retrieve the location of a given object from DSS */
static int obj_get_location(void *dss, const char *obj_id,
                            struct layout_descr *layout,
                            struct extent *ext)
{
#ifdef _TEST
    struct dss_crit crit[1]; /* criteria on obj_id */
    int             crit_cnt = 0;
    struct extent  *ext_ls = NULL;
    int             rc, cnt = 0;

    /** FIXME TEST ONLY */
    static const struct layout_descr simple_layout = {.type = PHO_LYT_SIMPLE};

    dss_crit_add(crit, &crit_cnt, DSS_EXT_oid, DSS_CMP_EQ, val_str, obj_id);

    rc = dss_extent_get(dss, crit, crit_cnt, &ext_ls, &cnt);
    if (rc != 0)
        return rc;
    if (cnt == 0)
        return -ENOENT;

    *ext = ext_ls[0];
    *layout = simple_layout;

    return 0;
#else
    return -ENOTSUP;
#endif
}

/** Retrieve a file from the object store
 * \param obj_id    unique arbitrary string to identify the object
 * \param tgt_file  target file
 * \param flags     behavior flags
 */
int phobos_get(const char *obj_id, const char *tgt_file, int flags)
{
    struct layout_descr layout = {0};
    struct data_loc read_loc = {0};
    int rc = 0;
    int fd, open_flags;
    void *dss = NULL;

    /* load configuration and get dss handle */
    rc = store_init(&dss);
    if (rc)
        LOG_RETURN(rc, "initialization failed");

    /* retrieve saved object location */
    rc = obj_get_location(dss, obj_id, &layout, &read_loc.extent);
    if (rc)
        LOG_GOTO(disconn, rc, "Failed to get information about object '%s'",
                 obj_id);

    open_flags = (flags & PHO_OBJ_REPLACE) ? O_CREAT|O_WRONLY|O_TRUNC
                                           : O_CREAT|O_WRONLY|O_EXCL;

    /* make sure we can write to the target file */
    fd = open(tgt_file, open_flags, 0640);
    if (fd < 0)
        LOG_GOTO(disconn, rc = -errno, "Failed to open %s for writing",
                 tgt_file);

    /* prepare storage resource to read the object */
    rc = lrs_read_intent(dss, &layout, &read_loc);
    if (rc)
        LOG_GOTO(close_tgt, rc, "failed to prepare resources to read '%s'",
                 obj_id);

    /* read data from the media */
    rc = read_extents(fd, obj_id, &layout, &read_loc, flags);
    if (rc)
        LOG_GOTO(out_lrs_end, rc, "failed to read extents");

out_lrs_end:
    /* release storage resources */
    lrs_done(dss, &read_loc);
    /* don't care about the error here, the object has been read successfully
     * and the LRS error should have been logged by lower layers.
     */
    /** @TODO release read_loc structure memory */
close_tgt:
    close(fd);
    if (rc == 0)
        pho_info("get complete: obj_id:%s -> '%s'", obj_id, tgt_file);
    else if (unlink(tgt_file) != 0 && errno != ENOENT)
        pho_warn("failed to clean '%s': %s", tgt_file, strerror(errno));

disconn:
    if (dss)
        dss_fini(dss);

    return rc;
}
