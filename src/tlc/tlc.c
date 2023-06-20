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
 * \brief  TLC main interface -- Tape Library Controller
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "pho_cfg.h"
#include "pho_comm.h"
#include "pho_common.h"
#include "pho_daemon.h"

#include "tlc_cfg.h"

static bool should_tlc_stop(void)
{
    return !running;
}

struct tlc {
    struct pho_comm_info comm; /*!< Communication handle */
};

static int tlc_init(struct tlc *tlc)
{
    union pho_comm_addr sock_addr;
    int rc;

    sock_addr.tcp.hostname = PHO_CFG_GET(cfg_tlc, PHO_CFG_TLC, hostname);
    sock_addr.tcp.port = PHO_CFG_GET_INT(cfg_tlc, PHO_CFG_TLC, port, -1);
    if (sock_addr.tcp.port == -1)
        LOG_RETURN(-EINVAL, "Unable to get a valid integer TLC port value");

    if (sock_addr.tcp.port > 65535)
        LOG_RETURN(-EINVAL, "TLC port value %d cannot be greater than 65535",
                   sock_addr.tcp.port);

    rc = pho_comm_open(&tlc->comm, &sock_addr, PHO_COMM_TCP_SERVER);
    if (rc)
        LOG_RETURN(rc, "Error while opening the TLC socket");

    return rc;
}

static void tlc_fini(struct tlc *tlc)
{
    int rc;

    ENTRY;

    if (tlc == NULL)
        return;

    rc = pho_comm_close(&tlc->comm);
    if (rc)
        pho_error(rc, "Error on closing the TLC socket");
}

int main(int argc, char **argv)
{
    int write_pipe_from_child_to_father;
    struct daemon_params param;
    struct tlc tlc = {};
    int rc;

    rc = daemon_creation(argc, argv, &param, &write_pipe_from_child_to_father,
                         "tlc");
    if (rc)
        return -rc;

    rc = daemon_init(param);

    if (!rc)
        rc = tlc_init(&tlc);

    if (param.is_daemon)
        daemon_notify_init_done(write_pipe_from_child_to_father, &rc);

    if (rc)
        return -rc;

    while (true) {
        if (should_tlc_stop())
            break;

        usleep(10000);
    }

    tlc_fini(&tlc);
    return EXIT_SUCCESS;
}
