/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2025 CEA/DAM.
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
#include "pho_dss_wrapper.h"
#include "pho_io.h"
#include "pho_layout.h"
#include "pho_srl_lrs.h"
#include "pho_type_utils.h"
#include "pho_types.h"
#include "store_profile.h"
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
    [PHO_CFG_STORE_lrs_socket] = LRS_SOCKET_CFG_ITEM,
};

/**
 * Phobos application state, eventually will offer methods to add transfers on
 * the fly.
 */
struct phobos_handle {
    struct dss_handle dss;          /**< DSS handle, configured from conf */
    struct pho_xfer_desc *xfers;    /**< Transfers being handled */
    struct pho_data_processor *processors;
                                    /**< Processors corresponding to xfers */
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

static const char *get_xfer_param_reference_copy_name(
                                               const struct pho_xfer_desc *xfer)
{
    switch (xfer->xd_op) {
    case PHO_XFER_OP_COPY:
        return xfer->xd_params.copy.get.copy_name;
    case PHO_XFER_OP_GET:
        return xfer->xd_params.get.copy_name;
    case PHO_XFER_OP_PUT:
        return xfer->xd_params.put.copy_name;
    default:
        return NULL;
    }
}

/**
 * Build a decoder for this xfer by retrieving the xfer layout and initializing
 * the decoder from it. Only valid for GET / COPY / ERASE xfers.
 *
 * @param[out]  decoder The decoder to be initialized
 * @param[in]   xfer    The xfer to be decoded
 * @param[in]   dss     A DSS handle to retrieve layout information for xfer
 *
 * @return 0 on success, -errno on error.
 */
static int decoder_build(struct pho_data_processor *decoder,
                         struct pho_xfer_desc *xfer, struct dss_handle *dss)
{
    const char *copy_name = get_xfer_param_reference_copy_name(xfer);
    struct layout_info *layout;
    struct dss_filter filter;
    struct copy_info *copy;
    int cnt = 0;
    int rc;

    assert(xfer->xd_op == PHO_XFER_OP_GET || xfer->xd_op == PHO_XFER_OP_DEL ||
           xfer->xd_op == PHO_XFER_OP_COPY);

    rc = dss_lazy_find_copy(dss, xfer->xd_targets->xt_objuuid,
                            xfer->xd_targets->xt_version, copy_name, &copy);
    if (rc)
        LOG_RETURN(rc, "Cannot get copy");

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                              "{\"DSS::LYT::object_uuid\": \"%s\"}, "
                              "{\"DSS::LYT::version\": \"%d\"},"
                              "{\"DSS::LYT::copy_name\": \"%s\"}"
                          "]}",
                          xfer->xd_targets->xt_objuuid,
                          xfer->xd_targets->xt_version,
                          copy->copy_name);
    if (rc)
        LOG_GOTO(err, rc, "Cannot build filter");

    rc = dss_full_layout_get(dss, &filter, NULL, &layout, &cnt, NULL);
    dss_filter_free(&filter);
    if (rc)
        GOTO(err, rc);

    if (cnt == 0)
        GOTO(err, rc = -ENOENT);

    /* @FIXME: duplicate layout to avoid calling dss functions to free this? */
    if (xfer->xd_op == PHO_XFER_OP_GET)
        rc = layout_decoder(decoder, xfer, layout);
    else if (xfer->xd_op == PHO_XFER_OP_COPY)
        rc = layout_copier(decoder, xfer, layout);
    else
        rc = layout_eraser(decoder, xfer, layout);

    if (rc)
        GOTO(err, rc);

err:
    copy_info_free(copy);
    if (rc)
        dss_res_free(layout, cnt);

    return rc;
}

static int send_generated_requests(struct pho_data_processor *proc,
                                   struct pho_comm_info *comm, int enc_id,
                                   pho_req_t *requests, size_t n_reqs, int rc)
{
    struct pho_comm_data data;
    size_t i;

    for (i = 0; i < n_reqs; i++) {
        pho_req_t *req;
        int rc2 = 0;

        req = requests + i;

        pho_debug("%s emitted a request of type %s", processor_type2str(proc),
                  pho_srl_request_kind_str(req));

        /* req_id is used to route responses to the appropriate encoder */
        req->id = enc_id;
        if (pho_request_is_write(req)) {
            struct pho_xfer_put_params *put_params;

            if (proc->xfer->xd_op == PHO_XFER_OP_COPY)
                put_params = &proc->xfer->xd_params.copy.put;
            else
                put_params = &proc->xfer->xd_params.put;

            req->walloc->family = put_params->family;
            req->walloc->library = xstrdup_safe(put_params->library);
            req->walloc->grouping = xstrdup_safe(put_params->grouping);
        }

        data = pho_comm_data_init(comm);
        pho_srl_request_pack(req, &data.buf);
        pho_srl_request_free(req, false);

        /* Send the request to the socket */
        rc2 = pho_comm_send(&data);
        free(data.buf.buff);
        if (rc2) {
            pho_error(rc2, "Error while sending request to LRS");
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

static int first_data_processor_call(struct pho_data_processor *proc,
                                     struct pho_comm_info *comm, int enc_id)
{
    pho_req_t *requests = NULL;
    size_t n_reqs = 0;
    int rc = 0;

    if (is_encoder(proc)) {
        /* init reader */
        rc = proc->reader_ops->step(proc, NULL, &requests, &n_reqs);
        if (rc)
            return rc;

        assert(n_reqs == 0); /* an encoder reader generates no request */
        /* init writer */
        rc = proc->writer_ops->step(proc, NULL, &requests, &n_reqs);
    } else if (is_decoder(proc)) {
        /* init writer */
        rc = proc->writer_ops->step(proc, NULL, &requests, &n_reqs);
        if (rc)
            return rc;

        assert(n_reqs == 0); /* an encoder writer generates no request */
        /* init reader */
        rc = proc->reader_ops->step(proc, NULL, &requests, &n_reqs);
    } else if (is_eraser(proc)) {
        /* init eraser */
        rc = proc->eraser_ops->step(proc, NULL, &requests, &n_reqs);
    } else {
        /* copier */
        /* init reader */
        rc = proc->reader_ops->step(proc, NULL, &requests, &n_reqs);
        if (rc)
            return rc;

        assert(n_reqs == 1);
        rc = send_generated_requests(proc, comm, enc_id, requests, n_reqs, 0);
        if (rc)
            return rc;

        /* init writer */
        rc = proc->writer_ops->step(proc, NULL, &requests, &n_reqs);
    }

    if (rc)
        return rc;

    assert(n_reqs == 1);
    return send_generated_requests(proc, comm, enc_id, requests, n_reqs, 0);
}

/**
 * Forward a response from the LRS to its destination processor, collect this
 * processor's next requests and forward them back to the LRS.
 *
 * @param[in/out]   proc    The data processor to give the response to.
 * @param[in]       comm    Communication information.
 * @param[in]       resp    The response to be forwarded to \a proc. Can be NULL
 *                          to generate the first request from \a proc.
 * @param[in]       enc_id  Identifier of this data processor (for request or
 *                          response tracking).
 *
 * @return 0 on success, -errno on error.
 */
static int processor_communicate(struct pho_data_processor *proc,
                                 struct pho_comm_info *comm, pho_resp_t *resp,
                                 int enc_id)
{
    bool writer_done = false;
    bool reader_done = false;
    pho_req_t *requests = NULL;
    size_t n_reqs = 0;
    int response_kind;
    int rc = 0;

    ENTRY;

    response_kind = request_kind_from_response(resp);
    if (response_kind < 0)
        LOG_RETURN(-EPROTO, "Unable to get response kind to chose processor");

    if (is_eraser(proc)) {
        rc = proc->eraser_ops->step(proc, resp, &requests, &n_reqs);
        /* an eraser needs no io */
        return send_generated_requests(proc, comm, enc_id, requests, n_reqs,
                                       rc);
    } else if (response_kind == PHO_REQUEST_KIND__RQ_WRITE ||
               response_kind == PHO_REQUEST_KIND__RQ_RELEASE_WRITE) {
        rc = proc->writer_ops->step(proc, resp, &requests, &n_reqs);
        if (n_reqs)
            writer_done = true;
    } else {
        assert(response_kind == PHO_REQUEST_KIND__RQ_READ ||
               response_kind == PHO_REQUEST_KIND__RQ_RELEASE_READ);
        rc = proc->reader_ops->step(proc, resp, &requests, &n_reqs);
        if (n_reqs)
            reader_done = true;
    }

    rc = send_generated_requests(proc, comm, enc_id, requests, n_reqs, rc);
    requests = NULL;
    n_reqs = 0;
    if (rc)
        return rc;


    /* A release generates no new IO. */
    if (pho_response_is_release(resp) &&
        !pho_response_is_partial_release(resp)) {
        return rc;
    }

    /* allocating buffer if ready to be done */
    if (!proc->buff.size) {
        if (proc->reader_stripe_size && proc->writer_stripe_size)
            pho_buff_alloc(&proc->buff, lcm(proc->reader_stripe_size,
                                            proc->writer_stripe_size));
        else
            return rc;
    }

    /* process buffer data until we face an error */
    /* no more io needed after a completed write step */
    while (!rc && !writer_done) {
        /* try read first */
        if (!reader_done && proc->reader_offset == proc->writer_offset &&
            !proc->object_size == 0) {
            rc = proc->reader_ops->step(proc, NULL, &requests, &n_reqs);
            if (n_reqs) {
                rc = send_generated_requests(proc, comm, enc_id, requests,
                                             n_reqs, rc);
                requests = NULL;
                n_reqs = 0;
                reader_done = true;
            }
        }

        /* only one posix writing is enough for decoder after a read step */
        if (!rc && reader_done && is_decoder(proc)) {
            rc = proc->writer_ops->step(proc, NULL, &requests, &n_reqs);
            assert(n_reqs == 0);
            return rc;
        }

        /* then try following write */
        if (!rc && (proc->reader_offset > proc->writer_offset ||
                proc->object_size == 0)) {
            rc = proc->writer_ops->step(proc, NULL, &requests, &n_reqs);
            if (n_reqs) {
                /* no more io needed after a completed write step */
                return send_generated_requests(proc, comm, enc_id, requests,
                                               n_reqs, rc);
            }
        }
    }

    return rc;
}

/**
 * Retrieve metadata associated with this xfer oid from the DSS and update the
 * \a xfer xd_attrs field accordingly.
 */
int object_md_get(struct dss_handle *dss, struct pho_xfer_target *xfer)
{
    struct dss_filter filter;
    struct object_info *obj;
    int obj_cnt;
    int rc;

    ENTRY;

    rc = dss_filter_build(&filter, "{\"DSS::OBJ::oid\": \"%s\"}",
                          xfer->xt_objid);
    if (rc)
        return rc;

    rc = dss_object_get(dss, &filter, &obj, &obj_cnt, NULL);
    if (rc)
        LOG_GOTO(filt_free, rc, "Cannot fetch objid:'%s'", xfer->xt_objid);

    assert(obj_cnt <= 1);

    if (obj_cnt == 0)
        LOG_GOTO(out_free, rc = -ENOENT, "No such object objid:'%s'",
                 xfer->xt_objid);

    rc = pho_json_to_attrs(&xfer->xt_attrs, obj[0].user_md);
    if (rc)
        LOG_GOTO(out_free, rc, "Cannot convert attributes of objid:'%s'",
                 xfer->xt_objid);

    xfer->xt_objuuid = xstrdup(obj->uuid);
    xfer->xt_version = obj->version;

out_free:
    dss_res_free(obj, obj_cnt);
filt_free:
    dss_filter_free(&filter);
    return rc;
}

/**
 * Save this xfer oid and metadata (xd_attrs) into the DSS.
 */
int object_md_save(struct dss_handle *dss, struct pho_xfer_target *xfer,
                   bool overwrite, const char *grouping, const char *copy_name)
{
    GString *md_repr = g_string_new(NULL);
    struct object_info *obj_res = NULL;
    struct object_info obj = {0};
    struct copy_info copy = {0};
    struct dss_filter filter;
    int obj_cnt;
    int rc2;
    int rc;

    ENTRY;

    rc = pho_attrs_to_json(&xfer->xt_attrs, md_repr, 0);
    if (rc)
        LOG_GOTO(out_md, rc, "Cannot convert attributes into JSON");

    obj.oid = xfer->xt_objid;
    obj.user_md = md_repr->str;
    obj.grouping = grouping;

    rc = dss_lock(dss, DSS_OBJECT, &obj, 1);
    if (rc)
        LOG_GOTO(out_md, rc, "Unable to lock object objid: '%s'",
                 obj.oid);

    if (!overwrite) {
        pho_debug("Storing object objid:'%s' (transient) with attributes: %s",
                  xfer->xt_objid, md_repr->str);

        rc = dss_object_insert(dss, &obj, 1, DSS_SET_INSERT);
        if (rc)
            LOG_GOTO(out_unlock, rc, "dss_object_insert failed for objid:'%s'",
                     obj.oid);

        goto out_update;
    } else {
        const char *save_obj_res_grouping;

        rc = dss_filter_build(&filter, "{\"DSS::OBJ::oid\": \"%s\"}", obj.oid);
        if (rc)
            LOG_GOTO(out_unlock, rc,
                     "Unable to build filter in object md save");

        rc = dss_object_get(dss, &filter, &obj_res, &obj_cnt, NULL);
        dss_filter_free(&filter);
        if (rc || obj_cnt == 0) {
            pho_verb("dss_object_get failed for objid:'%s'", xfer->xt_objid);

            /**
             * If we try overwritting an object that doesn't exist in the
             * object table, we treat the command as a normal
             * "object put", thus we insert the object we wanted to
             * overwrite with in the table.
             */
            if (rc == 0)
                pho_debug("Can't overwrite unexisting object:'%s'",
                         xfer->xt_objid);

            rc = dss_object_insert(dss, &obj, 1, DSS_SET_INSERT);
            if (rc)
                LOG_GOTO(out_filt, rc,
                         "dss_object_insert failed for objid:'%s'",
                         xfer->xt_objid);

            goto out_update;
        }

        rc = dss_move_object_to_deprecated(dss, obj_res, 1);
        if (rc)
            LOG_GOTO(out_res, rc, "object_move failed for objid:'%s'",
                     xfer->xt_objid);

        save_obj_res_grouping = obj_res->grouping;
        obj_res->grouping = grouping;
        ++obj_res->version;
        if (!pho_attrs_is_empty(&xfer->xt_attrs))
            obj_res->user_md = md_repr->str;

        rc = dss_object_insert(dss, obj_res, 1, DSS_SET_FULL_INSERT);
        obj_res->grouping = save_obj_res_grouping;
        if (rc)
            LOG_GOTO(out_res, rc, "object_insert failed for objid:'%s'",
                     xfer->xt_objid);
    }

out_update:
    dss_res_free(obj_res, 1);

    rc = dss_filter_build(&filter, "{\"DSS::OBJ::oid\": \"%s\"}",
                          xfer->xt_objid);
    if (rc)
        LOG_GOTO(out_unlock, rc, "dss_filter_build failed");

    rc = dss_object_get(dss, &filter, &obj_res, &obj_cnt, NULL);
    if (rc)
        LOG_GOTO(out_filt, rc, "Cannot fetch objid:'%s'", xfer->xt_objid);

    copy.object_uuid = obj_res->uuid;
    copy.version = obj_res->version;
    copy.copy_status = PHO_COPY_STATUS_INCOMPLETE;

    if (copy_name) {
        copy.copy_name = copy_name;
    } else {
        rc = get_cfg_default_copy_name(&copy.copy_name);
        if (rc)
            LOG_GOTO(out_res, rc, "Cannot get default copy_name from conf");
    }

    rc = dss_copy_insert(dss, &copy, 1);
    if (rc)
        LOG_GOTO(out_res, rc, "Cannot insert copy");

    xfer->xt_version = obj_res->version;
    xfer->xt_objuuid = xstrdup(obj_res->uuid);

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
                  xfer->xt_objid);
    }

out_md:
    g_string_free(md_repr, true);

    return rc;
}

/**
 * Delete xfer metadata from the DSS by \a objid, making the oid free to be used
 * again (unless layout information still lay in the DSS).
 */
int object_md_del(struct dss_handle *dss, struct pho_xfer_target *xfer,
                  const char *copy_name)
{
    struct object_info lock_obj = { .oid = xfer->xt_objid };
    struct object_info *prev_obj = NULL;
    struct layout_info *layout = NULL;
    struct object_info *obj = NULL;
    struct copy_info *copy = NULL;
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
                          xfer->xt_objid);
    if (rc)
        LOG_RETURN(rc, "Couldn't build filter in md_del for objid:'%s'.",
                   xfer->xt_objid);

    rc = dss_lock(dss, DSS_OBJECT, &lock_obj, 1);
    if (rc) {
        dss_filter_free(&filter);
        LOG_RETURN(rc, "Unable to lock object objid: '%s'", xfer->xt_objid);
    }

    rc = dss_object_get(dss, &filter, &obj, &obj_cnt, NULL);
    dss_filter_free(&filter);
    if (rc)
        LOG_GOTO(out_unlock, rc, "dss_object_get failed for objid:'%s'",
                 xfer->xt_objid);

    if (obj_cnt != 1)
        LOG_GOTO(out_res, rc = -EINVAL, "object '%s' does not exist",
                 xfer->xt_objid);

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

    rc = dss_deprecated_object_get(dss, &filter, &prev_obj, &prev_cnt, NULL);
    dss_filter_free(&filter);
    if (rc)
        LOG_GOTO(out_res, rc, "dss_deprecated_object_get failed for uuid:'%s'",
                 obj->uuid);

    if (prev_cnt == 1)
        need_undelete = true;

    /* Retrieve default copy of the object */
    rc = dss_lazy_find_copy(dss, obj->uuid, obj->version, copy_name, &copy);
    if (rc)
        LOG_GOTO(out_prev, rc, "Cannot find copy for objid:'%s'", obj->oid);

    /* Ensure the oid isn't used by an existing layout before deleting it */
    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          "  {\"DSS::LYT::object_uuid\": \"%s\"},"
                          "  {\"DSS::LYT::version\": %d},"
                          "  {\"DSS::LYT::copy_name\": \"%s\"}"
                          "]}", obj->uuid, obj->version, copy->copy_name);
    if (rc)
        LOG_GOTO(out_copy, rc,
                 "Couldn't build filter in md_del for extent uuid:'%s'.",
                 obj->uuid);

    rc = dss_layout_get(dss, &filter, &layout, &cnt);
    dss_filter_free(&filter);
    if (rc)
        LOG_GOTO(out_copy, rc, "dss_layout_get failed for uuid:'%s'",
                 xfer->xt_objuuid);

    dss_res_free(layout, cnt);

    if (cnt > 0)
        LOG_GOTO(out_copy, rc = -EEXIST,
                 "Cannot rollback objid:'%s' from DSS, a layout still exists "
                 "for this objid", xfer->xt_objid);

    /* Then the rollback can safely happen */
    pho_verb("Rolling back obj oid:'%s', obj uuid:'%s' and obj version:'%d' "
             "from DSS", obj->oid, obj->uuid, obj->version);

    rc = dss_copy_delete(dss, copy, 1);
    if (rc)
        LOG_GOTO(out_copy, rc, "dss_copy_delete failed for objid:'%s'",
                 xfer->xt_objid);

    rc = dss_object_delete(dss, obj, 1);
    if (rc)
        LOG_GOTO(out_copy, rc, "dss_object_delete failed for objid:'%s'",
                 xfer->xt_objid);

    if (need_undelete) {
        rc = dss_move_deprecated_to_object(dss, prev_obj, prev_cnt);
        if (rc)
            LOG_GOTO(out_copy, rc, "dss_object_move failed for uuid:'%s'",
                     obj->uuid);
    }

out_copy:
    copy_info_free(copy);

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
                  xfer->xt_objid);
    }

    return rc;
}

/**
 * TODO: from object_delete to objects_delete, to delete many objects (all or
 * nothing) into the same command.
 */
static int object_delete(struct dss_handle *dss, struct pho_xfer_target *xfer)
{
    struct object_info obj = { .oid = xfer->xt_objid };
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

    rc = dss_object_get(dss, &filter, &objs, &obj_cnt, NULL);
    dss_filter_free(&filter);
    if (rc)
        LOG_GOTO(out_unlock, rc, "Cannot fetch objid in object delete:'%s'",
                 obj.oid);

    if (obj_cnt != 1) {
        rc = -ENOENT;
        LOG_GOTO(out_free, rc, "Unable to get one object in object delete "
                               "for oid: '%s'", obj.oid);
    }

    /* move from object table to deprecated object table */
    rc = dss_move_object_to_deprecated(dss, &obj, 1);
    if (rc)
        LOG_GOTO(out_free, rc,
                "Unable to move from object to deprecated in "
                "object delete, for oid: '%s'", obj.oid);

out_free:
    dss_res_free(objs, obj_cnt);
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

static int object_undelete(struct dss_handle *dss, struct pho_xfer_target *xfer)
{
    struct dss_filter *p_filter_uuid = NULL;
    struct dss_filter *p_filter_oid = NULL;
    struct dss_filter filter_uuid;
    struct dss_filter filter_oid;
    struct object_info obj = {
        .oid = xfer->xt_objid,
        .uuid = xfer->xt_objuuid,
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
        rc = dss_deprecated_object_get(dss, p_filter_uuid, &objs, &n_objs,
                                       NULL);
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
        buf_oid = xstrdup(obj.oid);
        dss_res_free(objs, n_objs);

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
    rc = dss_object_get(dss, p_filter_oid, &objs, &n_objs, NULL);
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
                                   &objs, &n_objs, NULL);
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
        buf_uuid = xstrdup(obj.uuid);
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

    rc = dss_move_deprecated_to_object(dss, &obj, 1);
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
                                      struct pho_xfer_target *xfer)
{
    int rc;

    /** XXX: This is a temporary fix as the uuid and attrs may already exist
     * before they are copied, so we free them beforehand.
     */
    pho_xfer_clean(xfer);

    rc = pho_json_to_attrs(&xfer->xt_attrs, obj->user_md);
    if (rc)
        LOG_RETURN(rc, "Cannot convert attributes of objid: '%s'", obj->oid);

    xfer->xt_objuuid = xstrdup_safe(obj->uuid);
    xfer->xt_version = obj->version;

    return 0;
}

/**
 * Initialize a data processor to perform \a xfer, according to xfer->xd_op and
 * xfer->xd_flags.
 */
static int init_enc_or_dec(struct pho_data_processor *proc,
                           struct dss_handle *dss, struct pho_xfer_desc *xfer)
{
    struct object_info *obj;
    struct copy_info *copy;
    int rc;

    if (xfer->xd_op == PHO_XFER_OP_PUT)
        /* Handle encoder creation for PUT */
        return layout_encoder(proc, xfer);

    /* can't get md for undel without any objid */
    /* TODO: really necessary to create decoder for getmd, del and undel OP ? */
    if (xfer->xd_op != PHO_XFER_OP_UNDEL && xfer->xd_op != PHO_XFER_OP_GET &&
        (xfer->xd_op != PHO_XFER_OP_DEL &&
         !(xfer->xd_flags & PHO_XFER_OBJ_HARD_DEL))) {
        rc = object_md_get(dss, xfer->xd_targets);
        if (rc)
            LOG_RETURN(rc, "Cannot find metadata for objid:'%s'",
                       xfer->xd_targets->xt_objid);
    }

    if (xfer->xd_op == PHO_XFER_OP_GETMD ||
        (xfer->xd_op == PHO_XFER_OP_DEL &&
         !(xfer->xd_flags & PHO_XFER_OBJ_HARD_DEL)) ||
        xfer->xd_op == PHO_XFER_OP_UNDEL) {
        /* create dummy decoder if no I/O operations are made */
        proc->xfer = xfer;
        proc->done = true;
        proc->type = PHO_PROC_DECODER;
        return 0;
    }

    /* Handle decoder creation for COPY & GET & HARD DELETE */
    if (!xfer->xd_targets->xt_objid && !xfer->xd_targets->xt_objuuid)
        LOG_RETURN(rc = -EINVAL, "uuid or oid must be provided");

    if (xfer->xd_op == PHO_XFER_OP_GET)
        /* Keep dss_lazy_find with the get to keep the same behavior */
        rc = dss_lazy_find_object(dss, xfer->xd_targets->xt_objid,
                                  xfer->xd_targets->xt_objuuid,
                                  xfer->xd_targets->xt_version, &obj);
    else
        rc = dss_find_object(dss, xfer->xd_targets->xt_objid,
                             xfer->xd_targets->xt_objuuid,
                             xfer->xd_targets->xt_version,
                             xfer->xd_op == PHO_XFER_OP_DEL ?
                                xfer->xd_params.delete.scope :
                                xfer->xd_params.copy.get.scope,
                             &obj);
    if (rc)
        LOG_RETURN(rc, "Cannot find object for objid:'%s'",
                   xfer->xd_targets->xt_objid);

    rc = dss_lazy_find_copy(dss, obj->uuid, obj->version,
                            get_xfer_param_reference_copy_name(xfer), &copy);
    if (rc)
        LOG_RETURN(rc, "Cannot find copy for objid:'%s'", obj->oid);

    if (copy->copy_status == PHO_COPY_STATUS_INCOMPLETE) {
        pho_error(rc = -ENOENT, "Status of copy '%s' for the object '%s' is "
                  "incomplete, cannot be reconstructed", copy->copy_name,
                  obj->oid);
        copy_info_free(copy);
        return rc;
    }

    if (copy->copy_status != PHO_COPY_STATUS_COMPLETE)
        pho_warn("Copy '%s' status for the object '%s' is %s.", copy->copy_name,
                 obj->oid, copy_status2str(copy->copy_status));

    copy_info_free(copy);
    rc = object_info_copy_into_xfer(obj, xfer->xd_targets);
    object_info_free(obj);
    if (rc)
        return rc;

    return decoder_build(proc, xfer, dss);
}

static bool is_uuid_arg(struct pho_xfer_target *xfer, enum pho_xfer_op xd_op)
{
    return xd_op == PHO_XFER_OP_UNDEL && xfer->xt_objuuid != NULL;
}

static const char *oid_or_uuid_val(struct pho_xfer_target *xfer,
                                   enum pho_xfer_op xd_op)
{
    return is_uuid_arg(xfer, xd_op) ? xfer->xt_objuuid : xfer->xt_objid;
}

static int store_end_delete_xfer(struct phobos_handle *pho,
                                 struct pho_xfer_desc *xfer,
                                 struct pho_data_processor *proc)
{
    struct extent *extents = proc->src_layout->extents;
    int ext_count = proc->src_layout->ext_count;
    struct dss_handle *dss = &pho->dss;
    struct copy_info copy = {
        .object_uuid = xfer->xd_targets->xt_objuuid,
        .version = xfer->xd_targets->xt_version,
        .copy_name = proc->src_layout->copy_name,
    };
    struct object_info obj = {
        .oid = xfer->xd_targets->xt_objid,
        .uuid = xfer->xd_targets->xt_objuuid,
        .version = xfer->xd_targets->xt_version,
    };
    bool medium_is_tape;
    int rc;
    int i;

    medium_is_tape = ext_count != 0 ?
        extents[0].media.family == PHO_RSC_TAPE :
        false;

    rc = dss_layout_delete(dss, proc->src_layout, 1);
    if (rc)
        LOG_RETURN(rc, "Unable to delete layouts for object '%s:%d'",
                   obj.uuid, obj.version);

    if (ext_count > 0) {
        if (medium_is_tape) {
            for (i = 0; i < ext_count; ++i)
                extents[i].state = PHO_EXT_ST_ORPHAN;
            rc = dss_extent_update(dss, extents, extents, ext_count);
        } else {
            rc = dss_extent_delete(dss, extents, ext_count);
        }
    }

    if (rc)
        LOG_RETURN(rc, "Unable to %s object '%s:%d' extents",
                   medium_is_tape ? "update" : "delete",
                   obj.uuid, obj.version);

    rc = dss_copy_delete(dss, &copy, 1);
    if (rc)
        LOG_RETURN(rc, "Unable to delete copy of object '%s:%d'",
                   obj.uuid, obj.version);

    /* The object to delete can be alive or deprecated but there is no way to
     * know. So we move it to deprecated first, then we delete it.
     */
    if (xfer->xd_params.delete.scope != DSS_OBJ_DEPRECATED_ONLY) {
        rc = dss_move_object_to_deprecated(dss, &obj, 1);
        if (rc)
            LOG_RETURN(rc, "Unable to move object '%s:%d' to deprecated",
                       obj.uuid, obj.version);
    }

    rc = dss_deprecated_object_delete(dss, &obj, 1);
    if (rc)
        pho_error(rc, "Unable to delete object '%s:%d'", obj.uuid, obj.version);

    return rc;
}

/**
 * Mark the end of a transfer (successful or not) by updating the processor
 * structure, saving the data processor layout to the DSS if necessary, properly
 * positioning xfer->xd_rc and calling the termination callback.
 *
 * If this function is called twice for the same transfer, the operations will
 * only be performed once.
 *
 * @param[in]   pho         The phobos handle handling this processor.
 * @param[in]   xfer_idx    The index of the terminating xfer in \a pho.
 * @param[in]   rc          The outcome of the xfer (replaces the xfer's xd_rc
 *                          if it was 0).
 */
static void store_end_xfer(struct phobos_handle *pho, size_t xfer_idx, int rc)
{
    struct pho_data_processor *proc = &pho->processors[xfer_idx];
    struct pho_xfer_desc *xfer = &pho->xfers[xfer_idx];
    int rc2 = 0;
    int i, j;

    /* Don't end an encoder twice */
    if (pho->ended_xfers[xfer_idx])
        return;

    /* Remember we ended this encoder */
    pho->ended_xfers[xfer_idx] = true;
    pho->n_ended_xfers++;
    proc->done = true;

    /* Once the encoder is done and successful, save the layout and metadata */
    if ((is_encoder(proc) || is_copier(proc)) && xfer->xd_rc == 0 && rc == 0) {
        for (i = 0; i < xfer->xd_ntargets; i++) {
            pho_debug("Saving layout for objid:'%s'",
                      xfer->xd_targets[i].xt_objid);
            rc2 = dss_extent_insert(&pho->dss, proc->dest_layout[i].extents,
                                    proc->dest_layout[i].ext_count);
            if (rc2) {
                pho_error(rc2, "Error while saving extents for objid: '%s'",
                          xfer->xd_targets[i].xt_objid);
                rc = rc ? : rc2;
                break;
            } else {
                rc2 = dss_layout_insert(&pho->dss, &proc->dest_layout[i], 1);
                if (rc2) {
                    pho_error(rc2, "Error while saving layout for objid: '%s'",
                              xfer->xd_targets[i].xt_objid);
                    rc = rc ? : rc2;

                    for (j = 0; j < proc->dest_layout[i].ext_count; ++j)
                        proc->dest_layout[i].extents[j].state =
                            PHO_EXT_ST_ORPHAN;

                    rc2 = dss_extent_update(&pho->dss,
                                            proc->dest_layout[i].extents,
                                            proc->dest_layout[i].extents,
                                            proc->dest_layout[i].ext_count);

                    if (rc2) {
                        pho_error(rc2,
                                  "Error while updating extents to orphan");
                        rc = rc ? : rc2;
                        break;
                    }
                    break;
                } else {
                    struct copy_info copy = {
                        .object_uuid = xfer->xd_targets[i].xt_objuuid,
                        .version = xfer->xd_targets[i].xt_version,
                        .copy_name = proc->dest_layout[i].copy_name,
                        .copy_status = PHO_COPY_STATUS_COMPLETE,
                    };

                    rc2 = dss_copy_update(&pho->dss, &copy, &copy, 1,
                                          DSS_COPY_UPDATE_COPY_STATUS);
                    if (rc2) {
                        pho_error(rc2,
                                  "Error while updating copy status "
                                  "to complete");
                        rc = rc ? : rc2;
                        break;
                    }
                }
            }
        }
    }


    if (xfer->xd_rc == 0 && rc == 0 && xfer->xd_op == PHO_XFER_OP_GET) {
        struct copy_info *copy;

        rc = dss_lazy_find_copy(&pho->dss, xfer->xd_targets->xt_objuuid,
                                xfer->xd_targets->xt_version, NULL, &copy);
        if (rc)
            LOG_GOTO(cont, rc,
                     "Error while retrieving copy info, "
                     "will skip access time update");

        rc = gettimeofday(&copy->access_time, NULL);
        if (rc) {
            copy_info_free(copy);
            LOG_GOTO(cont, rc,
                     "Error while retrieving current time, "
                     "will skip access time update");
        }

        rc = dss_copy_update(&pho->dss, copy, copy, 1,
                             DSS_COPY_UPDATE_ACCESS_TIME);
        copy_info_free(copy);
        if (rc)
            pho_error(rc, "Error while updating copy access time");
    }

    if (xfer->xd_op == PHO_XFER_OP_DEL &&
        xfer->xd_flags & PHO_XFER_OBJ_HARD_DEL && xfer->xd_rc == 0 && rc == 0)
        rc = store_end_delete_xfer(pho, xfer, proc);

cont:
    /* Only overwrite xd_rc if it was 0 */
    if (xfer->xd_rc == 0 && rc != 0)
        xfer->xd_rc = rc;

    for (i = 0; i < xfer->xd_ntargets; i++)
        pho_info("%s operation for %s:'%s' %s",
                 xfer_op2str(xfer->xd_op),
                 is_uuid_arg(&xfer->xd_targets[i], xfer->xd_op) ?
                    "uuid" : "oid",
                 oid_or_uuid_val(&xfer->xd_targets[i], xfer->xd_op),
                 xfer->xd_rc ? "failed" : "succeeded");

    /* Cleanup metadata for failed PUT */
    if (pho->md_created && pho->md_created[xfer_idx] &&
            xfer->xd_op == PHO_XFER_OP_PUT && xfer->xd_rc) {
        for (i = 0; i < xfer->xd_ntargets; i++)
            object_md_del(&pho->dss, &xfer->xd_targets[i],
                          xfer->xd_params.put.copy_name);
    }

    /* Cleanup metadata for failed COPY */
    if (pho->md_created && pho->md_created[xfer_idx] &&
            xfer->xd_op == PHO_XFER_OP_COPY && xfer->xd_rc) {
        for (i = 0; i < xfer->xd_ntargets; i++) {
            struct copy_info copy = {0};
            int rc2;

            copy.object_uuid = xfer->xd_targets[i].xt_objuuid;
            copy.version = xfer->xd_targets[i].xt_version;
            if (xfer->xd_params.copy.put.copy_name) {
                copy.copy_name = xfer->xd_params.copy.put.copy_name;
            } else {
                rc2 = get_cfg_default_copy_name(&copy.copy_name);
                if (rc2) {
                    pho_error(rc2,
                              "Cannot get default copy_name from conf");
                    rc = rc ? : rc2;
                    continue;
                }
            }

            rc2 = dss_copy_delete(&pho->dss, &copy, 1);
            if (rc2) {
                pho_error(rc2, "dss_copy_delete failed for objuuid:'%s'",
                          xfer->xd_targets[i].xt_objuuid);
                rc = rc ? : rc2;
            }
        }
    }

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

    /* Cleanup processors */
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
        if (pho->processors) {
            if (is_decoder(&pho->processors[i]) ||
                is_eraser(&pho->processors[i]) ||
                is_copier(&pho->processors[i])) {
                dss_res_free(pho->processors[i].src_layout, 1);
                pho->processors[i].src_layout = NULL;
            }

            layout_destroy(&pho->processors[i]);
        }
    }

    free(pho->processors);
    free(pho->ended_xfers);
    free(pho->md_created);
    pho->processors = NULL;
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
    union pho_comm_addr sock_addr = {0};
    int rc, rc2 = 0;
    size_t i;

    memset(pho, 0, sizeof(*pho));
    pho->comm = pho_comm_info_init();

    pho->xfers = xfers;
    pho->n_xfers = n_xfers;
    pho->cb = cb;
    pho->udata = udata;
    pho->n_ended_xfers = 0;
    pho->ended_xfers = NULL;
    pho->processors = NULL;
    pho->md_created = NULL;

    /* Check xfers consistency */
    for (i = 0; i < n_xfers; i++) {
        rc = pho_xfer_desc_flag_check(&xfers[i]);
        if (rc)
            return rc;

        /* Xfer can only contain more than 1 object if it's a PUT operation
         * with the no_split option.
         */
        if (xfers->xd_ntargets > 1 && !xfers->xd_params.put.no_split &&
            xfers->xd_op != PHO_XFER_OP_PUT)
            return -ENOTSUP;
    }

    /* Ensure conf is loaded */
    rc = pho_cfg_init_local(NULL);
    if (rc && rc != -EALREADY)
        return rc;

    sock_addr.af_unix.path = PHO_CFG_GET(cfg_store, PHO_CFG_STORE, lrs_socket);

    /* Connect to the DSS */
    rc = dss_init(&pho->dss);
    if (rc != 0)
        return rc;

    /* Connect to the LRS */
    rc = pho_comm_open(&pho->comm, &sock_addr, PHO_COMM_UNIX_CLIENT);
    if (rc)
        LOG_GOTO(out, rc, "Cannot contact 'phobosd': will abort");

    /* Allocate memory for the processors */
    pho->processors = xcalloc(n_xfers, sizeof(*pho->processors));

    /* Allocate array of ended processors for completion tracking */
    pho->ended_xfers = xcalloc(n_xfers, sizeof(*pho->ended_xfers));

    /*
     * Allocate array of booleans to track which processors had their metadata
     * created.
     */
    pho->md_created = xcalloc(n_xfers, sizeof(*pho->md_created));

    /* Initialize all the processors */
    for (i = 0; i < n_xfers; i++) {
        pho_debug("Initializing %s %ld for %d objid(s)",
                  processor_type2str(&pho->processors[i]), i,
                  pho->xfers[i].xd_ntargets);
        rc2 = init_enc_or_dec(&pho->processors[i], &pho->dss, &pho->xfers[i]);
        if (rc2) {
            pho_error(rc2, "Error while creating processors for %d objid(s)",
                      pho->xfers[i].xd_ntargets);
            rc = rc ? : rc2;
        }

        if (rc2 || pho->processors[i].done)
            store_end_xfer(pho, i, rc);
        rc2 = 0;
    }

out:
    if (rc)
        store_fini(pho, rc);

    return rc;
}

static int store_lrs_response_process(struct phobos_handle *pho,
                                      pho_resp_t *resp)
{
    struct pho_data_processor *proc = &pho->processors[resp->req_id];
    int rc;

    pho_debug("%s %d for %d objid(s) received a response of type %s",
              processor_type2str(proc), resp->req_id,
              proc->xfer->xd_ntargets, pho_srl_response_kind_str(resp));

    rc = processor_communicate(proc, &pho->comm, resp, resp->req_id);

    /* Success or failure final callback */
    if (rc || proc->done)
        store_end_xfer(pho, resp->req_id, rc);

    if (rc)
        pho_error(rc, "Error while sending response to layout for %s %d",
                  processor_type2str(proc), resp->req_id);

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
        resps = xmalloc(n_responses * sizeof(*resps));

        for (i = 0; i < n_responses; ++i)
            resps[i] = pho_srl_response_unpack(&responses[i].buf);
        free(responses);
    }

    /* Dispatch LRS responses to processors */
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
 * - collect requests from processors
 * - forward them to the LRS
 * - collect responses from the LRS
 * - dispatch them to the corresponding processors
 * - handle potential xfer termination (successful or not)
 *
 * @param[in]   pho     Phobos handle describing the transfer.
 *
 * @return 0 on success, -errno on error.
 */
static int store_perform_xfers(struct phobos_handle *pho)
{
    int rc2 = 0;
    size_t i, j;
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
        if (pho->xfers[i].xd_op == PHO_XFER_OP_DEL &&
            !(pho->xfers[i].xd_flags & PHO_XFER_OBJ_HARD_DEL)) {
            rc = object_delete(&pho->dss, pho->xfers[i].xd_targets);
            if (rc)
                pho_error(rc, "Error while deleting objid: '%s'",
                              pho->xfers[i].xd_targets->xt_objid);
            store_end_xfer(pho, i, rc);
        }

        if (pho->xfers[i].xd_op == PHO_XFER_OP_UNDEL) {
            rc = object_undelete(&pho->dss, pho->xfers[i].xd_targets);
            if (rc) {
                pho_error(rc, "Error while undeleting oid: '%s', uuid: '%s'",
                          pho->xfers[i].xd_targets->xt_objid ?
                                pho->xfers[i].xd_targets->xt_objid : "NULL",
                          pho->xfers[i].xd_targets->xt_objuuid ?
                                pho->xfers[i].xd_targets->xt_objuuid : "NULL");
            }
            store_end_xfer(pho, i, rc);
        }

        if (pho->xfers[i].xd_op != PHO_XFER_OP_PUT &&
            pho->xfers[i].xd_op != PHO_XFER_OP_COPY)
            continue;

        if (pho->xfers[i].xd_op == PHO_XFER_OP_COPY) {
            for (j = 0; j < pho->xfers[i].xd_ntargets; j++) {
                struct copy_info copy = {0};

                copy.object_uuid = pho->xfers[i].xd_targets[j].xt_objuuid;
                copy.version = pho->xfers[i].xd_targets[j].xt_version;
                copy.copy_status = PHO_COPY_STATUS_INCOMPLETE;
                if (pho->xfers[i].xd_params.copy.put.copy_name) {
                    copy.copy_name = pho->xfers[i].xd_params.copy.put.copy_name;
                } else {
                    rc2 = get_cfg_default_copy_name(&copy.copy_name);
                    if (rc2) {
                        pho_error(rc2,
                                  "Cannot get default copy_name from conf");
                        rc = rc ? : rc2;
                        break;
                    }
                }

                rc2 = dss_copy_insert(&pho->dss, &copy, 1);
                if (rc2) {
                    pho_error(rc2, "Cannot insert copy");
                    rc = rc ? : rc2;
                    break;
                }
            }

            if (rc && !pho->processors[i].done) {
                store_end_xfer(pho, i, rc);
                break;
            }

            pho->md_created[i] = true;
            continue;
        }

        /* PHO_XFER_OP_PUT */
        for (j = 0; j < pho->xfers[i].xd_ntargets; j++) {
            rc2 = object_md_save(&pho->dss, &pho->xfers[i].xd_targets[j],
                                 pho->xfers[i].xd_params.put.overwrite,
                                 pho->xfers[i].xd_params.put.grouping,
                                 pho->xfers[i].xd_params.put.copy_name);
            if (rc2) {
                pho_error(rc2, "Error while saving metadata for objid:'%s'",
                          pho->xfers[i].xd_targets[i].xt_objid);
                rc = rc ? : rc2;
                break;
            }
        }

        if (rc && !pho->processors[i].done) {
            store_end_xfer(pho, i, rc);
            continue;
        }
        pho->md_created[i] = true;
    }

    /* Generate all first requests of processors */
    for (i = 0; i < pho->n_xfers; i++) {
        if (pho->processors[i].done)
            continue;

        rc = first_data_processor_call(&pho->processors[i], &pho->comm, i);
        if (rc)
            store_end_xfer(pho, i, rc);
    }

    /* Handle all processors and forward messages between them and the LRS */
    while (pho->n_ended_xfers < pho->n_xfers) {
        rc = store_dispatch_loop(pho);
        if (rc)
            break;
    }

    return rc ? : choose_xfer_rc(pho->xfers, pho->n_xfers);
}

/**
 * Common function to handle PHO_XFER_OP_PUT, PHO_XFER_OP_GET,
 * PHO_XFER_OP_GETMD and PHO_XFER_OP_COPY transfers.
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
        if (xfers[i].xd_targets->xt_objuuid)
            xfers[i].xd_targets->xt_objuuid =
                xstrdup(xfers[i].xd_targets->xt_objuuid);

        if (xfers[i].xd_flags & PHO_XFER_OBJ_BEST_HOST) {
            int nb_new_lock;

            if (!hostname) {
                hostname = get_hostname();
                if (!hostname) {
                    pho_warn("Get was cancelled for object '%s': "
                             "hostname couldn't be retrieved",
                             xfers[i].xd_targets->xt_objid);
                    xfers[i].xd_rc = -ECANCELED;
                    continue;
                }
            }

            rc2 = phobos_locate(xfers[i].xd_targets->xt_objid,
                                xfers[i].xd_targets->xt_objuuid,
                                xfers[i].xd_targets->xt_version, hostname,
                                &xfers[i].xd_params.get.node_name,
                                &nb_new_lock);
            rc = rc ? : rc2;
            if (rc2) {
                pho_warn("Object objid:'%s' couldn't be located",
                         xfers[i].xd_targets->xt_objid);
                xfers[i].xd_rc = rc2;
            } else {
                if (strcmp(xfers[i].xd_params.get.node_name, hostname)) {
                    pho_warn("Object objid:'%s' located on node: %s",
                             xfers[i].xd_targets->xt_objid,
                             xfers[i].xd_params.get.node_name);
                    xfers[i].xd_rc = -EREMOTE;
                } else {
                    pho_info("Object objid:'%s' located on local node",
                             xfers[i].xd_targets->xt_objid);
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

    xfers_to_get = xmalloc(n_xfers_to_get * sizeof(*xfers_to_get));

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
        if (xfers[i].xd_targets->xt_objuuid)
            xfers[i].xd_targets->xt_objuuid =
                xstrdup(xfers[i].xd_targets->xt_objuuid);
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

int phobos_rename(const char *old_oid, const char *uuid, char *new_oid)
{
    struct object_info *deprec_objects = NULL;
    struct object_info *objects = NULL;
    struct dss_filter filter;
    struct dss_handle dss;
    int objects_count = 0;
    int deprec_count = 0;
    int rc;

    if (old_oid == NULL && uuid == NULL)
        LOG_RETURN(rc = -EINVAL, "Either oid or uuid must be provided");
    else if (old_oid && uuid)
        LOG_RETURN(rc = -EINVAL,
                   "Cannot rename by giving both an oid and a uuid");

    if (!new_oid)
        LOG_GOTO(clean, rc = -EINVAL, "No new object id provided");

    /* Ensure conf is loaded */
    rc = pho_cfg_init_local(NULL);
    if (rc && rc != -EALREADY)
        LOG_GOTO(clean, rc, "Cannot init access to local config parameters");

    /* Connect to the DSS */
    rc = dss_init(&dss);
    if (rc)
        LOG_GOTO(clean, rc, "Cannot initialize a connection handle");

    /* Only oid is provided for rename, so retrieve the living object with this
     * oid, is any exists
     */
    if (uuid == NULL) {
        struct dss_filter living_filter;

        rc = dss_filter_build(&living_filter, "{\"DSS::OBJ::oid\": \"%s\"}",
                              old_oid);
        if (rc)
            LOG_GOTO(clean, rc,
                     "Cannot build filter for object oid '%s'", old_oid);

        rc = dss_object_get(&dss, &living_filter, &objects, &objects_count,
                            NULL);
        dss_filter_free(&living_filter);
        if (rc)
            LOG_GOTO(clean, rc,
                     "Error while trying to get objects with oid '%s'",
                     old_oid);

        if (objects_count == 0)
            LOG_GOTO(clean, rc = -ENOENT,
                     "Couldn't find objects with oid '%s' to rename", old_oid);

        uuid = objects[0].uuid;
    }

    rc = dss_filter_build(&filter, "{\"DSS::OBJ::uuid\": \"%s\"}", uuid);
    if (rc)
        LOG_GOTO(clean, rc,
                 "Cannot build filter for object oid '%s'", old_oid);

    /* If old_oid is NULL, we don't know yet if a living object with the given
     * uuid exists, so attempt to retrieve it.
     */
    if (old_oid == NULL) {
        rc = dss_object_get(&dss, &filter, &objects, &objects_count, NULL);
        if (rc) {
            dss_filter_free(&filter);
            LOG_GOTO(clean, rc,
                     "Error while trying to get objects with uuid '%s'", uuid);
        }
    }

    rc = dss_deprecated_object_get(&dss, &filter,
                                   &deprec_objects, &deprec_count, NULL);
    dss_filter_free(&filter);
    if (rc)
        LOG_GOTO(clean, rc,
                 "Error while trying to get deprecated objects with uuid '%s'",
                 uuid);

    if (deprec_count == 0 && objects_count == 0)
        LOG_GOTO(clean, rc = -ENOENT,
                 "Couldn't find objects with uuid '%s' to rename", uuid);

    /* Rename object */
    rc = dss_object_rename(&dss, objects, objects_count,
                           deprec_objects, deprec_count, new_oid);
    if (rc)
        LOG_GOTO(clean, rc,
                 "Failed to rename objects with %s '%s' to rename",
                 old_oid ? "oid" : "uuid", old_oid ? old_oid : uuid);

clean:
    if (deprec_objects)
        dss_res_free(deprec_objects, deprec_count);
    if (objects)
        dss_res_free(objects, objects_count);

    if (old_oid)
        uuid = NULL;

    dss_fini(&dss);

    return rc;
}

static void xfer_put_param_clean(struct pho_xfer_desc *xfer)
{
    struct pho_xfer_put_params *put_params;

    if (xfer->xd_op == PHO_XFER_OP_COPY)
        put_params = &xfer->xd_params.copy.put;
    else
        put_params = &xfer->xd_params.put;

    string_array_free(&put_params->tags);
    pho_attrs_free(&put_params->lyt_params);
}

static void (*xfer_param_cleaner[PHO_XFER_OP_LAST])(struct pho_xfer_desc *) = {
    xfer_put_param_clean,
    NULL,
};

void pho_xfer_desc_clean(struct pho_xfer_desc *xfer)
{
    int i;

    if (xfer_param_cleaner[xfer->xd_op] != NULL)
        xfer_param_cleaner[xfer->xd_op](xfer);

    for (i = 0; i < xfer->xd_ntargets; i++)
        pho_xfer_clean(&xfer->xd_targets[i]);
}

void pho_xfer_clean(struct pho_xfer_target *xfer)
{
    pho_attrs_free(&xfer->xt_attrs);
    free(xfer->xt_objuuid);
    xfer->xt_objuuid = NULL;
}

int phobos_locate(const char *oid, const char *uuid, int version,
                  const char *focus_host, char **hostname, int *nb_new_lock)
{
    struct object_info *obj = NULL;
    struct copy_info *copy = NULL;
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

    /* find default copy */
    rc = dss_lazy_find_copy(&dss, obj->uuid, obj->version, NULL, &copy);
    if (rc)
        LOG_GOTO(clean, rc,
                 "Unable to find the default copy of the object to locate");

    /* find layout to locate media */
    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                              "{\"DSS::LYT::object_uuid\": \"%s\"}, "
                              "{\"DSS::LYT::version\": \"%d\"},"
                              "{\"DSS::LYT::copy_name\": \"%s\"}"
                          "]}",
                          obj->uuid, obj->version, copy->copy_name);
    if (rc)
        LOG_GOTO(clean, rc,
                 "Unable to build filter oid %s uuid %s version %d to get "
                 "layout from extent", obj->oid, obj->uuid, obj->version);

    rc = dss_full_layout_get(&dss, &filter, NULL, &layout, &cnt, NULL);
    dss_filter_free(&filter);
    if (rc)
        GOTO(clean, rc);

    assert(cnt == 1);

    /* locate media */
    rc = layout_locate(&dss, layout, focus_host, hostname, nb_new_lock);
    dss_res_free(layout, cnt);

clean:
    copy_info_free(copy);
    object_info_free(obj);
    dss_fini(&dss);
    return rc;
}

int phobos_copy(struct pho_xfer_desc *xfers, size_t n,
                pho_completion_cb_t cb, void *udata)
{
    size_t i;
    int rc;

    /* Ensure conf is loaded, to retrieve default values */
    rc = pho_cfg_init_local(NULL);
    if (rc && rc != -EALREADY)
        return rc;

    for (i = 0; i < n; i++) {
        xfers[i].xd_op = PHO_XFER_OP_COPY;
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
        if (xfers[i].xd_targets->xt_objuuid)
            xfers[i].xd_targets->xt_objuuid =
                xstrdup(xfers[i].xd_targets->xt_objuuid);

        rc = fill_put_params(&xfers[i]);
        if (rc)
            return rc;
    }

    return phobos_xfer(xfers, n, cb, udata);
}
