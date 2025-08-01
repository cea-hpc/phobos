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
 * \brief  Phobosd main interface -- Local Resource Scheduler
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <fcntl.h>
#include <libgen.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "pho_cfg.h"
#include "pho_comm.h"
#include "pho_common.h"
#include "pho_daemon.h"
#include "pho_srl_lrs.h"
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
    const char *lock_file;                     /*!< Daemon lock file path */
};

/* ****************************************************************************/
/* Daemon context *************************************************************/
/* ****************************************************************************/

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

    path_copy = xstrdup(lock_file);
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

static void init_release_container(struct req_container *req_cont)
{
    struct tosync_medium *tosync_media = NULL;
    struct nosync_medium *nosync_media = NULL;
    size_t tosync_media_index = 0;
    size_t nosync_media_index = 0;
    pho_req_release_elt_t *media;
    size_t i;

    n_media_per_release(req_cont);

    if (req_cont->params.release.n_tosync_media)
        tosync_media = xcalloc(req_cont->params.release.n_tosync_media,
                               sizeof(*tosync_media));

    if (req_cont->params.release.n_nosync_media)
        nosync_media = xcalloc(req_cont->params.release.n_nosync_media,
                               sizeof(*nosync_media));

    for (i = 0; i < req_cont->req->release->n_media; ++i) {
        media = req_cont->req->release->media[i];
        if (media->to_sync) {
            tosync_media[tosync_media_index].status = SUB_REQUEST_TODO;
            tosync_media[tosync_media_index].medium.family =
                (enum rsc_family)media->med_id->family;
            tosync_media[tosync_media_index].written_size = media->size_written;
            tosync_media[tosync_media_index].nb_extents_written =
                media->nb_extents_written;
            tosync_media[tosync_media_index].client_rc = media->rc;
            pho_id_name_set(&tosync_media[tosync_media_index].medium,
                            media->med_id->name, media->med_id->library);

            tosync_media_index++;
        } else {
            nosync_media[nosync_media_index].medium.family =
                (enum rsc_family)media->med_id->family;
            nosync_media[tosync_media_index].written_size = media->size_written;
            pho_id_name_set(&nosync_media[nosync_media_index].medium,
                            media->med_id->name, media->med_id->library);

            nosync_media_index++;
        }
    }

    req_cont->params.release.tosync_media = tosync_media;
    req_cont->params.release.nosync_media = nosync_media;
}

static void notify_device_request_is_canceled(struct resp_container *respc)
{
    int i;

    for (i = 0; i < respc->devices_len; i++) {
        MUTEX_LOCK(&respc->devices[i]->ld_mutex);
        respc->devices[i]->ld_ongoing_io = false;
        if (respc->devices[i]->ld_ongoing_grouping.grouping) {
            free(respc->devices[i]->ld_ongoing_grouping.grouping);
            respc->devices[i]->ld_ongoing_grouping.grouping = NULL;
        }

        MUTEX_UNLOCK(&respc->devices[i]->ld_mutex);
    }
}

static void convert_response_to_error(struct resp_container *respc)
{
    int response_kind = request_kind_from_response(respc->resp);

    pho_srl_response_free(respc->resp, false);
    pho_srl_response_error_alloc(respc->resp);

    respc->resp->error->rc = -ESHUTDOWN;
    respc->resp->error->req_kind = response_kind;
}

static inline bool cancel_read_write(struct resp_container *respc)
{
    if (pho_response_is_read(respc->resp) ||
        pho_response_is_write(respc->resp)) {
        notify_device_request_is_canceled(respc);
        return true;
    }

    return false;
}

static inline void cancel_response(struct resp_container *respc)
{
    if (cancel_read_write(respc))
        convert_response_to_error(respc);
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
    if (!running)
        cancel_response(respc);

    pho_srl_response_pack(respc->resp, &msg.buf);

    /* XXX: \p running could change just before the call to send.
     * Which means that new I/O responses would be sent with running = false
     */
    rc = pho_comm_send(&msg);
    free(msg.buf.buff);
    if (client_disconnected_error(rc)) {
        pho_error(rc,
                  "Failed to send %s response to disconnected client %d, not "
                  "fatal", pho_srl_response_kind_str(respc->resp),
                  respc->socket_id);
        /* error not fatal for the LRS */
        rc = 0;
        /* do not block device's ongoing_io status if the client disconnects */
        goto cancel;
    } else if (rc) {
        /* Do not block device's ongoing_io status if the client never
         * receives the answer.
         */
        LOG_GOTO(cancel, rc, "Response cannot be sent");
    }

    return rc;

cancel:
    cancel_read_write(respc);
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

    resp_cont.resp = xmalloc(sizeof(*resp_cont.resp));

    prepare_error(&resp_cont, req_rc, req_cont);

    rc = _send_message(&lrs->comm, &resp_cont);
    pho_srl_response_free(resp_cont.resp, false);

    free(resp_cont.resp);

    return rc;
}

static int _process_ping_request(struct lrs *lrs,
                                 const struct req_container *req_cont)
{
    struct resp_container resp_cont;
    int rc;

    resp_cont.resp = xmalloc(sizeof(*resp_cont.resp));

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

    resp_cont.resp = xmalloc(sizeof(*resp_cont.resp));

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

static void init_rwalloc_container(struct req_container *reqc)
{
    struct rwalloc_params *rwalloc_params = &reqc->params.rwalloc;
    bool is_write = pho_request_is_write(reqc->req);
    size_t i;

    if (is_write)
        rwalloc_params->n_media = reqc->req->walloc->n_media;
    else
        rwalloc_params->n_media = reqc->req->ralloc->n_required;

    rwalloc_params->media = xcalloc(rwalloc_params->n_media,
                                    sizeof(*rwalloc_params->media));

    for (i = 0; i < rwalloc_params->n_media; i++)
        rwalloc_params->media[i].status = SUB_REQUEST_TODO;

    rwalloc_params->respc = xcalloc(1, sizeof(*rwalloc_params->respc));

    rwalloc_params->respc->socket_id = reqc->socket_id;
    rwalloc_params->respc->resp = xcalloc(1,
                                          sizeof(*rwalloc_params->respc->resp));

    if (is_write)
        pho_srl_response_write_alloc(rwalloc_params->respc->resp,
                                     rwalloc_params->n_media);
    else
        pho_srl_response_read_alloc(rwalloc_params->respc->resp,
                                    rwalloc_params->n_media);

    rwalloc_params->respc->resp->req_id = reqc->req->id;
    rwalloc_params->respc->devices_len = rwalloc_params->n_media;
    rwalloc_params->respc->devices =
        xcalloc(rwalloc_params->respc->devices_len,
                sizeof(*rwalloc_params->respc->devices));

    if (!is_write)
        rml_init(&rwalloc_params->media_list, reqc);
}

static void init_request_container_param(struct req_container *reqc)
{
    if (pho_request_is_release(reqc->req))
        init_release_container(reqc);
    else if (pho_request_is_write(reqc->req) || pho_request_is_read(reqc->req))
        init_rwalloc_container(reqc);
    else if (pho_request_is_notify(reqc->req))
        reqc->params.notify.notified_device = NULL;
}

static int update_phys_spc_free(struct dss_handle *dss,
                                struct media_info *dss_media_info,
                                size_t written_size)
{
    if (written_size > 0) {
        dss_media_info->stats.phys_spc_free -= written_size;
        /*
         * Written size could be overstated, especially when media have
         * automatic compression.
         *
         * This value will be correctly updated at sync with ldm_fs_df input.
         * Meanwhile, we set 0 instead of an inaccurate negative value.
         */
        if (dss_media_info->stats.phys_spc_free < 0) {
            pho_debug("Update negative phys_spc_free %zd of medium "
                      "(family '%s', name '%s', library '%s') is set to zero",
                      dss_media_info->stats.phys_spc_free,
                      rsc_family2str(dss_media_info->rsc.id.family),
                      dss_media_info->rsc.id.name,
                      dss_media_info->rsc.id.library);
            dss_media_info->stats.phys_spc_free = 0;
        }

        return dss_media_update(dss, dss_media_info, dss_media_info, 1,
                                PHYS_SPC_FREE);
    }

    return 0;
}

static int release_medium(struct lrs_sched *sched,
                          struct dss_handle *comm_dss,
                          struct req_container *reqc,
                          pho_req_release_elt_t *release,
                          size_t medium_index,
                          int *req_rc)
{
    struct lrs_dev *dev = NULL;
    int rc = 0;

    *req_rc = 0;

    /* find the corresponding device */
    dev = search_loaded_medium(sched->devices.ldh_devices,
                               release->med_id->name, release->med_id->library);
    if (!dev) {
        *req_rc = -ENODEV;
        pho_error(*req_rc,
                  "Unable to find loaded device of the medium (name '%s', "
                  "library '%s') to release",
                  release->med_id->name, release->med_id->library);
        return 0;
    } else if (!dev_is_release_ready(dev)) {
        pho_error(0, /* Do not display a POSIX error in the logs, as it would
                      * be confusing to see.
                      */
                  "device '%s' was stopped before the medium (name '%s', "
                  "library '%s') was released",
                  dev->ld_dss_dev_info->rsc.id.name,
                  release->med_id->name, release->med_id->library);
        *req_rc = -ESHUTDOWN;
        return 0;
    }

    /* update media phys_spc_free stats in advance, before next sync */
    MUTEX_LOCK(&dev->ld_mutex);
    if (release->rc == 0)
        rc = update_phys_spc_free(comm_dss, dev->ld_dss_media_info,
                                  release->size_written);
    if (release->to_sync)
        /* ownership of reqc is passed to the device thread */
        push_new_sync_to_device(dev, reqc, medium_index);

    /* Acknowledgement of the request */
    dev->ld_ongoing_io = false;
    if (dev->ld_ongoing_grouping.grouping && !reqc->req->release->partial) {
        free(dev->ld_ongoing_grouping.grouping);
        dev->ld_ongoing_grouping.grouping = NULL;
    }

    MUTEX_UNLOCK(&dev->ld_mutex);

    return rc;
}

/**
 * Process release request
 *
 * This function takes ownership of the request.
 * The request container is queued or freed by this call.
 * If an error occurs, this function creates and queues the corresponding
 * error message.
 *
 * The error code returned by this function stands for an error of the LRS
 * daemon itself, not an error about the release request which is managed by
 * an error message.
 */
static int process_release_request(struct lrs_sched *sched,
                                   struct dss_handle *comm_dss,
                                   struct req_container *reqc)
{
    int release_index = -1;
    int client_err = 0;
    int rc = 0;
    size_t i;
    size_t j;

    for (i = 0; i < reqc->req->release->n_media; i++) {
        pho_req_release_elt_t *release_elt = reqc->req->release->media[i];
        int req_rc = 0;

        rc = release_medium(sched, comm_dss, reqc, release_elt,
                            release_index + 1, &req_rc);
        if (rc)
            /* system error, stop */
            break;

        if (release_elt->to_sync && !client_err) {
            client_err = req_rc;
            release_index++; /* index of first error */
        }
    }

    if (!rc && !client_err) {
        if (reqc->params.release.n_tosync_media == 0)
            sched_req_free(reqc);

        return 0;
    }

    if (reqc->params.release.n_tosync_media == 0) {
        /* the client will not wait for the reponse, do not send one */
        sched_req_free(reqc);
        return rc;
    }

    /* send error to client */
    MUTEX_LOCK(&reqc->mutex);
    reqc->params.release.rc = rc ? : client_err;
    reqc->params.release.tosync_media[release_index].status =
        SUB_REQUEST_ERROR;
    queue_error_response(sched->response_queue, reqc->params.release.rc, reqc);

    for (j = 0; j < reqc->params.release.n_tosync_media; j++) {
        if (j == release_index)
            continue;

        reqc->params.release.tosync_media[j].status =
            SUB_REQUEST_CANCEL;
    }

    MUTEX_UNLOCK(&reqc->mutex);
    if (release_index == 0)
        /* never queued: we can free it */
        sched_req_free(reqc);

    return rc;
}

static const char *config_get_value(json_t *config, const char *key,
                                    const char *configurationstr)
{
    json_t *value;

    value = json_object_get(config, key);
    if (!value) {
        pho_error(-EINVAL, "Key '%s' not found in configuration '%s'",
                  key, configurationstr);
        return NULL;
    }

    if (!json_is_string(value)) {
        pho_error(-EINVAL,
                  "Value of '%s' in configuration '%s' is not a string",
                  key, configurationstr);
        return NULL;
    }

    return json_string_value(value);
}

static int handle_configure_request(struct lrs *lrs,
                                    const struct req_container *reqc,
                                    json_t *queried_elements)
{
    pho_req_configure_t *confreq = reqc->req->configure;
    json_t *configuration;
    json_error_t error;
    json_t *value;
    size_t index;
    int rc = 0;

    if (!pho_configure_op_is_valid(confreq->op))
        LOG_RETURN(-EPROTO, "Invalid configuration request %d", confreq->op);

    if (!confreq->configuration || !strcmp(confreq->configuration, ""))
        LOG_RETURN(-EPROTO,
                   "Received a configuration request without configuration "
                   "information");

    configuration = json_loads(confreq->configuration, JSON_REJECT_DUPLICATES,
                               &error);
    if (!configuration)
        LOG_RETURN(rc = -EINVAL, "Failed to parse configuration '%s': %s",
                   confreq->configuration, error.text);

    if (!json_is_array(configuration))
        LOG_GOTO(free_conf, rc = -EINVAL, "Expected JSON object");

    json_array_foreach(configuration, index, value) {
        const char *elem_value;
        const char *elem_key;
        const char *section;

        if (!json_is_object(value))
            LOG_GOTO(free_conf, rc = -EINVAL,
                     "Value at index %lu is not an object", index);

        section = config_get_value(value, "section", confreq->configuration);
        if (!section)
            GOTO(free_conf, rc = -EINVAL);

        elem_key = config_get_value(value, "key", confreq->configuration);
        if (!elem_key)
            GOTO(free_conf, rc = -EINVAL);

        if (confreq->op == (int)PHO_CONF_OP_SET) {
            elem_value = config_get_value(value, "value",
                                          confreq->configuration);
            if (!elem_value)
                GOTO(free_conf, rc = -EINVAL);

            rc = pho_cfg_set_val_local(section, elem_key, elem_value);
            if (rc)
                GOTO(free_conf, rc = -EINVAL);
        } else {
            const char *v;

            rc = pho_cfg_get_val(section, elem_key, &v);
            if (rc == -ENODATA) {
                pho_warn("Configuration element '%s' not found", elem_key);
                v = ""; /* return an empty string if not found */
            } else if (rc) {
                LOG_GOTO(free_conf, rc, "Failed to read '%s::%s' in config",
                         section, elem_key);
            }

            rc = json_array_append(queried_elements, json_string(v));
            if (rc == -1)
                GOTO(free_conf, rc = -errno);
        }
    }

free_conf:
    json_decref(configuration);

    return rc;
}

static int _process_configure_request(struct lrs *lrs,
                                      const struct req_container *reqc)
{
    json_t *queried_elements = json_array();
    struct resp_container respc;
    pho_resp_t resp;
    int rc;

    if (!queried_elements)
        LOG_GOTO(send_error, rc = -errno, "Failed to create JSON array");

    rc = handle_configure_request(lrs, reqc, queried_elements);
    if (rc)
        goto free_array;

    pho_srl_response_configure_alloc(&resp);
    resp.req_id = reqc->req->id;

    respc.resp = &resp;
    respc.socket_id = reqc->socket_id;

    if (reqc->req->configure->op == (int)PHO_CONF_OP_GET) {
        resp.configure->configuration =
            json_dumps(queried_elements, JSON_COMPACT);

        if (!resp.configure->configuration)
            LOG_GOTO(free_array, rc = -errno,
                     "Failed to dump JSON configuration");
    }

    json_decref(queried_elements);

    rc = _send_message(&lrs->comm, &respc);
    pho_srl_response_free(&resp, false);
    if (rc)
        /* No need to try to send an error if the sending response failed */
        LOG_RETURN(rc, "Failed to send configure response");

    return 0;

free_array:
    json_decref(queried_elements);
send_error:
    _send_error(lrs, rc, reqc);

    return rc;
}

static bool handle_quick_requests(struct lrs *lrs, struct req_container *reqc)
{
    if (pho_request_is_ping(reqc->req)) {
        _process_ping_request(lrs, reqc);
        sched_req_free(reqc);
        return true;
    } else if (pho_request_is_monitor(reqc->req)) {
        _process_monitor_request(lrs, reqc);
        sched_req_free(reqc);
        return true;
    } else if (pho_request_is_configure(reqc->req)) {
        _process_configure_request(lrs, reqc);
        sched_req_free(reqc);
        return true;
    } else {
        return false;
    }
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

        req_cont = xcalloc(1, sizeof(*req_cont));

        /* request processing */
        req_cont->socket_id = data[i].fd;
        req_cont->req = pho_srl_request_unpack(&data[i].buf);
        if (!req_cont->req) {
            free(req_cont);
            continue;
        }

        if (handle_quick_requests(lrs, req_cont))
            continue;

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

        init_request_container_param(req_cont);
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

    parse_list = xstrdup(list);

    /* Initialize a scheduler for each requested family */
    for (item = strtok_r(parse_list, ",", &saveptr);
         item != NULL;
         item = strtok_r(NULL, ",", &saveptr)) {
        int family = str2rsc_family(item);

        switch (family) {
        case PHO_RSC_RADOS_POOL:
        case PHO_RSC_TAPE:
        case PHO_RSC_DIR:
            if (lrs->sched[family]) {
                pho_warn("The family '%s' was already processed, ignore it",
                         item);
                continue;
            }

            lrs->sched[family] = xcalloc(1, sizeof(*lrs->sched[family]));

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
    int rc = 0;
    int i;

    ENTRY;

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
        pho_error(rc, "Failed to close the phobosd socket");

    tsqueue_destroy(&lrs->response_queue, sched_resp_free_with_cont);
    dss_fini(&lrs->dss);

    _delete_lock_file(lrs->lock_file);
}

/**
 * Initialize a new LRS.
 *
 * Set umask to "0000".
 * The LRS data structure is allocated in lrs_init()
 * and deallocated in lrs_fini().
 *
 * \param[in]   lrs         The LRS to be initialized.
 *
 * \return                  0 on success, -1 * posix error code on failure.
 */
static int lrs_init(struct lrs *lrs)
{
    union pho_comm_addr sock_addr = {0};
    int rc;

    umask(0000);

    lrs->lock_file = PHO_CFG_GET(cfg_lrs, PHO_CFG_LRS, lock_file);
    if (lrs->lock_file == NULL)
        LOG_RETURN(-ENODATA, "PHO_CFG_LRS_lock_file is not defined");

    rc = _create_lock_file(lrs->lock_file);
    if (rc)
        LOG_RETURN(rc, "Error while creating the daemon lock file %s",
                   lrs->lock_file);

    rc = tsqueue_init(&lrs->response_queue);
    if (rc)
        LOG_GOTO(err, rc, "Unable to init lrs response queue");

    lrs->stopped = false;

    rc = _load_schedulers(lrs);
    if (rc)
        LOG_GOTO(err, rc, "Error while loading the schedulers");

    sock_addr.af_unix.path = PHO_CFG_GET(cfg_lrs, PHO_CFG_LRS, server_socket);
    rc = pho_comm_open(&lrs->comm, &sock_addr, PHO_COMM_UNIX_SERVER);
    if (rc)
        LOG_GOTO(err, rc, "Failed to open the phobosd socket");

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

    /* check if some devices are still running */
    for (i = 0; i < PHO_RSC_LAST; ++i) {
        if (!lrs->sched[i])
            continue;

        if (running || sched_has_running_devices(lrs->sched[i]))
            stopped = false;
    }

    /* request reception and accept handling */
    rc = pho_comm_recv(&lrs->comm, &data, &n_data);
    if (rc) {
        for (i = 0; i < n_data; ++i)
            free(data[i].buf.buff);
        free(data);
        running = false;
        LOG_GOTO(end, rc, "Error during request reception");
    }

    rc = _prepare_requests(lrs, schedulers_to_signal, n_data, data);
    free(data);
    if (rc) {
        running = false;
        LOG_GOTO(end, rc, "Error during request enqueuing");
    }

    /* response processing */
    for (i = 0; i < PHO_RSC_LAST; ++i) {
        if (!lrs->sched[i])
            continue;

        if (schedulers_to_signal[i])
            thread_signal(&lrs->sched[i]->sched_thread);

        if (running || sched_has_running_devices(lrs->sched[i]))
            stopped = false;
    }

end:
    rc = send_responses_from_queue(lrs);
    if (rc)
        running = false;

    if (!running)
        lrs->stopped = stopped;

    return rc;
}

int main(int argc, char **argv)
{
    int write_pipe_from_child_to_father;
    struct daemon_params param;
    bool lrs_init_done = false;
    struct lrs lrs = {};
    int rc;

    rc = daemon_creation(argc, argv, &param, &write_pipe_from_child_to_father,
                         "phobosd");
    if (rc)
        return -rc;

    rc = daemon_init(param);

    /* lrs processing */
    if (!rc)
        rc = lrs_init(&lrs);

    if (!rc)
        lrs_init_done = true;

    if (param.is_daemon)
        daemon_notify_init_done(write_pipe_from_child_to_father, &rc);

    if (rc) {
        if (lrs_init_done)
            lrs_fini(&lrs);

        return -rc;
    }

    while (running || !lrs.stopped)
        lrs_process(&lrs);

    lrs_fini(&lrs);
    return EXIT_SUCCESS;
}
