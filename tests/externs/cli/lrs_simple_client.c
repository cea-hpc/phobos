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
    int             n_media;
    int             n_required;
    const char    **medium_name;
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

    if (option->action != WRITE)
        if (argc < 3)
            goto err_usage;

    if (option->action == READ) {
        int i;

        option->n_media = atoi(argv[2]);
        if (argc < 4 + option->n_media)
            goto err_usage;

        family_arg_index = 4 + option->n_media;

        option->n_required = atoi(argv[3]);
        if (option->n_required == 0)
            error(__func__, "get needs an argv[3] n_required different from 0");

        option->medium_name = xmalloc(sizeof(*option->medium_name) *
                                      option->n_media);
        for (i = 0; i < option->n_media; i++)
            option->medium_name[i] = argv[4 + i];
    } else if (option->action == RELEASE) {
        int i;

        option->n_media = atoi(argv[2]);
        if (argc < 3 + option->n_media)
            goto err_usage;

        family_arg_index = 3 + option->n_media;
        option->medium_name = xmalloc(sizeof(*option->medium_name) *
                                      option->n_media);
        for (i = 0; i < option->n_media; i++)
            option->medium_name[i] = argv[3 + i];
    } else if (option->action == FORMAT) {
        family_arg_index = 3;
        *option->medium_name = argv[2];
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
          "    get <n_media> <n_required> <medium> [<medium> ...] [<family>]\n"
          "    format <medium> [<family>]\n"
          "    release <n_media> <medium> [<medium> ...] [<family>]");
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
    pho_srl_request_pack(req, &data.buf);
    pho_srl_request_free(req, false);

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
                      int n_media, int n_required,
                      enum rsc_family family,
                      const char **medium_name)
{
    pho_resp_t *resp = NULL;
    pho_req_t req;
    int i;

    pho_srl_request_read_alloc(&req, n_media);
    req.ralloc->n_required = n_required;
    for (i = 0; i < n_media; i++) {
        req.ralloc->med_ids[i]->name = xstrdup(medium_name[i]);
        req.ralloc->med_ids[i]->library = xstrdup("legacy");
        req.ralloc->med_ids[i]->family = family;
    }

    send_and_receive(comm, &req, &resp);
    for (i = 0; i < n_required; i++)
        printf("%s\n", resp->ralloc->media[i]->med_id->name);

    pho_srl_response_free(resp, true);
}

static void send_write(struct pho_comm_info *comm,
                       enum rsc_family family)
{
    pho_resp_t *resp = NULL;
    pho_req_t req;
    size_t n = 0;

    pho_srl_request_write_alloc(&req, 1, &n);
    req.walloc->media[0]->size = 0;
    req.walloc->family = family;

    send_and_receive(comm, &req, &resp);
    printf("%s", resp->walloc->media[0]->med_id->name);
    pho_srl_response_free(resp, true);
}

static void send_release(struct pho_comm_info *comm,
                         int n_media,
                         enum rsc_family family,
                         const char **medium_name)
{
    pho_resp_t *resp = NULL;
    pho_req_t req;
    int i;

    pho_srl_request_release_alloc(&req, n_media);

    for (i = 0; i < n_media; i++) {
        req.release->media[i]->med_id->family = family;
        req.release->media[i]->med_id->name = xstrdup(medium_name[i]);
        req.release->media[i]->med_id->library = xstrdup("legacy");
        req.release->media[i]->to_sync = true;
    }


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

    pho_srl_request_format_alloc(&req);

    req.format->fs = fs;
    req.format->unlock = false;
    req.format->med_id->family = family;
    req.format->med_id->name = xstrdup(medium_name);
    req.format->med_id->library = xstrdup("legacy");
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
    option.n_media = 1;
    option.n_required = 1;
    parse_args(argc, argv, &option);
    pho_info("action: %s (n_media: %d, n_required: %d), family: %s",
             action_str[option.action], option.n_media, option.n_required,
             rsc_family2str(option.family));
    if (option.action == READ || option.action == RELEASE) {
        unsigned int i;

        for (i = 0; i < option.n_media; i++)
            pho_info("medium: %s", option.medium_name[i]);
    }


    addr.af_unix.path = PHO_CFG_GET(cfg_lrs, PHO_CFG_LRS, server_socket);
    rc = pho_comm_open(&comm, &addr, PHO_COMM_UNIX_CLIENT);
    if (rc)
        error("pho_comm_open", strerror(-rc));

    switch (option.action) {
    case WRITE:
        send_write(&comm, option.family);
        break;
    case READ:
        send_read(&comm, option.n_media, option.n_required, option.family,
                  option.medium_name);
        break;
    case FORMAT:
        send_format(&comm, option.family, *option.medium_name);
        break;
    case RELEASE:
        send_release(&comm, option.n_media, option.family, option.medium_name);
        break;
    }

    if (option.action == READ || option.action == RELEASE)
        free(option.medium_name);

    return EXIT_SUCCESS;
}
