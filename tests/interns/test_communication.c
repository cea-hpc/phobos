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
 * \brief  Test communication API
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "pho_test_utils.h"
#include "pho_comm.h"

#define TCP_PORT_TEST 65530

struct pho_comm_addr_type {
    union pho_comm_addr addr;
    enum pho_comm_socket_type server_type;
    enum pho_comm_socket_type client_type;
};

static int test_open(void *arg)
{
    struct pho_comm_addr_type *addr_type = (struct pho_comm_addr_type *)arg;
    struct pho_comm_data *data = NULL;
    struct pho_comm_info ci_client;
    struct pho_comm_info ci_server;
    int rc, nb_data;

    rc = pho_comm_open(&ci_server, &addr_type->addr, addr_type->server_type);
    if (rc) {
        pho_error(rc, "Server socket opening failed with status %d\n", rc);
        return rc;
    }

    rc = pho_comm_open(&ci_client, &addr_type->addr, addr_type->client_type);
    if (rc) {
        pho_error(rc, "Client socket opening failed with status %d\n", rc);
        pho_comm_close(&ci_server);
        return rc;
    }

    rc = pho_comm_recv(&ci_server, &data, &nb_data);
    free(data);
    if (rc) {
        pho_error(rc, "Server recv failed with status %d\n", rc);
        pho_comm_close(&ci_server);
        pho_comm_close(&ci_client);
        return rc;
    }
    if (nb_data) {
        pho_error(rc = PHO_TEST_FAILURE,
                  "Server recv returned %d messages (expected 0)\n", nb_data);
        pho_comm_close(&ci_server);
        pho_comm_close(&ci_client);
        return rc;
    }

    rc = pho_comm_close(&ci_client);
    if (rc) {
        pho_error(rc, "Client connection closing failed");
        pho_comm_close(&ci_server);
        return rc;
    }

    rc = pho_comm_close(&ci_server);
    if (rc)
        pho_error(rc, "Server connection closing failed");

    return rc;
}

static int test_open_ex(void *arg)
{
    struct pho_comm_addr_type addr_type;
    char *path = (char *)arg;
    struct sockaddr_un socka;
    int fd, rc = 0;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1)
        LOG_RETURN(PHO_TEST_FAILURE, "cannot create socket for test\n");

    socka.sun_family = AF_UNIX;
    strcpy(socka.sun_path, path);

    rc = bind(fd, (struct sockaddr_un *) &socka, sizeof(socka));
    if (rc) {
        pho_error(rc, "cannot create socket '%s' for test\n", path);
        close(fd);
        unlink(path);
        return PHO_TEST_FAILURE;
    }

    close(fd);

    addr_type.addr.af_unix.path = path;
    addr_type.server_type = PHO_COMM_UNIX_SERVER;
    addr_type.client_type = PHO_COMM_UNIX_CLIENT;
    return test_open(&addr_type);
}

static int test_open_offline_addr_null(void *arg)
{
    struct pho_comm_addr_type *addr_type = (struct pho_comm_addr_type *)arg;
    struct pho_comm_info ci_client;
    struct pho_comm_info ci_server;
    int rc;

    rc = pho_comm_open(&ci_server, NULL, addr_type->server_type);
    if (rc)
        LOG_RETURN(rc, "server socket 'opening' (offline) failed");

    rc = pho_comm_open(&ci_client, NULL, addr_type->client_type);
    if (rc) {
        pho_error(rc, "client socket 'opening' (offline) failed");
        pho_comm_close(&ci_server);
        return rc;
    }

    rc = pho_comm_close(&ci_client);
    if (rc) {
        pho_error(rc, "Client connection closing failed");
        pho_comm_close(&ci_server);
        return rc;
    }

    rc = pho_comm_close(&ci_server);
    if (rc)
        pho_error(rc, "Server connection closing failed");

    return rc;
}

static int test_open_offline(void *arg)
{
    struct pho_comm_addr_type *addr_type = (struct pho_comm_addr_type *)arg;
    struct pho_comm_info ci_client;
    struct pho_comm_info ci_server;
    int rc;

    rc = pho_comm_open(&ci_server, &addr_type->addr, addr_type->server_type);
    if (rc)
        LOG_RETURN(rc, "server socket 'opening' (offline) failed");

    rc = pho_comm_open(&ci_client, &addr_type->addr, addr_type->client_type);
    if (rc) {
        pho_error(rc, "client socket 'opening' (offline) failed");
        pho_comm_close(&ci_server);
        return rc;
    }

    rc = pho_comm_close(&ci_client);
    if (rc) {
        pho_error(rc, "Client connection closing failed");
        pho_comm_close(&ci_server);
        return rc;
    }

    rc = pho_comm_close(&ci_server);
    if (rc)
        pho_error(rc, "Server connection closing failed");

    return rc;
}

static int test_sendrecv_simple(void *arg)
{
    struct pho_comm_addr_type *addr_type = (struct pho_comm_addr_type *)arg;
    struct pho_comm_data send_data_client;
    struct pho_comm_data send_data_server;
    struct pho_comm_data *data = NULL;
    struct pho_comm_info ci_server;
    struct pho_comm_info ci_client;
    int rc = 0, nb_data;

    assert(!pho_comm_open(&ci_server, &addr_type->addr,
                          addr_type->server_type));
    assert(!pho_comm_open(&ci_client, &addr_type->addr,
                          addr_type->client_type));
    assert(!pho_comm_recv(&ci_server, &data, &nb_data));
    free(data);

    send_data_client = pho_comm_data_init(&ci_client);
    send_data_client.buf.buff = xstrdup("Hello?");
    send_data_client.buf.size = strlen(send_data_client.buf.buff);

    send_data_server.buf.buff = xstrdup("World!");
    send_data_server.buf.size = strlen(send_data_server.buf.buff);

    rc = pho_comm_send(&send_data_client);
    if (rc) {
        pho_error(rc, "client cannot send message, status %d\n", rc);
        goto err_rc;
    }

    rc = pho_comm_recv(&ci_server, &data, &nb_data);
    if (rc || nb_data != 1) {
        if (rc)
            pho_error(rc, "server recv failed, status %d\n", rc);
        else
            pho_error(rc, "server receives %d messages, expected 1\n", nb_data);
        if (data)
            free(data->buf.buff);
        free(data);
        goto err_fail;
    }

    rc = PHO_TEST_SUCCESS;

    if (!data || !data->buf.buff) {
        pho_error(rc = -EBADMSG,
                  "client message is corrupted (buffer is not allocated)\n");
        free(data);
        goto err_rc;
    }
    if (data->buf.size != strlen(send_data_client.buf.buff) ||
        strncmp(data->buf.buff, send_data_client.buf.buff, data->buf.size)) {
        pho_error(rc = -EBADMSG, "client message is corrupted ('%s' != '%s')\n",
                  data->buf.buff, send_data_client.buf.buff);
        free(data->buf.buff);
        free(data);
        rc = EBADMSG;
        goto err_rc;
    }

    send_data_server.fd = data->fd;
    free(data->buf.buff);
    free(data);

    rc = pho_comm_send(&send_data_server);
    if (rc) {
        pho_error(rc, "server cannot send message, status %d\n", rc);
        goto err_rc;
    }

    rc = pho_comm_recv(&ci_client, &data, &nb_data);
    if (rc || nb_data != 1) {
        if (rc)
            pho_error(rc, "client recv failed, status %d\n", rc);
        else
            pho_error(rc, "client receives %d messages, expected 1\n", rc);
        if (data)
            free(data->buf.buff);
        free(data);
        goto err_fail;
    }

    rc = PHO_TEST_SUCCESS;

    if (!data || !data->buf.buff) {
        pho_error(rc = -EBADMSG,
                  "server message is corrupted (buffer is not allocated)\n");
        free(data);
        goto err_rc;
    }
    if (data->buf.size != strlen(send_data_server.buf.buff) ||
        strncmp(data->buf.buff, send_data_server.buf.buff, data->buf.size)) {
        pho_error(rc = -EBADMSG, "server message is corrupted ('%s' != '%s')\n",
                  data->buf.buff, send_data_server.buf.buff);
        free(data->buf.buff);
        free(data);
        goto err_rc;
    }

    free(data->buf.buff);
    free(data);

err_rc:
    free(send_data_client.buf.buff);
    free(send_data_server.buf.buff);
    assert(!pho_comm_close(&ci_client));
    assert(!pho_comm_close(&ci_server));
    return rc;

err_fail:
    free(send_data_client.buf.buff);
    free(send_data_server.buf.buff);
    assert(!pho_comm_close(&ci_client));
    assert(!pho_comm_close(&ci_server));
    return PHO_TEST_FAILURE;
}

/* stress test to see if the LRS can handle
 * multiple messages coming from multiple clients
 */
static int test_sendrecv_multiple(void *arg)
{
    struct pho_comm_addr_type *addr_type = (struct pho_comm_addr_type *)arg;
    const int NCLIENT = 10, NMSG = 20, TOTAL = NCLIENT * NMSG;
    struct pho_comm_data send_data_client[TOTAL];
    struct pho_comm_info ci_client[NCLIENT];
    struct pho_comm_data send_data_server;
    struct pho_comm_info ci_server;
    struct pho_comm_data *data;
    int i, nb_data, rc, cnt = TOTAL;

    assert(!pho_comm_open(&ci_server, &addr_type->addr,
                          addr_type->server_type));
    for (i = 0; i < NCLIENT; ++i)
        assert(!pho_comm_open(ci_client + i, &addr_type->addr,
                              addr_type->client_type));
    assert(!pho_comm_recv(&ci_server, &data, &nb_data));

    // sending from clients
    for (i = 0; i < TOTAL; ++i) {
        send_data_client[i] = pho_comm_data_init(ci_client + i % NCLIENT);
        send_data_client[i].buf.buff = xmalloc(sizeof(i));
        memcpy(send_data_client[i].buf.buff, &i, 4);
        send_data_client[i].buf.size = sizeof(i);
        assert(!pho_comm_send(send_data_client + i));
    }

    // server side
    assert(!pho_comm_recv(&ci_server, &data, &nb_data));
    send_data_server.buf.buff = xmalloc(sizeof(i));
    send_data_server.buf.size = sizeof(i);
    while (cnt) {
        for (i = 0; i < nb_data; ++i) {
            int tmp;

            memcpy(&tmp, data[i].buf.buff, 4);
            free(data[i].buf.buff);

            send_data_server.fd = data[i].fd;
            tmp *= 2;
            memcpy(send_data_server.buf.buff, &tmp, 4);
            assert(!pho_comm_send(&send_data_server));
        }
        free(data);

        cnt -= nb_data;
        assert(!pho_comm_recv(&ci_server, &data, &nb_data));
    }

    // receiving by clients
    for (i = 0; i < NCLIENT * NMSG; ++i) {
        int tmp;

        assert(!pho_comm_recv(ci_client + i % NCLIENT, &data, &nb_data));
        memcpy(&tmp, data->buf.buff, 4);
        free(data->buf.buff);
        free(data);
        if (tmp != 2 * i) {
            LOG_GOTO(err_rc, rc = -EBADMSG, "received message is invalid: "
                     "received %d but expected %d (2*%d)\n", tmp, 2 * i, i);
        }

        rc = PHO_TEST_SUCCESS;
    }

err_rc:
    for (i = 0; i < NCLIENT * NMSG; ++i)
        free(send_data_client[i].buf.buff);
    free(send_data_server.buf.buff);
    for (i = 0; i < NCLIENT; ++i)
        pho_comm_close(ci_client + i);
    pho_comm_close(&ci_server);
    return rc;
}

static int test_bad_hostname_port(void *arg)
{
    struct pho_comm_info ci_client;
    struct pho_comm_info ci_server;
    union pho_comm_addr addr;
    int rc;

    /* bad hostname */
    addr.tcp.hostname = "bad_hostname_unknown";
    addr.tcp.port = TCP_PORT_TEST;

    rc = pho_comm_open(&ci_server, &addr, PHO_COMM_TCP_SERVER);
    if (rc != -EINVAL) {
        pho_comm_close(&ci_server);
        pho_error(rc,
                  "Server socket opening with bad hostname %s must failed with "
                  "%d, we get %d\n",
                  addr.tcp.hostname, -EINVAL, rc);
        return rc;
    }

    rc = pho_comm_open(&ci_client, &addr, PHO_COMM_TCP_CLIENT);
    if (rc != -EINVAL) {
        pho_comm_close(&ci_client);
        pho_error(rc,
                  "Server socket opening with bad hostname %s must failed with "
                  "%d, we get %d\n",
                  addr.tcp.hostname, -EINVAL, rc);
        return rc;
    }

    /* bad port */
    addr.tcp.hostname = "localhost";
    addr.tcp.port = 655359;

    rc = pho_comm_open(&ci_server, &addr, PHO_COMM_TCP_SERVER);
    if (rc != -EINVAL) {
        pho_comm_close(&ci_server);
        pho_error(rc,
                  "Server socket opening with bad port %d must failed with "
                  "%d, we get %d\n",
                  addr.tcp.port, -EINVAL, rc);
        return rc;
    }

    rc = pho_comm_open(&ci_client, &addr, PHO_COMM_TCP_CLIENT);
    if (rc != -EINVAL) {
        pho_comm_close(&ci_client);
        pho_error(rc,
                  "Server socket opening with bad port %d must failed with "
                  "%d, we get %d\n",
                  addr.tcp.port, -EINVAL, rc);
        return rc;
    }

    rc = pho_comm_close(&ci_server);
    rc = pho_comm_close(&ci_client);
    return PHO_TEST_SUCCESS;
}

int main(int argc, char **argv)
{
    struct pho_comm_addr_type addr_type;
    test_env_initialize();

    addr_type.addr.af_unix.path = "/tmp/test_socklrs";
    addr_type.server_type = PHO_COMM_UNIX_SERVER;
    addr_type.client_type = PHO_COMM_UNIX_CLIENT;
    pho_run_test("Test: good AF_UNIX socket opening", test_open, &addr_type,
                 PHO_TEST_SUCCESS);
    addr_type.addr.tcp.hostname = "localhost";
    addr_type.addr.tcp.port = TCP_PORT_TEST;
    addr_type.server_type = PHO_COMM_TCP_SERVER;
    addr_type.client_type = PHO_COMM_TCP_CLIENT;
    pho_run_test("Test: good AF_INET socket opening", test_open, &addr_type,
                 PHO_TEST_SUCCESS);
    pho_run_test("Test: socket opening (socket already exists)", test_open_ex,
                 "/tmp/test_socklrs", PHO_TEST_SUCCESS);
    addr_type.addr.af_unix.path = NULL;
    addr_type.server_type = PHO_COMM_UNIX_SERVER;
    addr_type.client_type = PHO_COMM_UNIX_CLIENT;
    pho_run_test("Test: offline socket AF_UNIX addr NULL",
                 test_open_offline_addr_null, &addr_type, PHO_TEST_SUCCESS);
    pho_run_test("Test: offline socket path NULL", test_open_offline,
                 &addr_type, PHO_TEST_SUCCESS);
    addr_type.addr.tcp.hostname = NULL;
    addr_type.addr.tcp.port = TCP_PORT_TEST;
    addr_type.server_type = PHO_COMM_TCP_SERVER;
    addr_type.client_type = PHO_COMM_TCP_CLIENT;
    pho_run_test("Test: offline socket AF_INET addr NULL",
                 test_open_offline_addr_null, &addr_type, PHO_TEST_SUCCESS);
    pho_run_test("Test: offline socket hostname NULL", test_open_offline,
                 &addr_type, PHO_TEST_SUCCESS);

    addr_type.addr.af_unix.path = "/tmp/test_socklrs";
    addr_type.server_type = PHO_COMM_UNIX_SERVER;
    addr_type.client_type = PHO_COMM_UNIX_CLIENT;
    pho_run_test("Test: simple sending/receiving AF_UNIX", test_sendrecv_simple,
                 &addr_type, PHO_TEST_SUCCESS);
    pho_run_test("Test: multiple sending/receiving AF_UNIX",
                 test_sendrecv_multiple, &addr_type, PHO_TEST_SUCCESS);
    addr_type.addr.tcp.hostname = "localhost";
    addr_type.addr.tcp.port = TCP_PORT_TEST;
    addr_type.server_type = PHO_COMM_TCP_SERVER;
    addr_type.client_type = PHO_COMM_TCP_CLIENT;
    pho_run_test("Test: simple sending/receiving AF_INET", test_sendrecv_simple,
                 &addr_type, PHO_TEST_SUCCESS);
    pho_run_test("Test: multiple sending/receiving AF_INET",
                 test_sendrecv_multiple, &addr_type, PHO_TEST_SUCCESS);
    pho_run_test("Test: AF_INET bad hostname or port", test_bad_hostname_port,
                 NULL, PHO_TEST_SUCCESS);

    exit(EXIT_SUCCESS);
}
