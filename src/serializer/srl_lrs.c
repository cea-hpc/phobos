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
    _RESP_RELEASE,
    _RESP_ERROR,
};

static const char *const SRL_REQ_KIND_STRS[] = {
    [PHO_REQUEST_KIND__RQ_WRITE] = "write alloc",
    [PHO_REQUEST_KIND__RQ_READ] = "read alloc",
    [PHO_REQUEST_KIND__RQ_RELEASE] = "release"
};

static const char *const SRL_RESP_KIND_STRS[] = {
    [_RESP_WRITE] = "write alloc",
    [_RESP_READ] = "read alloc",
    [_RESP_RELEASE] = "release",
    [_RESP_ERROR] = "error"
};

const char *pho_srl_request_kind_str(pho_req_t *req)
{
    if (pho_request_is_write(req))
        return SRL_REQ_KIND_STRS[PHO_REQUEST_KIND__RQ_WRITE];
    if (pho_request_is_read(req))
        return SRL_REQ_KIND_STRS[PHO_REQUEST_KIND__RQ_READ];
    if (pho_request_is_release(req))
        return SRL_REQ_KIND_STRS[PHO_REQUEST_KIND__RQ_RELEASE];

    return "<invalid>";
}

const char *pho_srl_response_kind_str(pho_resp_t *resp)
{
    if (pho_response_is_write(resp))
        return SRL_RESP_KIND_STRS[_RESP_WRITE];
    if (pho_response_is_read(resp))
        return SRL_RESP_KIND_STRS[_RESP_READ];
    if (pho_response_is_release(resp))
        return SRL_RESP_KIND_STRS[_RESP_RELEASE];
    if (pho_response_is_error(resp))
        return SRL_RESP_KIND_STRS[_RESP_ERROR];

    return "<invalid>";
}

const char *pho_srl_error_kind_str(pho_resp_error_t *err)
{
    if (err->req_kind > PHO_REQUEST_KIND__RQ_RELEASE)
        return "<invalid>";

    return SRL_REQ_KIND_STRS[err->req_kind];
}

int pho_srl_request_write_alloc(pho_req_t *req, size_t n_media,
                                size_t *n_tags)
{
    int i;

    pho_request__init(req);

    req->walloc = malloc(sizeof(*req->walloc));
    if (!req->walloc)
        goto err_walloc;
    pho_request__write__init(req->walloc);

    req->walloc->n_media = n_media;
    req->walloc->media = malloc(n_media * sizeof(*req->walloc->media));
    if (!req->walloc->media)
        goto err_media;

    for (i = 0; i < n_media; ++i) {
        req->walloc->media[i] = malloc(sizeof(*req->walloc->media[i]));
        if (!req->walloc->media[i])
            goto err_media_i;
        pho_request__write__elt__init(req->walloc->media[i]);

        req->walloc->media[i]->n_tags = n_tags[i];
        req->walloc->media[i]->tags =
            calloc(n_tags[i], sizeof(*req->walloc->media[i]->tags));
        if (!req->walloc->media[i]->tags)
            goto err_tags;
    }

    return 0;

err_tags:
    free(req->walloc->media[i]);

err_media_i:
    for (--i; i >= 0; --i) {
        free(req->walloc->media[i]->tags);
        free(req->walloc->media[i]);
    }
    free(req->walloc->media);

err_media:
    free(req->walloc);

err_walloc:
    return -ENOMEM;
}

int pho_srl_request_read_alloc(pho_req_t *req, size_t n_media)
{
    int i;

    pho_request__init(req);

    req->ralloc = malloc(sizeof(*req->ralloc));
    if (!req->ralloc)
        goto err_ralloc;
    pho_request__read__init(req->ralloc);

    req->ralloc->n_med_ids = n_media;
    req->ralloc->med_ids = malloc(n_media * sizeof(*req->ralloc->med_ids));
    if (!req->ralloc->med_ids)
        goto err_media;

    for (i = 0; i < n_media; ++i) {
        req->ralloc->med_ids[i] = malloc(sizeof(*req->ralloc->med_ids[i]));
        if (!req->ralloc->med_ids[i])
            goto err_media_i;
        pho_medium_info__init(req->ralloc->med_ids[i]);
    }

    return 0;

err_media_i:
    for (--i; i >= 0; --i)
        free(req->ralloc->med_ids[i]);
    free(req->ralloc->med_ids);

err_media:
    free(req->ralloc);

err_ralloc:
    return -ENOMEM;
}

int pho_srl_request_release_alloc(pho_req_t *req, size_t n_media)
{
    int i;

    pho_request__init(req);

    req->release = malloc(sizeof(*req->release));
    if (!req->release)
        goto err_release;
    pho_request__release__init(req->release);

    req->release->n_media = n_media;
    req->release->media = malloc(n_media * sizeof(*req->release->media));
    if (!req->release->media)
        goto err_media;

    for (i = 0; i < n_media; ++i) {
        req->release->media[i] = malloc(sizeof(*req->release->media[i]));
        if (!req->release->media[i])
            goto err_media_i;
        pho_request__release__elt__init(req->release->media[i]);

        req->release->media[i]->med_id =
            malloc(sizeof(*req->release->media[i]->med_id));
        if (!req->release->media[i]->med_id)
            goto err_id;
        pho_medium_info__init(req->release->media[i]->med_id);
    }

    return 0;

err_id:
    free(req->release->media[i]);

err_media_i:
    for (--i; i >= 0; --i) {
        free(req->release->media[i]->med_id);
        free(req->release->media[i]);
    }
    free(req->release->media);

err_media:
    free(req->release);

err_release:
    return -ENOMEM;
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
        free(req->walloc);
        req->walloc = NULL;
    }

    if (req->ralloc) {
        for (i = 0; i < req->ralloc->n_med_ids; ++i) {
            free(req->ralloc->med_ids[i]->id);
            free(req->ralloc->med_ids[i]);
        }
        free(req->ralloc->med_ids);
        free(req->ralloc);
        req->ralloc = NULL;
    }

    if (req->release) {
        for (i = 0; i < req->release->n_media; ++i) {
            free(req->release->media[i]->med_id->id);
            free(req->release->media[i]->med_id);
            free(req->release->media[i]);
        }
        free(req->release->media);
        free(req->release);
        req->release = NULL;
    }
}

int pho_srl_response_write_alloc(pho_resp_t *resp, size_t n_media)
{
    int i;

    pho_response__init(resp);

    resp->walloc = malloc(sizeof(*resp->walloc));
    if (!resp->walloc)
        goto err_walloc;
    pho_response__write__init(resp->walloc);

    resp->walloc->n_media = n_media;
    resp->walloc->media = malloc(n_media * sizeof(*resp->walloc->media));
    if (!resp->walloc->media)
        goto err_media;

    for (i = 0; i < n_media; ++i) {
        resp->walloc->media[i] = malloc(sizeof(*resp->walloc->media[i]));
        if (!resp->walloc->media[i])
            goto err_media_i;
        pho_response__write__elt__init(resp->walloc->media[i]);

        resp->walloc->media[i]->med_id =
            malloc(sizeof(*resp->walloc->media[i]->med_id));
        if (!resp->walloc->media[i]->med_id)
            goto err_id;
        pho_medium_info__init(resp->walloc->media[i]->med_id);
    }

    return 0;

err_id:
    free(resp->walloc->media[i]);

err_media_i:
    for (--i; i >= 0; --i) {
        free(resp->walloc->media[i]->med_id);
        free(resp->walloc->media[i]);
    }
    free(resp->walloc->media);

err_media:
    free(resp->walloc);

err_walloc:
    return -ENOMEM;
}

int pho_srl_response_read_alloc(pho_resp_t *resp, size_t n_media)
{
    int i;

    pho_response__init(resp);

    resp->ralloc = malloc(sizeof(*resp->ralloc));
    if (!resp->ralloc)
        goto err_ralloc;
    pho_response__read__init(resp->ralloc);

    resp->ralloc->n_media = n_media;
    resp->ralloc->media = malloc(n_media * sizeof(*resp->ralloc->media));
    if (!resp->ralloc->media)
        goto err_media;

    for (i = 0; i < n_media; ++i) {
        resp->ralloc->media[i] = malloc(sizeof(*resp->ralloc->media[i]));
        if (!resp->ralloc->media[i])
            goto err_media_i;
        pho_response__read__elt__init(resp->ralloc->media[i]);

        resp->ralloc->media[i]->med_id =
            malloc(sizeof(*resp->ralloc->media[i]->med_id));
        if (!resp->ralloc->media[i]->med_id)
            goto err_id;
        pho_medium_info__init(resp->ralloc->media[i]->med_id);
    }

    return 0;

err_id:
    free(resp->ralloc->media[i]);

err_media_i:
    for (--i; i >= 0; --i) {
        free(resp->ralloc->media[i]->med_id);
        free(resp->ralloc->media[i]);
    }
    free(resp->ralloc->media);

err_media:
    free(resp->ralloc);

err_ralloc:
    return -ENOMEM;
}

int pho_srl_response_release_alloc(pho_resp_t *resp, size_t n_media)
{
    int i;

    pho_response__init(resp);

    resp->release = malloc(sizeof(*resp->release));
    if (!resp->release)
        goto err_release;
    pho_response__release__init(resp->release);

    resp->release->n_med_ids = n_media;
    resp->release->med_ids = malloc(n_media * sizeof(*resp->release->med_ids));
    if (!resp->release->med_ids)
        goto err_media;

    for (i = 0; i < n_media; ++i) {
        resp->release->med_ids[i] = malloc(sizeof(*resp->release->med_ids[i]));
        if (!resp->release->med_ids[i])
            goto err_media_i;
        pho_medium_info__init(resp->release->med_ids[i]);
    }

    return 0;

err_media_i:
    for (--i; i >= 0; --i)
        free(resp->release->med_ids[i]);

    free(resp->release->med_ids);

err_media:
    free(resp->release);

err_release:
    return -ENOMEM;
}

int pho_srl_response_error_alloc(pho_resp_t *resp)
{
    pho_response__init(resp);

    resp->error = malloc(sizeof(*resp->error));
    if (!resp->error)
        return -ENOMEM;
    pho_response__error__init(resp->error);

    return 0;
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
            free(resp->walloc->media[i]->med_id->id);
            free(resp->walloc->media[i]->med_id);
            free(resp->walloc->media[i]->root_path);
            free(resp->walloc->media[i]);
        }
        free(resp->walloc->media);
        free(resp->walloc);
        resp->walloc = NULL;
    }

    if (resp->ralloc) {
        for (i = 0; i < resp->ralloc->n_media; ++i) {
            free(resp->ralloc->media[i]->med_id->id);
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
            free(resp->release->med_ids[i]->id);
            free(resp->release->med_ids[i]);
        }
        free(resp->release->med_ids);
        free(resp->release);
        resp->release = NULL;
    }

    if (resp->error) {
        free(resp->error);
        resp->error = NULL;
    }
}

/* If the protocol version is greater than 127, need to increase  */
int pho_srl_request_pack(pho_req_t *req, struct pho_ubuff *buf)
{
    buf->size = pho_request__get_packed_size(req) + PHO_PROTOCOL_VERSION_SIZE;
    buf->buff = malloc(buf->size);
    if (!buf->buff)
        return -ENOMEM;

    buf->buff[0] = PHO_PROTOCOL_VERSION;
    pho_request__pack(req, buf->buff + PHO_PROTOCOL_VERSION_SIZE);

    return 0;
}

pho_req_t *pho_srl_request_unpack(struct pho_ubuff *buf)
{
    pho_req_t *req = NULL;

    if (buf->buff[0] != PHO_PROTOCOL_VERSION)
        pho_error(-EPROTONOSUPPORT, "The protocol version '%d' is not correct,"
                  "requested version is '%d'",
                  buf->buff[0], PHO_PROTOCOL_VERSION);
    else
        req = pho_request__unpack(NULL, buf->size - PHO_PROTOCOL_VERSION_SIZE,
                                  buf->buff + PHO_PROTOCOL_VERSION_SIZE);

    if (!req)
        pho_error(-EINVAL, "Problem with request unpacking");

    free(buf->buff);

    return req;
}

int pho_srl_response_pack(pho_resp_t *resp, struct pho_ubuff *buf)
{
    buf->size = pho_response__get_packed_size(resp) + PHO_PROTOCOL_VERSION_SIZE;
    buf->buff = malloc(buf->size);
    if (!buf->buff)
        return -ENOMEM;

    buf->buff[0] = PHO_PROTOCOL_VERSION;
    pho_response__pack(resp, buf->buff + PHO_PROTOCOL_VERSION_SIZE);

    return 0;
}

pho_resp_t *pho_srl_response_unpack(struct pho_ubuff *buf)
{
    pho_resp_t *resp = NULL;

    if (buf->buff[0] != PHO_PROTOCOL_VERSION)
        pho_error(-EPROTONOSUPPORT, "The protocol version '%d' is not correct,"
                  "requested version is '%d'",
                  buf->buff[0], PHO_PROTOCOL_VERSION);
    else
        resp = pho_response__unpack(NULL, buf->size - PHO_PROTOCOL_VERSION_SIZE,
                                    buf->buff + PHO_PROTOCOL_VERSION_SIZE);

    if (!resp)
        pho_error(-EINVAL, "Problem with response unpacking");

    free(buf->buff);

    return resp;
}

