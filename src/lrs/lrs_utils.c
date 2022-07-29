#include "lrs_utils.h"

#include "pho_common.h"

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
