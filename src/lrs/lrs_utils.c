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

#include "lrs_sched.h"
#include "lrs_utils.h"
#include "pho_common.h"
#include "pho_srl_lrs.h"

#include <errno.h>
#include <unistd.h>

int lock_handle_init(struct lock_handle *lock_handle, struct dss_handle *dss)
{
    lock_handle->lock_hostname = get_hostname();
    if (!*lock_handle->lock_hostname)
        return -errno;

    lock_handle->lock_owner = getpid();
    lock_handle->dss = dss;

    return 0;
}

struct media_info **reqc_get_medium_to_alloc(struct req_container *reqc,
                                             size_t index)
{
    if (pho_request_is_format(reqc->req))
        return &reqc->params.format.medium_to_format;
    else if (pho_request_is_read(reqc->req) ||
             pho_request_is_write(reqc->req))
        return &reqc->params.rwalloc.media[index].alloc_medium;

    return NULL;
}

static struct pho_id *get_sub_request_medium(struct sub_request *sub_request)
{
    size_t medium_index = sub_request->medium_index;

    if (pho_request_is_write(sub_request->reqc->req) ||
        pho_request_is_read(sub_request->reqc->req)) {
        struct rwalloc_params *params = &sub_request->reqc->params.rwalloc;

        /*
         * If the medium to be allocated is already loaded in the device, the
         * former will be set to NULL in the sub_request
         */
        if (!params->media[medium_index].alloc_medium)
            return NULL;

        return &(params->media[medium_index].alloc_medium->rsc.id);
    } else {
        struct format_params *params = &sub_request->reqc->params.format;

        if (!params->medium_to_format)
            return NULL;

        return &(params->medium_to_format->rsc.id);
    }
}

void reqc_pho_id_from_index(struct req_container *reqc, size_t index,
                            struct pho_id *id)
{
    PhoResourceId *res_id;

    if (pho_request_is_read(reqc->req))
        res_id = reqc->req->ralloc->med_ids[index];
    else if (pho_request_is_format(reqc->req))
        res_id = reqc->req->format->med_id;
    else
        res_id = NULL;

    if (!res_id) {
        pho_error(-EINVAL,
                  "%s call for a %s request, abort", __func__,
                  pho_srl_request_kind_str(reqc->req));
        abort();
    }

    id->family = res_id->family;
    pho_id_name_set(id, res_id->name, res_id->library);
}

static struct lrs_dev *__search_loaded_medium(GPtrArray *devices,
                                              const char *name,
                                              const char *library,
                                              bool keep_lock)
{
    int i;

    ENTRY;

    pho_debug("Searching loaded medium (name '%s', library '%s')",
              name, library);

    for (i = 0; i < devices->len; i++) {
        struct lrs_dev *dev = NULL;
        const char *media_id;

        dev = g_ptr_array_index(devices, i);
        MUTEX_LOCK(&dev->ld_mutex);
        if (dev->ld_op_status != PHO_DEV_OP_ST_MOUNTED &&
            dev->ld_op_status != PHO_DEV_OP_ST_LOADED)
            goto err_continue;

        /* The drive may contain a media unknown to phobos, skip it */
        if (dev->ld_dss_media_info == NULL)
            goto err_continue;

        media_id = dev->ld_dss_media_info->rsc.id.name;
        if (media_id == NULL) {
            pho_warn("Cannot retrieve media ID from device '%s'",
                     dev->ld_dev_path);
            goto err_continue;
        }

        if (!strcmp(name, media_id) &&
            !strcmp(library, dev->ld_dss_media_info->rsc.id.library)) {
            if (!keep_lock)
                MUTEX_UNLOCK(&dev->ld_mutex);

            pho_debug("Found loaded medium (name '%s', library '%s') in '%s'",
                      name, library, dev->ld_dss_dev_info->rsc.id.name);
            return dev;
        }

err_continue:
        MUTEX_UNLOCK(&dev->ld_mutex);
    }

    pho_debug("Did not find loaded medium (name '%s', library '%s')",
              name, library);
    return NULL;
}

struct lrs_dev *search_loaded_medium(GPtrArray *devices,
                                     const char *name,
                                     const char *library)
{
    return __search_loaded_medium(devices, name, library, false);
}

struct lrs_dev *search_loaded_medium_keep_lock(GPtrArray *devices,
                                               const char *name,
                                               const char *library)
{
    return __search_loaded_medium(devices, name, library, true);
}

struct lrs_dev *search_in_use_medium(GPtrArray *devices, const char *name,
                                     const char *library, bool *sched_ready)
{
    int i;

    ENTRY;

    pho_debug("Searching in-use medium (name '%s', library '%s')",
              name, library);
    if (sched_ready)
        *sched_ready = false;

    for (i = 0; i < devices->len; i++) {
        struct lrs_dev *dev = NULL;
        const struct pho_id *media_id;

        dev = g_ptr_array_index(devices, i);
        MUTEX_LOCK(&dev->ld_mutex);

        if (dev->ld_sub_request != NULL) {
            media_id = get_sub_request_medium(dev->ld_sub_request);
            if (media_id == NULL) {
                pho_debug("Cannot retrieve medium ID from device '%s' sub_req",
                          dev->ld_dev_path);
                goto check_load;
            }

            if (!strcmp(name, media_id->name) &&
                !strcmp(library, media_id->library)) {
                MUTEX_UNLOCK(&dev->ld_mutex);
                pho_debug("Found '%s' in '%s' sub_request",
                          name, dev->ld_dss_dev_info->rsc.id.name);
                return dev;
            }
        }

check_load:
        if (dev->ld_op_status != PHO_DEV_OP_ST_EMPTY) {
            /* The drive may contain a media unknown to phobos, skip it */
            if (dev->ld_dss_media_info == NULL)
                goto err_continue;

            media_id = &(dev->ld_dss_media_info->rsc.id);
            if (!strcmp(name, media_id->name) &&
                !strcmp(library, media_id->library)) {
                if (sched_ready)
                    *sched_ready = dev_is_sched_ready(dev);

                MUTEX_UNLOCK(&dev->ld_mutex);
                pho_debug("Found loaded medium (name '%s', library '%s') in "
                          "'%s'",
                          name, library, dev->ld_dss_dev_info->rsc.id.name);
                return dev;
            }
        }

err_continue:
        MUTEX_UNLOCK(&dev->ld_mutex);
    }

    pho_debug("Did not find in-use medium (name '%s', library '%s')",
              name, library);

    return NULL;
}

void rml_init(struct read_media_list *list, struct req_container *reqc)
{
    list->rml_media = reqc->req->ralloc->med_ids;
    list->rml_size = reqc->req->ralloc->n_med_ids;
    list->rml_available = list->rml_size;
    list->rml_allocated = 0;
    list->rml_errors = 0;
}

static size_t rml_last_unavailable(struct read_media_list *list)
{
    return list->rml_size - 1 - list->rml_errors;
}

static size_t rml_last_free(struct read_media_list *list)
{
    return list->rml_size - list->rml_errors - 1;
}

/**
 * Swap the medium ID at \p index with the last available medium.
 */
static void rml_move_medium_to_unavailable(struct read_media_list *list,
                                           size_t index)
{
    assert(list->rml_available != 0);
    list->rml_available--;
    if (index < list->rml_available)
        /* move index to first unavailable slot, the last available item will
         * take the place of index
         */
        med_ids_switch(list->rml_media, index, list->rml_available);
}

enum read_medium_allocation_status rml_errno2status(int rc)
{
    switch (rc) {
    case 0:
        return RMAS_OK;
    case -EAGAIN:
        return RMAS_UNAVAILABLE;
    default:
        return RMAS_ERROR;
    }
}

size_t rml_medium_update(struct read_media_list *list, size_t index,
                         enum read_medium_allocation_status status)
{
    switch (status) {
    case RMAS_OK:
        /* Move the medium ID at the end of the allocated list */
        med_ids_switch(list->rml_media, index, list->rml_allocated++);
        break;
    case RMAS_ERROR:
        if ((list->rml_size - list->rml_errors) != list->rml_available)
            /* Some media are temporarily unavailable, move the error at index
             * to last unavailable slot which then becomes the first error.
             * The previous last unavailable is now at index and will be swapped
             * next.
             */
            med_ids_switch(list->rml_media, index, rml_last_unavailable(list));

        list->rml_errors++;
        if (list->rml_reset_done)
            /* After the reset, all the media required have been allocated once.
             * An error means that an already allocated medium has failed,
             * decrease rml_allocated.
             */
            list->rml_allocated--;

        /* fallthrough */
    case RMAS_UNAVAILABLE:
        rml_move_medium_to_unavailable(list, index);
        break;
    }

    return list->rml_available;
}

void rml_medium_realloc_failed(struct read_media_list *list,
                               size_t free_index,
                               size_t failed_index)
{
    /* This function must not be called with unavailable medium */
    assert(list->rml_available + list->rml_errors + list->rml_allocated
           == list->rml_size);

    /* Move the newly allocated medium to the last free position */
    med_ids_switch(list->rml_media, free_index, rml_last_free(list));

    /* Swap the allocated medium with the failed one */
    med_ids_switch(list->rml_media, failed_index, rml_last_free(list));

    /* increase the size of the error section making the last free an error */
    list->rml_errors++;
    /* decrease the number of free media */
    list->rml_available--;
}

void rml_medium_realloc(struct read_media_list *list,
                        size_t free_index,
                        size_t allocated_index)
{
    med_ids_switch(list->rml_media, free_index, allocated_index);
}

size_t rml_nb_usable_media(struct read_media_list *list)
{
    return list->rml_size - list->rml_errors;
}

void rml_reset(struct read_media_list *list)
{
    list->rml_available =
        list->rml_size - list->rml_errors - list->rml_allocated;
    list->rml_reset_done = true;
}

void rml_requeue(struct read_media_list *list)
{
    list->rml_available = list->rml_size - list->rml_errors;
    list->rml_allocated = 0;
}

void rml_display(struct read_media_list *list)
{
    size_t i;

    for (i = 0; i < list->rml_size; i++)
        pho_debug("rml %lu: (name %s, library %s)",
                  i, list->rml_media[i]->name, list->rml_media[i]->library);
}

