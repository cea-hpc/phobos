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
 * \brief  Phobos Layout Composition Layer
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_layout.h"

#include "phobos_store.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_type_utils.h"
#include "pho_io.h"
#include "pho_module_loader.h"
#include "posix_layout.h"

#include <assert.h>
#include <dlfcn.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int data_processor_read_into_buff(struct pho_data_processor *proc,
                                  struct pho_io_descr *reader_iod, size_t size)
{
    ssize_t read_size;

    read_size = ioa_read(reader_iod->iod_ioa, reader_iod,
                         proc->buff.buff +
                             (proc->reader_offset - proc->buffer_offset), size);

    if (read_size < 0)
        LOG_RETURN(read_size,
                   "reading %zu bytes fails in data processor at offset "
                   "%zu", size, proc->reader_offset);

    proc->reader_offset += read_size;
    reader_iod->iod_size += read_size;

    if (read_size < size)
        LOG_RETURN(-EIO,
                   "data processor reader expected %zu bytes to read and get "
                   "only %zd bytes, at offset %zu",
                   size, read_size, proc->reader_offset);

    return 0;
}

static int build_layout_name(const char *layout_name, char *path, size_t len)
{
    int rc;

    rc = snprintf(path, len, "layout_%s", layout_name);
    if (rc < 0 || rc >= len) {
        path[0] = '\0';
        return -EINVAL;
    }

    return 0;
}

int layout_encoder(struct pho_data_processor *encoder,
                   struct pho_xfer_desc *xfer)
{
    char layout_name[NAME_MAX];
    struct layout_module *mod;
    const char *copy_name;
    int rc;
    int i;

    rc = build_layout_name(xfer->xd_params.put.layout_name, layout_name,
                           sizeof(layout_name));
    if (rc)
        return rc;

    /* Load new module if necessary */
    rc = load_module(layout_name, sizeof(*mod), phobos_context(),
                     (void **) &mod);
    if (rc)
        return rc;

    /*
     * Note 1: we don't acquire layout_modules_rwlock because we consider that
     * the module we just retrieved will never be unloaded.
     */
    /*
     * Note 2: the encoder module must not be unloaded until all encoders of
     * this type have been destroyed, since they all hold a reference to the
     * module's code. In this implementation, the module is never unloaded.
     */
    encoder->type = PHO_PROC_ENCODER;
    encoder->done = false;
    encoder->xfer = xfer;
    encoder->layout = xcalloc(encoder->xfer->xd_ntargets,
                              sizeof(*encoder->layout));

    if (xfer->xd_params.put.copy_name) {
        copy_name = xfer->xd_params.put.copy_name;
    } else {
        rc = get_cfg_default_copy_name(&copy_name);
        if (rc) {
            free(encoder->layout);
            encoder->layout = NULL;
            return rc;
        }
    }

    /* set first object size */
    if (encoder->xfer->xd_ntargets > 0)
        encoder->object_size = xfer->xd_targets[0].xt_size;

    for (i = 0; i < encoder->xfer->xd_ntargets; i++) {
        encoder->layout[i].oid = xfer->xd_targets[i].xt_objid;
        encoder->layout[i].wr_size = xfer->xd_targets[i].xt_size;
        encoder->layout[i].copy_name = xstrdup(copy_name);
    }

    /* get io_block_size from conf */
    rc = get_cfg_io_block_size(&encoder->io_block_size,
                               xfer->xd_params.put.family);
    if (rc) {
        layout_destroy(encoder);
        return rc;
    }

    rc = mod->ops->encode(encoder);
    if (rc) {
        layout_destroy(encoder);
        LOG_RETURN(rc, "Unable to create encoder");
    }

    rc = set_posix_reader(encoder);
    if (rc) {
        layout_destroy(encoder);
        return rc;
    }

    return rc;
}

int layout_decoder(struct pho_data_processor *decoder,
                   struct pho_xfer_desc *xfer, struct layout_info *layout)
{
    char layout_name[NAME_MAX];
    struct layout_module *mod;
    int rc;

    rc = build_layout_name(layout->layout_desc.mod_name, layout_name,
                           sizeof(layout_name));
    if (rc)
        return rc;

    /* Load new module if necessary */
    rc = load_module(layout_name, sizeof(*mod), phobos_context(),
                     (void **) &mod);
    if (rc)
        return rc;

    /* See notes in layout_encode */
    decoder->type = PHO_PROC_DECODER;
    decoder->done = false;
    decoder->xfer = xfer;
    decoder->layout = layout;

    /* get io_block_size from conf */
    rc = get_cfg_io_block_size(&decoder->io_block_size,
                               xfer->xd_params.put.family);
    if (rc) {
        layout_destroy(decoder);
        LOG_RETURN(rc, "Unable to get io_block_size");
    }

    rc = mod->ops->decode(decoder);
    if (rc) {
        layout_destroy(decoder);
        LOG_RETURN(rc, "Unable to create decoder");
    }

    rc = set_posix_writer(decoder);
    if (rc) {
        layout_destroy(decoder);
        return rc;
    }

    return rc;
}

int layout_eraser(struct pho_data_processor *eraser, struct pho_xfer_desc *xfer,
                  struct layout_info *layout)
{
    char layout_name[NAME_MAX];
    struct layout_module *mod;
    int rc;

    rc = build_layout_name(layout->layout_desc.mod_name, layout_name,
                           sizeof(layout_name));
    if (rc)
        return rc;

    /* Load new module if necessary */
    rc = load_module(layout_name, sizeof(*mod), phobos_context(),
                     (void **) &mod);
    if (rc)
        return rc;

    /* See notes in layout_encode */
    eraser->type = PHO_PROC_ERASER;
    eraser->done = false;
    eraser->xfer = xfer;
    eraser->layout = layout;

    rc = mod->ops->erase(eraser);
    if (rc) {
        layout_destroy(eraser);
        LOG_RETURN(rc, "Unable to create eraser");
    }

    return rc;
}

int layout_locate(struct dss_handle *dss, struct layout_info *layout,
                  const char *focus_host, char **hostname, int *nb_new_lock)
{
    char layout_name[NAME_MAX];
    struct layout_module *mod;
    int rc;

    *hostname = NULL;

    rc = build_layout_name(layout->layout_desc.mod_name, layout_name,
                           sizeof(layout_name));
    if (rc)
        return rc;

    /* Load new module if necessary */
    rc = load_module(layout_name, sizeof(*mod), phobos_context(),
                     (void **) &mod);
    if (rc)
        return rc;

    return mod->ops->locate(dss, layout, focus_host, hostname, nb_new_lock);
}

int layout_get_specific_attrs(struct pho_io_descr *iod,
                              struct io_adapter_module *ioa,
                              struct extent *extent, struct layout_info *layout)
{
    char layout_name[NAME_MAX];
    struct layout_module *mod;
    int rc = 0;

    rc = build_layout_name(layout->layout_desc.mod_name, layout_name,
                           sizeof(layout_name));
    if (rc)
        return rc;

    rc = load_module(layout_name, sizeof(*mod), phobos_context(),
                     (void **) &mod);
    if (rc)
        return rc;

    return mod->ops->get_specific_attrs(iod, ioa, extent,
                                        &layout->layout_desc.mod_attrs);
}

int layout_reconstruct(struct layout_info lyt, struct copy_info *copy)
{
    char layout_name[NAME_MAX];
    struct layout_module *mod;
    int rc;

    rc = build_layout_name(lyt.layout_desc.mod_name, layout_name,
                           sizeof(layout_name));
    if (rc)
        return rc;

    /* Load new module if necessary */
    rc = load_module(layout_name, sizeof(*mod), phobos_context(),
                     (void **) &mod);
    if (rc)
        return rc;

    return mod->ops->reconstruct(lyt, copy);
}

void layout_destroy(struct pho_data_processor *proc)
{
    int i;

    if (proc->reader_ops)
        proc->reader_ops->destroy(proc);

    if (proc->writer_ops)
        proc->writer_ops->destroy(proc);

    if (proc->eraser_ops)
        proc->eraser_ops->destroy(proc);

    /* Only encoders own their layout */
    if (is_encoder(proc) && proc->layout != NULL) {
        for (i = 0; i < proc->xfer->xd_ntargets; i++) {
            pho_attrs_free(&proc->layout[i].layout_desc.mod_attrs);
            layout_info_free_extents(&proc->layout[i]);
            free(proc->layout[i].copy_name);
        }
        free(proc->layout);
        proc->layout = NULL;
    }

    if (proc->write_resp != NULL) {
        pho_srl_response_free(proc->write_resp, false);
        free(proc->write_resp);
        proc->write_resp = NULL;
    }

    pho_buff_free(&proc->buff);
}
