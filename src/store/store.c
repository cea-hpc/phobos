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
#include "pho_attrs.h"
#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_io.h"
#include "pho_layout.h"
#include "pho_lrs.h"
#include "pho_srl_lrs.h"
#include "pho_type_utils.h"
#include "pho_types.h"

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
 * Phobos application state, eventually will offer methods to add transfers on
 * the fly.
 */
struct phobos_handle {
    struct dss_handle dss;          /**< DSS handle, configured from conf */
    struct lrs lrs;                 /**< LRS handle, configured from conf */
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

    pho_completion_cb_t cb;         /**< Callback called on xfer completion */
    void *udata;                    /**< User-provided argument to `cb` */
};

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
        if (is_media_global_error(xfers[i].xd_rc))
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

    rc = dss_filter_build(&filter, "{\"DSS::EXT::oid\": \"%s\"}",
                          xfer->xd_objid);
    if (rc)
        return rc;

    rc = dss_layout_get(dss, &filter, &layout, &cnt);
    if (rc)
        GOTO(err_nores, rc);

    if (cnt == 0)
        GOTO(err, rc = -ENOENT);

    /* @FIXME: duplicate layout to avoid calling dss functions to free this? */
    rc = layout_decode(dec, xfer, layout);
    if (rc)
        GOTO(err, rc);

err:
    if (rc)
        dss_res_free(layout, cnt);

err_nores:
    dss_filter_free(&filter);
    return rc;
}

/**
 * Forward a response from the LRS to its destination encoder, collect this
 * encoder's next requests and forward them back to the LRS.
 *
 * @param[in/out]   lrs     The LRS the encoder talks to.
 * @param[in/out]   enc     The encoder to give the response to.
 * @param[in]       resp    The response to be forwarded to \a enc. Can be NULL
 *                          to generate the first request from \a enc.
 * @param[in]       enc_id  Identifier of this encoder (for request / response
 *                          tracking).
 *
 * @return 0 on success, -errno on error.
 */
static int encoder_communicate(struct lrs *lrs, struct pho_encoder *enc,
                               pho_resp_t *resp, int enc_id)
{
    pho_req_t *requests = NULL;
    struct pho_ubuff buf;
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
        if (pho_srl_request_pack(req, &buf)) {
            pho_srl_request_free(req, false);
            return -ENOMEM;
        }

        /* After this call, the LRS is responsible for freeing `req` */
        rc2 = lrs_request_enqueue(lrs, buf);
        if (rc2) {
            pho_error(rc2, "Error while sending request to LRS for %s",
                      enc->xfer->xd_objid);
            rc = rc ? : rc2;
        }

        pho_srl_request_free(req, false);
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
static int object_md_get(struct dss_handle *dss, struct pho_xfer_desc *xfer)
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

out_free:
    dss_res_free(obj, obj_cnt);
filt_free:
    dss_filter_free(&filter);
    return rc;
}

/**
 * Save this xfer oid and metadata (xd_attrs) into the DSS.
 */
static int object_md_save(struct dss_handle *dss,
                          const struct pho_xfer_desc *xfer)
{
    struct object_info   obj;
    GString             *md_repr = g_string_new(NULL);
    int                  rc;

    ENTRY;

    rc = pho_attrs_to_json(&xfer->xd_attrs, md_repr, 0);
    if (rc)
        LOG_GOTO(out_free, rc, "Cannot convert attributes into JSON");

    obj.oid = xfer->xd_objid;
    obj.user_md = md_repr->str;

    pho_debug("Storing object objid:'%s' (transient) with attributes: %s",
              xfer->xd_objid, md_repr->str);

    rc = dss_object_set(dss, &obj, 1, DSS_SET_INSERT);
    if (rc)
        LOG_GOTO(out_free, rc, "dss_object_set failed for objid:'%s'", obj.oid);

out_free:
    g_string_free(md_repr, true);
    return rc;
}

/**
 * Delete xfer metadata from the DSS by \a objid, making the oid free to be used
 * again (unless layout information still lay in the DSS).
 */
static int object_md_del(struct dss_handle *dss, char *objid)
{
    struct layout_info *layout = NULL;
    struct dss_filter filter;
    int cnt = 0;
    struct object_info  obj = {
        .oid     = objid,
        .user_md = NULL,
    };
    int rc;

    ENTRY;

    /* Ensure the oid isn't used by an existing layout before deleting it */
    rc = dss_filter_build(&filter, "{\"DSS::EXT::oid\": \"%s\"}", objid);
    if (rc)
        return rc;

    rc = dss_layout_get(dss, &filter, &layout, &cnt);
    dss_filter_free(&filter);
    dss_res_free(layout, cnt);
    if (rc == 0 && cnt > 0)
        LOG_RETURN(rc = -EEXIST,
                   "Cannot rollback objid:'%s' from DSS, a layout still exists "
                   "for this objid", objid);

    /* Then the rollback can safely happen */
    pho_verb("Rolling back objid:'%s' from DSS", objid);
    rc = dss_object_set(dss, &obj, 1, DSS_SET_DELETE);
    if (rc)
        LOG_RETURN(rc, "dss_object_set failed for objid:'%s'", objid);

    return 0;
}

/**
 * Initialize an encoder or decoder to perform \a xfer, according to
 * xfer->xd_op and xfer->xd_flags.
 */
static int init_enc_or_dec(struct pho_encoder *enc, struct dss_handle *dss,
                           struct pho_xfer_desc *xfer)
{
    int rc;

    if (xfer->xd_op == PHO_XFER_OP_PUT)
        /* Handle encoder creation for PUT */
        return layout_encode(enc, xfer);

    /* GET/GETMD: handle metadata retrieval */
    rc = object_md_get(dss, xfer);
    if (rc)
        LOG_RETURN(rc, "Cannot find metadata for objid:'%s'", xfer->xd_objid);

    if (xfer->xd_op == PHO_XFER_OP_GETMD) {
        /* GETMD: create dummy decoder if only metadata are retrieved */
        enc->xfer = xfer;
        enc->done = true;
        enc->is_decoder = true;
        return 0;
    }

    /* Handle decoder creation for GET */
    return decoder_build(enc, xfer, dss);
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
        int rc2;

        pho_debug("Saving layout for objid:'%s'", xfer->xd_objid);
        rc2 = dss_layout_set(&pho->dss, enc->layout, 1, DSS_SET_INSERT);
        if (rc2) {
            pho_error(rc2, "Error while saving layout for objid:'%s'",
                      xfer->xd_objid);
            rc = rc ? : rc2;
        }
    }

    /* Only overwrite xd_rc if it was 0 */
    if (xfer->xd_rc == 0 && rc != 0)
        xfer->xd_rc = rc;

    pho_info("%s operation for objid:'%s' %s",
             xfer->xd_op == PHO_XFER_OP_PUT ? "PUT" : "GET",
             xfer->xd_objid, xfer->xd_rc ? "failed" : "succeeded");

    /* Cleanup metadata for failed PUT */
    if (pho->md_created[xfer_idx] &&
            xfer->xd_op == PHO_XFER_OP_PUT && xfer->xd_rc)
        object_md_del(&pho->dss, xfer->xd_objid);

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
        if (!pho->ended_xfers[i])
            store_end_xfer(pho, i, rc);

        /*
         * We allocated the decoder layouts from the dss (in decoder_build),
         * hence we also free them.
         */
        if (pho->encoders[i].is_decoder) {
            dss_res_free(pho->encoders[i].layout, 1);
            pho->encoders[i].layout = NULL;
        }
        layout_destroy(&pho->encoders[i]);
    }

    free(pho->encoders);
    free(pho->ended_xfers);
    free(pho->md_created);
    pho->encoders = NULL;
    pho->ended_xfers = NULL;
    pho->md_created = NULL;

    lrs_fini(&pho->lrs);
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
    size_t i;
    int rc;

    memset(pho, 0, sizeof(*pho));

    pho->xfers = xfers;
    pho->n_xfers = n_xfers;
    pho->cb = cb;
    pho->udata = udata;

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

    /* Connect to the DSS */
    rc = dss_init(&pho->dss);
    if (rc != 0)
        return rc;

    /* Instanciate the LRS (in the future: connect to the LRS service) */
    rc = lrs_init(&pho->lrs, &pho->dss);
    if (rc)
        LOG_GOTO(out, rc, "Cannot initialize LRS");

    /* Allocate memory for the encoders */
    pho->encoders = calloc(n_xfers, sizeof(*pho->encoders));
    if (pho->encoders == NULL)
        GOTO(out, rc = -ENOMEM);

    /* Allocate array of ended encoders for completion tracking */
    pho->ended_xfers = calloc(n_xfers, sizeof(*pho->ended_xfers));
    if (pho->ended_xfers == NULL)
        GOTO(out, rc = -ENOMEM);

    /* Allocate array of to track which encoders had their metadata created */
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

    rc = encoder_communicate(&pho->lrs, encoder, resp, resp->req_id);

    /* Success or failure final callback */
    if (rc || encoder->done)
        store_end_xfer(pho, resp->req_id, rc);

    if (rc)
        pho_error(rc, "Error while sending response to layout for %s",
                  encoder->xfer->xd_objid);

    return rc;
}

static void response_buffers_free(struct pho_ubuff *bufs, int n_buf)
{
    free(bufs);
}

static int store_dispatch_loop(struct phobos_handle *pho)
{
    struct pho_ubuff *responses = NULL;
    size_t n_responses = 0;
    int rc;
    int i;

    /* Collect LRS responses */
    rc = lrs_responses_get(&pho->lrs, &responses, &n_responses);
    if (rc) {
        pho_error(rc, "Error while collecting responses from LRS");
        response_buffers_free(responses, n_responses);
        return rc;
    }

    pho_debug("Collected %ld responses from LRS", n_responses);

    /* Deserialize LRS responses */
    pho_resp_t **resps = malloc(n_responses * sizeof(*resps));

    if (resps == NULL) {
        response_buffers_free(responses, n_responses);
        return -ENOMEM;
    }

    for (i = 0; i < n_responses; ++i)
        resps[i] = pho_srl_response_unpack(responses + i);
    response_buffers_free(responses, n_responses);

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

    /*
     * Save object metadata as a way to "reserve" the OID and ensure its
     * unicity before performing any IO. From now on, any failed object must
     * have its metadata cleared from the DSS.
     */
    for (i = 0; i < pho->n_xfers; i++) {
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

        rc = encoder_communicate(&pho->lrs, &pho->encoders[i], NULL, i);
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
    const char *layout = PHO_CFG_GET(cfg_store, PHO_CFG_STORE, layout);
    size_t i;

    for (i = 0; i < n; i++) {
        xfers[i].xd_op = PHO_XFER_OP_PUT;
        if (xfers[i].xd_layout_name == NULL)
            xfers[i].xd_layout_name = layout;
    }

    return phobos_xfer(xfers, n, cb, udata);
}

int phobos_get(struct pho_xfer_desc *xfers, size_t n,
               pho_completion_cb_t cb, void *udata)
{
    size_t i;

    for (i = 0; i < n; i++)
        xfers[i].xd_op = PHO_XFER_OP_GET;

    return phobos_xfer(xfers, n, cb, udata);
}

int phobos_getmd(struct pho_xfer_desc *xfers, size_t n,
                 pho_completion_cb_t cb, void *udata)
{
    size_t i;

    for (i = 0; i < n; i++)
        xfers[i].xd_op = PHO_XFER_OP_GETMD;

    return phobos_xfer(xfers, n, cb, udata);
}

void pho_xfer_desc_destroy(struct pho_xfer_desc *xfer)
{
    tags_free(&xfer->xd_tags);
    pho_attrs_free(&xfer->xd_attrs);
}
