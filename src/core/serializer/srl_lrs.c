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
 * \brief  Phobos communication data structure helper.
 *         'srl' stands for SeRiaLizer.
 */
#include "pho_srl_lrs.h"

#include <errno.h>
#include <stdlib.h>

#include "pho_common.h"

enum _RESP_KIND {
    _RESP_WRITE,
    _RESP_READ,
    _RESP_RELEASE_READ,
    _RESP_RELEASE_WRITE,
    _RESP_FORMAT,
    _RESP_NOTIFY,
    _RESP_MONITOR,
    _RESP_ERROR,
    _RESP_CONFIGURE,
};

static const char *const SRL_REQ_KIND_STRS[] = {
    [PHO_REQUEST_KIND__RQ_WRITE]     = "write alloc",
    [PHO_REQUEST_KIND__RQ_READ]      = "read alloc",
    [PHO_REQUEST_KIND__RQ_RELEASE_READ]   = "read release",
    [PHO_REQUEST_KIND__RQ_RELEASE_WRITE]   = "write release",
    [PHO_REQUEST_KIND__RQ_FORMAT]    = "format",
    [PHO_REQUEST_KIND__RQ_NOTIFY]    = "notify",
    [PHO_REQUEST_KIND__RQ_MONITOR]   = "monitor",
    [PHO_REQUEST_KIND__RQ_CONFIGURE] = "configure",
};

static const char *const SRL_RESP_KIND_STRS[] = {
    [_RESP_WRITE]     = "write alloc",
    [_RESP_READ]      = "read alloc",
    [_RESP_RELEASE_READ]   = "read release",
    [_RESP_RELEASE_WRITE]   = "write release",
    [_RESP_FORMAT]    = "format",
    [_RESP_NOTIFY]    = "notify",
    [_RESP_MONITOR]   = "monitor",
    [_RESP_CONFIGURE] = "configure",
    [_RESP_ERROR]     = "error"
};

const char *pho_srl_request_kind_str(pho_req_t *req)
{
    if (pho_request_is_write(req))
        return SRL_REQ_KIND_STRS[PHO_REQUEST_KIND__RQ_WRITE];
    if (pho_request_is_read(req))
        return SRL_REQ_KIND_STRS[PHO_REQUEST_KIND__RQ_READ];
    if (pho_request_is_release_read(req))
        return SRL_REQ_KIND_STRS[PHO_REQUEST_KIND__RQ_RELEASE_READ];
    if (pho_request_is_partial_release_write(req))
        return "partial write release";
    if (pho_request_is_release_write(req))
        return SRL_REQ_KIND_STRS[PHO_REQUEST_KIND__RQ_RELEASE_WRITE];
    if (pho_request_is_format(req))
        return SRL_REQ_KIND_STRS[PHO_REQUEST_KIND__RQ_FORMAT];
    if (pho_request_is_notify(req))
        return SRL_REQ_KIND_STRS[PHO_REQUEST_KIND__RQ_NOTIFY];
    if (pho_request_is_monitor(req))
        return SRL_REQ_KIND_STRS[PHO_REQUEST_KIND__RQ_MONITOR];
    if (pho_request_is_configure(req))
        return SRL_REQ_KIND_STRS[PHO_REQUEST_KIND__RQ_CONFIGURE];

    return "<invalid>";
}

const char *pho_srl_response_kind_str(pho_resp_t *resp)
{
    if (pho_response_is_write(resp))
        return SRL_RESP_KIND_STRS[_RESP_WRITE];
    if (pho_response_is_read(resp))
        return SRL_RESP_KIND_STRS[_RESP_READ];
    if (pho_response_is_release_read(resp))
        return SRL_RESP_KIND_STRS[_RESP_RELEASE_READ];
    if (pho_response_is_release_write(resp))
        return SRL_RESP_KIND_STRS[_RESP_RELEASE_WRITE];
    if (pho_response_is_format(resp))
        return SRL_RESP_KIND_STRS[_RESP_FORMAT];
    if (pho_response_is_notify(resp))
        return SRL_RESP_KIND_STRS[_RESP_NOTIFY];
    if (pho_response_is_monitor(resp))
        return SRL_RESP_KIND_STRS[_RESP_MONITOR];
    if (pho_response_is_error(resp))
        return SRL_RESP_KIND_STRS[_RESP_ERROR];
    if (pho_response_is_configure(resp))
        return SRL_RESP_KIND_STRS[_RESP_CONFIGURE];

    return "<invalid>";
}

int request_kind_from_response(pho_resp_t *resp)
{
    if (pho_response_is_write(resp))
        return PHO_REQUEST_KIND__RQ_WRITE;
    else if (pho_response_is_read(resp))
        return PHO_REQUEST_KIND__RQ_READ;
    else if (pho_response_is_release_read(resp))
        return PHO_REQUEST_KIND__RQ_RELEASE_READ;
    else if (pho_response_is_release_write(resp))
        return PHO_REQUEST_KIND__RQ_RELEASE_WRITE;
    else if (pho_response_is_format(resp))
        return PHO_REQUEST_KIND__RQ_FORMAT;
    else if (pho_response_is_notify(resp))
        return PHO_REQUEST_KIND__RQ_NOTIFY;
    else if (pho_response_is_monitor(resp))
        return PHO_REQUEST_KIND__RQ_MONITOR;
    else if (pho_response_is_configure(resp))
        return PHO_REQUEST_KIND__RQ_CONFIGURE;
    else if (pho_response_is_error(resp))
        return resp->error->req_kind;
    else
        return -1;
}

const char *pho_srl_error_kind_str(pho_resp_error_t *err)
{
    if (err->req_kind > PHO_REQUEST_KIND__RQ_CONFIGURE)
        return "<invalid>";

    return SRL_REQ_KIND_STRS[err->req_kind];
}

void pho_srl_request_write_alloc(pho_req_t *req, size_t n_media,
                                 size_t *n_tags)
{
    int i;

    pho_request__init(req);

    req->walloc = xmalloc(sizeof(*req->walloc));
    pho_request__write__init(req->walloc);

    req->walloc->n_media = n_media;
    req->walloc->media = xmalloc(n_media * sizeof(*req->walloc->media));
    req->walloc->prevent_duplicate = false;
    req->walloc->library = NULL;
    req->walloc->no_split = false;
    req->walloc->grouping = NULL;

    for (i = 0; i < n_media; ++i) {
        req->walloc->media[i] = xmalloc(sizeof(*req->walloc->media[i]));
        pho_request__write__elt__init(req->walloc->media[i]);

        req->walloc->media[i]->n_tags = n_tags[i];
        req->walloc->media[i]->tags = NULL;
        if (n_tags[i] > 0)
            req->walloc->media[i]->tags =
                xcalloc(n_tags[i], sizeof(*req->walloc->media[i]->tags));

        req->walloc->media[i]->empty_medium = false;
    }
}

void pho_srl_request_read_alloc(pho_req_t *req, size_t n_media)
{
    int i;

    pho_request__init(req);

    req->ralloc = xmalloc(sizeof(*req->ralloc));
    pho_request__read__init(req->ralloc);

    req->ralloc->n_med_ids = n_media;
    req->ralloc->med_ids = xmalloc(n_media * sizeof(*req->ralloc->med_ids));

    for (i = 0; i < n_media; ++i) {
        req->ralloc->med_ids[i] = xmalloc(sizeof(*req->ralloc->med_ids[i]));
        pho_resource_id__init(req->ralloc->med_ids[i]);
    }
}

void pho_srl_request_release_alloc(pho_req_t *req, size_t n_media, bool is_read)
{
    int i;

    pho_request__init(req);

    req->release = xmalloc(sizeof(*req->release));
    pho_request__release__init(req->release);

    req->release->n_media = n_media;
    req->release->media = xmalloc(n_media * sizeof(*req->release->media));

    for (i = 0; i < n_media; ++i) {
        req->release->media[i] = xmalloc(sizeof(*req->release->media[i]));
        pho_request__release__elt__init(req->release->media[i]);

        req->release->media[i]->med_id =
            xmalloc(sizeof(*req->release->media[i]->med_id));
        pho_resource_id__init(req->release->media[i]->med_id);

        req->release->media[i]->grouping = NULL;
    }
    req->release->partial = false;
    if (is_read)
        req->release->kind = PHO_REQUEST_KIND__RQ_RELEASE_READ;
    else
        req->release->kind = PHO_REQUEST_KIND__RQ_RELEASE_WRITE;
}

void pho_srl_request_format_alloc(pho_req_t *req)
{
    pho_request__init(req);

    req->format = xmalloc(sizeof(*req->format));
    pho_request__format__init(req->format);

    req->format->med_id = xmalloc(sizeof(*req->format->med_id));
    pho_resource_id__init(req->format->med_id);
}

void pho_srl_request_ping_alloc(pho_req_t *req)
{
    pho_request__init(req);
    req->has_ping = true;
}

void pho_srl_request_configure_alloc(pho_req_t *req)
{
    pho_request__init(req);
    req->configure = xmalloc(sizeof(*req->configure));

    pho_request__configure__init(req->configure);
}

void pho_srl_request_notify_alloc(pho_req_t *req)
{
    pho_request__init(req);

    req->notify = xmalloc(sizeof(*req->notify));
    pho_request__notify__init(req->notify);

    req->notify->rsrc_id = xmalloc(sizeof(*req->notify->rsrc_id));
    pho_resource_id__init(req->notify->rsrc_id);

    req->notify->wait = true;
}

void pho_srl_request_monitor_alloc(pho_req_t *req)
{
    pho_request__init(req);

    req->monitor = xmalloc(sizeof(*req->monitor));

    pho_request__monitor__init(req->monitor);
}

void pho_srl_request_free(pho_req_t *req, bool unpack)
{
    if (unpack) {
        pho_request__free_unpacked(req, NULL);
        return;
    }

    int i;
    int j;

    if (req->walloc) {
        for (i = 0; i < req->walloc->n_media; ++i) {
            for (j = 0; j < req->walloc->media[i]->n_tags; ++j)
                free(req->walloc->media[i]->tags[j]);
            free(req->walloc->media[i]->tags);
            free(req->walloc->media[i]);
        }
        free(req->walloc->media);
        free(req->walloc->library);
        free(req->walloc->grouping);
        free(req->walloc);
        req->walloc = NULL;
    }

    if (req->ralloc) {
        for (i = 0; i < req->ralloc->n_med_ids; ++i) {
            free(req->ralloc->med_ids[i]->name);
            free(req->ralloc->med_ids[i]->library);
            free(req->ralloc->med_ids[i]);
        }
        free(req->ralloc->med_ids);
        free(req->ralloc);
        req->ralloc = NULL;
    }

    if (req->release) {
        for (i = 0; i < req->release->n_media; ++i) {
            free(req->release->media[i]->med_id->name);
            free(req->release->media[i]->med_id->library);
            free(req->release->media[i]->med_id);
            free(req->release->media[i]->grouping);
            free(req->release->media[i]);
        }
        free(req->release->media);
        free(req->release);
        req->release = NULL;
    }

    if (req->format) {
        free(req->format->med_id->name);
        free(req->format->med_id->library);
        free(req->format->med_id);
        free(req->format);
        req->format = NULL;
    }

    if (req->notify) {
        free(req->notify->rsrc_id->name);
        free(req->notify->rsrc_id->library);
        free(req->notify->rsrc_id);
        free(req->notify);
        req->notify = NULL;
    }

    if (req->monitor) {
        free(req->monitor);
        req->monitor = NULL;
    }

    if (req->configure) {
        free(req->configure->configuration);
        free(req->configure);
        req->configure = NULL;
    }
}

void pho_srl_response_write_alloc(pho_resp_t *resp, size_t n_media)
{
    int i;

    pho_response__init(resp);

    resp->walloc = xmalloc(sizeof(*resp->walloc));
    pho_response__write__init(resp->walloc);

    resp->walloc->n_media = n_media;
    resp->walloc->media = xmalloc(n_media * sizeof(*resp->walloc->media));

    for (i = 0; i < n_media; ++i) {
        resp->walloc->media[i] = xmalloc(sizeof(*resp->walloc->media[i]));
        pho_response__write__elt__init(resp->walloc->media[i]);

        resp->walloc->media[i]->med_id =
            xmalloc(sizeof(*resp->walloc->media[i]->med_id));
        pho_resource_id__init(resp->walloc->media[i]->med_id);
    }

    resp->walloc->threshold = NULL;
}

void pho_srl_response_read_alloc(pho_resp_t *resp, size_t n_media)
{
    int i;

    pho_response__init(resp);

    resp->ralloc = xmalloc(sizeof(*resp->ralloc));
    pho_response__read__init(resp->ralloc);

    resp->ralloc->n_media = n_media;
    resp->ralloc->media = xmalloc(n_media * sizeof(*resp->ralloc->media));

    for (i = 0; i < n_media; ++i) {
        resp->ralloc->media[i] = xmalloc(sizeof(*resp->ralloc->media[i]));
        pho_response__read__elt__init(resp->ralloc->media[i]);

        resp->ralloc->media[i]->med_id =
            xmalloc(sizeof(*resp->ralloc->media[i]->med_id));
        pho_resource_id__init(resp->ralloc->media[i]->med_id);
    }
}

void pho_srl_response_release_alloc(pho_resp_t *resp, size_t n_media)
{
    int i;

    pho_response__init(resp);
    resp->release = xmalloc(sizeof(*resp->release));

    pho_response__release__init(resp->release);
    resp->release->n_med_ids = n_media;
    resp->release->med_ids = xmalloc(n_media * sizeof(*resp->release->med_ids));

    for (i = 0; i < n_media; ++i) {
        resp->release->med_ids[i] = xmalloc(sizeof(*resp->release->med_ids[i]));
        pho_resource_id__init(resp->release->med_ids[i]);
    }
}

void pho_srl_response_format_alloc(pho_resp_t *resp)
{
    pho_response__init(resp);

    resp->format = xmalloc(sizeof(*resp->format));
    pho_response__format__init(resp->format);

    resp->format->med_id = xmalloc(sizeof(*resp->format->med_id));
    pho_resource_id__init(resp->format->med_id);
}

void pho_srl_response_ping_alloc(pho_resp_t *resp)
{
    pho_response__init(resp);
    resp->has_ping = true;
}

void pho_srl_response_configure_alloc(pho_resp_t *resp)
{
    pho_response__init(resp);
    resp->configure = xmalloc(sizeof(*resp->configure));

    pho_response__configure__init(resp->configure);
}

void pho_srl_response_notify_alloc(pho_resp_t *resp)
{
    pho_response__init(resp);

    resp->notify = xmalloc(sizeof(*resp->notify));
    pho_response__notify__init(resp->notify);

    resp->notify->rsrc_id = xmalloc(sizeof(*resp->notify->rsrc_id));
    pho_resource_id__init(resp->notify->rsrc_id);
}

void pho_srl_response_monitor_alloc(pho_resp_t *resp)
{
    pho_response__init(resp);

    resp->monitor = xmalloc(sizeof(*resp->monitor));

    pho_response__monitor__init(resp->monitor);
    resp->monitor->status = NULL;
}

void pho_srl_response_error_alloc(pho_resp_t *resp)
{
    pho_response__init(resp);

    resp->error = xmalloc(sizeof(*resp->error));

    pho_response__error__init(resp->error);
}

void pho_srl_response_free(pho_resp_t *resp, bool unpack)
{
    if (unpack) {
        pho_response__free_unpacked(resp, NULL);
        return;
    }

    int i;

    if (resp->walloc) {
        for (i = 0; i < resp->walloc->n_media; ++i) {
            free(resp->walloc->media[i]->med_id->name);
            free(resp->walloc->media[i]->med_id->library);
            free(resp->walloc->media[i]->med_id);
            free(resp->walloc->media[i]->root_path);
            free(resp->walloc->media[i]);
        }
        free(resp->walloc->threshold);
        free(resp->walloc->media);
        free(resp->walloc);
        resp->walloc = NULL;
    }

    if (resp->ralloc) {
        for (i = 0; i < resp->ralloc->n_media; ++i) {
            free(resp->ralloc->media[i]->med_id->name);
            free(resp->ralloc->media[i]->med_id->library);
            free(resp->ralloc->media[i]->med_id);
            free(resp->ralloc->media[i]->root_path);
            free(resp->ralloc->media[i]);
        }
        free(resp->ralloc->media);
        free(resp->ralloc);
        resp->ralloc = NULL;
    }

    if (resp->release) {
        for (i = 0; i < resp->release->n_med_ids; ++i) {
            free(resp->release->med_ids[i]->name);
            free(resp->release->med_ids[i]->library);
            free(resp->release->med_ids[i]);
        }
        free(resp->release->med_ids);
        free(resp->release);
        resp->release = NULL;
    }

    if (resp->format) {
        free(resp->format->med_id->name);
        free(resp->format->med_id->library);
        free(resp->format->med_id);
        free(resp->format);
        resp->format = NULL;
    }

    if (resp->ping)
        resp->has_ping = false;

    if (resp->notify) {
        free(resp->notify->rsrc_id->name);
        free(resp->notify->rsrc_id->library);
        free(resp->notify->rsrc_id);
        free(resp->notify);
        resp->notify = NULL;
    }

    if (resp->error) {
        free(resp->error);
        resp->error = NULL;
    }

    if (resp->monitor) {
        free(resp->monitor->status);
        free(resp->monitor);
        resp->monitor = NULL;
    }

    if (resp->configure) {
        free(resp->configure->configuration);
        free(resp->configure);
        resp->configure = NULL;
    }
}

void pho_srl_request_pack(pho_req_t *req, struct pho_buff *buf)
{
    buf->size = pho_request__get_packed_size(req) + PHO_PROTOCOL_VERSION_SIZE;
    buf->buff = xmalloc(buf->size);

    buf->buff[0] = PHO_PROTOCOL_VERSION;
    pho_request__pack(req, (uint8_t *)buf->buff + PHO_PROTOCOL_VERSION_SIZE);
}

pho_req_t *pho_srl_request_unpack(struct pho_buff *buf)
{
    pho_req_t *req = NULL;

    if (buf->buff[0] != PHO_PROTOCOL_VERSION)
        pho_error(-EPROTONOSUPPORT, "The protocol version '%d' is not correct,"
                  "requested version is '%d'",
                  buf->buff[0], PHO_PROTOCOL_VERSION);
    else
        req = pho_request__unpack(NULL, buf->size - PHO_PROTOCOL_VERSION_SIZE,
                                  (uint8_t *)buf->buff +
                                      PHO_PROTOCOL_VERSION_SIZE);

    if (!req)
        pho_error(-EINVAL, "Problem with request unpacking");

    free(buf->buff);

    return req;
}

void pho_srl_response_pack(pho_resp_t *resp, struct pho_buff *buf)
{
    buf->size = pho_response__get_packed_size(resp) + PHO_PROTOCOL_VERSION_SIZE;
    buf->buff = xmalloc(buf->size);

    buf->buff[0] = PHO_PROTOCOL_VERSION;
    pho_response__pack(resp, (uint8_t *)buf->buff + PHO_PROTOCOL_VERSION_SIZE);
}

pho_resp_t *pho_srl_response_unpack(struct pho_buff *buf)
{
    pho_resp_t *resp = NULL;

    if (buf->buff[0] != PHO_PROTOCOL_VERSION)
        pho_error(-EPROTONOSUPPORT, "The protocol version '%d' is not correct,"
                  "requested version is '%d'",
                  buf->buff[0], PHO_PROTOCOL_VERSION);
    else
        resp = pho_response__unpack(NULL, buf->size - PHO_PROTOCOL_VERSION_SIZE,
                                    (uint8_t *)buf->buff +
                                        PHO_PROTOCOL_VERSION_SIZE);

    if (!resp)
        pho_error(-EINVAL, "Problem with response unpacking");

    free(buf->buff);

    return resp;
}

