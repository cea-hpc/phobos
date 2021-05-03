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
 * \brief  Phobos Data Layout management.
 */
#ifndef _PHO_LAYOUT_H
#define _PHO_LAYOUT_H

#include <stdio.h>
#include <assert.h>
#include <stdbool.h>

#include "phobos_store.h"
#include "pho_srl_lrs.h"

/**
 * Operation names for dynamic loading with dlsym()
 * This is the publicly exposed API that layout modules provide.
 *
 * See below for the corresponding prototypes and additional information.
 */
#define PLM_OP_INIT         "pho_layout_mod_register"

struct pho_io_descr;
struct layout_info;

struct pho_encoder;

/**
 * Operation provided by a layout module.
 *
 * See layout_encode and layout_decode for a more complete documentation.
 */
struct pho_layout_module_ops {
    /** Initialize a new encoder to put an object into phobos */
    int (*encode)(struct pho_encoder *enc);

    /** Initialize a new decoder to get an object from phobos */
    int (*decode)(struct pho_encoder *dec);
};

/**
 * Operations provided by a given encoder (or decoder, both are the same
 * structure with a different operation vector).
 *
 * The encoders communicate their needs to the LRS via requests (see pho_lrs.h)
 * and retrieve corresponding responses, allowing them to eventually perform the
 * required IOs.
 *
 * See layout_next_request, layout_receive_response and layout_destroy for a
 * more complete documentation.
 */
struct pho_enc_ops {
    /** Give a response and get requests from this encoder / decoder */
    int (*step)(struct pho_encoder *enc, pho_resp_t *resp,
                pho_req_t **reqs, size_t *n_reqs);

    /** Destroy this encoder / decoder */
    void (*destroy)(struct pho_encoder *enc);
};

/**
 * A layout module, implementing one way of encoding a file into a phobos
 * object (simple, raid1, compression, etc.).
 *
 * Each layout module fills this structure in its entry point (PLM_OP_INIT).
 */
struct layout_module {
    void *dl_handle;            /**< Handle to the layout plugin */
    struct module_desc desc;    /**< Description of this layout */
    const struct pho_layout_module_ops *ops; /**< Operations of this layout */
};

/** An encoder encoding or decoding one object on a set of media */
struct pho_encoder {
    void *priv_enc;                 /**< Layout specific data */
    const struct pho_enc_ops *ops;  /**< Layout specific operations */
    bool is_decoder;                /**< This encoder is a decoder */
    bool done;                      /**< True if this encoder has no more work
                                      *  to do (check rc to know if an error
                                      *  happened)
                                      */
    struct pho_xfer_desc *xfer;     /**< Transfer descriptor (managed
                                      *  externally)
                                      */
    struct layout_info *layout;     /**< Layout of the current transfer filled
                                      *  out when decoding
                                      */
    size_t io_block_size;           /**< Block size of the buffer when writing
                                      */
};

/**
 * @defgroup pho_layout_mod Public API for layout modules
 * @{
 */

/**
 * Not for direct call.
 * Entry point of layout modules.
 *
 * The function fills the module description (.desc) and operation (.ops) fields
 * for this specific layout module.
 *
 * Global initialization operations can be performed here if need be.
 */
int pho_layout_mod_register(struct layout_module *self);

/** @} end of pho_layout_mod group */

#define CHECK_ENC_OP(_enc, _func) do { \
    assert(_enc); \
    assert(_enc->ops); \
    assert(_enc->ops->_func); \
} while (0)

/**
 * Initialize a new encoder \a enc to put an object described by \a xfer in
 * phobos.
 *
 * @param[out]  enc     Encoder to be initialized
 * @param[in]   xfer    Transfer to make this encoder work on. A reference on it
 *                      will be kept as long as \a enc is in use and some of its
 *                      fields may be modified (notably xd_rc). A xfer may only
 *                      be handled by one encoder.
 *
 * @return 0 on success, -errno on error.
 */
int layout_encode(struct pho_encoder *enc, struct pho_xfer_desc *xfer);

/**
 * Initialize a new encoder \a dec to get an object described by \a xfer from
 * phobos.
 *
 * @param[out]  dec     Decoder to be initialized
 * @param[in]   xfer    Transfer to make this encoder work on. A reference on it
 *                      will be kept as long as \a enc is in use and some of its
 *                      fields may be modified (notably xd_rc).
 * @param[in]   layout  Layout of the object to retrieve. It is used in-place by
 *                      the decoder and must be freed separately by the caller
 *                      after the decoder is destroyed.
 * @return 0 on success, -errno on error.
 */
int layout_decode(struct pho_encoder *dec, struct pho_xfer_desc *xfer,
                  struct layout_info *layout);

/**
 * Advance the layout operation of one step by providing a response from the LRS
 * (or NULL for the first call to this function) and collecting newly emitted
 * requests.
 *
 * @param[in]   enc     The encoder to advance.
 * @param[in]   resp    The response to pass to the encoder (or NULL for the
 *                      first call to this function).
 * @param[out]  reqs    Caller allocated array of newly emitted requests (to be
 *                      freed by the caller).
 * @param[out]  n_reqs  Number of emitted requests.
 *
 * @return 0 on success, -errno on error. -EINVAL is returned when the encoder
 * has already finished its work (the call to this function was unexpected).
 */
static inline int layout_step(struct pho_encoder *enc, pho_resp_t *resp,
                              pho_req_t **reqs, size_t *n_reqs)
{
    if (enc->done)
        return -EINVAL;

    CHECK_ENC_OP(enc, step);
    return enc->ops->step(enc, resp, reqs, n_reqs);
}

/**
 * Destroy this encoder or decoder and all associated resources.
 */
void layout_destroy(struct pho_encoder *enc);

#endif
