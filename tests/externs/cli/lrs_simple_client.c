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

enum action {
    READ, WRITE, RELEASE, FORMAT,
};

static const char * const action_str[] = {
    [READ]    = "read",
    [WRITE]   = "write",
    [RELEASE] = "release",
    [FORMAT]  = "format",
};

static void error(const char *func, const char *msg)
{
    fprintf(stderr, "%s: %s\n", func, msg);
    exit(EXIT_FAILURE);
}

struct option {
    enum action     action;
    enum rsc_family family;
    const char     *medium_name;
};

static void parse_args(int argc, char **argv, struct option *option)
{
    int family_arg_index = 2;

    if (argc < 2)
        goto err_usage;

    option->family = PHO_RSC_DIR;
    option->medium_name = NULL;

    if (!strcmp(argv[1], "put"))
        option->action = WRITE;
    else if (!strcmp(argv[1], "get"))
        option->action = READ;
    else if (!strcmp(argv[1], "format"))
        option->action = FORMAT;
    else if (!strcmp(argv[1], "release"))
        option->action = RELEASE;
    else
        goto err_usage;

    if (option->action != WRITE) {
        if (argc < 3)
            goto err_usage;

        family_arg_index = 3;
        option->medium_name = argv[2];
    }

    if (argc > family_arg_index) {
        option->family = str2rsc_family(argv[family_arg_index]);
        if (option->family == PHO_RSC_INVAL)
            goto err_usage;
    }

    return;

err_usage:
    error(__func__,
          "usage lrs_simple_client <action> [args...]\n\n"
          "<action>:\n"
          "    put [<family>]\n"
          "    get <medium> [<family>]\n"
          "    format <medium> [<family>]\n"
          "    release <medium> [<family>]");
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

static void send_read(struct pho_comm_info *comm,
                      enum rsc_family family,
                      const char *medium_name)
{
    pho_resp_t *resp = NULL;
    pho_req_t req;
    int rc;

    rc = pho_srl_request_read_alloc(&req, 1);
    if (rc)
        error(__func__, strerror(-rc));
    req.ralloc->med_ids[0]->name = strdup(medium_name);
    req.ralloc->med_ids[0]->family = family;

    send_and_receive(comm, &req, &resp);
    pho_srl_response_free(resp, true);
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
                         const char *medium_name)
{
    pho_resp_t *resp = NULL;
    pho_req_t req;
    int rc;

    rc = pho_srl_request_release_alloc(&req, 1);
    if (rc)
        error(__func__, strerror(-rc));

    req.release->media[0]->med_id->family = family;
    req.release->media[0]->med_id->name = strdup(medium_name);
    req.release->media[0]->to_sync = true;
    send_and_receive(comm, &req, &resp);
    pho_srl_response_free(resp, true);
}

static void send_format(struct pho_comm_info *comm,
                        enum rsc_family family,
                        const char *medium_name)
{
    pho_resp_t *resp = NULL;
    enum fs_type fs;
    pho_req_t req;
    int rc;

    switch (family) {
    case PHO_RSC_DIR:
        fs = PHO_FS_POSIX;
        break;
    case PHO_RSC_TAPE:
        fs = PHO_FS_LTFS;
        break;
    default:
        error(__func__, "invalid family");
        break;
    }

    rc = pho_srl_request_format_alloc(&req);
    if (rc)
        error(__func__, strerror(-rc));

    req.format->fs = fs;
    req.format->unlock = false;
    req.format->med_id->family = family;
    req.format->med_id->name = strdup(medium_name);
    send_and_receive(comm, &req, &resp);
    pho_srl_response_free(resp, true);
}

int main(int argc, char **argv)
{
    union pho_comm_addr addr;
    struct pho_comm_info comm;
    struct option option;
    int rc;

    pho_context_init();
    atexit(pho_context_fini);

    pho_cfg_init_local(NULL);
    parse_args(argc, argv, &option);
    pho_info("action: %s, family: %s, medium: %s",
             action_str[option.action],
             rsc_family2str(option.family),
             option.medium_name);


    addr.af_unix.path = PHO_CFG_GET(cfg_lrs, PHO_CFG_LRS, server_socket);
    rc = pho_comm_open(&comm, &addr, PHO_COMM_UNIX_CLIENT);
    if (rc)
        error("pho_comm_open", strerror(-rc));

    switch (option.action) {
    case WRITE:
        send_write(&comm, option.family);
        break;
    case READ:
        send_read(&comm, option.family, option.medium_name);
        break;
    case FORMAT:
        send_format(&comm, option.family, option.medium_name);
        break;
    case RELEASE:
        send_release(&comm, option.family, option.medium_name);
        break;
    }

    return EXIT_SUCCESS;
}
