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

static char *get_sub_request_medium_name(struct sub_request *sub_request)
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

        return params->media[medium_index].alloc_medium->rsc.id.name;
    } else {
        struct format_params *params = &sub_request->reqc->params.format;

        if (!params->medium_to_format)
            return NULL;

        return params->medium_to_format->rsc.id.name;
    }
}

struct lrs_dev *search_loaded_medium(GPtrArray *devices,
                                     const char *name)
{
    int i;

    ENTRY;

    pho_debug("Searching loaded medium '%s'", name);

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

        if (!strcmp(name, media_id)) {
            MUTEX_UNLOCK(&dev->ld_mutex);
            pho_debug("Found loaded medium '%s' in '%s'",
                      name, dev->ld_dss_dev_info->rsc.id.name);
            return dev;
        }

err_continue:
        MUTEX_UNLOCK(&dev->ld_mutex);
    }

    pho_debug("Did not find loaded medium '%s'", name);
    return NULL;
}

struct lrs_dev *search_in_use_medium(GPtrArray *devices,
                                     const char *name,
                                     bool *sched_ready)
{
    int i;

    ENTRY;

    pho_debug("Searching in-use medium '%s'", name);
    if (sched_ready)
        *sched_ready = false;

    for (i = 0; i < devices->len; i++) {
        struct lrs_dev *dev = NULL;
        const char *media_id;

        dev = g_ptr_array_index(devices, i);
        MUTEX_LOCK(&dev->ld_mutex);

        if (dev->ld_sub_request != NULL) {
            media_id = get_sub_request_medium_name(dev->ld_sub_request);
            if (media_id == NULL) {
                pho_debug("Cannot retrieve medium ID from device '%s' sub_req",
                          dev->ld_dev_path);
                goto check_load;
            }

            if (!strcmp(name, media_id)) {
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

            media_id = dev->ld_dss_media_info->rsc.id.name;
            if (media_id == NULL) {
                pho_warn("Cannot retrieve media ID from device '%s'",
                         dev->ld_dev_path);

                goto err_continue;
            }

            if (!strcmp(name, media_id)) {
                if (sched_ready)
                    *sched_ready = dev_is_sched_ready(dev);
                MUTEX_UNLOCK(&dev->ld_mutex);
                pho_debug("Found loaded medium '%s' in '%s'",
                          name, dev->ld_dss_dev_info->rsc.id.name);
                return dev;
            }
        }

err_continue:
        MUTEX_UNLOCK(&dev->ld_mutex);
    }

    pho_debug("Did not find in-use medium '%s'", name);

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

/**
 * Swap the medium ID at \p index with the last available medium.
 */
static void rml_move_medium_to_unavailable(struct read_media_list *list,
                                           size_t index)
{
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

size_t rml_nb_usable_media(struct read_media_list *list)
{
    return list->rml_size - list->rml_errors;
}

void rml_reset(struct read_media_list *list)
{
    list->rml_available = list->rml_size - list->rml_errors;
    list->rml_reset_done = true;
}
