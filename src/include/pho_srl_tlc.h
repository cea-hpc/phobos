/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2023 CEA/DAM.
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
 * \brief  Phobos TLC communication data structure helper.
 *         'srl' stands for SeRiaLizer.
 */
#ifndef _PHO_SRL_TLC_H
#define _PHO_SRL_TLC_H

#include "pho_types.h"

#include "pho_proto_tlc.pb-c.h"

/******************************************************************************/
/* Typedefs *******************************************************************/
/******************************************************************************/

typedef PhoTlcRequest               pho_tlc_req_t;

typedef PhoTlcResponse              pho_tlc_resp_t;
typedef PhoTlcResponse__Ping        pho_tlc_resp_ping_t;
typedef PhoTlcResponse__DriveLookup pho_tlc_resp_drive_lookup_t;
typedef PhoTlcResponse__Load        pho_tlc_resp_load_t;
typedef PhoTlcResponse__Unload      pho_tlc_resp_unload_t;
typedef PhoTlcResponse__Status      pho_tlc_resp_status_t;

/******************************************************************************/
/* Macros & constants *********************************************************/
/******************************************************************************/

/**
 * Current version of the protocol.
 * If the protocol version is greater than 127, need to increase its size
 * to an integer size (4 bytes).
 */
#define PHO_TLC_PROTOCOL_VERSION    1

/**
 * Protocol version size in bytes.
 */
#define PHO_TLC_PROTOCOL_VERSION_SIZE   1

/******************************************************************************/
/** Type checkers *************************************************************/
/******************************************************************************/

/**
 * Request ping checker.
 *
 * \param[in]       req         Request.
 *
 * \return                      true if the request is a ping one,
 *                              false otherwise.
 */
static inline bool pho_tlc_request_is_ping(const pho_tlc_req_t *req)
{
    return (req->has_ping && req->ping);
}

/**
 * Request drive lookup checker.
 *
 * \param[in]       req         Request.
 *
 * \return                      true if the request is a drive lookup one,
 *                              false otherwise.
 */
static inline bool pho_tlc_request_is_drive_lookup(const pho_tlc_req_t *req)
{
    return req->drive_lookup != NULL;
}

/**
 * Request load checker.
 *
 * \param[in]       req         Request.
 *
 * \return                      true if the request is a load one,
 *                              false otherwise.
 */
static inline bool pho_tlc_request_is_load(const pho_tlc_req_t *req)
{
    return req->load != NULL;
}

/**
 * Request unload checker.
 *
 * \param[in]       req         Request.
 *
 * \return                      true if the request is an unload one,
 *                              false otherwise.
 */
static inline bool pho_tlc_request_is_unload(const pho_tlc_req_t *req)
{
    return req->unload != NULL;
}

/**
 * Request status checker.
 *
 * \param[in]       req         Request.
 *
 * \return                      true if the request is a status one,
 *                              false otherwise.
 */
static inline bool pho_tlc_request_is_status(const pho_tlc_req_t *req)
{
    return req->status != NULL;
}

/**
 * Response ping checker.
 *
 * \param[in]       resp        Response.
 *
 * \return                      true if the response is a ping one,
 *                              false otherwise.
 */
static inline bool pho_tlc_response_is_ping(const pho_tlc_resp_t *resp)
{
    return resp->ping != NULL;
}

/**
 * Response drive lookup checker.
 *
 * \param[in]       resp        Response.
 *
 * \return                      true if the response is a drive lookup one,
 *                              false otherwise.
 */
static inline bool pho_tlc_response_is_drive_lookup(const pho_tlc_resp_t *resp)
{
    return resp->drive_lookup != NULL;
}

/**
 * Response error checker.
 *
 * \param[in]       resp        Response.
 *
 * \return                      true if the response is an error one, false
 *                              otherwise.
 */
static inline bool pho_tlc_response_is_error(const pho_tlc_resp_t *resp)
{
    return resp->error != NULL;
}

/**
 * Response load checker.
 *
 * \param[in]       resp        Response.
 *
 * \return                      true if the response is a load one,
 *                              false otherwise.
 */
static inline bool pho_tlc_response_is_load(const pho_tlc_resp_t *resp)
{
    return resp->load != NULL;
}

/**
 * Response unload checker.
 *
 * \param[in]       resp        Response.
 *
 * \return                      true if the response is an unload one, false
 *                              otherwise.
 */
static inline bool pho_tlc_response_is_unload(const pho_tlc_resp_t *resp)
{
    return resp->unload != NULL;
}
/**
 * Response status checker.
 *
 * \param[in]       resp        Response.
 *
 * \return                      true if the response is a status one, false
 *                              otherwise.
 */
static inline bool pho_tlc_response_is_status(const pho_tlc_resp_t *resp)
{
    return resp->status != NULL;
}

/******************************************************************************/
/** Allocators & Deallocators *************************************************/
/******************************************************************************/
/**
 * Allocation of ping request contents.
 *
 * \param[out]      req         Pointer to the request data structure.
 *
 * \return                      0 on success, -ENOMEM on failure.
 */
void pho_srl_tlc_request_ping_alloc(pho_tlc_req_t *req);

/**
 * Allocation of drive lookup request contents.
 *
 * \param[out]      req         Pointer to the request data structure.
 *
 * \return                      0 on success, -ENOMEM on failure.
 */
int pho_srl_tlc_request_drive_lookup_alloc(pho_tlc_req_t *req);

/**
 * Allocation of load request contents.
 *
 * \param[out]      req         Pointer to the request data structure.
 *
 * \return                      0 on success, -ENOMEM on failure.
 */
int pho_srl_tlc_request_load_alloc(pho_tlc_req_t *req);

/**
 * Allocation of unload request contents.
 *
 * \param[out]      req         Pointer to the request data structure.
 *
 * \return                      0 on success, -ENOMEM on failure.
 */
void pho_srl_tlc_request_unload_alloc(pho_tlc_req_t *req);

/**
 * Allocation of status request contents.
 *
 * \param[out]      req         Pointer to the request data structure.
 */
void pho_srl_tlc_request_status_alloc(pho_tlc_req_t *req);

/**
 * Release of request contents.
 *
 * \param[in]       req         Pointer to the request data structure.
 * \param[in]       unpack      true if the request comes from an unpack,
 *                              false otherwise.
 */
void pho_srl_tlc_request_free(pho_tlc_req_t *req, bool unpack);

/**
 * Allocation of ping response contents.
 *
 * \param[out]      resp        Pointer to the response data structure.
 */
int pho_srl_tlc_response_ping_alloc(pho_tlc_resp_t *resp);

/**
 * Allocation of drive lookup response content.
 *
 * \param[out]      resp        Pointer to the response data structure.
 *
 * \return                      0 on success, -ENOMEM on failure.
 */
int pho_srl_tlc_response_drive_lookup_alloc(pho_tlc_resp_t *resp);

/**
 * Allocation of load response content.
 *
 * \param[out]      resp        Pointer to the response data structure.
 */
void pho_srl_tlc_response_load_alloc(pho_tlc_resp_t *resp);

/**
 * Allocation of unload response content.
 *
 * \param[out]      resp        Pointer to the response data structure.
 */
void pho_srl_tlc_response_unload_alloc(pho_tlc_resp_t *resp);

/**
 * Allocation of status response content.
 *
 * \param[out]      resp        Pointer to the response data structure.
 */
void pho_srl_tlc_response_status_alloc(pho_tlc_resp_t *resp);

/**
 * Allocation of error response content.
 *
 * \param[out]      resp        Pointer to the response data structure.
 */
void pho_srl_tlc_response_error_alloc(pho_tlc_resp_t *resp);

/**
 * Release of response contents.
 *
 * \param[in]       resp        Pointer to the response data structure.
 * \param[in]       unpack      true if the response comes from an unpack,
 *                              false otherwise.
 */
void pho_srl_tlc_response_free(pho_tlc_resp_t *resp, bool unpack);

/******************************************************************************/
/* Packers & Unpackers ********************************************************/
/******************************************************************************/

/**
 * Serialization of a request.
 *
 * The allocation of the buffer is made in this function. buf->buff must be
 * freed after this calling this function.
 *
 * \param[in]       req         Request data structure.
 * \param[out]      buf         Serialized buffer data structure.
 *
 * \return                      0 on success, -ENOMEM on failure.
 */
int pho_srl_tlc_request_pack(pho_tlc_req_t *req, struct pho_buff *buf);

/**
 * Deserialization of a request.
 *
 * Once the request in unpacked, the buffer is released. The request structure
 * must be freed using pho_srl_tlc_request_free(r, true).
 *
 * \param[in]       buf         Serialized buffer data structure.
 *
 * \return                      Request data structure.
 */
pho_tlc_req_t *pho_srl_tlc_request_unpack(struct pho_buff *buf);

/**
 * Serialization of a response.
 *
 * The allocation of the buffer is made in this function. buf->buff must be
 * freed after calling this function.
 *
 * \param[in]       resp        Response data structure.
 * \param[out]      buf         Serialized buffer data structure.
 *
 * \return                      0 on success, -ENOMEM on failure.
 */
int pho_srl_tlc_response_pack(pho_tlc_resp_t *resp, struct pho_buff *buf);

/**
 * Deserialization of a response.
 *
 * Once the response is unpacked, the buffer is released. The response structure
 * must be freed using pho_srl_response_free(r, true).
 *
 * \param[in]       buf         Serialized buffer data structure.
 *
 * \return                      Response data structure.
 */
pho_tlc_resp_t *pho_srl_tlc_response_unpack(struct pho_buff *buf);

#endif
