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
#include "pho_layout.h"

#include <sys/types.h>
#include <attr/xattr.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PHO_ATTR_BACKUP_JSON_FLAGS (JSON_COMPACT | JSON_SORT_KEYS)

#define PHO_EA_ID_NAME      "id"
#define PHO_EA_UMD_NAME     "user_md"
#define PHO_EA_EXT_NAME     "ext_info"

#define RETRY_SLEEP_MAX_US (1000 * 1000) /* 1 second */
#define RETRY_SLEEP_MIN_US (10 * 1000)   /* 10 ms */

/**
 * List of configuration parameters for store
 */
enum pho_cfg_params_store {
    /* Actual parameters */
    PHO_CFG_STORE_layout,

    /* Delimiters, update when modifying options */
    PHO_CFG_STORE_FIRST = PHO_CFG_STORE_layout,
    PHO_CFG_STORE_LAST  = PHO_CFG_STORE_layout,
};

const struct pho_config_item cfg_store[] = {
    [PHO_CFG_STORE_layout] = {
        .section = "store",
        .name    = "layout",
        .value   = "simple"
    },
};

/**
 * The operation step of a slice is updated after the operation has succeeded.
 */
enum mput_step {
    MPUT_STEP_INVAL          = -1,
    MPUT_STEP_INITIAL        = 0,
    MPUT_STEP_GET_FILE_SIZE  = 1,
    MPUT_STEP_OBJ_PUT_START  = 2,
    MPUT_STEP_LAYOUT_DECLARE = 3,
    MPUT_STEP_EXT_PUT_START  = 4,
    MPUT_STEP_EXT_WRITE      = 5,
    MPUT_STEP_OBJ_DONE       = 6,

    MPUT_STEP_COUNT = 7     /* For iterators */
};

/**
 * Single PUT slice, element of a MPUT operation.
 * Note that even though MPUT groups objects under a same LRS
 * intent, each slice has a copy, since this is where the object
 * address is stored.
 */
struct mput_slice {
    const struct pho_xfer_desc *xfer;   /**< pointer to the corresp. xfer */
    struct layout_info          layout; /**< this slice data layout */
    enum mput_step              step;   /**< current or first failed step  */
    int                         rc;     /**< further processing stops if != 0 */
};

/**
 * Composite MPUT operation
 */
struct mput_desc {
    struct dss_handle        dss;       /**< A cached DSS handle */
    struct layout_composer   comp;      /**< Arrange particular layouts */
    char                    *layout;    /**< Layout name to use */
    int                      slice_cnt; /**< Count of objects to PUT */
    struct mput_slice        slices[0]; /**< Objects to write */
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
static int _layout_declare_cb(struct mput_desc *mput, struct mput_slice *slice);
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
    [MPUT_STEP_GET_FILE_SIZE]  = _get_file_size_cb,
    [MPUT_STEP_OBJ_PUT_START]  = _obj_put_start_cb,
    [MPUT_STEP_LAYOUT_DECLARE] = _layout_declare_cb,
    [MPUT_STEP_EXT_PUT_START]  = _ext_put_start_cb,
    [MPUT_STEP_EXT_WRITE]      = _ext_write_cb,
    [MPUT_STEP_OBJ_DONE]       = _obj_done_cb,
};


static int _ext_clean_cb(struct mput_desc *mput, struct mput_slice *slice);
static int _ext_abort_cb(struct mput_desc *mput, struct mput_slice *slice);
static int _obj_clean_cb(struct mput_desc *mput, struct mput_slice *slice);

/**
 * Cleanup actions to do for each failed slice.
 * Return values are ignored.
 */
static const mput_operation_t mput_state_machine_clean_ops[MPUT_STEP_COUNT] = {
    [MPUT_STEP_OBJ_PUT_START] = _obj_clean_cb,
    [MPUT_STEP_EXT_PUT_START] = _ext_clean_cb,
    [MPUT_STEP_EXT_WRITE]     = _ext_abort_cb,
};

/**
 * Make active slices progress from their current step to the given one.
 * Return 0 if at least one step has succeeded and -EIO otherwise.
 *
 * @param(in,out)   mput     Global bulk operation descriptor.
 * @param(in)       barrier  Step to progress to.
 *
 * @return 0 on success (even partial), min(error) if everything failed.
 *
 * XXX Note that due to errors being coded on negative numbers, the return value
 *     corresponds to max(abs(rc)).
 */
static int mput_slices_progress(struct mput_desc *mput, enum mput_step barrier)
{
    int     min_err = 0;
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
                pho_warn("Failing slice objid:'%s'", slice->xfer->xd_objid);
                slice->rc = rc;
            } else {
                slice->step = step;
            }
        }
    }

    for (i = 0; i < mput->slice_cnt; i++) {
        struct mput_slice *slice = &mput->slices[i];

        if (slice->rc == 0)
            /* At least one slice succeeded, that happens */
            return 0;

        if (slice->rc < min_err)
            /* Get the highest error code in absolute value */
            min_err = slice->rc;
    }

    return min_err;
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
            pho_debug("Marking slice objid:'%s' as failed with error %d (%s)",
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
    struct stat st;
    int         rc;
    ENTRY;

    rc = stat(slice->xfer->xd_fpath, &st);
    if (rc < 0)
        LOG_RETURN(-errno, "stat(%s) failed", slice->xfer->xd_fpath);

    slice->layout.wr_size = st.st_size;
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
    struct object_info   obj;
    GString             *md_repr = g_string_new(NULL);
    int                  rc;
    ENTRY;

    rc = pho_attrs_to_json(slice->xfer->xd_attrs, md_repr, 0);
    if (rc)
        LOG_GOTO(out_free, rc, "Cannot convert attributes into JSON");

    obj.oid = slice->xfer->xd_objid;
    obj.user_md = md_repr->str;

    pho_debug("Storing object objid:'%s' (transient) with attributes: %s",
              slice->xfer->xd_objid, md_repr->str);

    rc = dss_object_set(&mput->dss, &obj, 1, DSS_SET_INSERT);
    if (rc)
        LOG_GOTO(out_free, rc, "dss_object_set failed for objid:'%s'", obj.oid);

out_free:
    g_string_free(md_repr, true);
    return rc;
}

/**
 * Declare the object to the layout management layer.
 */
int _layout_declare_cb(struct mput_desc *mput, struct mput_slice *slice)
{
    char    *objid = slice->xfer->xd_objid;
    int      rc;
    ENTRY;

    slice->layout.oid   = objid;
    slice->layout.state = PHO_EXT_ST_PENDING;

    slice->layout.layout_desc.mod_name = mput->layout;

    rc = layout_declare(&mput->comp, &slice->layout);
    if (rc)
        LOG_RETURN(rc, "layout_declare failed for object objid:'%s'", objid);

    return 0;
}

int _ext_put_start_cb(struct mput_desc *mput, struct mput_slice *slice)
{
    char    *objid = slice->xfer->xd_objid;
    int      rc;
    ENTRY;

    rc = dss_extent_set(&mput->dss, &slice->layout, 1, DSS_SET_INSERT);
    if (rc)
        LOG_RETURN(rc, "dss_extent_set failed for objid:'%s'", objid);

    return 0;
}

/** fill an array with metadata blobs to be stored on the media */
static int build_extent_md(const char *id, const struct pho_attrs *md,
                           const struct layout_info *layout,
                           struct pho_attrs *dst_md)
{
    GString *str;
    int      rc;

    rc = pho_attr_set(dst_md, PHO_EA_ID_NAME, id);
    if (rc)
        return rc;

    str = g_string_new(NULL);

    /* TODO This conversion is done at several place. Consider caching the
     * result and pass it to the functions that need it. */
    rc = pho_attrs_to_json(md, str, PHO_ATTR_BACKUP_JSON_FLAGS);
    if (rc)
        goto free_values;

    if (!gstring_empty(str)) {
        rc = pho_attr_set(dst_md, PHO_EA_UMD_NAME, str->str);
        if (rc)
            goto free_values;
    }

#if 0
    rc = pho_layout_to_json(layout, str, PHO_ATTR_BACKUP_JSON_FLAGS);
    if (rc != 0)
        goto free_values;

    if (!gstring_empty(str)) {
        rc = pho_attr_set(dst_md, PHO_EA_EXT_NAME, str->str);
        if (rc)
            goto free_values;
    }
#endif

free_values:
    if (rc != 0)
        pho_attrs_free(dst_md);

    g_string_free(str, TRUE);
    return rc;
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
    const struct pho_xfer_desc  *xfer = slice->xfer;
    struct pho_io_descr          iod  = {0};
    const char                  *objid = xfer->xd_objid;
    int                          fd;
    int                          rc;
    ENTRY;

    fd = open_noatime(xfer->xd_fpath, O_RDONLY);
    if (fd < 0)
        LOG_RETURN(rc = -errno, "open(%s) failed", xfer->xd_fpath);

    /* Allow extent overwrite. Duplicated objects have been checked by dss
     * already at this point, so we are actually overwriting an orphan. */
    iod.iod_flags = PHO_IO_REPLACE | PHO_IO_NO_REUSE;
    iod.iod_fd    = fd;

    /* Prepare the attributes to be saved */
    rc = build_extent_md(objid, xfer->xd_attrs, &slice->layout, &iod.iod_attrs);
    if (rc)
        LOG_GOTO(out_close, rc, "Cannot build MD representation for objid:'%s'",
                 objid);

    /* write the extent */
    rc = layout_io(&mput->comp, objid, &iod);
    if (rc)
        LOG_GOTO(out_free, rc, "PUT failed for objid:'%s'", objid);

out_free:
    pho_attrs_free(&iod.iod_attrs);

out_close:
    close(fd);
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
        LOG_RETURN(rc, "obj_put_done failed for objid:'%s'",
                   slice->xfer->xd_objid);

    return 0;
}

int _ext_clean_cb(struct mput_desc *mput, struct mput_slice *slice)
{
    int rc;
    ENTRY;

    rc = dss_extent_set(&mput->dss, &slice->layout, 1, DSS_SET_DELETE);
    if (rc)
        LOG_RETURN(rc, "dss_extent_set failed for objid:'%s'",
                   slice->xfer->xd_objid);

    return 0;
}

int _ext_abort_cb(struct mput_desc *mput, struct mput_slice *slice)
{
    const struct pho_xfer_desc  *xfer = slice->xfer;
    struct pho_io_descr          iod  = {0};
    int                          rc;
    ENTRY;

    iod.iod_flags = PHO_IO_DELETE;
    iod.iod_fd    = -1;

    /* write the extent */
    rc = layout_io(&mput->comp, xfer->xd_objid, &iod);
    if (rc)
        LOG_RETURN(rc, "Extent deletion failed for objid:'%s'", xfer->xd_objid);

    return 0;
}

int _obj_clean_cb(struct mput_desc *mput, struct mput_slice *slice)
{
    struct object_info  obj = {
        .oid     = slice->xfer->xd_objid,
        .user_md = NULL,
    };
    int rc;
    ENTRY;

    rc = dss_object_set(&mput->dss, &obj, 1, DSS_SET_DELETE);
    if (rc)
        LOG_RETURN(rc, "dss_object_set failed for objid:'%s'",
                   slice->xfer->xd_objid);

    return 0;
}

static int store_dss_init(struct dss_handle *dss)
{
    int         rc;

    rc = pho_cfg_init_local(NULL);
    if (rc && rc != -EALREADY)
        return rc;

    rc = dss_init(dss);
    if (rc != 0)
        return rc;

    /* FUTURE: return pho_cfg_set_thread_conn(dss_hdl); */
    return 0;
}

static int store_mput_init(struct mput_desc **desc,
                           const struct pho_xfer_desc *xfer, size_t n)
{
    struct mput_desc    *mput;
    const char          *layout = PHO_CFG_GET(cfg_store, PHO_CFG_STORE, layout);
    int                  rc;
    int                  i;

    if (layout == NULL)
        LOG_RETURN(-EINVAL, "Unable to determine layout type to use");

    mput = calloc(1, sizeof(*mput) + n * sizeof(struct mput_slice));
    if (mput == NULL)
        LOG_RETURN(-ENOMEM, "Store multiop initialization failed");

    rc = store_dss_init(&mput->dss);
    if (rc)
        LOG_GOTO(err_free, rc, "Cannot initialize DSS");

    mput->layout    = strdup(layout);
    mput->slice_cnt = n;

    for (i = 0; i < n; i++) {
        mput->slices[i].step = MPUT_STEP_INITIAL;
        mput->slices[i].xfer = &xfer[i];
    }

    rc = layout_init(&mput->dss, &mput->comp, LA_ENCODE);
    if (rc)
        LOG_GOTO(err_free, rc, "Cannot initialize layout composition");

    *desc = mput;

err_free:
    if (rc) {
        free(mput->layout);
        free(mput);
    }

    return rc;
}

static void store_mput_fini(struct mput_desc *mput)
{
    if (!mput)
        return;

    free(mput->layout);
    dss_fini(&mput->dss);
    free(mput);
}

static void xfer_get_notify(pho_completion_cb_t cb, void *udata,
                            const struct pho_xfer_desc *desc, int rc)
{
    if (cb != NULL)
        cb(udata, desc, rc);

    /* don't display any message for GETATTR operation */
    if (desc->xd_flags & PHO_XFER_OBJ_GETATTR)
        return;

    pho_info("GET operation objid:'%s' -> '%s' %s",
             desc->xd_objid, desc->xd_fpath, rc ? "failed" : "succeeded");
}

static void xfer_put_notify(pho_completion_cb_t cb, void *udata,
                            const struct pho_xfer_desc *desc, int rc)
{
    if (cb != NULL)
        cb(udata, desc, rc);

    pho_info("PUT operation '%s' -> objid:'%s' %s",
             desc->xd_fpath, desc->xd_objid, rc ? "failed" : "succeeded");
}

static int xfer_notify_all(pho_completion_cb_t cb, void *udata,
                           const struct mput_desc *mput)
{
    int first_rc = 0;
    int i;

    if (mput == NULL)
        return 0;

    for (i = 0; i < mput->slice_cnt; i++) {
        if (!first_rc && mput->slices[i].rc)
            first_rc = mput->slices[i].rc;

        xfer_put_notify(cb, udata, mput->slices[i].xfer, mput->slices[i].rc);
    }

    return first_rc;
}

/**
 * Get a representative return code for the whole batch.
 * Used to provide other layers with indications about how things ended,
 * even though we have a per-slice code for proper error management.
 *
 * The choice here is to return an media-global error code if any, or zero if at
 * least one slice succeeded.
 *
 * XXX
 * Note that given the context where this function is used, we can assume that
 * at least one slice has succeeded.
 */
static int mput_rc(const struct mput_desc *mput)
{
    int i;

    for (i = 0; i < mput->slice_cnt; i++) {
        if (is_media_global_error(mput->slices[i].rc))
            return mput->slices[i].rc;
    }

    return 0;
}

static int retry_layout_acquire(struct mput_desc *mput)
{
    unsigned int rand_seed;
    int rc;

    /* Seed the PRNG and hope two different phobos instances will be seeded
     * differently
     */
    rand_seed = getpid() + time(NULL);
    /* Get storage resource to write objects and retry periodically on EGAIN */
    while (true) {
        useconds_t sleep_time;

        rc = layout_acquire(&mput->comp);
        if (rc != -EAGAIN)
            return rc;

        /* Sleep before retrying */
        sleep_time =
            (rand_r(&rand_seed) % (RETRY_SLEEP_MAX_US - RETRY_SLEEP_MIN_US))
            + RETRY_SLEEP_MIN_US;
        pho_info("No resource available to perform IO, retrying in %d ms",
                 sleep_time / 1000);
        usleep(sleep_time);
    }

    /* Unreachable */
}

int phobos_put(const struct pho_xfer_desc *desc, size_t n,
               pho_completion_cb_t cb, void *udata)
{
    struct mput_desc    *mput = NULL;
    int                  i;
    int                  rc;
    int                  rc2;

    if (desc->xd_flags & PHO_XFER_OBJ_REPLACE)
        LOG_RETURN(-ENOTSUP, "OBJ_REPLACE not supported for put");

    /* Check that all tags are the same (temporary limitation) */
    if (n > 0) {
        const struct tags *ref_tags = &desc[0].xd_tags;

        for (i = 1; i < n; i++)
            if (!tags_eq(ref_tags, &desc[i].xd_tags))
                LOG_GOTO(out_finalize, rc = -EINVAL,
                         "Tags must be identical for all objects");
    }

    /* load configuration and get dss handle... */
    rc = store_mput_init(&mput, desc, n);
    if (rc) {
        /* We have no context here, thus we cannot use xfer_notify_all().
         * Deliver rc to all components directly. */
        for (i = 0; i < n; i++)
            xfer_put_notify(cb, udata, &desc[i], rc);
        LOG_GOTO(disconn, rc, "Initialization failed");
    }

    rc = mput_slices_progress(mput, MPUT_STEP_LAYOUT_DECLARE);
    if (rc)
        LOG_GOTO(out_finalize, rc, "All slices failed");

    /*
     * All tags being the same, initialize the composer tag with the first
     * transfer descriptor tags
     */
    if (n > 0) {
        /* Freed in layout_fini */
        rc = tags_dup(&mput->comp.lc_tags, &mput->slices[0].xfer->xd_tags);
        if (rc)
            LOG_GOTO(out_finalize, rc, "Tags memory allocation failed");
    }

    rc = retry_layout_acquire(mput);
    if (rc != 0)
        LOG_GOTO(out_finalize, rc,
                 "Failed to get resources to put %d objects",
                 mput->slice_cnt);

    rc = mput_slices_progress(mput, MPUT_STEP_EXT_WRITE);
    if (rc) {
        layout_fini(&mput->comp);
        LOG_GOTO(out_finalize, rc, "All slices failed");
    }

    /* Release storage resources and update device/media info */
    rc = layout_commit(&mput->comp, mput_rc(mput));
    if (rc)
        LOG_GOTO(out_finalize, rc, "Failed to flush data");

    /* Data successfully flushed. Mark objects as stable */
    rc = mput_slices_progress(mput, MPUT_STEP_OBJ_DONE);
    if (rc)
        LOG_GOTO(out_finalize, rc, "All slices failed");

out_finalize:
    mput_slices_cleanup(mput, rc);

    layout_fini(&mput->comp);

    rc2 = xfer_notify_all(cb, udata, mput);
    if (rc == 0)
        rc = rc2;

disconn:
    store_mput_fini(mput);
    return rc;
}

/**
 * Retrieve the location of a given object from DSS, ie. its layout type and
 * list of extents.
 */
static int obj_location_get(struct dss_handle *dss, const char *obj_id,
                            struct layout_info **layout)
{
    struct dss_filter   filter;
    int                 cnt = 0;
    int                 rc;

    rc = dss_filter_build(&filter, "{\"DSS::EXT::oid\": \"%s\"}", obj_id);
    if (rc)
        return rc;

    /** @TODO check if there is a pending copy of the object */

    rc = dss_extent_get(dss, &filter, layout, &cnt);
    if (rc)
        GOTO(err_nores, rc);

    if (cnt == 0)
        GOTO(err, rc = -ENOENT);

err:
    if (rc) {
        dss_res_free(*layout, cnt);
        *layout = NULL;
    }

err_nores:
    dss_filter_free(&filter);
    return rc;
}

/**
 * Release object location
 * \see \a obj_location_get
 */
static void obj_location_free(struct layout_info *layout)
{
    dss_res_free(layout, 1);
}

static inline int xfer2open_flags(enum pho_xfer_flags flags)
{
    return (flags & PHO_XFER_OBJ_REPLACE) ? O_CREAT|O_WRONLY|O_TRUNC
                                          : O_CREAT|O_WRONLY|O_EXCL;
}

static int store_data_get(struct dss_handle *dss,
                          const struct pho_xfer_desc *desc)
{
    struct layout_info      *layout = NULL;
    struct layout_composer   comp = {0};
    struct pho_io_descr      iod  = {0};
    const char              *objid = desc->xd_objid;
    int                      fd;
    int                      rc;
    ENTRY;

    /* retrieve saved object location */
    rc = obj_location_get(dss, desc->xd_objid, &layout);
    if (rc)
        LOG_RETURN(rc, "Failed to get information about objid:'%s'", objid);

    assert(layout != NULL);

    /* make sure we can write to the target file */
    fd = open(desc->xd_fpath, xfer2open_flags(desc->xd_flags), 0640);
    if (fd < 0)
        LOG_GOTO(free_res, rc = -errno, "Failed to open %s for writing",
                 desc->xd_fpath);

    rc = layout_init(dss, &comp, LA_DECODE);
    if (rc)
        LOG_GOTO(out_close, rc, "Cannot initialize composite layout");

    rc = layout_declare(&comp, layout);
    if (rc)
        LOG_GOTO(out_freecomp, rc,
                 "Cannot declare layout to read objid:'%s'", objid);

    /* Prepare storage resources to read the object */
    rc = layout_acquire(&comp);
    if (rc)
        LOG_GOTO(out_freecomp, rc,
                 "Failed to prepare resources to read objid:'%s'", objid);

    /* Actually read data from the media */
    iod.iod_fd = fd;

    rc = layout_io(&comp, desc->xd_objid, &iod);
    if (rc)
        LOG_GOTO(out_freecomp, rc,
                 "Failed to issue data transfer for objid:'%s'", objid);

    rc = layout_commit(&comp, 0);
    if (rc)
        LOG_GOTO(out_freecomp, rc,
                 "Failed to complete operations for objid:'%s'", objid);

out_freecomp:
    /* Release storage resources */
    layout_fini(&comp);

out_close:
    close(fd);
    if (rc != 0 && unlink(desc->xd_fpath) != 0 && errno != ENOENT)
        pho_warn("Failed to clean '%s': %s", desc->xd_fpath, strerror(errno));

free_res:
    obj_location_free(layout);
    return rc;
}

static int store_attr_get(struct dss_handle *dss, struct pho_xfer_desc *desc)
{
    struct object_info  *obj;
    struct dss_filter    filter;
    int                  obj_cnt;
    int                  rc;
    ENTRY;

    rc = dss_filter_build(&filter, "{\"DSS::OBJ::oid\": \"%s\"}",
                          desc->xd_objid);
    if (rc)
        return rc;

    rc = dss_object_get(dss, &filter, &obj, &obj_cnt);
    if (rc)
        LOG_GOTO(filt_free, rc, "Cannot fetch objid:'%s'", desc->xd_objid);

    assert(obj_cnt <= 1);

    if (obj_cnt == 0)
        LOG_GOTO(out_free, rc = -ENOENT, "No such object objid:'%s'",
                 desc->xd_objid);

    rc = pho_json_to_attrs(desc->xd_attrs, obj[0].user_md);
    if (rc)
        LOG_GOTO(out_free, rc, "Cannot convert attributes of objid:'%s'",
                 desc->xd_objid);

out_free:
    dss_res_free(obj, obj_cnt);
filt_free:
    dss_filter_free(&filter);
    return rc;
}

int phobos_get(const struct pho_xfer_desc *desc, size_t n,
               pho_completion_cb_t cb, void *udata)
{
    struct pho_xfer_desc     desc_res = *desc; /* internal r/w copy */
    struct pho_attrs         attrs = {0};
    struct dss_handle        dss;
    int                      rc;

    /* XXX
     * Although we plan to eventually support it MGET is not yet implemented */
    if (n > 1)
        LOG_RETURN(-EINVAL, "Bulk GET not supported in this version");

    /* load configuration and get dss handle */
    rc = store_dss_init(&dss);
    if (rc)
        LOG_RETURN(rc, "Initialization failed");

    desc_res.xd_attrs = &attrs;
    rc = store_attr_get(&dss, &desc_res);
    if (rc)
        LOG_GOTO(out_notify, rc, "Cannot retrieve object attributes");

    /* Attributes only requested, skip actual object retrieval */
    if (desc->xd_flags & PHO_XFER_OBJ_GETATTR)
        GOTO(out_notify, rc = 0);

    rc = store_data_get(&dss, &desc_res);
    if (rc)
        LOG_GOTO(out_notify, rc, "Cannot retrieve object data");

out_notify:
    xfer_get_notify(cb, udata, &desc_res, rc);
    pho_attrs_free(&attrs);
    dss_fini(&dss);
    return rc;
}
