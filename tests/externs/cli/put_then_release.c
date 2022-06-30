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
 * \brief  Simple program that execute write allocation and print allocate
 *         medium name on stdout or execute a release
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

static void error(const char *func, const char *msg)
{
    fprintf(stderr, "%s: %s\n", func, msg);
    exit(EXIT_FAILURE);
}

struct option {
    bool                put; /* true for a put, false for a release */
    enum rsc_family     family;
    const char         *release_medium_name;
};

static void parse_args(int argc, char **argv, struct option *option)
{
    int family_arg_index = 2;

    if (argc < 2)
        goto err_usage;

    option->family = PHO_RSC_DIR;

    if (!strcmp(argv[1], "put"))
        option->put = true;
    else if (!strcmp(argv[1], "release"))
        option->put = false;
    else
        goto err_usage;

    if (!option->put) {
        if (argc < 3)
            goto err_usage;

        family_arg_index = 3;
        option->release_medium_name = argv[2];
    }

    if (argc > family_arg_index) {
        option->family = str2rsc_family(argv[family_arg_index]);
        if (option->family == PHO_RSC_INVAL)
            goto err_usage;
    }

    return;

err_usage:
    error(__func__,
          "usage put_then_release <put | release <release_medium_name>> "
          "[family]");
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
                       enum rsc_family family)
{
    pho_resp_t *resp = NULL;
    pho_req_t req;
    size_t n = 0;
    int rc;

    rc = pho_srl_request_write_alloc(&req, 1, &n);
    if (rc)
        error(__func__, strerror(-rc));
    req.walloc->media[0]->size = 0;
    req.walloc->family = family;

    send_and_receive(comm, &req, &resp);
    printf("%s", resp->walloc->media[0]->med_id->name);
    pho_srl_response_free(resp, true);
}

static void send_release(struct pho_comm_info *comm,
                         enum rsc_family family,
                         const char *release_medium_name)
{
    pho_resp_t *resp = NULL;
    pho_req_t req;
    int rc;

    rc = pho_srl_request_release_alloc(&req, 1);
    if (rc)
        error(__func__, strerror(-rc));

    req.release->media[0]->med_id->family = family;
    req.release->media[0]->med_id->name = strdup(release_medium_name);
    req.release->media[0]->to_sync = true;
    send_and_receive(comm, &req, &resp);
    pho_srl_response_free(resp, true);
}

int main(int argc, char **argv)
{
    struct pho_comm_info comm;
    const char *socket_path;
    struct option option;
    int rc;

    pho_cfg_init_local(NULL);
    parse_args(argc, argv, &option);

    socket_path = PHO_CFG_GET(cfg_lrs, PHO_CFG_LRS, server_socket);
    rc = pho_comm_open(&comm, socket_path, false);
    if (rc)
        error("pho_comm_open", strerror(-rc));

    if (option.put)
        send_write(&comm, option.family);
    else
        send_release(&comm, option.family, option.release_medium_name);

    return EXIT_SUCCESS;
}
