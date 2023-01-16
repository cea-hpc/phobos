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
 * \brief  Phobos Object Store implementation
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "phobos_store.h"
#include "pho_attrs.h"
#include "pho_cfg.h"
#include "pho_comm.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_io.h"
#include "pho_layout.h"
#include "pho_srl_lrs.h"
#include "pho_type_utils.h"
#include "pho_types.h"
#include "store_alias.h"
#include "store_utils.h"

#include <attr/xattr.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define RETRY_SLEEP_MAX_US (1000 * 1000) /* 1 second */
#define RETRY_SLEEP_MIN_US (10 * 1000)   /* 10 ms */

/**
 * List of configuration parameters for store
 */
enum pho_cfg_params_store {
    PHO_CFG_STORE_FIRST,

    /* store parameters */
    PHO_CFG_STORE_lrs_socket = PHO_CFG_STORE_FIRST,

    PHO_CFG_STORE_LAST
};

const struct pho_config_item cfg_store[] = {
    [PHO_CFG_STORE_lrs_socket] = {
        .section = "lrs",
        .name    = "server_socket",
        .value   = "/tmp/socklrs"
    },
};

/**
 * Phobos application state, eventually will offer methods to add transfers on
 * the fly.
 */
struct phobos_handle {
    struct dss_handle dss;          /**< DSS handle, configured from conf */
    struct pho_xfer_desc *xfers;    /**< Transfers being handled */
    struct pho_encoder *encoders;   /**< Encoders corresponding to xfers */
    size_t n_xfers;                 /**< Number of xfers */
    size_t n_ended_xfers;           /**< Number of "true" in `ended_xfers`,
                                      *  maintained for performance purposes
                                      */
    bool *ended_xfers;              /**< Array of bool, true means that the
                                      *  transfer at this index has been
                                      *  marked as ended (successful or failed)
                                      *  and no more work has to be done on it.
                                      */
    bool *md_created;              /**< Array of bool, true means that the
                                     *  metadata for this transfer were created
                                     *  in the DSS by this handle (it therefore
                                     *  may need to roll them back in case of
                                     *  failure)
                                     */

    struct pho_comm_info comm;      /**< Communication socket info. */

    pho_completion_cb_t cb;         /**< Callback called on xfer completion */
    void *udata;                    /**< User-provided argument to `cb` */
};

int phobos_init(void)
{
    int rc;

    rc = pho_context_init();
    if (rc)
        return rc;

    return 0;
}

void phobos_fini(void)
{
    pho_context_fini();
}

/**
 * Get a representative return code for the whole batch.
 * Used to provide other layers with indications about how things ended,
 * even though we have a per-transfer code for proper error management.
 *
 * The choice here is to return a media-global error code if any, otherwise the
 * first non zero rc, and finally zero if all the transfers succeeded.
 */
static int choose_xfer_rc(const struct pho_xfer_desc *xfers, size_t n)
{
    int rc = 0;
    int i;

    for (i = 0; i < n; i++) {
        if (is_medium_global_error(xfers[i].xd_rc))
            return xfers[i].xd_rc;
        else if (rc == 0 && xfers[i].xd_rc != 0)
            rc = xfers[i].xd_rc;
    }

    return rc;
}

/** Check for inconsistencies or unsupported feature in an xfer->xd_flags */
static int pho_xfer_desc_flag_check(const struct pho_xfer_desc *xfer)
{
    int flags = xfer->xd_flags;

    if (xfer->xd_op == PHO_XFER_OP_PUT && flags & PHO_XFER_OBJ_REPLACE)
        LOG_RETURN(-ENOTSUP, "OBJ_REPLACE not supported for put");

    if (xfer->xd_op == PHO_XFER_OP_GETMD && flags & PHO_XFER_OBJ_REPLACE)
        LOG_RETURN(0, "OBJ_REPLACE is not relevant for getmd");

    if (xfer->xd_op != PHO_XFER_OP_GET && flags & PHO_XFER_OBJ_BEST_HOST)
        LOG_RETURN(-EINVAL, "OBJ_BEST_HOST is only relevant for get");

    return 0;
}

/**
 * Build a decoder for this xfer by retrieving the xfer layout and initializing
 * the decoder from it. Only valid for GET xfers.
 *
 * @param[out]  dec     The decoder to be initialized
 * @param[in]   xfer    The xfer to be decoded
 * @param[in]   dss     A DSS handle to retrieve layout information for xfer
 *
 * @return 0 on success, -errno on error.
 */
static int decoder_build(struct pho_encoder *dec, struct pho_xfer_desc *xfer,
                         struct dss_handle *dss)
{
    struct layout_info *layout;
    struct dss_filter filter;
    int cnt = 0;
    int rc;

    assert(xfer->xd_op == PHO_XFER_OP_GET);

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                              "{\"DSS::EXT::uuid\": \"%s\"}, "
                              "{\"DSS::EXT::version\": \"%d\"}"
                          "]}",
                          xfer->xd_objuuid, xfer->xd_version);
    if (rc)
        LOG_RETURN(rc, "Cannot build filter");

    rc = dss_layout_get(dss, &filter, &layout, &cnt);
    dss_filter_free(&filter);
    if (rc)
        GOTO(err, rc);

    if (cnt == 0)
        GOTO(err, rc = -ENOENT);

    /* @FIXME: duplicate layout to avoid calling dss functions to free this? */
    rc = layout_decode(dec, xfer, layout);
    if (rc)
        GOTO(err, rc);

err:
    if (rc)
        dss_res_free(layout, cnt);
    return rc;
}

/**
 * Forward a response from the LRS to its destination encoder, collect this
 * encoder's next requests and forward them back to the LRS.
 *
 * @param[in/out]   enc     The encoder to give the response to.
 * @param[in]       ci      Communication information.
 * @param[in]       resp    The response to be forwarded to \a enc. Can be NULL
 *                          to generate the first request from \a enc.
 * @param[in]       enc_id  Identifier of this encoder (for request / response
 *                          tracking).
 *
 * @return 0 on success, -errno on error.
 */
static int encoder_communicate(struct pho_encoder *enc,
                               struct pho_comm_info *comm, pho_resp_t *resp,
                               int enc_id)
{
    pho_req_t *requests = NULL;
    struct pho_comm_data data;
    size_t n_reqs = 0;
    size_t i = 0;
    int rc;

    rc = layout_step(enc, resp, &requests, &n_reqs);
    if (rc)
        pho_error(rc, "Error while communicating with encoder for %s",
                  enc->xfer->xd_objid);

    /* Dispatch generated requests (even on error, if any) */
    for (i = 0; i < n_reqs; i++) {
        pho_req_t *req;
        int rc2 = 0;

        req = requests + i;

        pho_debug("%s for objid:'%s' emitted a request of type %s",
                  enc->is_decoder ? "Decoder" : "Encoder", enc->xfer->xd_objid,
                  pho_srl_request_kind_str(req));

        /* req_id is used to route responses to the appropriate encoder */
        req->id = enc_id;
        if (pho_request_is_write(req))
            req->walloc->family = enc->xfer->xd_params.put.family;

        data = pho_comm_data_init(comm);
        if (pho_srl_request_pack(req, &data.buf)) {
            pho_srl_request_free(req, false);
            return -ENOMEM;
        }
        pho_srl_request_free(req, false);

        /* Send the request to the socket */
        rc2 = pho_comm_send(&data);
        free(data.buf.buff);
        if (rc2) {
            pho_error(rc2, "Error while sending request to LRS for %s",
                      enc->xfer->xd_objid);
            rc = rc ? : rc2;
            continue;
        }
    }

    /* Free any undelivered request */
    for (; i < n_reqs; i++)
        pho_srl_request_free(requests + i, false);
    free(requests);

    return rc;
}

/**
 * Retrieve metadata associated with this xfer oid from the DSS and update the
 * \a xfer xd_attrs field accordingly.
 */
int object_md_get(struct dss_handle *dss, struct pho_xfer_desc *xfer)
{
    struct object_info  *obj;
    struct dss_filter    filter;
    int                  obj_cnt;
    int                  rc;

    ENTRY;

    rc = dss_filter_build(&filter, "{\"DSS::OBJ::oid\": \"%s\"}",
                          xfer->xd_objid);
    if (rc)
        return rc;

    rc = dss_object_get(dss, &filter, &obj, &obj_cnt);
    if (rc)
        LOG_GOTO(filt_free, rc, "Cannot fetch objid:'%s'", xfer->xd_objid);

    assert(obj_cnt <= 1);

    if (obj_cnt == 0)
        LOG_GOTO(out_free, rc = -ENOENT, "No such object objid:'%s'",
                 xfer->xd_objid);

    rc = pho_json_to_attrs(&xfer->xd_attrs, obj[0].user_md);
    if (rc)
        LOG_GOTO(out_free, rc, "Cannot convert attributes of objid:'%s'",
                 xfer->xd_objid);

    xfer->xd_objuuid = strdup(obj->uuid);
    xfer->xd_version = obj->version;

out_free:
    dss_res_free(obj, obj_cnt);
filt_free:
    dss_filter_free(&filter);
    return rc;
}

/**
 * Save this xfer oid and metadata (xd_attrs) into the DSS.
 */
int object_md_save(struct dss_handle *dss, struct pho_xfer_desc *xfer)
{
    GString *md_repr = g_string_new(NULL);
    struct object_info *obj_res = NULL;
    struct dss_filter filter;
    struct object_info obj;
    int obj_cnt;
    int rc2;
    int rc;

    ENTRY;

    rc = pho_attrs_to_json(&xfer->xd_attrs, md_repr, 0);
    if (rc)
        LOG_GOTO(out_md, rc, "Cannot convert attributes into JSON");

    obj.oid = xfer->xd_objid;
    obj.user_md = md_repr->str;

    rc = dss_lock(dss, DSS_OBJECT, &obj, 1);
    if (rc)
        LOG_GOTO(out_md, rc, "Unable to lock object objid: '%s'",
                 obj.oid);

    if (!xfer->xd_params.put.overwrite) {
        pho_debug("Storing object objid:'%s' (transient) with attributes: %s",
                  xfer->xd_objid, md_repr->str);

        rc = dss_object_set(dss, &obj, 1, DSS_SET_INSERT);
        if (rc)
            LOG_GOTO(out_unlock, rc, "dss_object_set failed for objid:'%s'",
                     obj.oid);

        goto out_update;
    } else {
        rc = dss_filter_build(&filter, "{\"DSS::OBJ::oid\": \"%s\"}", obj.oid);
        if (rc)
            LOG_GOTO(out_unlock, rc,
                     "Unable to build filter in object md save");

        rc = dss_object_get(dss, &filter, &obj_res, &obj_cnt);
        dss_filter_free(&filter);
        if (rc || obj_cnt == 0) {
            pho_verb("dss_object_get failed for objid:'%s'", xfer->xd_objid);

            /**
             * If we try overwritting an object that doesn't exist in the
             * object table, we treat the command as a normal
             * "object put", thus we insert the object we wanted to
             * overwrite with in the table.
             */
            if (rc == 0)
                pho_debug("Can't overwrite unexisting object:'%s'",
                         xfer->xd_objid);

            rc = dss_object_set(dss, &obj, 1, DSS_SET_INSERT);
            if (rc)
                LOG_GOTO(out_filt, rc, "dss_object_set failed for objid:'%s'",
                         xfer->xd_objid);

            goto out_update;
        }

        rc = dss_object_move(dss, DSS_OBJECT, DSS_DEPREC, obj_res, 1);
        if (rc)
            LOG_GOTO(out_res, rc, "object_move failed for objid:'%s'",
                     xfer->xd_objid);

        ++obj_res->version;
        if (!pho_attrs_is_empty(&xfer->xd_attrs))
            obj_res->user_md = md_repr->str;

        rc = dss_object_set(dss, obj_res, 1, DSS_SET_FULL_INSERT);
        if (rc)
            LOG_GOTO(out_res, rc, "object_set failed for objid:'%s'",
                     xfer->xd_objid);
    }

out_update:
    dss_res_free(obj_res, 1);

    rc = dss_filter_build(&filter, "{\"DSS::OBJ::oid\": \"%s\"}",
                          xfer->xd_objid);
    if (rc)
        LOG_GOTO(out_unlock, rc, "dss_filter_build failed");

    rc = dss_object_get(dss, &filter, &obj_res, &obj_cnt);
    if (rc)
        LOG_GOTO(out_filt, rc, "Cannot fetch objid:'%s'", xfer->xd_objid);

    xfer->xd_version = obj_res->version;
    xfer->xd_objuuid = strdup(obj_res->uuid);
    if (xfer->xd_objuuid == NULL)
        rc = -ENOMEM;

out_res:
    dss_res_free(obj_res, 1);

out_filt:
    dss_filter_free(&filter);

out_unlock:
    rc2 = dss_unlock(dss, DSS_OBJECT, &obj, 1, false);
    if (rc2) {
        rc = rc ? : rc2;
        pho_error(rc2,
                  "Couldn't unlock object:'%s'. Database may be corrupted.",
                  xfer->xd_objid);
    }
out_md:
    g_string_free(md_repr, true);

    return rc;
}

/**
 * Delete xfer metadata from the DSS by \a objid, making the oid free to be used
 * again (unless layout information still lay in the DSS).
 */
int object_md_del(struct dss_handle *dss, struct pho_xfer_desc *xfer)
{
    struct object_info lock_obj = { .oid = xfer->xd_objid };
    struct object_info *prev_obj = NULL;
    struct layout_info *layout = NULL;
    struct object_info *obj = NULL;
    bool need_undelete = false;
    struct dss_filter filter;
    int prev_cnt = 0;
    int obj_cnt = 0;
    int cnt = 0;
    int rc2;
    int rc;

    ENTRY;

    /* Retrieve object to get uuid and version info */
    rc = dss_filter_build(&filter, "{\"DSS::OBJ::oid\": \"%s\"}",
                          xfer->xd_objid);
    if (rc)
        LOG_RETURN(rc, "Couldn't build filter in md_del for objid:'%s'.",
                   xfer->xd_objid);

    rc = dss_lock(dss, DSS_OBJECT, &lock_obj, 1);
    if (rc) {
        dss_filter_free(&filter);
        LOG_RETURN(rc, "Unable to lock object objid: '%s'", xfer->xd_objid);
    }

    rc = dss_object_get(dss, &filter, &obj, &obj_cnt);
    dss_filter_free(&filter);
    if (rc)
        LOG_GOTO(out_unlock, rc, "dss_object_get failed for objid:'%s'",
                 xfer->xd_objid);

    if (obj_cnt != 1)
        LOG_GOTO(out_res, rc = -EINVAL, "object '%s' does not exist",
                 xfer->xd_objid);


    /* Check if the performed operation was an overwrite PUT */
    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          "  {\"DSS::OBJ::uuid\": \"%s\"},"
                          "  {\"DSS::OBJ::version\": %d}"
                          "]}", obj->uuid, obj->version - 1);
    if (rc)
        LOG_GOTO(out_res, rc,
                 "Couldn't build filter in md_del for object uuid:'%s'.",
                 obj->uuid);

    rc = dss_deprecated_object_get(dss, &filter, &prev_obj, &prev_cnt);
    dss_filter_free(&filter);
    if (rc)
        LOG_GOTO(out_res, rc, "dss_deprecated_object_get failed for uuid:'%s'",
                 obj->uuid);

    if (prev_cnt == 1)
        need_undelete = true;

    /* Ensure the oid isn't used by an existing layout before deleting it */
    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          "  {\"DSS::EXT::uuid\": \"%s\"},"
                          "  {\"DSS::EXT::version\": %d}"
                          "]}", obj->uuid, obj->version);
    if (rc)
        LOG_GOTO(out_prev, rc,
                 "Couldn't build filter in md_del for extent uuid:'%s'.",
                 obj->uuid);

    rc = dss_layout_get(dss, &filter, &layout, &cnt);
    dss_filter_free(&filter);
    if (rc)
        LOG_GOTO(out_prev, rc, "dss_layout_get failed for uuid:'%s'",
                 xfer->xd_objuuid);

    dss_res_free(layout, cnt);

    if (cnt > 0)
        LOG_GOTO(out_prev, rc = -EEXIST,
                 "Cannot rollback objid:'%s' from DSS, a layout still exists "
                 "for this objid", xfer->xd_objid);

    /* Then the rollback can safely happen */
    pho_verb("Rolling back obj oid:'%s', obj uuid:'%s' and obj version:'%d' "
             "from DSS", obj->oid, obj->uuid, obj->version);
    rc = dss_object_set(dss, obj, 1, DSS_SET_DELETE);
    if (rc)
        LOG_GOTO(out_prev, rc, "dss_object_set failed for objid:'%s'",
                 xfer->xd_objid);

    if (need_undelete) {
        rc = dss_object_move(dss, DSS_DEPREC, DSS_OBJECT, prev_obj, prev_cnt);
        if (rc)
            LOG_GOTO(out_prev, rc, "dss_object_move failed for uuid:'%s'",
                     obj->uuid);
    }

out_prev:
    dss_res_free(prev_obj, prev_cnt);

out_res:
    dss_res_free(obj, obj_cnt);

out_unlock:
    rc2 = dss_unlock(dss, DSS_OBJECT, &lock_obj, 1, false);
    if (rc2) {
        rc = rc ? : rc2;
        pho_error(rc2,
                  "Couldn't unlock object:'%s'. Database may be corrupted.",
                  xfer->xd_objid);
    }

    return rc;
}

/**
 * TODO: from object_delete to objects_delete, to delete many objects (all or
 * nothing) into the same command.
 */
static int object_delete(struct dss_handle *dss, struct pho_xfer_desc *xfer)
{
    struct object_info obj = { .oid = xfer->xd_objid };
    struct dss_filter filter;
    struct object_info *objs;
    int obj_cnt;
    int rc2;
    int rc;

    rc = dss_lock(dss, DSS_OBJECT, &obj, 1);
    if (rc)
        LOG_RETURN(rc, "Unable to get lock for oid %s before delete", obj.oid);

    /* checking oid exists into object table */
    rc = dss_filter_build(&filter, "{\"DSS::OBJ::oid\": \"%s\"}", obj.oid);
    if (rc)
        LOG_GOTO(out_unlock, rc, "Unable to build oid filter in object delete");

    rc = dss_object_get(dss, &filter, &objs, &obj_cnt);
    if (rc)
        LOG_GOTO(out_filter, rc, "Cannot fetch objid in object delete:'%s'",
                                 obj.oid);

    if (obj_cnt != 1) {
        rc = -ENOENT;
        LOG_GOTO(out_free, rc, "Unable to get one object in object delete "
                               "for oid: '%s'", obj.oid);
    }

    /* move from object table to deprecated object table */
    rc = dss_object_move(dss, DSS_OBJECT, DSS_DEPREC, &obj, 1);
    if (rc)
        LOG_GOTO(out_free, rc, "Unable to move from object to deprecated in "
                               "object delete, for oid: '%s'", obj.oid);

out_free:
    dss_res_free(objs, obj_cnt);
out_filter:
    dss_filter_free(&filter);
out_unlock:
    /* releasing oid lock */
    rc2 = dss_unlock(dss, DSS_OBJECT, &obj, 1, false);
    if (rc2) {
        pho_error(rc, "Unable to unlock at end of object delete (oid: %s)",
                  obj.oid);
        rc = rc ? : rc2;
    }
    return rc;
}

static int object_undelete(struct dss_handle *dss, struct pho_xfer_desc *xfer)
{
    struct dss_filter *p_filter_uuid = NULL;
    struct dss_filter *p_filter_oid = NULL;
    struct dss_filter filter_uuid;
    struct dss_filter filter_oid;
    struct object_info obj = {
        .oid = xfer->xd_objid,
        .uuid = xfer->xd_objuuid,
        .version = 0,
    };
    struct object_info *objs;
    char *buf_uuid = NULL;
    char *buf_oid = NULL;
    int n_objs;
    int rc2;
    int rc;
    int i;

    /* build uuid filter */
    if (obj.uuid) {
        rc = dss_filter_build(&filter_uuid,
                              "{\"DSS::OBJ::uuid\": \"%s\"}", obj.uuid);
        if (rc)
            LOG_RETURN(rc, "Unable to build uuid filter in object undelete");

        p_filter_uuid = &filter_uuid;
    }

    /* if oid is NULL: we get it from uuid */
    if (!obj.oid) {
        /* obj.oid == NULL => obj.uuid != NULL, we have the filter */
        rc = dss_deprecated_object_get(dss, p_filter_uuid, &objs, &n_objs);
        if (rc)
            LOG_GOTO(out, rc, "To undelete, unable to get oid from deprecated "
                              "object with uuid %s", obj.uuid);

        if (n_objs == 0) {
            dss_res_free(objs, n_objs);
            LOG_GOTO(out, rc = -ENOENT,
                     "Unable to undelete uuid %s, no entry found into "
                     "deprecated_object table", obj.uuid);
        }

        /* found oid from max version */
        for (i = 0; i < n_objs; i++) {
            if (objs[i].version > obj.version) {
                obj.oid = objs[i].oid;
                obj.version = objs[i].version;
            }
        }

        /* obj.oid is necessary for the unlock later, so we keep a copy */
        buf_oid = strdup(obj.oid);
        dss_res_free(objs, n_objs);
        if (!buf_oid)
            LOG_GOTO(out, rc = -ENOMEM,
                     "Unable to allocate buf_oid for oid:'%s'", obj.oid);

        obj.oid = buf_oid;
    }

    /* build oid filter */
    rc = dss_filter_build(&filter_oid, "{\"DSS::OBJ::oid\": \"%s\"}", obj.oid);
    if (rc)
        LOG_GOTO(out_id, rc, "Unable to build oid filter in object undelete");

    p_filter_oid = &filter_oid;

    /* LOCK */
    /* taking oid lock */
    rc = dss_lock(dss, DSS_OBJECT, &obj, 1);
    if (rc)
        LOG_GOTO(out_id, rc, "Unable to get lock for oid %s before undelete",
                 obj.oid);

    /* CHECK */
    /* check oid does not already exists in object table */
    rc = dss_object_get(dss, p_filter_oid, &objs, &n_objs);
    if (rc)
        LOG_GOTO(out_unlock, rc,
                 "To undelete, unable to get existing oid from object "
                 "with oid %s", obj.oid);

    dss_res_free(objs, n_objs);
    if (n_objs > 0) {
        rc = -EEXIST;
        LOG_GOTO(out_unlock, rc,
                 "Unable to undelete oid %s, existing entry found into object "
                 "table", obj.oid);
    }

    /* check oid/uuid/version in deprecated_object table */
    rc = dss_deprecated_object_get(dss, obj.uuid ? p_filter_uuid : p_filter_oid,
                                   &objs, &n_objs);
    if (rc)
        LOG_GOTO(out_unlock, rc,
                 "To undelete, unable to get %s from deprecated object with "
                 "%s %s",
                 obj.uuid ? "oid and version" : "uuid",
                 obj.uuid ? "uuid" : "oid",
                 obj.uuid ? : obj.oid);

    if (n_objs == 0) {
        rc = -ENOENT;
        LOG_GOTO(out_free, rc,
                 "Unable to undelete %s %s, no entry found into "
                 "deprecated_object table",
                 obj.uuid ? "uuid" : "oid", obj.uuid ? : obj.oid);
    }

    /* if not uuid: get uuid and check its unicity, get latest version */
    if (!obj.uuid) {
        /* found uuid and max version */
        obj.uuid = objs[0].uuid;
        obj.version = objs[0].version;
        for (i = 1; i < n_objs; i++) {
            /* check uuid unicity */
            if (strcmp(objs[i].uuid, obj.uuid)) {
                rc = -EINVAL;
                LOG_GOTO(out_free, rc,
                         "Unable to undelete oid %s because we find several "
                         "corresponding uuid into deprecated_object", obj.oid);
            }

            /* check latest version */
            if (objs[i].version > obj.version)
                obj.version = objs[i].version;
        }

        /* obj.uuid is necessary for the unlock later, so we keep a copy */
        buf_uuid = strdup(obj.uuid);
        if (!buf_uuid)
            LOG_GOTO(out, rc = -ENOMEM,
                     "Unable to allocate buf_uuid for uuid:'%s'", obj.uuid);

        obj.uuid = buf_uuid;
    } else {
    /* if uuid: get latest version and check oid */
        int max_version_id;

        /* found max version */
        obj.version = objs[0].version;
        max_version_id = 0;
        for (i = 1; i < n_objs; i++) {
            if (objs[i].version > obj.version) {
                obj.version = objs[i].version;
                max_version_id = i;
            }
        }

        /* check oid */
        if (strcmp(obj.oid, objs[max_version_id].oid)) {
            rc = -EINVAL;
            LOG_GOTO(out_free, rc,
                     "Unable to undelete oid %s / uuid %s, latest version "
                     "%d matches an other oid", obj.oid, obj.uuid, obj.version);
        }
    }

    /* MOVE */
    /* move from deprecated_object to object table */
    rc = dss_object_move(dss, DSS_DEPREC, DSS_OBJECT, &obj, 1);
    if (rc)
        LOG_GOTO(out_free, rc,
                 "Unable to move from deprecated_object to object in object "
                 "undelete for oid %s, uuid %s, version %d",
                 obj.oid, obj.uuid, obj.version);


out_free:
    dss_res_free(objs, n_objs);
out_unlock:
    /* UNLOCK */
    /* releasing oid lock */
    rc2 = dss_unlock(dss, DSS_OBJECT, &obj, 1, false);
    if (rc2) {
        pho_error(rc, "Unable to unlock at end of object undelete (oid: %s)",
                  obj.oid);
        rc = rc ? : rc2;
    }
out_id:
    free(buf_oid);
    free(buf_uuid);
out:
    dss_filter_free(p_filter_uuid);
    dss_filter_free(p_filter_oid);
    return rc;
}

static int object_info_copy_into_xfer(struct object_info *obj,
                                      struct pho_xfer_desc *xfer)
{
    int rc;

    /** XXX: This is a temporary fix as the uuid and attrs may already exist
     * before they are copied, so we free them beforehand.
     */
    pho_xfer_desc_clean(xfer);

    rc = pho_json_to_attrs(&xfer->xd_attrs, obj->user_md);
    if (rc)
        LOG_RETURN(rc, "Cannot convert attributes of objid: '%s'", obj->oid);

    rc = strdup_safe(&xfer->xd_objuuid, obj->uuid);
    if (rc) {
        pho_attrs_free(&xfer->xd_attrs);
        LOG_RETURN(rc, "Unable to duplicate object uuid: %s", obj->uuid);
    }

    xfer->xd_version = obj->version;
    return 0;
}

/**
 * Initialize an encoder or a decoder to perform \a xfer, according to
 * xfer->xd_op and xfer->xd_flags.
 */
static int init_enc_or_dec(struct pho_encoder *enc, struct dss_handle *dss,
                           struct pho_xfer_desc *xfer)
{
    struct object_info *obj;
    int rc;

    if (xfer->xd_op == PHO_XFER_OP_PUT)
        /* Handle encoder creation for PUT */
        return layout_encode(enc, xfer);

    /* can't get md for undel without any objid */
    /* TODO: really necessary to create decoder for getmd, del and undel OP ? */
    if (xfer->xd_op != PHO_XFER_OP_UNDEL && xfer->xd_op != PHO_XFER_OP_GET) {
        rc = object_md_get(dss, xfer);
        if (rc)
            LOG_RETURN(rc, "Cannot find metadata for objid:'%s'",
                       xfer->xd_objid);
    }

    if (xfer->xd_op == PHO_XFER_OP_GETMD ||
        xfer->xd_op == PHO_XFER_OP_DEL ||
        xfer->xd_op == PHO_XFER_OP_UNDEL) {
        /* create dummy decoder if no I/O operations are made */
        enc->xfer = xfer;
        enc->done = true;
        enc->is_decoder = true;
        return 0;
    }

    /* Handle decoder creation for GET */
    if (!xfer->xd_objid && !xfer->xd_objuuid)
        LOG_RETURN(rc = -EINVAL, "uuid or oid must be provided");

    rc = dss_lazy_find_object(dss, xfer->xd_objid, xfer->xd_objuuid,
                              xfer->xd_version, &obj);
    if (rc)
        LOG_RETURN(rc, "Cannot find metadata for objid:'%s'",
                   xfer->xd_objid);
    rc = object_info_copy_into_xfer(obj, xfer);
    object_info_free(obj);
    if (rc)
        return rc;

    return decoder_build(enc, xfer, dss);
}

static bool is_uuid_arg(struct pho_xfer_desc *xfer)
{
    return xfer->xd_op == PHO_XFER_OP_UNDEL &&
           xfer->xd_objuuid != NULL;
}

static const char *oid_or_uuid_val(struct pho_xfer_desc *xfer)
{
    return is_uuid_arg(xfer) ? xfer->xd_objuuid : xfer->xd_objid;
}

/**
 * Mark the end of a transfer (successful or not) by updating the encoder
 * structure, saving the encoder layout to the DSS if necessary, properly
 * positioning xfer->xd_rc and calling the termination callback.
 *
 * If this function is called twice for the same transfer, the operations will
 * only be performed once.
 *
 * @param[in]   pho         The phobos handle handling this encoder.
 * @param[in]   xfer_idx    The index of the terminating xfer in \a pho.
 * @param[in]   rc          The outcome of the xfer (replaces the xfer's xd_rc
 *                          if it was 0).
 */
static void store_end_xfer(struct phobos_handle *pho, size_t xfer_idx, int rc)
{
    struct pho_encoder *enc = &pho->encoders[xfer_idx];
    struct pho_xfer_desc *xfer = &pho->xfers[xfer_idx];

    /* Don't end an encoder twice */
    if (pho->ended_xfers[xfer_idx])
        return;

    /* Remember we ended this encoder */
    pho->ended_xfers[xfer_idx] = true;
    pho->n_ended_xfers++;
    enc->done = true;

    /* Once the encoder is done and successful, save the layout and metadata */
    if (!enc->is_decoder && xfer->xd_rc == 0 && rc == 0) {
        pho_debug("Saving layout for objid:'%s'", xfer->xd_objid);
        rc = dss_layout_set(&pho->dss, enc->layout, 1, DSS_SET_INSERT);
        if (rc)
            pho_error(rc, "Error while saving layout for objid:'%s'",
                      xfer->xd_objid);
    }

    /* Only overwrite xd_rc if it was 0 */
    if (xfer->xd_rc == 0 && rc != 0)
        xfer->xd_rc = rc;

    pho_info("%s operation for %s:'%s' %s",
             xfer_op2str(xfer->xd_op),
             is_uuid_arg(xfer) ? "uuid" : "oid",
             oid_or_uuid_val(xfer),
             xfer->xd_rc ? "failed" : "succeeded");

    /* Cleanup metadata for failed PUT */
    if (pho->md_created && pho->md_created[xfer_idx] &&
            xfer->xd_op == PHO_XFER_OP_PUT && xfer->xd_rc)
        object_md_del(&pho->dss, xfer);

    if (pho->cb)
        pho->cb(pho->udata, xfer, rc);
}

/**
 * Destroy a phobos handle and all associated resources. All unfinished
 * transfers will end with return code \a rc.
 */
static void store_fini(struct phobos_handle *pho, int rc)
{
    size_t i;

    /* Cleanup encoders */
    for (i = 0; i < pho->n_xfers; i++) {
        /**
         * Encoders that have not finished at this point are marked as failed
         * with the global rc
         */
        if (pho->ended_xfers && !pho->ended_xfers[i])
            store_end_xfer(pho, i, rc);

        /*
         * We allocated the decoder layouts from the dss (in decoder_build),
         * hence we also free them.
         */
        if (pho->encoders) {
            if (pho->encoders[i].is_decoder) {
                dss_res_free(pho->encoders[i].layout, 1);
                pho->encoders[i].layout = NULL;
            }
            layout_destroy(&pho->encoders[i]);
        }
    }

    free(pho->encoders);
    free(pho->ended_xfers);
    free(pho->md_created);
    pho->encoders = NULL;
    pho->ended_xfers = NULL;
    pho->md_created = NULL;

    rc = pho_comm_close(&pho->comm);
    if (rc)
        pho_error(rc, "Cannot close the communication socket");

    dss_fini(&pho->dss);
}

/**
 * Initialize a phobos handle with a set of transfers to perform.
 *
 * @param[out]  pho         Phobos handle to be initialized.
 * @param[in]   xfers       Transfers to be handled.
 * @param[in]   n_xfers     Number of transfers.
 * @param[in]   cb          Completion callback called on each transfer end (may
 *                          be NULL)
 * @param[in]   udata       Callback user data (may be NULL).
 *
 * @return 0 on success, -errno on error.
 */
static int store_init(struct phobos_handle *pho, struct pho_xfer_desc *xfers,
                      size_t n_xfers, pho_completion_cb_t cb, void *udata)
{
    const char *sock_path;
    size_t i;
    int rc;

    memset(pho, 0, sizeof(*pho));
    pho->comm = pho_comm_info_init();

    pho->xfers = xfers;
    pho->n_xfers = n_xfers;
    pho->cb = cb;
    pho->udata = udata;
    pho->n_ended_xfers = 0;
    pho->ended_xfers = NULL;
    pho->encoders = NULL;
    pho->md_created = NULL;

    /* Check xfers consistency */
    for (i = 0; i < n_xfers; i++) {
        rc = pho_xfer_desc_flag_check(&xfers[i]);
        if (rc)
            return rc;
    }

    /* Ensure conf is loaded */
    rc = pho_cfg_init_local(NULL);
    if (rc && rc != -EALREADY)
        return rc;

    sock_path = PHO_CFG_GET(cfg_store, PHO_CFG_STORE, lrs_socket);

    /* Connect to the DSS */
    rc = dss_init(&pho->dss);
    if (rc != 0)
        return rc;

    /* Connect to the LRS */
    rc = pho_comm_open(&pho->comm, sock_path, false);
    if (rc)
        LOG_GOTO(out, rc, "Cannot contact 'phobosd': will abort");

    /* Allocate memory for the encoders */
    pho->encoders = calloc(n_xfers, sizeof(*pho->encoders));
    if (pho->encoders == NULL)
        GOTO(out, rc = -ENOMEM);

    /* Allocate array of ended encoders for completion tracking */
    pho->ended_xfers = calloc(n_xfers, sizeof(*pho->ended_xfers));
    if (pho->ended_xfers == NULL)
        GOTO(out, rc = -ENOMEM);

    /*
     * Allocate array of booleans to track which encoders had their metadata
     * created.
     */
    pho->md_created = calloc(n_xfers, sizeof(*pho->md_created));
    if (pho->md_created == NULL)
        GOTO(out, rc = -ENOMEM);

    /* Initialize all the encoders */
    for (i = 0; i < n_xfers; i++) {
        pho_debug("Initializing %s %ld for objid:'%s'",
                  pho->encoders[i].is_decoder ? "decoder" : "encoder",
                  i, pho->xfers[i].xd_objid);
        rc = init_enc_or_dec(&pho->encoders[i], &pho->dss, &pho->xfers[i]);
        if (rc)
            pho_error(rc, "Error while creating encoders for objid:'%s'",
                      xfers[i].xd_objid);
        if (rc || pho->encoders[i].done)
            store_end_xfer(pho, i, rc);
        rc = 0;
    }

out:
    if (rc)
        store_fini(pho, rc);

    return rc;
}

static int store_lrs_response_process(struct phobos_handle *pho,
                                      pho_resp_t *resp)
{
    struct pho_encoder *encoder = &pho->encoders[resp->req_id];
    int rc;

    pho_debug("%s for objid:'%s' received a response of type %s",
              encoder->is_decoder ? "Decoder" : "Encoder",
              encoder->xfer->xd_objid,
              pho_srl_response_kind_str(resp));

    rc = encoder_communicate(encoder, &pho->comm, resp, resp->req_id);

    /* Success or failure final callback */
    if (rc || encoder->done)
        store_end_xfer(pho, resp->req_id, rc);

    if (rc)
        pho_error(rc, "Error while sending response to layout for %s",
                  encoder->xfer->xd_objid);

    return rc;
}

static int store_dispatch_loop(struct phobos_handle *pho)
{
    struct pho_comm_data *responses = NULL;
    int n_responses = 0;
    int rc = 0;
    int i;
    pho_resp_t **resps = NULL;

    /* Collect LRS responses */
    rc = pho_comm_recv(&pho->comm, &responses, &n_responses);
    if (rc) {
        for (i = 0; i < n_responses; ++i)
            free(responses[i].buf.buff);
        free(responses);
        LOG_RETURN(rc, "Error while collecting responses from LRS");
    }

    /* Deserialize LRS responses */
    if (n_responses) {
        resps = malloc(n_responses * sizeof(*resps));
        if (resps == NULL) {
            free(responses);
            return -ENOMEM;
        }

        for (i = 0; i < n_responses; ++i)
            resps[i] = pho_srl_response_unpack(&responses[i].buf);
        free(responses);
    }

    /* Dispatch LRS responses to encoders */
    for (i = 0; i < n_responses; i++) {
        /*
         * If an error occured on the deserialization, resps[i] is now null
         * just skip it.
         */
        if (!resps[i]) {
            pho_error(-EINVAL,
                      "an error occured during a response deserialization");
            continue;
        }

        rc = store_lrs_response_process(pho, resps[i]);
        pho_srl_response_free(resps[i], true);
        if (rc)
            break;
    }

    /*
     * If there are no new answer, it means no resource is available yet,
     * wait a bit before retrying.
     */
    if (n_responses == 0) {
        unsigned int rand_seed = getpid() + time(NULL);
        useconds_t sleep_time;

        sleep_time =
            (rand_r(&rand_seed) % (RETRY_SLEEP_MAX_US - RETRY_SLEEP_MIN_US))
            + RETRY_SLEEP_MIN_US;
        pho_info("No resource available to perform IO, retrying in %d ms",
                 sleep_time / 1000);
        usleep(sleep_time);
    }

    free(resps);

    return rc;
}

/**
 * Perform the main store loop:
 * - collect requests from encoders
 * - forward them to the LRS
 * - collect responses from the LRS
 * - dispatch them to the corresponding encoders
 * - handle potential xfer termination (successful or not)
 *
 * @param[in]   pho     Phobos handle describing the transfer.
 *
 * @return 0 on success, -errno on error.
 */
static int store_perform_xfers(struct phobos_handle *pho)
{
    size_t i;
    int rc = 0;

    /**
     * TODO: delete or undelete many objects (all or
     * nothing) into the same command.
     */
    /*
     * DELETE or UNDELETE : perform metada move
     *
     * PUT : Save object metadata as a way to "reserve" the OID and ensure its
     * unicity before performing any IO. From now on, any failed object must
     * have its metadata cleared from the DSS.
     */
    for (i = 0; i < pho->n_xfers; i++) {
        if (pho->xfers[i].xd_op == PHO_XFER_OP_DEL) {
            rc = object_delete(&pho->dss, &pho->xfers[i]);
            if (rc)
                pho_error(rc, "Error while deleting objid: '%s'",
                          pho->xfers[i].xd_objid);
            store_end_xfer(pho, i, rc);
        }

        if (pho->xfers[i].xd_op == PHO_XFER_OP_UNDEL) {
            rc = object_undelete(&pho->dss, &pho->xfers[i]);
            if (rc) {
                pho_error(rc, "Error while undeleting oid: '%s', uuid: '%s'",
                          pho->xfers[i].xd_objid ? pho->xfers[i].xd_objid :
                              "NULL",
                          pho->xfers[i].xd_objuuid ?
                              pho->xfers[i].xd_objuuid : "NULL");
            }
            store_end_xfer(pho, i, rc);
        }

        if (pho->xfers[i].xd_op != PHO_XFER_OP_PUT)
            continue;
        rc = object_md_save(&pho->dss, &pho->xfers[i]);
        if (rc && !pho->encoders[i].done) {
            pho_error(rc, "Error while saving metadata for objid:'%s'",
                      pho->xfers[i].xd_objid);
            store_end_xfer(pho, i, rc);
        }
        pho->md_created[i] = true;
    }

    /* Generate all first requests of encoders */
    for (i = 0; i < pho->n_xfers; i++) {
        if (pho->encoders[i].done)
            continue;

        rc = encoder_communicate(&pho->encoders[i], &pho->comm, NULL, i);
        if (rc)
            store_end_xfer(pho, i, rc);
    }

    /* Handle all encoders and forward messages between them and the LRS */
    while (pho->n_ended_xfers < pho->n_xfers) {
        rc = store_dispatch_loop(pho);
        if (rc)
            break;
    }

    return rc ? : choose_xfer_rc(pho->xfers, pho->n_xfers);
}

/**
 * Common function to handle PHO_XFER_OP_PUT, PHO_XFER_OP_GET and
 * PHO_XFER_OP_GETMD transfers.
 *
 * @param[in/out]   xfers   Transfers to be performed, they will be updated with
 *                          an appropriate xd_rc upon successful completion of
 *                          this function
 * @param[in]       n       Number of transfers.
 * @param[in]       cb      Xfer callback.
 * @param[in]       udata   Xfer callback user provided argument.
 *
 * @return 0 on success, -errno on error.
 */
static int phobos_xfer(struct pho_xfer_desc *xfers, size_t n,
                       pho_completion_cb_t cb, void *udata)
{
    struct phobos_handle pho;
    int rc;

    rc = store_init(&pho, xfers, n, cb, udata);
    if (rc)
        return rc;

    rc = store_perform_xfers(&pho);
    store_fini(&pho, rc);

    return rc;
}

int phobos_put(struct pho_xfer_desc *xfers, size_t n,
               pho_completion_cb_t cb, void *udata)
{
    size_t i;
    int rc;

    /* Ensure conf is loaded, to retrieve default values */
    rc = pho_cfg_init_local(NULL);
    if (rc && rc != -EALREADY)
        return rc;

    for (i = 0; i < n; i++) {
        xfers[i].xd_op = PHO_XFER_OP_PUT;
        xfers[i].xd_rc = 0;

        rc = fill_put_params(&xfers[i]);
        if (rc)
            return rc;
    }

    return phobos_xfer(xfers, n, cb, udata);
}

int phobos_get(struct pho_xfer_desc *xfers, size_t n,
               pho_completion_cb_t cb, void *udata)
{
    struct pho_xfer_desc *xfers_to_get = NULL;
    const char *hostname = NULL;
    size_t n_xfers_to_get = 0;
    size_t j = 0;
    int rc2 = 0;
    int rc = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        xfers[i].xd_op = PHO_XFER_OP_GET;
        xfers[i].xd_rc = 0;
        /* If the uuid is given by the user, we don't own that memory.
         * The simplest solution is to duplicate it here so that it can
         * be freed at the end by pho_xfer_desc_clean().
         *
         * The user of this function must free any allocated string passed to
         * the xfer.
         *
         * For the Python CLI, the garbage collector will take care of
         * this pointer.
         */
        if (xfers[i].xd_objuuid) {
            xfers[i].xd_objuuid = strdup(xfers[i].xd_objuuid);
            if (!xfers[i].xd_objuuid) {
                size_t j;

                for (j = 0; j < i; j++)
                    free(xfers[j].xd_objuuid);

                for (i = 0; i < n; ++i)
                    xfers[i].xd_rc = -ENOMEM;

                LOG_RETURN(-ENOMEM, "Cannot duplicate uuid before get");
            }
        }

        if (xfers[i].xd_flags & PHO_XFER_OBJ_BEST_HOST) {
            int nb_new_lock;

            if (!hostname) {
                hostname = get_hostname();
                if (!hostname) {
                    pho_warn("Get was cancelled for object '%s': "
                             "hostname couldn't be retrieved",
                             xfers[i].xd_objid);
                    xfers[i].xd_rc = -ECANCELED;
                    continue;
                }
            }

            rc2 = phobos_locate(xfers[i].xd_objid, xfers[i].xd_objuuid,
                                xfers[i].xd_version, hostname,
                                &xfers[i].xd_params.get.node_name,
                                &nb_new_lock);
            rc = rc ? : rc2;
            if (rc2) {
                pho_warn("Object objid:'%s' couldn't be located",
                         xfers[i].xd_objid);
                xfers[i].xd_rc = rc2;
            } else {
                if (strcmp(xfers[i].xd_params.get.node_name, hostname)) {
                    pho_warn("Object objid:'%s' located on node: %s",
                             xfers[i].xd_objid,
                             xfers[i].xd_params.get.node_name);
                    xfers[i].xd_rc = -EREMOTE;
                } else {
                    pho_info("Object objid:'%s' located on local node",
                             xfers[i].xd_objid);
                    n_xfers_to_get++;
                    free(xfers[i].xd_params.get.node_name);
                    xfers[i].xd_params.get.node_name = NULL;
                }
            }
        } else {
            n_xfers_to_get++;
        }
    }

    if (!n_xfers_to_get)
        return -EREMOTE;

    if (n_xfers_to_get == n)
        return phobos_xfer(xfers, n, cb, udata);

    xfers_to_get = malloc(n_xfers_to_get * sizeof(*xfers_to_get));
    if (!xfers_to_get)
        LOG_RETURN(-ENOMEM, "Couldn't allocate xfers_to_get");

    for (i = 0; i < n; ++i)
        if (xfers[i].xd_rc == 0)
            xfers_to_get[j++] = xfers[i];

    rc2 = phobos_xfer(xfers_to_get, n_xfers_to_get, cb, udata);
    rc = rc ? : rc2;

    for (j = 0, i = 0; i < n; ++i)
        if (xfers[i].xd_rc == 0)
            xfers[i] = xfers_to_get[j++];
    free(xfers_to_get);

    return rc;
}

int phobos_getmd(struct pho_xfer_desc *xfers, size_t n,
                 pho_completion_cb_t cb, void *udata)
{
    size_t i;

    for (i = 0; i < n; i++) {
        xfers[i].xd_op = PHO_XFER_OP_GETMD;
        xfers[i].xd_rc = 0;
    }

    return phobos_xfer(xfers, n, cb, udata);
}

int phobos_delete(struct pho_xfer_desc *xfers, size_t num_xfers)
{
    size_t i;

    for (i = 0; i < num_xfers; i++) {
        xfers[i].xd_op = PHO_XFER_OP_DEL;
        xfers[i].xd_rc = 0;
    }

    return phobos_xfer(xfers, num_xfers, NULL, NULL);
}

int phobos_undelete(struct pho_xfer_desc *xfers, size_t num_xfers)
{
    size_t i;

    for (i = 0; i < num_xfers; i++) {
        xfers[i].xd_op = PHO_XFER_OP_UNDEL;
        xfers[i].xd_rc = 0;
    }

    return phobos_xfer(xfers, num_xfers, NULL, NULL);
}

static void xfer_put_param_clean(struct pho_xfer_desc *xfer)
{
    tags_free(&xfer->xd_params.put.tags);
    pho_attrs_free(&xfer->xd_params.put.lyt_params);
}

static void (*xfer_param_cleaner[PHO_XFER_OP_LAST])(struct pho_xfer_desc *) = {
    xfer_put_param_clean,
    NULL,
};

void pho_xfer_desc_clean(struct pho_xfer_desc *xfer)
{
    if (xfer_param_cleaner[xfer->xd_op] != NULL)
        xfer_param_cleaner[xfer->xd_op](xfer);

    pho_attrs_free(&xfer->xd_attrs);
    free(xfer->xd_objuuid);
};

int phobos_locate(const char *oid, const char *uuid, int version,
                  const char *focus_host, char **hostname, int *nb_new_lock)
{
    struct object_info *obj = NULL;
    struct layout_info *layout;
    struct dss_filter filter;
    struct dss_handle dss;
    int cnt;
    int rc;

    *hostname = NULL;

    if (!uuid && !oid)
        LOG_RETURN(rc = -EINVAL, "uuid or oid must be provided");

    /* Ensure conf is loaded */
    rc = pho_cfg_init_local(NULL);
    if (rc && rc != -EALREADY)
        return rc;

    /* Connect to the DSS */
    rc = dss_init(&dss);
    if (rc)
        return rc;

    /* find object */
    rc = dss_lazy_find_object(&dss, oid, uuid, version, &obj);
    if (rc)
        LOG_GOTO(clean, rc, "Unable to find object to locate");

    /* find layout to locate media */
    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                              "{\"DSS::EXT::oid\": \"%s\"}, "
                              "{\"DSS::EXT::uuid\": \"%s\"}, "
                              "{\"DSS::EXT::version\": \"%d\"}"
                          "]}",
                          obj->oid, obj->uuid, obj->version);
    if (rc)
        LOG_GOTO(clean, rc,
                 "Unable to build filter oid %s uuid %s version %d to get "
                 "layout from extent", obj->oid, obj->uuid, obj->version);

    rc = dss_layout_get(&dss, &filter, &layout, &cnt);
    dss_filter_free(&filter);
    if (rc)
        GOTO(clean, rc);

    assert(cnt == 1);

    /* locate media */
    rc = layout_locate(&dss, layout, focus_host, hostname, nb_new_lock);
    dss_res_free(layout, cnt);

clean:
    object_info_free(obj);
    dss_fini(&dss);
    return rc;
}
