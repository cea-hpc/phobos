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
#ifndef _PHO_COMM_WRAPPER_H
#define _PHO_COMM_WRAPPER_H

#include "pho_srl_lrs.h"

/**
 * Send a request to a communication socket
 *
 * The request is packed, then freed.
 *
 * @param[in]  comm     Communication socket interface
 * @param[in]  req      Request to send through the socket
 * @return     0 on success, negated errno on failure
 */
int comm_send(struct pho_comm_info *comm, pho_req_t *req);

/**
 * Receive a response from a communication socket
 *
 * The response is unpacked.
 *
 * @param[in]  comm     Communication socket interface
 * @param[out] resp     Response retrieved from the socket
 * @return     0 on success, negated errno on failure
 */
int comm_recv(struct pho_comm_info *comm, pho_resp_t **resp);

/**
 * Send a request to a communication socket and wait for a response
 *
 * The request is packed, then freed.
 * The response is unpacked.
 *
 * @param[in]  comm     Communication socket interface
 * @param[in]  req      Request to send through the socket
 * @param[out] resp     Response retrieve from the socket
 * @return     0 on success, negated errno on failure
 */
int comm_send_and_recv(struct pho_comm_info *comm, pho_req_t *req,
                       pho_resp_t **resp);

#endif
