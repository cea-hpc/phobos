/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2024 CEA/DAM.
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
 * \brief  Wrapper for phobos communication interface.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "pho_comm.h"
#include "pho_comm_wrapper.h"
#include "pho_common.h"

int comm_send(struct pho_comm_info *comm, pho_req_t *req)
{
    struct pho_comm_data data_out;
    int rc;

    data_out = pho_comm_data_init(comm);
    pho_srl_request_pack(req, &data_out.buf);
    pho_srl_request_free(req, false);
    rc = pho_comm_send(&data_out);
    free(data_out.buf.buff);
    if (rc)
        LOG_RETURN(rc, "Cannot send request");

    return 0;
}

int comm_recv(struct pho_comm_info *comm, pho_resp_t **resp)
{
    struct pho_comm_data *data_in = NULL;
    int n_data_in = 0;
    int rc;

    rc = pho_comm_recv(comm, &data_in, &n_data_in);
    if (rc || n_data_in != 1) {
        if (data_in)
            free(data_in->buf.buff);

        free(data_in);
        if (rc)
            LOG_RETURN(rc, "Cannot receive responses");
        else
            LOG_RETURN(-EINVAL, "Received %d responses (expected 1)",
                       n_data_in);
    }

    *resp = pho_srl_response_unpack(&data_in->buf);
    if (*resp) {
        rc = 0;
    } else {
        rc = -EINVAL;
        pho_error(rc, "The received response cannot be deserialized");
    }

    free(data_in);
    return rc;
}

int comm_send_and_recv(struct pho_comm_info *comm, pho_req_t *req,
                       pho_resp_t **resp)
{
    int rc;

    rc = comm_send(comm, req);
    if (rc)
        return rc;

    rc = comm_recv(comm, resp);

    return rc;
}
