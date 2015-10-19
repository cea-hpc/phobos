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
                           const struct layout_info *lay,
                           struct pho_ext_loc *loc, struct pho_attrs *dst_md)
{
    GString *str;
    int      rc;

    rc = pho_attr_set(dst_md, PHO_EA_ID_NAME, id);
    if (rc)
        return rc;

    str = g_string_new("");

    /* TODO This conversion is done at several place. Consider caching the
     * result and pass it to the functions that need it. */
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
    rc = storage_info_to_json(lay, str,
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

static inline int obj2io_flags(enum pho_xfer_flags flags)
{
    if (flags & PHO_XFER_OBJ_REPLACE)
        return PHO_IO_REPLACE;
    else
        return 0;
}

static const struct layout_info simple_layout = {.type = PHO_LYT_SIMPLE};

/** copy data from the source fd to the extent identified by loc */
static int write_extents(const struct src_info *src, const char *obj_id,
                         const struct pho_attrs *md,
                         const struct layout_info *lay,
                         struct lrs_intent *intent, enum pho_xfer_flags flags)
{
    char                 tag[PHO_LAYOUT_TAG_MAX] = "";
    struct pho_ext_loc  *loc = &intent->li_location;
    struct io_adapter    ioa;
    struct pho_io_descr  iod;
    int                  rc;

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

    iod.iod_flags = obj2io_flags(flags) | PHO_IO_NO_REUSE;
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
                        const struct layout_info *layout,
                        struct lrs_intent *intent, enum pho_xfer_flags flags)
{
    char                 tag[PHO_LAYOUT_TAG_MAX] = "";
    struct pho_ext_loc  *loc = &intent->li_location;
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

static int store_init(struct dss_handle *dss)
{
    int         rc;
    const char *str;

    rc = pho_cfg_init_local(NULL);
    if (rc)
        return rc;

    str = pho_cfg_get(PHO_CFG_DSS_connect_string);
    if (str == NULL)
        return -EINVAL;

    rc = dss_init(str, dss);
    if (rc != 0)
        return rc;

    /* FUTURE: return pho_cfg_set_thread_conn(dss_hdl); */
    return 0;
}

static int obj_put_start(struct dss_handle *dss, const char *obj_id,
                         const struct pho_attrs *md, enum pho_xfer_flags flags)
{
    enum dss_set_action  action;
    struct object_info   obj;
    GString             *md_repr = g_string_new(NULL);
    int                  rc;

    rc = pho_attrs_to_json(md, md_repr, 0);
    if (rc)
        LOG_GOTO(out_free, rc, "Cannot convert attributes into JSON");

    obj.oid = obj_id;
    obj.user_md = md_repr->str;

    action = (flags & PHO_XFER_OBJ_REPLACE) ? DSS_SET_UPDATE : DSS_SET_INSERT;

    pho_debug("Storing object %s (transient) with attributes: %s",
              obj_id, md_repr->str);

    rc = dss_object_set(dss, &obj, 1, action);
    if (rc)
        LOG_GOTO(out_free, rc, "dss_object_set failed");

out_free:
    g_string_free(md_repr, true);
    return rc;
}

static int extent_put_start(struct dss_handle *dss, const char *obj_id,
                            struct layout_info *layout,
                            struct lrs_intent *intent,
                            enum pho_xfer_flags flags)
{
    enum dss_set_action action;
    int                 rc;

    layout->oid = obj_id;
    layout->copy_num = 0;
    layout->state = PHO_EXT_ST_PENDING;
    layout->extents = &intent->li_location.extent;
    layout->ext_count = 1;

    action = (flags & PHO_XFER_OBJ_REPLACE) ? DSS_SET_UPDATE : DSS_SET_INSERT;

    rc = dss_extent_set(dss, layout, 1, action);
    if (rc)
        LOG_RETURN(rc, "dss_extent_set failed");

    return 0;
}

static int obj_put_done(struct dss_handle *dss, const char *obj_id,
                        struct layout_info *layout, struct lrs_intent *intent)
{
    layout->state = PHO_EXT_ST_SYNC;
    return dss_extent_set(dss, layout, 1, DSS_SET_UPDATE);
}

static int clean_extents(struct dss_handle *dss, const char *obj_id,
                         struct layout_info *layout, struct lrs_intent *intent)
{
    int rc;

    assert(strcmp(obj_id, layout->oid) == 0);
    assert(layout->extents == &intent->li_location.extent);

    rc = dss_extent_set(dss, layout, 1, DSS_SET_DELETE);
    if (rc)
        LOG_RETURN(rc, "dss_extent_set failed");

    return 0;
}

static int extent_put_abort(struct dss_handle *dss, const char *obj_id,
                            struct layout_info *layout,
                            struct lrs_intent *intent)
{
    int rc;

    assert(strcmp(obj_id, layout->oid) == 0);
    assert(layout->extents == &intent->li_location.extent);

    rc = dss_extent_set(dss, layout, 1, DSS_SET_DELETE);
    if (rc)
        LOG_RETURN(rc, "dss_extent_set failed");

    return 0;
}

static int obj_put_abort(struct dss_handle *dss, const char *obj_id)
{
    struct object_info  obj;

    obj.oid = obj_id;
    obj.user_md = NULL;

    return dss_object_set(dss, &obj, 1, DSS_SET_DELETE);
}

static inline void desc_compl_notify(const struct pho_xfer_desc *desc, int rc)
{
    if (desc->pxd_callback != NULL) {
        pho_debug("Notifying xfer completion on objid:'%s' (rc = %d)",
                  desc->pxd_objid, rc);
        desc->pxd_callback(desc, rc);
    }
}

int phobos_mput(const struct pho_xfer_desc *desc, size_t n)
{
    LOG_RETURN(-ENOTSUP, "MPUT not implemented yet");
}

int phobos_put(const struct pho_xfer_desc *desc)
{
    /* the only layout type we can handle for now */
    struct layout_info  layout = simple_layout;
    struct src_info     info = {0};
    struct lrs_intent   intent;
    struct dss_handle   dss;
    int                 rc;

    /* load configuration and get dss handle */
    rc = store_init(&dss);
    if (rc)
        LOG_RETURN(rc, "initialization failed");

    /* get the size of the source file and check its availability */
    info.fd = open_noatime(desc->pxd_fpath, O_RDONLY);
    if (info.fd < 0)
        LOG_GOTO(disconn, rc = -errno, "open(%s) failed", desc->pxd_fpath);

    if (fstat(info.fd, &info.st) != 0)
        LOG_GOTO(close_src, rc = -errno, "fstat(%s) failed", desc->pxd_fpath);

    /* store object info in the DB (transient state) with pre-existence check */
    rc = obj_put_start(&dss, desc->pxd_objid, desc->pxd_attrs, desc->pxd_flags);
    if (rc)
        LOG_GOTO(close_src, rc, "obj_put_start(%s) failed", desc->pxd_objid);

    /* get storage resource to write the object */
    rc = lrs_write_prepare(&dss, info.st.st_size, &layout, &intent);
    if (rc)
        LOG_GOTO(out_clean_obj, rc, "failed to get storage resource to write "
                 "%zu bytes", info.st.st_size);

    /* set extent info in DB (transient state) */
    rc = extent_put_start(&dss, desc->pxd_objid, &layout, &intent,
                          desc->pxd_flags);
    if (rc)
        LOG_GOTO(out_lrs_end, rc, "couln't save extents info");

    /* write data to the media */
    rc = write_extents(&info, desc->pxd_objid, desc->pxd_attrs, &layout,
                       &intent, desc->pxd_flags);
    if (rc)
        LOG_GOTO(out_clean_db_ext, rc, "failed to write extents");

    close(info.fd);

    /* complete DB info (object status & extents) */
    rc = obj_put_done(&dss, desc->pxd_objid, &layout, &intent);
    if (rc)
        LOG_GOTO(out_clean_ext, rc, "obj_put_done(%s) failed", desc->pxd_objid);

    /* release storage resources + update device/media info
     * XXX Note that we do not care about the error here, the object has been
     * saved successfully and the LRS error should have been logged by lower
     * layers. */
    (void)lrs_done(&intent, 0);

    pho_info("put complete: '%s' -> '%s'", desc->pxd_fpath, desc->pxd_objid);
    rc = 0;
    goto disconn;

    /* cleaning after error cases */
out_clean_ext:
    clean_extents(&dss, desc->pxd_objid, &layout, &intent);

out_clean_db_ext:
    extent_put_abort(&dss, desc->pxd_objid, &layout, &intent);

out_lrs_end:
    /* release resource reservations */
    lrs_done(&intent, rc);

out_clean_obj:
    obj_put_abort(&dss, desc->pxd_objid);

close_src:
    close(info.fd);

disconn:
    desc_compl_notify(desc, rc);
    dss_fini(&dss);
    return rc;
}

/** retrieve the location of a given object from DSS */
static int obj_get_location(struct dss_handle *dss, const char *obj_id,
                            struct layout_info **layout)
{
    struct dss_crit crit[2]; /* criteria on obj_id and copynum */
    int             crit_cnt = 0;
    int             cnt = 0;
    int             rc;

    dss_crit_add(crit, &crit_cnt, DSS_EXT_oid, DSS_CMP_EQ, val_str, obj_id);
    /* v00: object have a single copy */
    dss_crit_add(crit, &crit_cnt, DSS_EXT_copy_num, DSS_CMP_EQ, val_uint, 0);

    /** @TODO check if there is a pending copy of the object */

    rc = dss_extent_get(dss, crit, crit_cnt, layout, &cnt);
    if (rc != 0)
        return rc;

    if (cnt == 0)
        GOTO(err, rc = -ENOENT);

    if (cnt > 1)
        LOG_GOTO(err, rc = -EINVAL, "Too many layouts found matching oid '%s'",
                 obj_id);

    return 0;

err:
    dss_res_free(*layout, cnt);
    *layout = NULL;
    return rc;
}

int phobos_get(const struct pho_xfer_desc *desc)
{
    enum pho_xfer_flags  flags = desc->pxd_flags;
    struct layout_info  *layout = NULL;
    struct lrs_intent    intent;
    int                  rc = 0;
    int                  fd;
    int                  open_flags;
    struct dss_handle    dss;

    /* load configuration and get dss handle */
    rc = store_init(&dss);
    if (rc)
        LOG_RETURN(rc, "initialization failed");

    /* retrieve saved object location */
    rc = obj_get_location(&dss, desc->pxd_objid, &layout);
    if (rc)
        LOG_GOTO(disconn, rc, "Failed to get information about object '%s'",
                 desc->pxd_objid);

    assert(layout != NULL);

    open_flags = (flags & PHO_XFER_OBJ_REPLACE) ? O_CREAT|O_WRONLY|O_TRUNC
                                                : O_CREAT|O_WRONLY|O_EXCL;

    /* make sure we can write to the target file */
    fd = open(desc->pxd_fpath, open_flags, 0640);
    if (fd < 0)
        LOG_GOTO(free_res, rc = -errno, "Failed to open %s for writing",
                 desc->pxd_fpath);

    /* prepare storage resource to read the object */
    rc = lrs_read_prepare(&dss, layout, &intent);
    if (rc)
        LOG_GOTO(close_tgt, rc, "failed to prepare resources to read '%s'",
                 desc->pxd_objid);

    /* read data from the media */
    rc = read_extents(fd, desc->pxd_objid, layout, &intent, flags);
    if (rc)
        LOG_GOTO(out_lrs_end, rc, "failed to read extents");

out_lrs_end:
    /* release storage resources */
    lrs_done(&intent, rc);
    /* don't care about the error here, the object has been read successfully
     * and the LRS error should have been logged by lower layers.
     */
close_tgt:
    close(fd);
    if (rc == 0)
        pho_info("get complete: objid:%s -> '%s'", desc->pxd_objid,
                 desc->pxd_fpath);
    else if (unlink(desc->pxd_fpath) != 0 && errno != ENOENT)
        pho_warn("failed to clean '%s': %s", desc->pxd_fpath, strerror(errno));

free_res:
    dss_res_free(layout, 1);

disconn:
    desc_compl_notify(desc, rc);
    dss_fini(&dss);
    return rc;
}
