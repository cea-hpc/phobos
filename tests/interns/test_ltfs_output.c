/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2026 CEA/DAM.
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
 * \brief  Test LTFS output handling through command_call
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <setjmp.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cmocka.h>

#include "pho_test_utils.h"
#include "pho_common.h"
#include "pho_ldm.h"

/*
 * Mimic an LTFS command printing a VTL cartridge's non-UTF-8 tape attribute,
 * as observed on the quadstor test library.
 */
#define GARBAGE_SCRIPT "test_ltfs_garbage.sh"

static GList *verb_msgs;

static void capture_verb_cb(const struct pho_logrec *rec)
{
    if (rec->plr_level == PHO_LOG_VERB)
        verb_msgs = g_list_append(verb_msgs, xstrdup(rec->plr_msg));
}

/*
 * All logged lines must be valid UTF-8 and keep the readable part of the
 * raw line, without the non-UTF-8 bytes.
 */
static void check_logged_lines_valid_utf8(void)
{
    GList *iter;
    bool found_attribute_line = false;

    assert_true(g_list_length(verb_msgs) > 0);

    for (iter = verb_msgs; iter != NULL; iter = iter->next) {
        char *msg = iter->data;

        assert_true(g_utf8_validate(msg, -1, NULL));
        assert_null(memchr(msg, 0x8d, strlen(msg)));

        if (strstr(msg, "Medium Label = P000s"))
            found_attribute_line = true;
    }

    assert_true(found_attribute_line);
}

/*
 * Override the ltfs command to run the garbage script, through the
 * PHOBOS_LTFS_cmd_<name> environment parameter.
 */
static void set_garbage_cmd(const char *cfg_name)
{
    char cmd[PATH_MAX];
    char pwd[PATH_MAX / 2];

    assert_non_null(getcwd(pwd, sizeof(pwd)));
    snprintf(cmd, sizeof(cmd), "%s/%s \"%s\" \"%s\"", pwd, GARBAGE_SCRIPT,
             "%s", "%s");
    assert_int_equal(setenv(cfg_name, cmd, 1), 0);
}

static void ltfs_format_invalid_utf8(void **state)
{
    struct fs_adapter_module *fsa = NULL;
    struct ldm_fs_space spc = {0};
    int rc;

    set_garbage_cmd("PHOBOS_LTFS_cmd_format");

    rc = get_fs_adapter(PHO_FS_LTFS, &fsa);
    assert_return_code(rc, -rc);

    pho_log_callback_set(capture_verb_cb);

    rc = fsa->ops->fs_format("/dev/nst0", "P00004L5", &spc, NULL);
    assert_return_code(rc, -rc);

    /*
     * the capacity is parsed from the raw output, untouched by the log
     * sanitization
     */
    assert_int_equal(spc.spc_avail, (ssize_t)3000 * 1024 * 1024 * 1024);

    check_logged_lines_valid_utf8();

    pho_log_callback_set(NULL);
    g_list_free_full(verb_msgs, free);
    verb_msgs = NULL;
}

static void ltfs_mount_invalid_utf8(void **state)
{
    struct fs_adapter_module *fsa = NULL;
    char mnt_path[] = "/tmp/test_ltfs_output_mount.XXXXXX";
    int rc;

    assert_non_null(mkdtemp(mnt_path));

    set_garbage_cmd("PHOBOS_LTFS_cmd_mount");

    rc = get_fs_adapter(PHO_FS_LTFS, &fsa);
    assert_return_code(rc, -rc);

    pho_log_callback_set(capture_verb_cb);

    rc = fsa->ops->fs_mount("/dev/nst0", mnt_path, NULL, NULL);
    assert_return_code(rc, -rc);

    check_logged_lines_valid_utf8();

    pho_log_callback_set(NULL);
    g_list_free_full(verb_msgs, free);
    verb_msgs = NULL;

    rmdir(mnt_path);
}

int main(void)
{
    const struct CMUnitTest test_ltfs_output[] = {
        cmocka_unit_test(ltfs_format_invalid_utf8),
        cmocka_unit_test(ltfs_mount_invalid_utf8),
    };

    test_env_initialize();

    /* turn a stuck command_call() into a test failure instead of a hang */
    alarm(30);

    return cmocka_run_group_tests(test_ltfs_output, NULL, NULL);
}
