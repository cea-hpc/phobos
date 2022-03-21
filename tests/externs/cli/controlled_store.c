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
/*
 * \brief  Simple script which send requests to the LRS but waits for signal
 *         SIGUSR1 before sending the release request. This is useful to test
 *         behavior which depends on the timing at which requests are received.
 */
#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <phobos_store.h>
#include <pho_cfg.h>
#include <pho_comm.h>
#include <pho_common.h>
#include <pho_srl_common.h>
#include <pho_srl_lrs.h>

#include "lrs_cfg.h"

static bool release_signaled;

static void error(const char *func, const char *msg)
{
    fprintf(stderr, "%s: %s\n", func, msg);
    exit(EXIT_FAILURE);
}

static void on_signal_received(int signum)
{
    release_signaled = true;
}

static void setup_signal_handler(void)
{
    struct sigaction sa;

    sa.sa_handler = on_signal_received;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);
}

static void wait_release_signal(void)
{
    struct timespec duration;

    duration.tv_sec = 0;
    duration.tv_nsec = 50000000; /* 50 ms */

    while (!release_signaled) {
        int rc;

        rc = nanosleep(&duration, NULL);
        if (rc && errno != EINTR)
            error(__func__, strerror(errno));
    }
}

struct option {
    enum pho_xfer_op op;
    enum rsc_family family;
};

static void parse_args(int argc, char **argv, struct option *option)
{
    if (argc < 2)
        error("controlled_strore", "usage: controlled_store <put|get>");

    option->family = PHO_RSC_DIR;

    if (argc == 3) {
        option->family = str2rsc_family(argv[2]);
        if (option->family == PHO_RSC_INVAL)
            goto err_usage;
    }

    if (!strcmp(argv[1], "put"))
        option->op = PHO_XFER_OP_PUT;
    else if (!strcmp(argv[1], "get"))
        option->op = PHO_XFER_OP_GET;
    else
        goto err_usage;

    return;

err_usage:
    error(__func__, "usage controlled_store <action> [<family>]");
}

static void send_and_receive(struct pho_comm_info *comm,
                             pho_req_t *req,
                             pho_resp_t **resp)
{
    struct pho_comm_data *responses = NULL;
    struct pho_comm_data data;
    int n_responses = 0;
    int rc;

    data = pho_comm_data_init(comm);
    rc = pho_srl_request_pack(req, &data.buf);
    pho_srl_request_free(req, false);
    if (rc) {
        pho_srl_request_free(req, false);
        error(__func__, strerror(-rc));
    }

    rc = pho_comm_send(&data);
    free(data.buf.buff);
    if (rc)
        error(__func__, strerror(-rc));

    rc = pho_comm_recv(comm, &responses, &n_responses);
    if (rc)
        error(__func__, strerror(-rc));

    assert(n_responses == 1);
    *resp = pho_srl_response_unpack(&responses[0].buf);
    free(responses);
    if (pho_response_is_error(*resp)) {
        char *msg;
        int rc;

        rc = asprintf(&msg, "received an error response: %s",
                      strerror(-(*resp)->error->rc));
        if (rc == -1)
            error(__func__, "asprintf failed");
        error(__func__, msg);
    }
}

static void send_write(struct pho_comm_info *comm,
                       pho_resp_t **resp,
                       enum rsc_family family)
{
    pho_req_t req;
    size_t n = 0;
    int rc;

    rc = pho_srl_request_write_alloc(&req, 1, &n);
    if (rc)
        error(__func__, strerror(-rc));
    req.walloc->media[0]->size = 0;
    req.walloc->family = family;

    send_and_receive(comm, &req, resp);
}

static void send_release(struct pho_comm_info *comm,
                         pho_resp_t **resp)
{
    pho_req_t req;
    int rc;

    rc = pho_srl_request_release_alloc(&req, 1);
    if (rc)
        error(__func__, strerror(-rc));

    rsc_id_cpy(req.release->media[0]->med_id,
               (*resp)->walloc->media[0]->med_id);
    req.release->media[0]->to_sync = true;

    pho_srl_response_free(*resp, false);
    send_and_receive(comm, &req, resp);
}

int main(int argc, char **argv)
{
    struct pho_comm_info comm;
    const char *socket_path;
    pho_resp_t *resp = NULL;
    struct option option;
    int rc;

    pho_cfg_init_local(NULL);
    setup_signal_handler();
    parse_args(argc, argv, &option);

    socket_path = PHO_CFG_GET(cfg_lrs, PHO_CFG_LRS, server_socket);
    rc = pho_comm_open(&comm, socket_path, false);
    if (rc)
        error("pho_comm_open", strerror(-rc));

    send_write(&comm, &resp, option.family);
    pho_info("allocation request sent, waiting for signal");

    wait_release_signal();
    pho_info("signal received, sending release request");

    send_release(&comm, &resp);

    pho_srl_response_free(resp, true);

    return EXIT_SUCCESS;
}
