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
 * \brief  data processor reader and writer from one posix FD
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "posix_layout.h"

#include "pho_common.h"
#include "pho_io.h"
#include "pho_layout.h"
#include "pho_srl_lrs.h"
#include "pho_types.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

static int posix_reader_step(struct pho_data_processor *proc, pho_resp_t *resp,
                             pho_req_t **reqs, size_t *n_reqs)
{
    struct pho_io_descr *posix_reader =
        &((struct pho_io_descr *)proc->private_reader)[proc->current_target];
    size_t to_read;

    /* first init step from the data processor */
    if (proc->buff.size == 0) {
        proc->reader_stripe_size = proc->io_block_size;
        update_io_size(posix_reader, &proc->reader_stripe_size);

        return 0;
    }

    if (proc->xfer->xd_targets[proc->current_target].xt_rc != 0)
        return proc->xfer->xd_targets[proc->current_target].xt_rc;

    /* limit read : object -> buffer */
    to_read = min(proc->object_size - proc->reader_offset,
                  proc->buff.size -
                      (proc->reader_offset - proc->buffer_offset));
    return data_processor_read_into_buff(proc, posix_reader, to_read);
}

static void posix_reader_destroy(struct pho_data_processor *proc)
{
    int target_idx;

    if (!proc->private_reader)
        return;

    for (target_idx = 0; target_idx < proc->xfer->xd_ntargets; target_idx++) {
        struct pho_io_descr *posix_reader =
            &((struct pho_io_descr *)proc->private_reader)[target_idx];

        if (posix_reader->iod_ioa)
            ioa_close(posix_reader->iod_ioa, posix_reader);

    }

    free(proc->private_reader);
    proc->private_reader = NULL;
}

static const struct pho_proc_ops POSIX_READER_OPS = {
    .step = posix_reader_step,
    .destroy = posix_reader_destroy,
};

static int set_private_posix_io_descr(struct pho_io_descr **private_io_descr,
                                      struct pho_data_processor *proc)
{
    int target_idx;
    int rc;

    *private_io_descr = xcalloc(proc->xfer->xd_ntargets,
                                sizeof(struct pho_io_descr));

    for (target_idx = 0; target_idx < proc->xfer->xd_ntargets; target_idx++) {
        struct pho_io_descr *current_io_descr =
            &(*private_io_descr)[target_idx];
        int input_fd = proc->xfer->xd_targets[target_idx].xt_fd;
        int fd;

        rc = get_io_adapter(PHO_FS_POSIX, &current_io_descr->iod_ioa);
        if (rc)
            goto end;

        /*
         * We duplicate the file descriptor so that ioa_close doesn't close the
         * file descriptor of the Xfer. This file descriptor is managed by
         * Python in the CLI for example.
         */
        fd = dup(input_fd);
        if (fd == -1) {
            rc = -errno;
            goto end;
        }

        rc = iod_from_fd(current_io_descr->iod_ioa, current_io_descr, fd);
        if (rc)
            goto end;
    }

    return 0;

end:
    for (; target_idx >= 0; target_idx--) {
        struct pho_io_descr *current_io_descr =
            &(*private_io_descr)[target_idx];

        ioa_close(current_io_descr->iod_ioa, current_io_descr);
    }

    free(*private_io_descr);
    *private_io_descr = NULL;
    return rc;
}

int set_posix_reader(struct pho_data_processor *encoder)
{
    encoder->reader_ops = &POSIX_READER_OPS;

    return set_private_posix_io_descr(
               (struct pho_io_descr **)&encoder->private_reader, encoder);
}

static int posix_writer_step(struct pho_data_processor *proc, pho_resp_t *resp,
                             pho_req_t **reqs, size_t *n_reqs)
{
    struct pho_io_descr *posix_writer =
        &((struct pho_io_descr *)proc->private_writer)[proc->current_target];
    size_t to_write = proc->reader_offset - proc->writer_offset;
    int rc;

    ENTRY;

    /* first init step from the data processor */
    if (proc->buff.size == 0) {
        proc->writer_stripe_size = proc->io_block_size;
        update_io_size(posix_writer, &proc->writer_stripe_size);

        return 0;
    }

    if (proc->xfer->xd_targets[proc->current_target].xt_rc != 0)
        return proc->xfer->xd_targets[proc->current_target].xt_rc;

    rc = data_processor_write_from_buff(proc, posix_writer, to_write, 0);
    if (rc)
        LOG_RETURN(rc,
                   "Error when writting %zu bytes with posix writer at offset "
                   "%zu", to_write, proc->writer_offset);

    proc->writer_offset += to_write;
    if (proc->writer_offset == proc->reader_offset)
        proc->buffer_offset = proc->writer_offset;

    /* Switch to next target */
    if (proc->writer_offset == proc->object_size) {
        proc->current_target++;
        proc->buffer_offset = 0;
        proc->reader_offset = 0;
        proc->writer_offset = 0;

        /*
         * Currently, there is always only one target per decoder.
         */
        assert(proc->current_target == proc->xfer->xd_ntargets);
        proc->done = true;
    }

    return 0;
}

static void posix_writer_destroy(struct pho_data_processor *proc)
{
    int target_idx;

    struct pho_io_descr *posix_writer =
        (struct pho_io_descr *)proc->private_writer;

    if (!posix_writer)
        return;

    for (target_idx = 0; target_idx < proc->xfer->xd_ntargets; target_idx++) {
        struct pho_io_descr *posix_writer =
            &((struct pho_io_descr *)proc->private_writer)[target_idx];

        if (posix_writer->iod_ioa)
            ioa_close(posix_writer->iod_ioa, posix_writer);

    }

    free(posix_writer);
    proc->private_writer = NULL;
}

static const struct pho_proc_ops POSIX_WRITER_OPS = {
    .step = posix_writer_step,
    .destroy = posix_writer_destroy,
};

int set_posix_writer(struct pho_data_processor *decoder)
{
    decoder->writer_ops = &POSIX_WRITER_OPS;
    return set_private_posix_io_descr(
               (struct pho_io_descr **)&decoder->private_writer, decoder);
}
