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
#include "ldm_common.h"
#include "pho_test_utils.h"
#include "pho_ldm.h"

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

static int test_mnttab(void *arg)
{
    int rc;

    rc = mnttab_foreach(_find_dev, "proc");

    if (rc == 0)
        LOG_RETURN(-ENOENT, "proc not found");
    else if (rc == 1)
        /* expected return */
        return 0;

    /* other error */
    return -1;
}

static int test_df_0(void *arg)
{
    struct ldm_fs_space spc;

    return simple_statfs("/tmp", &spc);
}

static int test_df_1(void *arg)
{
    struct fs_adapter_module *fsa;
    struct ldm_fs_space spc;
    int rc;

    rc = get_fs_adapter(PHO_FS_POSIX, &fsa);
    if (rc)
        return rc;

    return ldm_fs_df(fsa, "/tmp", &spc, NULL);
}

static int test_df_2(void *arg)
{
    struct ldm_fs_space spc;

    return simple_statfs(NULL, &spc);
}

int main(int argc, char **argv)
{
    test_env_initialize();

    pho_run_test("test mnttab", test_mnttab, NULL, PHO_TEST_SUCCESS);

    pho_run_test("test df (direct call)", test_df_0, NULL, PHO_TEST_SUCCESS);
    pho_run_test("test df (via fs_adapter)", test_df_1, NULL, PHO_TEST_SUCCESS);
    pho_run_test("test df (NULL path)", test_df_2, NULL, PHO_TEST_FAILURE);

    pho_info("ldm_common: All tests succeeded");
    exit(EXIT_SUCCESS);
}
