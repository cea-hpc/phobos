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
/**
 * \brief   Phobos communication interface.
 */
#ifndef _PHO_COMM_H
#define _PHO_COMM_H

#include <glib.h>

#include "pho_types.h"

#define LRS_SOCKET_CFG_ITEM {.section = "lrs", \
                             .name = "server_socket", \
                             .value = "/tmp/socklrs"}
#define TLC_HOSTNAME_CFG_ITEM {.section = "tlc", \
                               .name = "hostname", \
                               .value = "localhost"}
#define TLC_PORT_CFG_ITEM {.section = "tlc", \
                           .name = "port", \
                           .value = "20123"}


#define TLC_SECTION_CFG "tlc_%s"
#define TLC_HOSTNAME_CFG_PARAM "hostname"
#define DEFAULT_TLC_HOSTNAME "localhost"
#define TLC_PORT_CFG_PARAM "port"
#define DEFAULT_TLC_PORT 20123
/* listen_hostname/hostname and port are failover settings:
 * if listen_hostname is not set, hostame is used
 */
#define TLC_LISTEN_HOSTNAME_CFG_PARAM "listen_hostname"
#define TLC_LISTEN_PORT_CFG_PARAM "listen_port"
#define TLC_LIB_DEVICE_CFG_PARAM "lib_device"
#define DEFAULT_TLC_LIB_DEVICE "/dev/changer"

/**
 * Address of an AF_UNIX or AF_INET socket
 */
union pho_comm_addr {
    struct {
        const char *path;
    } af_unix;
    struct {
        const char *hostname;
        int port;
        const char *interface;
    } tcp;
};

enum pho_comm_socket_type {
    PHO_COMM_UNIX_SERVER,
    PHO_COMM_UNIX_CLIENT,
    PHO_COMM_TCP_SERVER,
    PHO_COMM_TCP_CLIENT,
};

/**
 * Data structure used to store communication information needed by this API.
 * This structure is initialized using pho_comm_open() and cleaned
 * using pho_comm_close().
 */
struct pho_comm_info {
    enum pho_comm_socket_type type;
    char *path;         /*!< AF_UNIX: path of the socket
                         *   AF_INET: "hostname:port"
                         */
    int socket_fd;      /*!< Main socket descriptor
                         *   (the one open with socket() call).
                         */
    int epoll_fd;       /*!< Socket poll descriptor (used by the server). */
    GHashTable *ev_tab; /*!< Hash table of events of the socket poll
                         *   (used by the server for cleaning).
                         */
};

/**
 * Initializer for the communication information data structure where
 * .socket_fd is initialized to -1, and gets the correct behavior if
 * pho_comm_close() is called before pho_comm_open().
 *
 * It is the caller responsability to call it if pho_comm_close() may be
 * called before pho_comm_open().
 *
 * \return                      An initialized data structure.
 */
static inline struct pho_comm_info pho_comm_info_init(void)
{
    struct pho_comm_info info = {
        .type = PHO_COMM_UNIX_CLIENT,
        .path = NULL,
        .socket_fd = -1,
        .epoll_fd = -1,
        .ev_tab = NULL
    };

    return info;
}

/** Data structure used to send and receive messages. */
struct pho_comm_data {
    int fd;             /*!< Socket descriptor where the msg comes from/to. */
    struct pho_buff buf;/*!< Message contents. */
};

/**
 * Initializer for the send & receive data structure where .fd is initialized
 * to the ci.socket_fd and the buffer is empty.
 *
 * It is the caller responsability to call it if .fd is not directly assigned,
 * which may lead to a socket error during sendings.
 *
 * \param[in]       ci          Communication info.
 * \return                      An initialized data structure.
 */
static inline struct pho_comm_data pho_comm_data_init(struct pho_comm_info *ci)
{
    struct pho_comm_data dt = {
        .fd = ci->socket_fd,
        .buf.size = 0,
        .buf.buff = NULL
    };

    return dt;
}

/**
 * Get TLC hostname from config
 *
 * \param[in]       library         Targeted library
 * \param[out]      tlc_hostname    TLC hostname
 *
 * \return                      0 on success, negative POSIX error on failure
 */
int tlc_hostname_from_cfg(const char *library, const char **tlc_hostname);

/**
 * Get TLC listen hostname from config
 *
 * \param[in]       library                 Targeted library
 * \param[out]      tlc_listen_hostname     TLC listen hostname
 *
 * \return                      0 on success, negative POSIX error on failure
 */
int tlc_listen_hostname_from_cfg(const char *library,
                                 const char **tlc_listen_hostname);

/**
 * Get tlc port from config
 *
 * \param[in]       library     Targeted library
 * \param[out]      tlc_port    TLC port
 *
 * \return                      0 on success, negative POSIX error on failure
 */
int tlc_port_from_cfg(const char *library, int *tlc_port);

/**
 * Get tlc listen port from config
 *
 * \param[in]       library             Targeted library
 * \param[out]      tlc_listen_port     TLC listen port
 *
 * \return                      0 on success, negative POSIX error on failure
 */
int tlc_listen_port_from_cfg(const char *library, int *tlc_listen_port);

/**
 * Get TLC library device from config
 *
 * \param[in]       library         Targeted library
 * \param[out]      tlc_lib_device  TLC library device
 *
 * \return                      0 on success, negative POSIX error on failure
 */
int tlc_lib_device_from_cfg(const char *library,
                            const char **tlc_library_device);

/**
 * Open a socket
 *
 * \param[out]      ci          Communication info to be initialized.
 * \param[in]       addr        Address of the server.
 * \param[in]       type        Which type of socket we are opening.
 *
 * \return                      0 on success, negative POSIX error on failure
 */
int pho_comm_open(struct pho_comm_info *ci, const union pho_comm_addr *addr,
                  enum pho_comm_socket_type type);

/**
 * Closer for the unix socket.
 *
 * The communication info structure will be cleaned.
 *
 * \param[in]       ci          Communication info.
 *
 * \return                      0 on success, -errno on failure.
 */
int pho_comm_close(struct pho_comm_info *ci);

/**
 * Send a message through the unix socket provided in data.
 *
 * \param[in]       data        Message data to send.
 *
 * \return                      0 on success, -errno on failure.
 */
int pho_comm_send(const struct pho_comm_data *data);

/**
 * Receive a message from the unix socket.
 *
 * The client receives one message per call.
 * The server will check its socket poll and receive all the available
 * messages ie. process the accept/close requests and retrieve the contents
 * sent by the clients.
 * The caller has to free the data array and each data contents (buffers).
 *
 *
 * \param[in]       ci          Communication info.
 * \param[out]      data        Received message data.
 * \param[out]      nb_data     Number of received message data (possibly 0).
 *
 * \return                      0 on succes, -errno on failure.
 */
int pho_comm_recv(struct pho_comm_info *ci, struct pho_comm_data **data,
                  int *nb_data);

#endif
