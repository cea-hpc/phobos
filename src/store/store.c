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


/**
 * The operation step of a slice is updated after the operation has succeeded.
 */
enum mput_step {
    MPUT_STEP_INVAL         = -1,
    MPUT_STEP_INITIAL       = 0,
    MPUT_STEP_GET_FILE_SIZE = 1,
    MPUT_STEP_OBJ_PUT_START = 2,
    MPUT_STEP_EXT_PUT_START = 3,
    MPUT_STEP_EXT_WRITE     = 4,
    MPUT_STEP_OBJ_DONE      = 5,

    MPUT_STEP_COUNT = 6     /* For iterators */
};

/**
 * Single PUT slice, element of a MPUT operation.
 * Note that even though MPUT groups objects under a same LRS
 * intent, each slice has a copy, since this is where the object
 * address is stored.
 */
struct mput_slice {
    int                         rc;     /**< further processing stops if != 0 */
    int                         fd;     /**< file descriptor to the source */
    struct stat                 st;     /**< source file inode information */
    enum mput_step              step;   /**< current or first failed step  */
    struct layout_info          layout; /**< layout description */
    struct lrs_intent           intent; /**< per-component intent copy */
    const struct pho_xfer_desc *xfer;   /**< pointer to the given xfer info */
};

/**
 * Composite MPUT operation
 */
struct mput_desc {
    struct dss_handle   dss;        /**< A DSS handle, set by store_mput_init */
    struct lrs_intent   intent;     /**< The unique intent expressed for MPUT */
    size_t              sum_sizes;  /**< Total sizes, not updated on failures */
    int                 slice_cnt;  /**< Count of objects to PUT */
    struct mput_slice   slices[0];  /**< Objects to insert */
};

/**
 * MPUT state machine step handle, invoked for each active slice. Returning
 * non-zero from a handler will mark the current slice as failed and inactive.
 *
 * @param(in,out)  mput   The MPUT global descriptor.
 * @param(in,out)  slice  The current slice to process.
 * @return 0 on success, negative error code on failure.
 */
typedef int (*mput_operation_t)(struct mput_desc *, struct mput_slice *);


static int _get_file_size_cb(struct mput_desc *mput, struct mput_slice *slice);
static int _obj_put_start_cb(struct mput_desc *mput, struct mput_slice *slice);
static int _ext_put_start_cb(struct mput_desc *mput, struct mput_slice *slice);
static int _ext_write_cb(struct mput_desc *mput, struct mput_slice *slice);
static int _obj_done_cb(struct mput_desc *mput, struct mput_slice *slice);

/**
 * Actions to do for each slice.
 *
 * If any of these function returns a non-zero code the slice will be marked
 * as failed and its step indicator will designate the last successful step,
 * for proper iteration over the cleanup handlers.
 */
static const mput_operation_t mput_state_machine_ops[MPUT_STEP_COUNT] = {
    [MPUT_STEP_GET_FILE_SIZE] = _get_file_size_cb,
    [MPUT_STEP_OBJ_PUT_START] = _obj_put_start_cb,
    [MPUT_STEP_EXT_PUT_START] = _ext_put_start_cb,
    [MPUT_STEP_EXT_WRITE]     = _ext_write_cb,
    [MPUT_STEP_OBJ_DONE]      = _obj_done_cb,
};


static int _ext_clean_cb(struct mput_desc *mput, struct mput_slice *slice);
static int _ext_abort_cb(struct mput_desc *mput, struct mput_slice *slice);
static int _obj_abort_cb(struct mput_desc *mput, struct mput_slice *slice);

/**
 * Cleanup actions to do for each failed slice.
 * Return values are ignored.
 */
static const mput_operation_t mput_state_machine_clean_ops[MPUT_STEP_COUNT] = {
    [MPUT_STEP_OBJ_PUT_START] = _ext_clean_cb,
    [MPUT_STEP_EXT_PUT_START] = _ext_abort_cb,
    [MPUT_STEP_EXT_WRITE]     = _obj_abort_cb,
};


static const struct layout_info simple_layout = {.type = PHO_LYT_SIMPLE};


/**
 * Make active slices progress from their current step to the given one.
 * Return 0 if at least one step has succeeded and -EIO otherwise.
 *
 * @param(in,out)   mput     Global bulk operation descriptor.
 * @param(in)       barrier  Step to progress to.
 *
 * @return 0 on success (even partial), -EIO if everything failed.
 */
static int mput_slices_progress(struct mput_desc *mput, enum mput_step barrier)
{
    bool    all_failed = true;
    int     i;

    if (barrier >= MPUT_STEP_COUNT)
        return -EINVAL;

    for (i = 0; i < mput->slice_cnt; i++) {
        struct mput_slice   *slice = &mput->slices[i];
        enum mput_step       step;

        for (step = slice->step + 1; step <= barrier; step++) {
            int rc;

            /* Skip failed slices */
            if (slice->rc != 0)
                break;

            rc = mput_state_machine_ops[step](mput, slice);
            if (rc != 0) {
                pho_warn("Failing slice '%s'", slice->xfer->xd_objid);
                slice->rc = rc;
            } else {
                all_failed = false;
                slice->step = step;
            }
        }
    }

    return all_failed ? -EIO : 0;
}

/**
 * Force error on all successful slices.
 * This is used to propagate error state after an operation on the
 * shared state has failed. Note that slices that already failed are
 * ignored, in order to preserve the first encountered error.
 */
static void fail_all_slices(struct mput_desc *mput, int err)
{
    int i;

    for (i = 0; i < mput->slice_cnt; i++) {
        struct mput_slice   *slice = &mput->slices[i];

        if (slice->rc == 0) {
            slice->rc = err;
            pho_debug("Marking slice '%s' as failed with error %d (%s)",
                      slice->xfer->xd_objid, err, strerror(-err));
        }
    }
}

/**
 * For each failed slices, iterate over the cleanup handlers.
 * Start from last successful step (slice->step) to the initial ones to unwind
 * the full procedure.
 *
 * @param(in,out)  mput  Global put operation descriptor.
 * @param(in)      err   Error code to override slices' ones, if set.
 */
static void mput_slices_cleanup(struct mput_desc *mput, int err)
{
    int i;

    /* If set, distribute the global error */
    if (err != 0)
        fail_all_slices(mput, err);

    for (i = 0; i < mput->slice_cnt; i++) {
        struct mput_slice   *slice = &mput->slices[i];
        enum mput_step       step;

        /* No error, nothing to clean for this slice */
        if (slice->rc == 0)
            continue;

        for (step = slice->step; step > MPUT_STEP_INVAL; step--) {
            /* unlike regular operations that are mandatory, not all steps
             * expose cleanup handlers, thus check for holes. */
            if (mput_state_machine_clean_ops[step] != NULL)
                mput_state_machine_clean_ops[step](mput, slice);
            slice->step = step;
        }
    }
}

int _get_file_size_cb(struct mput_desc *mput, struct mput_slice *slice)
{
    int rc;
    ENTRY;

    rc = stat(slice->xfer->xd_fpath, &slice->st);
    if (rc < 0)
        LOG_RETURN(rc = -errno, "stat(%s) failed", slice->xfer->xd_fpath);

    mput->sum_sizes += slice->st.st_size;
    return 0;
}

/**
 * @TODO this function should properly address the case where extents already
 *      exist:
 *      - on update: keep the old ones but mark them as orphans for cleaning
 *        by LRS on mount.
 *      - on insert: ???
 */
int _obj_put_start_cb(struct mput_desc *mput, struct mput_slice *slice)
{
    enum dss_set_action  action;
    struct object_info   obj;
    GString             *md_repr = g_string_new(NULL);
    int                  rc;
    ENTRY;

    rc = pho_attrs_to_json(slice->xfer->xd_attrs, md_repr, 0);
    if (rc)
        LOG_GOTO(out_free, rc, "Cannot convert attributes into JSON");

    obj.oid = slice->xfer->xd_objid;
    obj.user_md = md_repr->str;

    action = (slice->xfer->xd_flags & PHO_XFER_OBJ_REPLACE) ? DSS_SET_UPDATE
                                                            : DSS_SET_INSERT;

    pho_debug("Storing object %s (transient) with attributes: %s",
              slice->xfer->xd_objid, md_repr->str);

    rc = dss_object_set(&mput->dss, &obj, 1, action);
    if (rc)
        LOG_GOTO(out_free, rc, "dss_object_set failed");

out_free:
    g_string_free(md_repr, true);
    return rc;
}

int _ext_put_start_cb(struct mput_desc *mput, struct mput_slice *slice)
{
    enum dss_set_action action;
    int                 rc;
    ENTRY;

    /* XXX no support for other layouts yet */
    slice->layout = simple_layout;

    /**
     * Each slice gets a copy of the global mput intent,
     * so that they can store their own object address and size in it.
     */
    slice->intent = mput->intent;
    slice->intent.li_location.extent.size = slice->st.st_size;

    slice->layout.oid = slice->xfer->xd_objid;
    slice->layout.copy_num = 0;
    slice->layout.state = PHO_EXT_ST_PENDING;

    /* The layout points to the slice's extent and NOT the global one */
    slice->layout.extents = &slice->intent.li_location.extent;
    slice->layout.ext_count = 1;

    action = (slice->xfer->xd_flags & PHO_XFER_OBJ_REPLACE) ? DSS_SET_UPDATE
                                                            : DSS_SET_INSERT;

    rc = dss_extent_set(&mput->dss, &slice->layout, 1, action);
    if (rc)
        LOG_RETURN(rc, "dss_extent_set failed");

    return 0;
}

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

static inline int obj2io_flags(enum pho_xfer_flags flags)
{
    if (flags & PHO_XFER_OBJ_REPLACE)
        return PHO_IO_REPLACE;
    else
        return 0;
}

/**
 * Try to open a file with O_NOATIME flag.
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

int _ext_write_cb(struct mput_desc *mput, struct mput_slice *slice)
{
    char                         tag[PHO_LAYOUT_TAG_MAX] = "";
    struct pho_ext_loc          *loc = &slice->intent.li_location;
    struct io_adapter            ioa;
    struct pho_io_descr          iod = {0};
    const struct pho_xfer_desc  *xfer = slice->xfer;
    int                          rc;
    ENTRY;

    /* get vector of functions to access the media */
    rc = get_io_adapter(loc->extent.fs_type, &ioa);
    if (rc)
        LOG_RETURN(rc, "No suitable I/O adapter found");

    if (!io_adapter_is_valid(&ioa))
        LOG_RETURN(-EINVAL, "Invalid I/O adapter, check implementation!");

    /* build extent tag from layout description */
    rc = layout2tag(&slice->layout, loc->extent.layout_idx, tag);
    if (rc)
        LOG_RETURN(rc, "Cannot infer tags from layout description");

    slice->fd = open_noatime(xfer->xd_fpath, O_RDONLY);
    if (slice->fd < 0)
        LOG_RETURN(rc = -errno, "open(%s) failed", xfer->xd_fpath);

    iod.iod_flags = obj2io_flags(xfer->xd_flags) | PHO_IO_NO_REUSE;
    iod.iod_fd    = slice->fd;
    iod.iod_off   = 0;
    iod.iod_size  = slice->st.st_size;
    iod.iod_loc   = loc;

    /* Prepare the attributes to be saved */
    rc = build_extent_md(xfer->xd_objid, xfer->xd_attrs, &slice->layout, loc,
                         &iod.iod_attrs);
    if (rc)
        LOG_GOTO(out_close, rc, "Cannot build MD representation");

    /* write the extent */
    rc = ioa_put(&ioa, xfer->xd_objid, tag[0] ? tag : NULL, &iod, NULL, NULL);
    if (rc)
        LOG_GOTO(out_free, rc, "PUT failed");

out_free:
    pho_attrs_free(&iod.iod_attrs);

out_close:
    close(slice->fd);
    return rc;
}

int _obj_done_cb(struct mput_desc *mput, struct mput_slice *slice)
{
    struct layout_info  *layout = &slice->layout;
    int                  rc;
    ENTRY;

    layout->state = PHO_EXT_ST_SYNC;
    rc = dss_extent_set(&mput->dss, layout, 1, DSS_SET_UPDATE);
    if (rc)
        LOG_RETURN(rc, "obj_put_done(%s) failed", slice->xfer->xd_objid);

    return 0;
}

int _ext_clean_cb(struct mput_desc *mput, struct mput_slice *slice)
{
    int rc;
    ENTRY;

    rc = dss_extent_set(&mput->dss, &slice->layout, 1, DSS_SET_DELETE);
    if (rc)
        LOG_RETURN(rc, "dss_extent_set(%s) failed", slice->xfer->xd_objid);

    return 0;
}

int _ext_abort_cb(struct mput_desc *mput, struct mput_slice *slice)
{
    struct pho_ext_loc  *loc = &slice->intent.li_location;
    char                 tag[PHO_LAYOUT_TAG_MAX] = "";
    struct io_adapter    ioa;
    int                  rc;
    ENTRY;

    rc = get_io_adapter(loc->extent.fs_type, &ioa);
    if (rc)
        LOG_RETURN(rc, "Cannot get suitable I/O adapter");

    rc = layout2tag(&slice->layout, loc->extent.layout_idx, tag);
    if (rc)
        LOG_RETURN(rc, "Cannot generate tag");

    rc = ioa_del(&ioa, slice->xfer->xd_objid, tag, loc);
    if (rc)
        LOG_RETURN(rc, "Cannot delete extent");

    return 0;
}

int _obj_abort_cb(struct mput_desc *mput, struct mput_slice *slice)
{
    struct object_info  obj = {
        .oid     = slice->xfer->xd_objid,
        .user_md = NULL,
    };
    int rc;
    ENTRY;

    rc = dss_object_set(&mput->dss, &obj, 1, DSS_SET_DELETE);
    if (rc)
        LOG_RETURN(rc, "dss_object_set(%s) failed", slice->xfer->xd_objid);

    return 0;
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

static int store_init(struct dss_handle *dss)
{
    int         rc;
    const char *str;

    rc = pho_cfg_init_local(NULL);
    if (rc && rc != -EALREADY)
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

static int store_mput_init(struct mput_desc **desc,
                           const struct pho_xfer_desc *xfer, size_t n)
{
    struct mput_desc    *mput;
    int                  rc;
    int                  i;

    mput = calloc(1, sizeof(*mput) + n * sizeof(struct mput_slice));
    if (mput == NULL)
        LOG_RETURN(-ENOMEM, "Store multiop initialization failed");

    rc = store_init(&mput->dss);
    if (rc)
        LOG_GOTO(err_free, rc, "Cannot initialize DSS");

    mput->slice_cnt = n;

    for (i = 0; i < n; i++) {
        mput->slices[i].fd = -1;
        mput->slices[i].step = MPUT_STEP_INITIAL;
        mput->slices[i].layout = simple_layout;
        mput->slices[i].xfer = &xfer[i];
    }

    *desc = mput;
    return 0;

err_free:
    free(mput);
    return rc;
}

static void store_mput_fini(struct mput_desc *mput)
{
    if (mput != NULL) {
        dss_fini(&mput->dss);
        free(mput);
    }
}

static void xfer_get_notify(const struct pho_xfer_desc *desc, int rc)
{
    if (desc->xd_callback != NULL)
        desc->xd_callback(desc, rc);

    pho_info("GET operation objid:'%s' -> '%s' %s",
             desc->xd_objid, desc->xd_fpath, rc ? "failed" : "succeeded");
}

static void xfer_put_notify(const struct pho_xfer_desc *desc, int rc)
{
    if (desc->xd_callback != NULL)
        desc->xd_callback(desc, rc);

    pho_info("PUT operation '%s' -> objid:'%s' %s",
             desc->xd_fpath, desc->xd_objid, rc ? "failed" : "succeeded");
}

static int xfer_notify_all(const struct mput_desc *mput)
{
    int first_rc = 0;
    int i;

    if (mput == NULL)
        return 0;

    for (i = 0; i < mput->slice_cnt; i++) {
        if (!first_rc && mput->slices[i].rc)
            first_rc = mput->slices[i].rc;

        xfer_put_notify(mput->slices[i].xfer, mput->slices[i].rc);
    }

    return first_rc;
}

int phobos_mput(const struct pho_xfer_desc *desc, size_t n)
{
    struct mput_desc    *mput = NULL;
    int                  i;
    int                  rc;
    int                  rc2;

    /* load configuration and get dss handle... */
    rc = store_mput_init(&mput, desc, n);
    if (rc) {
        /* We have no context here, thus we cannot use xfer_notify_all().
         * Deliver rc to all components directly. */
        for (i = 0; i < n; i++)
            xfer_put_notify(&desc[i], rc);
        LOG_GOTO(disconn, rc, "Initialization failed");
    }

    rc = mput_slices_progress(mput, MPUT_STEP_OBJ_PUT_START);
    if (rc)
        LOG_GOTO(out_finalize, rc, "All slices failed");

    /* get storage resource to write objects / everyone uses simple_layout */
    rc = lrs_write_prepare(&mput->dss, mput->sum_sizes, &simple_layout,
                           &mput->intent);
    if (rc)
        LOG_GOTO(out_finalize, rc,
                 "Failed to get resources to write %zu bytes", mput->sum_sizes);

    rc = mput_slices_progress(mput, MPUT_STEP_EXT_WRITE);
    if (rc) {
        lrs_done(&mput->intent, rc);
        LOG_GOTO(out_finalize, rc, "All slices failed");
    }

    /* Release storage resources and update device/media info */
    rc = lrs_done(&mput->intent, rc);
    if (rc)
        LOG_GOTO(out_finalize, rc, "Failed to flush data");

    /* Data successfully flushed. Mark objects as stable */
    rc = mput_slices_progress(mput, MPUT_STEP_OBJ_DONE);
    if (rc)
        LOG_GOTO(out_finalize, rc, "All slices failed");

out_finalize:
    mput_slices_cleanup(mput, rc);

    rc2 = xfer_notify_all(mput);
    if (rc == 0)
        rc = rc2;

disconn:
    store_mput_fini(mput);
    return rc;
}

int phobos_put(const struct pho_xfer_desc *desc)
{
    return phobos_mput(desc, 1);
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

static inline int xfer2open_flags(enum pho_xfer_flags flags)
{
    return (flags & PHO_XFER_OBJ_REPLACE) ? O_CREAT|O_WRONLY|O_TRUNC
                                          : O_CREAT|O_WRONLY|O_EXCL;
}

int phobos_get(const struct pho_xfer_desc *desc)
{
    struct layout_info  *layout = NULL;
    struct lrs_intent    intent;
    int                  rc = 0;
    int                  fd;
    struct dss_handle    dss;

    /* load configuration and get dss handle */
    rc = store_init(&dss);
    if (rc)
        LOG_RETURN(rc, "Initialization failed");

    /* retrieve saved object location */
    rc = obj_get_location(&dss, desc->xd_objid, &layout);
    if (rc)
        LOG_GOTO(disconn, rc, "Failed to get information about object '%s'",
                 desc->xd_objid);

    assert(layout != NULL);

    /* make sure we can write to the target file */
    fd = open(desc->xd_fpath, xfer2open_flags(desc->xd_flags), 0640);
    if (fd < 0)
        LOG_GOTO(free_res, rc = -errno, "Failed to open %s for writing",
                 desc->xd_fpath);

    /* prepare storage resource to read the object */
    rc = lrs_read_prepare(&dss, layout, &intent);
    if (rc)
        LOG_GOTO(close_tgt, rc, "Failed to prepare resources to read '%s'",
                 desc->xd_objid);

    /* read data from the media */
    rc = read_extents(fd, desc->xd_objid, layout, &intent, desc->xd_flags);
    if (rc)
        LOG_GOTO(out_lrs_end, rc, "Failed to read extents");

out_lrs_end:
    /* release storage resources */
    lrs_done(&intent, rc);
    /* don't care about the error here, the object has been read successfully
     * and the LRS error should have been logged by lower layers.
     */
close_tgt:
    close(fd);
    if (rc != 0 && unlink(desc->xd_fpath) != 0 && errno != ENOENT)
        pho_warn("Failed to clean '%s': %s", desc->xd_fpath, strerror(errno));

free_res:
    dss_res_free(layout, 1);

disconn:
    xfer_get_notify(desc, rc);
    dss_fini(&dss);
    return rc;
}
