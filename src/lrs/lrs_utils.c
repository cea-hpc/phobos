#include "lrs_utils.h"

#include "pho_common.h"
#include "pho_srl_lrs.h"
#include "lrs_sched.h"

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

