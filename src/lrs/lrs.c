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
 * \brief  Phobosd main interface -- Local Resource Scheduler
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
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
#include "pho_type_utils.h"

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
    struct tsqueue        response_queue;      /*!< Response queue */
    bool                  stopped;             /*!< true when every I/O has been
                                                * completed after the LRS
                                                * stopped.
                                                */
    struct dss_handle     dss;                 /*!< DSS handle of the
                                                * communication thread
                                                */
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
    char *path_copy;
    struct stat sb;
    char *folder;
    int rc;
    int fd;

    path_copy = strdup(lock_file);
    if (path_copy == NULL)
        LOG_RETURN(-errno, "Unable to copy '%s'", lock_file);

    folder = dirname(path_copy);

    rc = stat(folder, &sb);
    if (rc)
        LOG_GOTO(free_copy, rc = -errno,
                 "Unable to stat '%s' path, cannot create lock file '%s'",
                 folder, lock_file);

    if (!S_ISDIR(sb.st_mode))
        LOG_GOTO(free_copy, rc = -EPERM,
                 "Unable to create lock file '%s', '%s' is not a dir",
                 lock_file, folder);

    fd = open(lock_file, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0)
        LOG_GOTO(free_copy, rc = -errno,
                 "Unable to create lock file '%s'", lock_file);

    close(fd);

free_copy:
    free(path_copy);

    return rc;
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
        return (enum rsc_family)req->walloc->family;

    if (pho_request_is_read(req)) {
        if (!req->ralloc->n_med_ids)
            return PHO_RSC_INVAL;
        return (enum rsc_family)req->ralloc->med_ids[0]->family;
    }

    if (pho_request_is_release(req)) {
        if (!req->release->n_media)
            return PHO_RSC_INVAL;
        return (enum rsc_family)req->release->media[0]->med_id->family;
    }

    if (pho_request_is_format(req))
        return (enum rsc_family)req->format->med_id->family;

    if (pho_request_is_notify(req))
        return (enum rsc_family)req->notify->rsrc_id->family;

    return PHO_RSC_INVAL;
}

static void n_media_per_release(struct req_container *req_cont)
{
    const pho_req_release_t *rel = req_cont->req->release;
    size_t i;

    req_cont->params.release.n_tosync_media = 0;
    for (i = 0; i < rel->n_media; i++)
        if (rel->media[i]->to_sync)
            req_cont->params.release.n_tosync_media++;

    req_cont->params.release.n_nosync_media =
        rel->n_media - req_cont->params.release.n_tosync_media;
}

static int init_release_container(struct req_container *req_cont)
{
    struct tosync_medium *tosync_media = NULL;
    struct nosync_medium *nosync_media = NULL;
    size_t tosync_media_index = 0;
    size_t nosync_media_index = 0;
    pho_req_release_elt_t *media;
    size_t i;
    int rc;

    n_media_per_release(req_cont);

    if (req_cont->params.release.n_tosync_media) {
        tosync_media = calloc(req_cont->params.release.n_tosync_media,
                              sizeof(*tosync_media));
        if (tosync_media == NULL)
            GOTO(clean_on_error, rc = -errno);
    }

    if (req_cont->params.release.n_nosync_media) {
        nosync_media = calloc(req_cont->params.release.n_nosync_media,
                              sizeof(*nosync_media));
        if (nosync_media == NULL)
            GOTO(clean_on_error, rc = -errno);
    }

    for (i = 0; i < req_cont->req->release->n_media; ++i) {
        media = req_cont->req->release->media[i];
        if (media->to_sync) {
            tosync_media[tosync_media_index].status = SUB_REQUEST_TODO;
            tosync_media[tosync_media_index].medium.family =
                (enum rsc_family)media->med_id->family;
            tosync_media[tosync_media_index].written_size = media->size_written;
            rc = pho_id_name_set(&tosync_media[tosync_media_index].medium,
                                 media->med_id->name);
            if (rc)
                GOTO(clean_on_error, rc);

            tosync_media_index++;
        } else {
            nosync_media[nosync_media_index].medium.family =
                (enum rsc_family)media->med_id->family;
            nosync_media[tosync_media_index].written_size = media->size_written;
            rc = pho_id_name_set(&nosync_media[nosync_media_index].medium,
                                 media->med_id->name);
            if (rc)
                GOTO(clean_on_error, rc);

            nosync_media_index++;
        }
    }

    req_cont->params.release.tosync_media = tosync_media;
    req_cont->params.release.nosync_media = nosync_media;
    return 0;

clean_on_error:
    free(tosync_media);
    free(nosync_media);
    return rc;
}

static void notify_device_request_is_canceled(struct resp_container *respc)
{
    int i;

    for (i = 0; i < respc->devices_len; i++) {
        MUTEX_LOCK(&respc->devices[i]->ld_mutex);
        respc->devices[i]->ld_ongoing_io = false;
        MUTEX_UNLOCK(&respc->devices[i]->ld_mutex);
    }
}

static int request_kind_from_response(pho_resp_t *resp)
{
    if (pho_response_is_write(resp))
        return PHO_REQUEST_KIND__RQ_WRITE;
    else if (pho_response_is_read(resp))
        return PHO_REQUEST_KIND__RQ_READ;
    else if (pho_response_is_release(resp))
        return PHO_REQUEST_KIND__RQ_RELEASE;
    else if (pho_response_is_format(resp))
        return PHO_REQUEST_KIND__RQ_FORMAT;
    else if (pho_response_is_notify(resp))
        return PHO_REQUEST_KIND__RQ_NOTIFY;
    else if (pho_response_is_error(resp))
        return resp->error->req_kind;
    else
        return -1;
}

static int convert_response_to_error(struct resp_container *respc)
{
    int rc;

    pho_srl_response_free(respc->resp, false);
    rc = pho_srl_response_error_alloc(respc->resp);
    if (rc)
        return rc;

    respc->resp->error->rc = -ESHUTDOWN;
    respc->resp->error->req_kind = request_kind_from_response(respc->resp);

    return 0;
}

static int cancel_response(struct resp_container *respc)
{
    pho_resp_t *resp = respc->resp;

    if (!pho_response_is_read(resp) && !pho_response_is_write(resp))
        return 0;

    notify_device_request_is_canceled(respc);
    return convert_response_to_error(respc);
}

static inline bool client_disconnected_error(int rc)
{
    return rc == -EPIPE || rc == -ECONNRESET;
}

static int _send_message(struct pho_comm_info *comm,
                         struct resp_container *respc)
{
    struct pho_comm_data msg;
    int rc = 0;

    msg = pho_comm_data_init(comm);
    msg.fd = respc->socket_id;
    if (!running) {
        rc = cancel_response(respc);
        if (rc)
            return rc;
    }

    rc = pho_srl_response_pack(respc->resp, &msg.buf);
    if (rc) {
        pho_error(rc, "Response cannot be packed");
        return rc;
    }

    /* XXX: \p running could change just before the call to send.
     * Which means that new I/O responses would be sent with running = false
     */
    rc = pho_comm_send(&msg);
    free(msg.buf.buff);
    if (client_disconnected_error(rc)) {
        pho_error(rc, "Failed to send %s response to client %d, not fatal",
                  pho_srl_response_kind_str(respc->resp),
                  respc->socket_id);
        if (pho_response_is_read(respc->resp) ||
            pho_response_is_write(respc->resp))
            /* do not block device's ongoing_io status if the client disconnects
             */
            notify_device_request_is_canceled(respc);

        /* error not fatal for the LRS */
        rc = 0;
    } else if (rc) {
        pho_error(rc, "Response cannot be sent");
    }

    return rc;
}

static int send_responses_from_queue(struct lrs *lrs)
{
    struct resp_container *respc;
    int rc = 0;
    int rc2;

    while ((respc = tsqueue_pop(&lrs->response_queue)) != NULL) {
        rc2 = _send_message(&lrs->comm, respc);
        rc = rc ? : rc2;
        sched_resp_free_with_cont(respc);
    }

    return rc;
}

static int _send_error(struct lrs *lrs, int req_rc,
                       const struct req_container *req_cont)
{
    struct resp_container resp_cont;
    int rc;

    resp_cont.resp = malloc(sizeof(*resp_cont.resp));
    if (!resp_cont.resp)
        LOG_RETURN(-ENOMEM, "Cannot allocate error response");

    rc = prepare_error(&resp_cont, req_rc, req_cont);
    if (rc)
        LOG_GOTO(err_resp, rc, "Cannot prepare error response");

    rc = _send_message(&lrs->comm, &resp_cont);
    pho_srl_response_free(resp_cont.resp, false);
    if (rc)
        LOG_GOTO(err_resp, rc, "Error during response sending");

err_resp:
    free(resp_cont.resp);

    return rc;
}

static int _process_ping_request(struct lrs *lrs,
                                 const struct req_container *req_cont)
{
    struct resp_container resp_cont;
    int rc;

    resp_cont.resp = malloc(sizeof(*resp_cont.resp));
    if (!resp_cont.resp)
        LOG_RETURN(-ENOMEM, "Cannot allocate ping response");

    resp_cont.socket_id = req_cont->socket_id;
    pho_srl_response_ping_alloc(resp_cont.resp);
    resp_cont.resp->req_id = req_cont->req->id;
    rc = _send_message(&lrs->comm, &resp_cont);
    pho_srl_response_free(resp_cont.resp, false);
    free(resp_cont.resp);
    if (rc)
        pho_error(rc, "Error during ping response sending");

    return rc;
}

static int _process_monitor_request(struct lrs *lrs,
                                    const struct req_container *req_cont)
{
    struct resp_container resp_cont;
    enum rsc_family family;
    json_t *status;
    int rc;

    family = (enum rsc_family) req_cont->req->monitor->family;

    if (family < 0 || family >= PHO_RSC_LAST)
        LOG_GOTO(send_error, rc = -EINVAL, "Invalid family argument");

    if (!lrs->sched[family])
        LOG_GOTO(send_error, rc = -EINVAL,
                 "Requested family is not handled by the daemon");

    resp_cont.resp = malloc(sizeof(*resp_cont.resp));
    if (!resp_cont.resp)
        LOG_GOTO(send_error, rc = -ENOMEM,
                 "Failed to allocate monitor response");

    status = json_array();
    if (!status)
        LOG_GOTO(free_resp, rc = -ENOMEM, "Failed to allocate json array");

    resp_cont.socket_id = req_cont->socket_id;
    pho_srl_response_monitor_alloc(resp_cont.resp);
    resp_cont.resp->req_id = req_cont->req->id;

    rc = sched_handle_monitor(lrs->sched[family], status);
    if (rc)
        goto free_status;

    resp_cont.resp->monitor->status = json_dumps(status, 0);
    json_decref(status);
    if (!resp_cont.resp->monitor->status)
        LOG_GOTO(free_resp, rc = -ENOMEM, "Failed to dump status string");

    rc = _send_message(&lrs->comm, &resp_cont);
    pho_srl_response_free(resp_cont.resp, false);
    free(resp_cont.resp);
    if (rc)
        LOG_GOTO(send_error, rc, "Failed to send monitor response");

    return 0;

free_status:
    json_decref(status);
free_resp:
    free(resp_cont.resp);
send_error:
    _send_error(lrs, rc, req_cont);

    return rc;
}

static int init_rwalloc_container(struct req_container *reqc)
{
    struct rwalloc_params *rwalloc_params = &reqc->params.rwalloc;
    bool is_write = pho_request_is_write(reqc->req);
    size_t i;
    int rc;

    if (is_write)
        rwalloc_params->n_media = reqc->req->walloc->n_media;
    else
        rwalloc_params->n_media = reqc->req->ralloc->n_required;

    rwalloc_params->media = calloc(rwalloc_params->n_media,
                                   sizeof(*rwalloc_params->media));
    if (!rwalloc_params->media)
        return -ENOMEM;

    for (i = 0; i < rwalloc_params->n_media; i++)
        rwalloc_params->media[i].status = SUB_REQUEST_TODO;

    rwalloc_params->respc = calloc(1, sizeof(*rwalloc_params->respc));
    if (!rwalloc_params->respc)
        GOTO(out_free_media, rc = -ENOMEM);

    rwalloc_params->respc->socket_id = reqc->socket_id;
    rwalloc_params->respc->resp = calloc(1,
                                         sizeof(*rwalloc_params->respc->resp));
    if (!rwalloc_params->respc->resp)
        GOTO(out_free_respc, rc = -ENOMEM);

    if (is_write)
        rc = pho_srl_response_write_alloc(rwalloc_params->respc->resp,
                                          rwalloc_params->n_media);
    else
        rc = pho_srl_response_read_alloc(rwalloc_params->respc->resp,
                                         rwalloc_params->n_media);

    if (rc)
        goto out_free_resp;

    rwalloc_params->respc->resp->req_id = reqc->req->id;
    rwalloc_params->respc->devices_len = rwalloc_params->n_media;
    rwalloc_params->respc->devices =
        calloc(rwalloc_params->respc->devices_len,
               sizeof(*rwalloc_params->respc->devices));
    if (!rwalloc_params->respc->devices)
        GOTO(out_free, rc = -ENOMEM);

    return 0;
out_free:
    pho_srl_response_free(rwalloc_params->respc->resp, false);
out_free_resp:
    free(rwalloc_params->respc->resp);
    rwalloc_params->respc->resp = NULL;
out_free_respc:
    free(rwalloc_params->respc);
    rwalloc_params->respc = NULL;
out_free_media:
    free(rwalloc_params->media);
    rwalloc_params->media = NULL;
    return rc;
}

static int init_request_container_param(struct req_container *reqc)
{
    if (pho_request_is_release(reqc->req))
        return init_release_container(reqc);

    if (pho_request_is_write(reqc->req) || pho_request_is_read(reqc->req))
        return init_rwalloc_container(reqc);

    if (pho_request_is_notify(reqc->req))
        reqc->params.notify.notified_device = NULL;

    return 0;
}

/**
 * schedulers_to_signal is a bool array of length PHO_RSC_LAST, representing
 * every scheduler that could be signaled
 */
static int _prepare_requests(struct lrs *lrs, bool *schedulers_to_signal,
                             const int n_data, struct pho_comm_data *data)
{
    enum rsc_family fam;
    int rc = 0;
    int i;

    for (i = 0; i < n_data; ++i) {
        struct req_container *req_cont;
        int rc2;

        if (data[i].buf.size == -1) /* close notification, ignore */
            continue;

        req_cont = calloc(1, sizeof(*req_cont));
        if (!req_cont)
            LOG_RETURN(-ENOMEM, "Cannot allocate request structure");

        /* request processing */
        req_cont->socket_id = data[i].fd;
        req_cont->req = pho_srl_request_unpack(&data[i].buf);
        if (!req_cont->req) {
            free(req_cont);
            continue;
        }

        /* send back the ping request */
        if (pho_request_is_ping(req_cont->req)) {
            _process_ping_request(lrs, req_cont);
            sched_req_free(req_cont);
            continue;
        }

        if (pho_request_is_monitor(req_cont->req)) {
            _process_monitor_request(lrs, req_cont);
            sched_req_free(req_cont);
            continue;
        }

        rc2 = pthread_mutex_init(&req_cont->mutex, NULL);
        if (rc2) {
            rc = rc ? : rc2;
            LOG_GOTO(send_err, rc2,
                     "Unable to init mutex at request container init");
        }

        rc2 = clock_gettime(CLOCK_REALTIME, &req_cont->received_at);
        if (rc2) {
            rc2 = -errno;
            rc = rc ? : rc2;
            LOG_GOTO(send_err, rc2,
                     "Unable to get CLOCK_REALTIME at request container init");
        }

        fam = _determine_family(req_cont->req);
        if (fam == PHO_RSC_INVAL)
            LOG_GOTO(send_err, rc2 = -EINVAL,
                     "Requested family is not recognized");

        if (!lrs->sched[fam])
            LOG_GOTO(send_err, rc2 = -EINVAL,
                     "Requested family is not handled by the daemon");

        rc2 = init_request_container_param(req_cont);
        if (rc2)
            LOG_GOTO(send_err, rc2, "Cannot init request container");

        if (pho_request_is_release(req_cont->req)) {
            rc2 = process_release_request(lrs->sched[fam], &lrs->dss, req_cont);
            rc = rc ? : rc2;
            if (!rc2)
                schedulers_to_signal[fam] = true;
        } else {
            if (running) {
                tsqueue_push(&lrs->sched[fam]->incoming, req_cont);
                schedulers_to_signal[fam] = true;
            } else {
                LOG_GOTO(send_err, rc2 = -ESHUTDOWN,
                         "Daemon stopping, not accepting new requests");
            }
        }

        continue;

send_err:
        _send_error(lrs, rc2, req_cont);
        sched_req_free(req_cont);
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
            break;
        case PHO_RSC_RADOS_POOL:
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
            rc = sched_init(lrs->sched[family], family, &lrs->response_queue);
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
        if (lrs->sched[i])
            thread_signal_stop(&lrs->sched[i]->sched_thread);
    }

    for (i = 0; i < PHO_RSC_LAST; ++i) {
        if (!lrs->sched[i])
            continue;

        thread_wait_end(&lrs->sched[i]->sched_thread);
        sched_fini(lrs->sched[i]);
        free(lrs->sched[i]);
    }

    rc = pho_comm_close(&lrs->comm);
    if (rc)
        pho_error(rc, "Error on closing the socket");

    tsqueue_destroy(&lrs->response_queue, sched_resp_free_with_cont);
    dss_fini(&lrs->dss);

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
        LOG_RETURN(rc, "Error while creating the daemon lock file %s",
                   lock_file);

    rc = tsqueue_init(&lrs->response_queue);
    if (rc)
        LOG_GOTO(err, rc, "Unable to init lrs response queue");

    lrs->stopped = false;

    rc = _load_schedulers(lrs);
    if (rc)
        LOG_GOTO(err, rc, "Error while loading the schedulers");

    sock_path = PHO_CFG_GET(cfg_lrs, PHO_CFG_LRS, server_socket);
    rc = pho_comm_open(&lrs->comm, sock_path, true);
    if (rc)
        LOG_GOTO(err, rc, "Error while opening the socket");

    rc = dss_init(&lrs->dss);
    if (rc)
        LOG_GOTO(err, rc, "Failed to init comm dss handle");

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
    bool schedulers_to_signal[PHO_RSC_LAST] = {false};
    struct pho_comm_data *data = NULL;
    bool stopped = true;
    int n_data;
    int rc = 0;
    int i;

    /* request reception and accept handling */
    rc = pho_comm_recv(&lrs->comm, &data, &n_data);
    if (rc) {
        for (i = 0; i < n_data; ++i)
            free(data[i].buf.buff);
        free(data);
        LOG_RETURN(rc, "Error during request reception");
    }

    rc = _prepare_requests(lrs, schedulers_to_signal, n_data, data);
    free(data);
    if (rc)
        LOG_RETURN(rc, "Error during request enqueuing");

    /* response processing */
    for (i = 0; i < PHO_RSC_LAST; ++i) {
        if (!lrs->sched[i])
            continue;

        if (schedulers_to_signal[i])
            thread_signal(&lrs->sched[i]->sched_thread);

        if (running || sched_has_running_devices(lrs->sched[i]))
            stopped = false;
    }

    rc = send_responses_from_queue(lrs);
    if (rc)
        return rc;

    if (!running)
        lrs->stopped = stopped;

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
        int _errno;

        fd = open(pid_filepath, O_WRONLY | O_CREAT, 0666);
        if (fd == -1) {
            _errno = -errno;

            kill(pid, SIGKILL);
            pho_error(_errno, "cannot open the pid file at '%s'",
                      pid_filepath);
            rc = EXIT_FAILURE;
            goto out;
        }

        sprintf(buf, "%d", pid);
        rc = write(fd, buf, strlen(buf));
        _errno = -errno;
        close(fd);
        if (rc == -1) {
            kill(pid, SIGKILL);
            pho_error(_errno, "cannot write the pid file at '%s'",
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

    while (running || !lrs.stopped) {
        rc = lrs_process(&lrs);
        if (rc && rc != -EINTR)
            break;
    }

    lrs_fini(&lrs);

    return EXIT_SUCCESS;
}
