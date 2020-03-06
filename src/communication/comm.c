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
 * \brief   Phobos communication interface.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_comm.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "pho_common.h"

/** Used to limit the received buffer size and avoid large allocations. */
#define MAX_RECV_BUF_SIZE (16*1024LL)

enum _pho_comm_cri_msg_kind {
    PHO_CRI_MSG_SIZE,
    PHO_CRI_MSG_BUFF
};

/** Used to track the context of each incoming message in epoll. */
struct _pho_comm_recv_info {
    int fd;         /*!< Socket descriptor. */
    enum _pho_comm_cri_msg_kind mkind;
                    /*!< Message kind, used to determine what we are
                     *   currently reading.
                     */
    size_t len;     /*!< Requested buffer size. */
    size_t cur;     /*!< Current size of received data. */
    char *buf;      /*!< Buffer. */
};

static inline void _init_comm_recv_info(struct _pho_comm_recv_info *cri,
                                        const int fd,
                                        const enum _pho_comm_cri_msg_kind mkind,
                                        const size_t len,
                                        const size_t cur, char *buf)
{
    cri->fd = fd;
    cri->mkind = mkind;
    cri->len = len;
    cri->cur = cur;
    cri->buf = buf;
}

int pho_comm_open(struct pho_comm_info *ci, const char *sock_path,
                  const bool is_server)
{
    struct _pho_comm_recv_info *cri = NULL;
    const int n_max_listen = 128;
    struct sockaddr_un socka;
    struct epoll_event ev;
    int rc = 0;

    *ci = pho_comm_info_init();
    ci->is_server = is_server;
    ev.data.ptr = NULL;

    /* offline mode */
    if (sock_path == NULL)
        return 0;

    if (strlen(sock_path) >= sizeof(socka.sun_path))
        LOG_RETURN(-EINVAL, "sock_path length of %lu, greater than "
                   "socka.sun_path length of %lu, sock_path value: %s",
                   strlen(sock_path), sizeof(socka.sun_path), sock_path);

    if (is_server) {
        if (access(sock_path, F_OK) != -1) {
            pho_warn("Socket already exists(%s), will remove the old one",
                     sock_path);
            unlink(sock_path);
        }
    } else if (access(sock_path, F_OK) == -1) {
        /* if the client does not see the LRS socket, we return ENOTCONN,
         * then each client will decide if they need the LRS or not.
         */
        LOG_RETURN(-ENOTCONN,
                   "Socket does not exist(%s), means that the LRS is not "
                   "up or the socket path is not correct", sock_path);
    }

    ci->path = strdup(sock_path);
    ci->socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ci->socket_fd == -1)
        LOG_GOTO(out_err, rc = -errno, "Socket opening failed");

    socka.sun_family = AF_UNIX;
    strncpy(socka.sun_path, ci->path, sizeof(socka.sun_path));
    /* make sure the path is zero-terminated */
    socka.sun_path[sizeof(socka.sun_path)-1] = '\0';

    if (!is_server) {
        if (connect(ci->socket_fd, (struct sockaddr *)&socka, sizeof(socka)))
            LOG_GOTO(out_err, rc = -errno,
                     "Socket connection(%s) failed", ci->path);

        return 0;
    }

    cri = malloc(sizeof(*cri));
    if (!cri)
        LOG_GOTO(out_err, rc = -ENOMEM,
                "Socket poll main event allocation failed");
    /* only initialize the fd field here: the other ones are not used for
     * accepting new clients.
     */
    _init_comm_recv_info(cri, ci->socket_fd, PHO_CRI_MSG_SIZE, 0, 0, NULL);

    ev.events = EPOLLIN;
    ev.data.ptr = cri;

    if (bind(ci->socket_fd, (struct sockaddr *) &socka, sizeof(socka)))
        LOG_GOTO(out_err, rc = -errno,
                 "Socket binding(%s) failed", ci->path);

    if (listen(ci->socket_fd, n_max_listen))
        LOG_GOTO(out_err, rc = -errno, "Socket listening failed");

    ci->epoll_fd = epoll_create(1);
    if (ci->epoll_fd == -1)
        LOG_GOTO(out_err, rc = -errno, "Socket poll creation failed");

    if (epoll_ctl(ci->epoll_fd, EPOLL_CTL_ADD, ci->socket_fd, &ev))
        LOG_GOTO(out_err, rc = -errno,
                 "Socket poll control failed in adding(%s)", ci->path);

    ci->ev_tab = g_hash_table_new(NULL, NULL);
    g_hash_table_insert(ci->ev_tab, &cri->fd, cri);

    return 0;

out_err:
    if (ci->epoll_fd != -1) {
        close(ci->epoll_fd);
        ci->epoll_fd = -1;
    }
    if (ci->socket_fd != -1) {
        close(ci->socket_fd);
        ci->socket_fd = -1;
    }
    free(cri);
    free(ci->path);
    ci->path = NULL;

    return rc;
}

static void _release_comm_recv_info(struct _pho_comm_recv_info *cri)
{
    if (cri == NULL)
        return;

    close(cri->fd);
    free(cri->buf);
    free(cri);
}

static void _release_event(void *key, void *val, void *udata)
{
    _release_comm_recv_info((struct _pho_comm_recv_info *)val);
}

int pho_comm_close(struct pho_comm_info *ci)
{
    int rc = 0;

    /* offline mode */
    if (ci->socket_fd < 0)
        return 0;

    if (!ci->is_server) {
        if (close(ci->socket_fd))
            rc = -errno;
        free(ci->path);
        return rc;
    }

    /* close sockets (including ci->socket_fd) and free event information */
    g_hash_table_foreach(ci->ev_tab, _release_event, NULL);
    g_hash_table_destroy(ci->ev_tab);

    if (close(ci->epoll_fd))
        rc = rc ? : -errno;

    if (unlink(ci->path))
        rc = rc ? : -errno;

    free(ci->path);

    return rc;
}

static int _send_until_complete(int fd, const void *buf, size_t size)
{
    size_t count;

    while (size) {
        count = send(fd, buf, size, MSG_NOSIGNAL);
        if (count == -1)
            return -errno;
        size -= count;
    }

    return 0;
}

/**
 * The message is split in two parts:
 * - the buffer size (a 32-bit integer)
 * - the buffer contents (a byte array)
 */
int pho_comm_send(const struct pho_comm_data *data)
{
    uint32_t tlen;
    int rc;

    assert(data->fd >= 0); /* if assert, programming error */

    tlen = htonl(data->buf.size);

    rc = _send_until_complete(data->fd, &tlen, sizeof(tlen));
    if (rc)
        LOG_RETURN(rc, "Socket send failed (size part)");

    rc = _send_until_complete(data->fd, data->buf.buff, data->buf.size);
    if (rc)
        LOG_RETURN(rc, "Socket send failed (contents part)");

    pho_debug("Sending %zu bytes", data->buf.size);

    return 0;
}

/**
 * Read data until the message is fully received or failure (timeout or error).
 *
 * \return      0     if the message is complete,
 *             -errno else
 */
static int _recv_full(struct _pho_comm_recv_info *cri)
{
    ssize_t sz;

    sz = recv(cri->fd, cri->buf, cri->len, MSG_WAITALL);

    if (sz == -1)
        return -errno;
    else if (sz == 0)
        return -ENOTCONN;

    return 0;
}

/**
 * Read the available data.
 *
 * \return      0      if all data were read,
 *             -EAGAIN if not all data were read,
 *             -errno  else
 */
static int _recv_partial(struct _pho_comm_recv_info *cri)
{
    ssize_t sz;

    sz = recv(cri->fd, cri->buf + cri->cur, cri->len - cri->cur, MSG_DONTWAIT);

    if (sz == -1)
        return -errno;
    else if (sz == 0)
        return -ENOTCONN;

    cri->cur += sz;
    if (cri->cur != cri->len) {
        pho_debug("Message is incomplete, must be retrieved later");
        return -EAGAIN;
    }

    return 0;
}

/**
 * Receives data in client side.
 */
static int _recv_client(struct pho_comm_info *ci, struct pho_comm_data **data,
                        int *nb_data)
{
    struct _pho_comm_recv_info cri;
    uint32_t rc = 0;
    uint32_t tlen;

    *nb_data = 0;
    *data = malloc(sizeof(**data));
    if (!*data)
        LOG_RETURN(-ENOMEM, "Socket response alloc failed");

    /* receiving buffer size */
    _init_comm_recv_info(&cri, ci->socket_fd, PHO_CRI_MSG_SIZE, sizeof(tlen),
                         0, (char *)&tlen);

    rc = _recv_full(&cri);
    /* considering no response (which is a success) */
    if (rc == -EAGAIN || rc == -EWOULDBLOCK) {
        rc = 0;
        goto err;
    } else if (rc) {
        LOG_GOTO(err, rc, "Client socket recv failed");
    }

    (*data)->buf.size = ntohl(tlen);
    if ((*data)->buf.size > MAX_RECV_BUF_SIZE)
        LOG_GOTO(err, rc = -EBADMSG, "Requested buffer size is too large");

    (*data)->buf.buff = malloc((*data)->buf.size *
                                sizeof(*(*data)->buf.buff));
    if (!(*data)->buf.buff)
        LOG_GOTO(err, rc = -ENOMEM, "Buffer allocation failed");

    /* receiving buffer contents */
    _init_comm_recv_info(&cri, ci->socket_fd, PHO_CRI_MSG_BUFF,
                         (*data)->buf.size, 0, (*data)->buf.buff);

    rc = _recv_full(&cri);
    if (rc)
        LOG_GOTO(err_buf, rc, "Client socket recv failed");

    pho_debug("Received a message of %zu bytes", (*data)->buf.size);

    *nb_data = 1;
    return 0;

err_buf:
    free((*data)->buf.buff);

err:
    free(*data);
    *data = NULL;
    return rc;
}

/**
 * Process an accept request from a client by adding its socket descriptor
 * to the socket poll.
 */
static int _process_accept(struct pho_comm_info *ci,
                           struct _pho_comm_recv_info *cri)
{
    struct _pho_comm_recv_info *n_cri;
    struct sockaddr_un socka;
    struct epoll_event ev;
    socklen_t lensocka;
    int sfd;
    int rc;

    /* accepting a new client */
    lensocka = sizeof(socka);
    sfd = accept(cri->fd, (struct sockaddr *) &socka, &lensocka);
    if (sfd == -1)
        LOG_RETURN(-errno, "Socket accept failed");

    /* configuring the server-side client socket */
    rc = fcntl(sfd, F_GETFL);
    if (rc == -1) {
        close(sfd);
        LOG_RETURN(-errno, "Socket config. getter failed");
    }
    rc |= O_NONBLOCK;
    rc = fcntl(sfd, F_SETFL, rc);
    if (rc == -1) {
        close(sfd);
        LOG_RETURN(-errno, "Socket config. setter failed");
    }

    n_cri = malloc(sizeof(*n_cri));
    if (!n_cri) {
        close(sfd);
        LOG_RETURN(-errno, "Socket poll ev. allocation failed");
    }
    _init_comm_recv_info(n_cri, sfd, PHO_CRI_MSG_SIZE, 0, 0, NULL);

    ev.data.ptr = n_cri;
    ev.events = EPOLLIN;

    /* adding the client to the socket poll */
    rc = epoll_ctl(ci->epoll_fd, EPOLL_CTL_ADD, sfd, &ev);
    if (rc == -1) {
        free(n_cri);
        close(sfd);
        LOG_RETURN(-errno, "Socket poll control failed in adding");
    }

    g_hash_table_insert(ci->ev_tab, &n_cri->fd, n_cri);

    return 0;
}

/**
 * Close a client connection.
 */
static int _process_close(struct pho_comm_info *ci,
                          struct _pho_comm_recv_info *cri,
                          struct pho_comm_data *data)
{
    int rc;

    data->fd = cri->fd;
    data->buf.size = -1;    /* 'close' message */
    data->buf.buff = NULL;

    rc = epoll_ctl(ci->epoll_fd, EPOLL_CTL_DEL,
                   cri->fd, NULL);
    if (rc == -1)
        pho_warn("Socket poll control failed in deleting");

    /* remove the cri from the event data array */
    g_hash_table_remove(ci->ev_tab, &cri->fd);

    _release_comm_recv_info(cri);

    return rc;
}

static int _process_recv_size(struct pho_comm_info *ci,
                              struct _pho_comm_recv_info *cri,
                              struct pho_comm_data *data)
{
    int tlen;
    int rc;

    if (!cri->buf) { /* not resuming, allocate the size buffer */
        _init_comm_recv_info(cri, cri->fd, PHO_CRI_MSG_SIZE, sizeof(tlen), 0,
                             (char *)malloc(sizeof(tlen)));
        if (!cri->buf)
            LOG_RETURN(rc = -ENOMEM, "Size buffer allocation failed");
    }

    rc = _recv_partial(cri);
    if (rc)
        return rc;

    /* initializing recv_info for the message contents */
    memcpy(&tlen, cri->buf, sizeof(tlen));
    free(cri->buf);

    _init_comm_recv_info(cri, cri->fd, PHO_CRI_MSG_BUFF, ntohl(tlen), 0, NULL);
    if (cri->len > MAX_RECV_BUF_SIZE)
        LOG_RETURN(rc = -EBADMSG, "Requested buffer size is too large");

    return rc;
}

static int _process_recv_contents(struct pho_comm_info *ci,
                                  struct _pho_comm_recv_info *cri,
                                  struct pho_comm_data *data)
{
    if (!cri->buf) { /* not resuming, allocate the msg buffer */
        _init_comm_recv_info(cri, cri->fd, PHO_CRI_MSG_BUFF, cri->len, 0,
                             (char *)malloc(cri->len));
        if (!cri->buf)
            LOG_RETURN(-ENOMEM, "Message buffer allocation failed");
    }

    return _recv_partial(cri);
}

/**
 * Receives data in server side.
 */
static int _recv_server(struct pho_comm_info *ci, struct pho_comm_data **data,
                        int *nb_data)
{
    struct epoll_event ev[g_hash_table_size(ci->ev_tab)];
    int idx_event, idx_data = 0;
    int rca = 0;

    /* probing the socket poll */
    *nb_data = epoll_wait(ci->epoll_fd, ev, g_hash_table_size(ci->ev_tab), 100);
    if (*nb_data == -1)
        LOG_RETURN(-errno, "Socket poll probe failed");
    if (*nb_data == 0)
        return 0;

    *data = malloc(*nb_data * sizeof(**data));
    if (!*data) {
        *nb_data = 0;
        LOG_RETURN(-ENOMEM, "Buffer allocation failed");
    }

    /* processing the socket poll events */
    for (idx_event = 0; idx_event < *nb_data; ++idx_event) {
        int rc;
        struct _pho_comm_recv_info *cri
            = (struct _pho_comm_recv_info *) ev[idx_event].data.ptr;

        if (cri->fd == ci->socket_fd) { /* accept socket */
            rc = _process_accept(ci, cri);
            if (rc) {
                pho_error(rc, "Client accept failed");
                rca = rca ? : rc;
            }
            continue;
        }

        /* receiving a client message */
        if (cri->mkind == PHO_CRI_MSG_SIZE) {
            rc = _process_recv_size(ci, cri, (*data) + idx_data);

            if (rc) {
                if (rc == -EAGAIN || rc == -EWOULDBLOCK)
                    continue;
                if (rc == -ENOMEM)
                    LOG_GOTO(err, rc = -ENOMEM, "Error on allocation during "
                             "receiving");
                else if (rc != -ENOTCONN && rc != -ECONNRESET)
                    pho_error(rc, "Error with client connection, "
                              "will close it");
                else /* ENOTCONN & ECONNRESET are not considered as an error */
                    rc = 0;

                _process_close(ci, cri, (*data) + idx_data);
                ++idx_data;
                rca = rca ? : rc;
                continue;
            }
        }

        /* receiving the message contents */
        rc = _process_recv_contents(ci, cri, (*data) + idx_data);
        if (rc) {
            /* EAGAIN means we will try again later, not an error */
            if (rc != -EAGAIN) {
                pho_error(rc, "Error with client connection, "
                        "will close it");
                _process_close(ci, cri, (*data) + idx_data);
                ++idx_data;
                rca = rca ? : rc;
            }
            continue;
        }

        pho_debug("Received a message of %zu bytes", cri->len);

        (*data)[idx_data].fd = cri->fd;
        (*data)[idx_data].buf.size = cri->len;
        (*data)[idx_data].buf.buff = cri->buf;
        ++idx_data;

        /* init for next message */
        _init_comm_recv_info(cri, cri->fd, PHO_CRI_MSG_SIZE, 0, 0, NULL);
    }

err:
    if (idx_data != *nb_data) {
        struct pho_comm_data *ndata;

        ndata = realloc(*data, idx_data * sizeof(**data));
        if (idx_data && !ndata)
            pho_warn("Message pool realloc failed");
        else
            *data = ndata;

        *nb_data = idx_data;
    }

    return rca;
}

int pho_comm_recv(struct pho_comm_info *ci, struct pho_comm_data **data,
                  int *nb_data)
{
    assert(ci->socket_fd >= 0); /* if assert, programming error */

    *data = NULL;

    return ci->is_server ? _recv_server(ci, data, nb_data)
                         : _recv_client(ci, data, nb_data);
}
