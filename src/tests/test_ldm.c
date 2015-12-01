/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
#include "pho_test_utils.h"
#include "../ldm/ldm_common.h"

static int _find_dev(const struct mntent *mntent, void *cb_data)
{
    const char *dev_name = cb_data;

    if (!strcmp(mntent->mnt_fsname, dev_name)) {
        pho_info("found device '%s': fstype='%s'", dev_name,
                 mntent->mnt_type);
        return 1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    int rc;

    rc = mnttab_foreach(_find_dev, "proc");

    if (rc == 0) {
        pho_error(-ENOENT, "proc not found");
        exit(EXIT_FAILURE);
    } else if (rc == 1) {
        exit(EXIT_SUCCESS);
    }

    exit(EXIT_FAILURE);
}
