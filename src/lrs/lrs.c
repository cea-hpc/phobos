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
 * \brief  Phobosd main interface -- Local Resource Scheduler
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "pho_cfg.h"
#include "pho_comm.h"
#include "pho_common.h"

#include "lrs_cfg.h"
#include "lrs_sched.h"

/**
 * Local Resource Scheduler instance, composed of two parts:
 * - Scheduler: manages media and local devices for the actual IO
 *   to be performed
 * - Communication info: stores info related to the communication with Store
 */
struct lrs {
    struct lrs_sched     *sched[PHO_RSC_LAST]; /*!< Scheduler handles */
    struct pho_comm_info  comm;                /*!< Communication handle */
};

struct lrs_params {
    int log_level;                      /*!< Logging level. */
    bool is_daemon;                     /*!< True if executed as a daemon. */
    bool use_syslog;                    /*!< True if syslog is requested. */
    char *cfg_path;                     /*!< Configuration file path. */
};
#define LRS_PARAMS_DEFAULT {PHO_LOG_INFO, true, false, NULL}

#define pholog2syslog(lvl) (lvl?2+lvl:lvl)

static void phobos_log_callback_def_with_sys(const struct pho_logrec *rec)
{
    struct tm time;

    localtime_r(&rec->plr_time.tv_sec, &time);

    if (rec->plr_err != 0)
        syslog(pholog2syslog(rec->plr_level),
               "<%s> [%u/%s:%s:%d] %s: %s (%d)",
               pho_log_level2str(rec->plr_level),
               rec->plr_pid, rec->plr_func, rec->plr_file, rec->plr_line,
               rstrip(rec->plr_msg), strerror(rec->plr_err), rec->plr_err);
    else
        syslog(pholog2syslog(rec->plr_level),
               "<%s> [%u/%s:%s:%d] %s",
               pho_log_level2str(rec->plr_level),
               rec->plr_pid, rec->plr_func, rec->plr_file, rec->plr_line,
               rstrip(rec->plr_msg));

}

/* ****************************************************************************/
/* Daemon context *************************************************************/
/* ****************************************************************************/

bool running = true;

/**
 * Create a lock file.
 *
 * If other instances with the same configuration parameter try to create it,
 * the call will failed.
 *
 * This file must be deleted using _delete_lock_file().
 *
 * \param[in]   lock_file   Lock file path.
 * \return                  0 on success, -1 * posix error code on failure.
 */
static int _create_lock_file(const char *lock_file)
{
    int fd;

    fd = open(lock_file, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0)
        return -errno;

    close(fd);

    return 0;
}

/**
 * Delete the lock file created with _create_lock_file().
 *
 * \param[in]   lock_file   Lock file path.
 * \return                  0 on success, -1 * posix error code on failure.
 */
static int _delete_lock_file(const char *lock_file)
{
    int rc;

    rc = unlink(lock_file);
    if (rc)
        pho_error(rc = -errno, "Could not unlink lock file '%s'", lock_file);

    return rc;
}


/* ****************************************************************************/
/* LRS helpers ****************************************************************/
/* ****************************************************************************/

static enum rsc_family _determine_family(const pho_req_t *req)
{
    if (pho_request_is_write(req))
        return req->walloc->family;

    if (pho_request_is_read(req)) {
        if (!req->ralloc->n_med_ids)
            return PHO_RSC_INVAL;
        return req->ralloc->med_ids[0]->family;
    }

    if (pho_request_is_release(req)) {
        if (!req->release->n_media)
            return PHO_RSC_INVAL;
        return req->release->media[0]->med_id->family;
    }

    if (pho_request_is_format(req))
        return req->format->med_id->family;

    if (pho_request_is_notify(req))
        return req->notify->rsrc_id->family;

    return PHO_RSC_INVAL;
}

static int _prepare_error(struct resp_container *resp_cont, int req_rc,
                          const struct req_container *req_cont)
{
    int rc;

    resp_cont->token = req_cont->token;
    rc = pho_srl_response_error_alloc(resp_cont->resp);
    if (rc)
        LOG_RETURN(rc, "Failed to allocate response");

    resp_cont->resp->error->rc = req_rc;
    if (!req_cont->req) /* If the error happened during request unpacking */
        return 0;

    resp_cont->resp->req_id = req_cont->req->id;
    if (pho_request_is_write(req_cont->req))
        resp_cont->resp->error->req_kind = PHO_REQUEST_KIND__RQ_WRITE;
    else if (pho_request_is_read(req_cont->req))
        resp_cont->resp->error->req_kind = PHO_REQUEST_KIND__RQ_READ;
    else if (pho_request_is_release(req_cont->req))
        resp_cont->resp->error->req_kind = PHO_REQUEST_KIND__RQ_RELEASE;
    else if (pho_request_is_format(req_cont->req))
        resp_cont->resp->error->req_kind = PHO_REQUEST_KIND__RQ_FORMAT;
    else if (pho_request_is_notify(req_cont->req))
        resp_cont->resp->error->req_kind = PHO_REQUEST_KIND__RQ_NOTIFY;
    return 0;
}

static int _send_responses(struct lrs *lrs, const int n_resp,
                           struct resp_container *resp_cont)
{
    int rc = 0;
    int i;

    for (i = 0; i < n_resp; ++i) {
        int rc2;
        struct pho_comm_data msg;

        msg = pho_comm_data_init(&lrs->comm);
        msg.fd = resp_cont[i].token;
        rc2 = pho_srl_response_pack(resp_cont[i].resp, &msg.buf);
        pho_srl_response_free(resp_cont[i].resp, false);
        if (rc2) {
            pho_error(rc2, "Response cannot be packed");
            rc = rc ? : rc2;
            continue;
        }

        rc2 = pho_comm_send(&msg);
        free(msg.buf.buff);
        if (rc2 == -EPIPE) {
            pho_debug("Client closed socket");
            continue;
        } else if (rc2) {
            pho_error(rc2, "Response cannot be sent");
            rc = rc ? : rc2;
            continue;
        }
    }

    return rc;
}

static int _send_error(struct lrs *lrs, struct req_container *req_cont)
{
    struct resp_container resp_cont;
    int rc;

    resp_cont.resp = malloc(sizeof(*resp_cont.resp));
    if (!resp_cont.resp)
        LOG_RETURN(-ENOMEM, "Cannot allocate error response");

    rc = _prepare_error(&resp_cont, -EINVAL, req_cont);
    if (rc)
        LOG_GOTO(err_resp, rc, "Cannot prepare error response");

    rc = _send_responses(lrs, 1, &resp_cont);
    if (rc)
        LOG_GOTO(err_resp, rc, "Error during response sending");

err_resp:
    free(resp_cont.resp);

    return rc;
}

static int _prepare_requests(struct lrs *lrs, const int n_data,
                             struct pho_comm_data *data)
{
    enum rsc_family fam;
    int rc = 0;
    int i;

    for (i = 0; i < n_data; ++i) {
        struct req_container *req_cont;

        if (data[i].buf.size == -1) /* close notification, ignore */
            continue;

        req_cont = malloc(sizeof(*req_cont));
        if (!req_cont)
            LOG_RETURN(-ENOMEM, "Cannot allocate request structure");

        /* request processing */
        req_cont->token = data[i].fd;
        req_cont->req = pho_srl_request_unpack(&data[i].buf);
        if (!req_cont->req)
            LOG_GOTO(send_err, rc = -EINVAL, "Request cannot be unpacked");

        fam = _determine_family(req_cont->req);
        if (fam == PHO_RSC_INVAL)
            LOG_GOTO(send_err, rc = -EINVAL,
                     "Requested family is not recognized");

        if (!lrs->sched[fam])
            LOG_GOTO(send_err, rc = -EINVAL,
                     "Requested family is not handled by the daemon");

        rc = sched_request_enqueue(lrs->sched[fam], req_cont);
        if (rc)
            LOG_GOTO(send_err, rc, "Request cannot be enqueue");

        continue;

send_err:
        rc = _send_error(lrs, req_cont);
        if (req_cont->req)
            pho_srl_request_free(req_cont->req, true);
        free(req_cont);
        if (rc)
            LOG_RETURN(rc, "Cannot send error response");
    }

    return rc;
}

static int _load_schedulers(struct lrs *lrs)
{
    const char *list;
    char *parse_list;
    char *saveptr;
    char *item;
    int rc = 0;
    int i;

    list = PHO_CFG_GET(cfg_lrs, PHO_CFG_LRS, families);

    for (i = 0; i < PHO_RSC_LAST; ++i)
        lrs->sched[i] = NULL;

    parse_list = strdup(list);
    if (!parse_list)
        LOG_RETURN(-errno, "Error on family list duplication");

    /* Initialize a scheduler for each requested family */
    for (item = strtok_r(parse_list, ",", &saveptr);
         item != NULL;
         item = strtok_r(NULL, ",", &saveptr)) {
        int family = str2rsc_family(item);

        switch (family) {
        case PHO_RSC_DISK:
            LOG_GOTO(out_free, rc = -ENOTSUP,
                     "The family '%s' is not supported yet", item);

        case PHO_RSC_TAPE:
        case PHO_RSC_DIR:
            if (lrs->sched[family]) {
                pho_warn("The family '%s' was already processed, ignore it",
                         item);
                continue;
            }

            lrs->sched[family] = malloc(sizeof(*lrs->sched[family]));
            if (!lrs->sched[family])
                LOG_GOTO(out_free, rc = -ENOMEM,
                         "Error on lrs scheduler allocation");
            rc = sched_init(lrs->sched[family], family);
            if (rc) {
                free(lrs->sched[family]);
                lrs->sched[family] = NULL;
                LOG_GOTO(out_free, rc, "Error on lrs scheduler initialization");
            }

            break;
        default:
            LOG_GOTO(out_free, rc = -EINVAL,
                     "The family '%s' is not recognized", item);
        }
    }

out_free:
    /* in case of error, allocated schedulers will be terminated in the error
     * handling of lrs_init()
     */

    free(parse_list);
    return rc;
}

/* ****************************************************************************/
/* LRS main functions *********************************************************/
/* ****************************************************************************/

/**
 * Free all resources associated with this LRS except for the dss, which must be
 * deinitialized by the caller if necessary.
 *
 * The LRS data structure is allocated in lrs_init()
 * and deallocated in lrs_fini().
 *
 * \param[in/out]   lrs The LRS to be deinitialized.
 */
static void lrs_fini(struct lrs *lrs)
{
    const char *lock_file;
    int rc = 0;
    int i;

    if (lrs == NULL)
        return;

    for (i = 0; i < PHO_RSC_LAST; ++i) {
        sched_fini(lrs->sched[i]);
        free(lrs->sched[i]);
    }

    rc = pho_comm_close(&lrs->comm);
    if (rc)
        pho_error(rc, "Error on closing the socket");

    lock_file = PHO_CFG_GET(cfg_lrs, PHO_CFG_LRS, lock_file);
    _delete_lock_file(lock_file);
}

/**
 * Initialize a new LRS.
 *
 * The LRS data structure is allocated in lrs_init()
 * and deallocated in lrs_fini().
 *
 * \param[in]   lrs         The LRS to be initialized.
 * \param[in]   parm        The LRS parameters.
 *
 * \return                  0 on success, -1 * posix error code on failure.
 */
static int lrs_init(struct lrs *lrs, struct lrs_params parm)
{
    const char *lock_file;
    const char *sock_path;
    int rc;

    /* Load configuration */
    rc = pho_cfg_init_local(parm.cfg_path);
    if (rc && rc != -EALREADY)
        return rc;

    pho_log_level_set(parm.log_level);
    if (parm.use_syslog)
        pho_log_callback_set(phobos_log_callback_def_with_sys);

    lock_file = PHO_CFG_GET(cfg_lrs, PHO_CFG_LRS, lock_file);
    rc = _create_lock_file(lock_file);
    if (rc)
        LOG_GOTO(err, rc, "Error while creating the daemon lock file");

    rc = _load_schedulers(lrs);
    if (rc)
        LOG_GOTO(err, rc, "Error while loading the schedulers");

    sock_path = PHO_CFG_GET(cfg_lrs, PHO_CFG_LRS, server_socket);
    rc = pho_comm_open(&lrs->comm, sock_path, true);
    if (rc)
        LOG_GOTO(err, rc, "Error while opening the socket");

    return rc;

err:
    lrs_fini(lrs);
    return rc;
}

/**
 * Process pending requests from the unix socket and send the associated
 * responses to clients.
 *
 * Requests are guaranteed to be answered at some point.
 *
 * TODO: we need to think about a way to avoid the EPIPE error in the future,
 * due to a client departure before the release ack is sent.
 * I got three ideas (the latter, the better):
 * - consider that this EPIPE error is not critical and can happen if
 *   the client does not care about the release acknowledgement;
 * - consider a boolean 'send_resp' in the release message protocol to
 *   indicate if the client need a response, and then send it if needed;
 * - force the client to always receive the ack, but putting a boolean
 *   'with_flush' in the release message protocol to let the client
 *   be responded before or after a flush operation. If not, the client
 *   only says to the LRS that its operation is done and that it does
 *   not need the device anymore. The LRS sends its response once the
 *   release request is received.
 *
 * \param[in]       lrs         The LRS that will handle the requests.
 *
 * \return                      0 on succes, -errno on failure.
 */
static int lrs_process(struct lrs *lrs)
{
    struct pho_comm_data *data = NULL;
    struct resp_container *resp_cont;
    int n_data, n_resp = 0;
    int rc = 0;
    int i, j;

    /* request reception and accept handling */
    rc = pho_comm_recv(&lrs->comm, &data, &n_data);
    if (rc) {
        for (i = 0; i < n_data; ++i)
            free(data[i].buf.buff);
        free(data);
        LOG_RETURN(rc, "Error during request reception");
    }

    rc = _prepare_requests(lrs, n_data, data);
    free(data);
    if (rc)
        LOG_RETURN(rc, "Error during request enqueuing");

    /* response processing */
    for (i = 0; i < PHO_RSC_LAST; ++i) {
        if (!lrs->sched[i])
            continue;

        rc = sched_responses_get(lrs->sched[i], &n_resp, &resp_cont);
        if (rc)
            LOG_RETURN(rc, "Error during sched processing");

        rc = _send_responses(lrs, n_resp, resp_cont);
        for (j = 0; j < n_resp; ++j)
            free(resp_cont[j].resp);
        free(resp_cont);
        if (rc)
            LOG_RETURN(rc, "Error during responses sending");
    }

    return rc;
}

/* ****************************************************************************/
/* Daemonization helpers ******************************************************/
/* ****************************************************************************/

static int init_daemon(pid_t pid, int pipe_in)
{
    char *pid_filepath;
    ssize_t read_rc;
    char buf[16];
    int fd;
    int rc;

    pid_filepath = getenv("PHOBOSD_PID_FILEPATH");
    if (pid_filepath) {
        fd = open(pid_filepath, O_WRONLY | O_CREAT, 0666);
        if (fd == -1) {
            kill(pid, SIGKILL);
            fprintf(stderr, "ERR: cannot open the pid file at '%s'",
                    pid_filepath);
            rc = EXIT_FAILURE;
            goto out;
        }

        sprintf(buf, "%d", pid);
        rc = write(fd, buf, strlen(buf));
        close(fd);
        if (rc == -1) {
            kill(pid, SIGKILL);
            fprintf(stderr, "ERR: cannot write the pid file at '%s'",
                    pid_filepath);
            rc = EXIT_FAILURE;
            goto out;
        }
    }

    do {
        read_rc = read(pipe_in, &rc, sizeof(rc));
    } while (read_rc == -1 && errno == EAGAIN);
    if (read_rc != sizeof(rc))
        rc = EXIT_FAILURE;

out:
    close(pipe_in);

    return rc;
}

/* SIGTERM handler -- needs to release the LRS context */
static void sa_sigterm(int signum)
{
    running = false;
}

/* Argument parsing */
static void print_usage(void)
{
    printf("usage: phobosd [--interactive] [--config cfg_file] "
               "[--verbose/--quiet] [--syslog]\n"
           "\nOptional arguments:\n"
           "    -i,--interactive        execute the daemon in foreground\n"
           "    -c,--config cfg_file    "
                "use cfg_file as the daemon configuration file\n"
           "    -v,--verbose            increase verbose level\n"
           "    -q,--quiet              decrease verbose level\n"
           "    -s,--syslog             print the daemon logs to syslog\n");
}

static struct lrs_params parse_args(int argc, char **argv)
{
    static struct option long_options[] = {
        {"help",        no_argument,       0,  'h'},
        {"interactive", no_argument,       0,  'i'},
        {"config",      required_argument, 0,  'c'},
        {"verbose",     no_argument,       0,  'v'},
        {"quiet",       no_argument,       0,  'q'},
        {"syslog",      no_argument,       0,  's'},
        {0,             0,                 0,  0}
    };
    struct lrs_params parm = LRS_PARAMS_DEFAULT;

    while (1) {
        int c;

        c = getopt_long(argc, argv, "hic:vqs", long_options, NULL);
        if (c == -1)
            break;

        switch (c) {
        case 'h':
            print_usage();
            exit(EXIT_SUCCESS);
        case 'i':
            parm.is_daemon = false;
            break;
        case 'c':
            parm.cfg_path = optarg;
            break;
        case 'v':
            ++parm.log_level;
            break;
        case 'q':
            --parm.log_level;
            break;
        case 's':
            parm.use_syslog = true;
            break;
        default:
            print_usage();
            exit(EXIT_FAILURE);
        }
    }

    return parm;
}

int main(int argc, char **argv)
{
    struct lrs_params parm;
    struct sigaction sa;
    int init_pipe[2];
    struct lrs lrs = {};
    pid_t pid;
    int rc;

    parm = parse_args(argc, argv);

    /* forking type daemon initialization */
    if (parm.is_daemon) {
        rc = pipe(init_pipe);
        if (rc) {
            fprintf(stderr, "ERR: cannot init the communication pipe");
            return EXIT_FAILURE;
        }

        pid = fork();
        if (pid < 0) {
            fprintf(stderr, "ERR: cannot create child process");
            return EXIT_FAILURE;
        }
        if (pid) {
            close(init_pipe[1]);
            exit(init_daemon(pid, init_pipe[0]));
        }
        close(init_pipe[0]);
    }

    /* signal handler */
    sa.sa_handler = sa_sigterm;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    umask(0000);

    /* lrs processing */
    rc = lrs_init(&lrs, parm);

    if (parm.is_daemon) {
        if (write(init_pipe[1], &rc, sizeof(rc)) != sizeof(rc))
            rc = -1;
        close(init_pipe[1]);
    }

    if (rc)
        return rc;

    while (running) {
        rc = lrs_process(&lrs);
        if (rc)
            break;
    }

    lrs_fini(&lrs);

    return EXIT_SUCCESS;
}
