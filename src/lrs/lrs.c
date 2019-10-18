/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2019 CEA/DAM.
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
 * \brief  Phobos Local Resource Scheduler (LRS) interface
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_lrs.h"
#include "pho_comm.h"
#include "pho_common.h"

#include "lrs_sched.h"

/* TODO: LRS features that will integrate the communication protocol
 * in a future patch
 */

int lrs_format(struct lrs *lrs, const struct media_id *id,
               enum fs_type fs, bool unlock)
{
    return sched_format(&lrs->sched, id, fs, unlock);
}

int lrs_device_add(struct lrs *lrs, const struct dev_info *devi)
{
    return sched_device_add(&lrs->sched, devi);
}

/* ****************************************************************************/
/* LRS main functions *********************************************************/
/* ****************************************************************************/

int lrs_init(struct lrs *lrs, struct dss_handle *dss, const char *sock_path)
{
    int rc;

    rc = sched_init(&lrs->sched, dss);
    if (rc)
        LOG_RETURN(rc, "Error on lrs scheduler initialization");

    rc = pho_comm_open(&lrs->comm, sock_path, true);
    if (rc)
        LOG_RETURN(rc, "Error on opening the socket");

    return 0;
}

static int _prepare_requests(struct lrs *lrs, const int n_data,
                             struct pho_comm_data *data)
{
    int rc = 0;
    int i;

    for (i = 0; i < n_data; ++i) {
        struct req_container *req_cont;
        int rc2 = 0;

        req_cont = malloc(sizeof(*req_cont));
        if (!req_cont) {
            pho_error(rc = -ENOMEM, "Cannot allocate request structure");
            break;
        }

        /* request processing */
        req_cont->token = data[i].fd;
        req_cont->req = pho_srl_request_unpack(&data[i].buf);
        if (!req_cont->req) {
            pho_error(-EINVAL, "Request can not be unpacked");
            free(req_cont);
            rc = rc ? : -EINVAL;
            continue;
        }

        rc2 = sched_request_enqueue(&lrs->sched, req_cont);
        if (rc2) {
            pho_error(rc2, "Request can not be enqueue");
            pho_srl_request_free(req_cont->req, true);
            free(req_cont);
            rc = rc ? : rc2;
            continue;
        }
    }

    return rc;
}

static int _send_responses(struct lrs *lrs, const int n_resp,
                           struct resp_container *resp_cont)
{
    int rc = 0;
    int i;

    for (i = 0; i < n_resp; ++i) {
        int rc2 = 0;
        struct pho_comm_data msg;

        msg = pho_comm_init_data(&lrs->comm);
        msg.fd = resp_cont[i].token;
        rc2 = pho_srl_response_pack(resp_cont[i].resp, &msg.buf);
        pho_srl_response_free(resp_cont[i].resp, false);
        free(resp_cont[i].resp);
        if (rc2) {
            pho_error(rc2, "Response can not be packed");
            rc = rc ? : rc2;
            continue;
        }
        rc2 = pho_comm_send(&msg);
        free(msg.buf.buff);
        if (rc2) {
            pho_error(rc2, "Response can not be sent");
            rc = rc ? : rc2;
            continue;
        }
    }

    return rc;
}

int lrs_process(struct lrs *lrs)
{
    struct pho_comm_data *data = NULL;
    struct resp_container *resp_cont;
    int n_data, n_resp = 0;
    int rc = 0;

    /* request reception and accept handling */
    rc = pho_comm_recv(&lrs->comm, &data, &n_data);
    if (rc)
        LOG_RETURN(rc, "Error during request reception");

    rc = _prepare_requests(lrs, n_data, data);
    free(data);
    if (rc)
        LOG_RETURN(rc, "Error during request enqueuing");

    /* response processing */
    rc = sched_responses_get(&lrs->sched, &n_resp, &resp_cont);
    if (rc)
        LOG_RETURN(rc, "Error during sched processing");

    rc = _send_responses(lrs, n_resp, resp_cont);
    free(resp_cont);
    if (rc)
        LOG_RETURN(rc, "Error during responses sending");

    return rc;
}

void lrs_fini(struct lrs *lrs)
{
    int rc = 0;

    if (lrs == NULL)
        return;

    sched_fini(&lrs->sched);

    rc = pho_comm_close(&lrs->comm);
    if (rc)
        pho_error(rc, "Error on closing the socket");
}
