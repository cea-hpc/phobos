/*
 *  All rights reserved (c) 2014-2023 CEA/DAM.
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

#ifdef HAVE_CONFIG_H

#include "config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <openssl/evp.h>
#include <string.h>
#include <unistd.h>
#include <xxhash.h>

#include "pho_attrs.h"
#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_io.h"
#include "pho_layout.h"
#include "pho_module_loader.h"
#include "pho_srl_common.h"
#include "pho_type_utils.h"
#include "pho_types.h"
#include "raid_common.h"

bool no_more_alloc(struct pho_encoder *enc)
{
    const struct raid_io_context *io_context = enc->priv_enc;

    /* ended encoder */
    if (enc->done)
        return true;

    /* still something to write */
    if (io_context->to_write > 0)
        return false;

    /* decoder with no more to read */
    if (enc->is_decoder)
        return true;

    /* encoder with no more to write and at least one written extent */
    if (io_context->written_extents->len > 0)
        return true;

    /* encoder with no more to write but needing to write at least one extent */
    return false;
}

int raid_build_write_allocation_req(struct pho_encoder *enc,
                                    pho_req_t *req, int repl_count)
{
    struct raid_io_context *io_context = enc->priv_enc;
    size_t *n_tags;
    int rc;
    int i;
    int j;

    n_tags = xcalloc(repl_count, sizeof(*n_tags));

    for (i = 0; i < repl_count; ++i)
        n_tags[i] = enc->xfer->xd_params.put.tags.n_tags;

    rc = pho_srl_request_write_alloc(req, repl_count, n_tags);
    free(n_tags);
    if (rc)
        return rc;

    for (i = 0; i < repl_count; ++i) {
        req->walloc->media[i]->size = io_context->to_write;

        for (j = 0; j < enc->xfer->xd_params.put.tags.n_tags; ++j)
            req->walloc->media[i]->tags[j] =
                xstrdup(enc->xfer->xd_params.put.tags.tags[j]);
    }

    return 0;
}

int raid_enc_handle_resp(struct pho_encoder *enc, pho_resp_t *resp,
                          pho_req_t **reqs, size_t *n_reqs)
{
    struct raid_io_context *io_context = enc->priv_enc;
    int rc = 0;

    if (pho_response_is_error(resp)) {
        enc->xfer->xd_rc = resp->error->rc;
        enc->done = true;
        LOG_RETURN(enc->xfer->xd_rc,
                  "%s for objid:'%s' received error %s to last request",
                  enc->is_decoder ? "Decoder" : "Encoder", enc->xfer->xd_objid,
                  pho_srl_error_kind_str(resp->error));

    } else {
        LOG_RETURN(rc = -EPROTO, "Invalid response type");
    }

    (void) io_context;
    return rc;
}

void raid_encoder_destroy(struct pho_encoder *enc)
{
    struct raid_io_context *io_context = enc->priv_enc;

    if (io_context == NULL)
        return;

    if (io_context->written_extents != NULL) {
        g_array_free(io_context->written_extents, TRUE);
        io_context->written_extents = NULL;
    }

    if (io_context->to_release_media != NULL) {
        g_hash_table_destroy(io_context->to_release_media);
        io_context->to_release_media = NULL;
    }

    free(io_context);
    enc->priv_enc = NULL;
}

int raid_encoder_step(struct pho_encoder *enc, pho_resp_t *resp,
                       pho_req_t **reqs, size_t *n_reqs)
{
    struct raid_io_context *io_context = enc->priv_enc;
    int rc = 0;

    /* At most 2 requests will be emitted, allocate optimistically */
    *reqs = xcalloc(2, sizeof(**reqs));
    *n_reqs = 0;

    /* Handle a possible response */
    if (resp != NULL)
        rc = raid_enc_handle_resp(enc, resp, reqs, n_reqs);

    /* Do we need to generate a new alloc ? */
    if (rc || /* an error happened */
        io_context->requested_alloc || /* an alloc was already requested */
        no_more_alloc(enc))
        goto out;

    /* Build next request */
    if (!enc->is_decoder)
        rc = raid_build_write_allocation_req(enc, *reqs + *n_reqs, 3);

    if (rc)
        return rc;

    (*n_reqs)++;
    io_context->requested_alloc = true;

out:
    if (*n_reqs == 0) {
        free(*reqs);
        *reqs = NULL;
    }

    /* For now, orphaned extents are not cleaned up on failure */
    return rc;
}
