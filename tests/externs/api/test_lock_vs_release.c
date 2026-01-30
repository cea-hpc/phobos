/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * All rights reserved (c) 2014-2026 CEA/DAM.
 *
 * This file is part of Phobos.
 *
 * Phobos is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * Phobos is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Phobos. If not, see <http://www.gnu.org/licenses/>.
 */
/**
 * This test checks that we can concurrently removes dir and put new objects.
 *
 * We create 25 + 10 dirs with a specific tag on the last ten dirs.
 *
 * 10 putter threads loops to create object each on their dedicated dir among
 * the last 10 ones.
 * Concurrently to the active puts, a thread is locking the 25 first dirs to
 * delete them from the LRS.
 */

#define _GNU_SOURCE /* for asprintf */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <phobos_admin.h>
#include <phobos_store.h>
#include <pho_type_utils.h>

#define NB_DELETED_DIR 25
#define NB_PUT_DIR 10
#define NB_PUT 10

#define DELETED_DIR_ROOT_NAME "deleted_dir"
#define PUT_DIR_ROOT_NAME "put_dir"
#define OBJECT_SIZE 1024 /* input file and object size of 1k bytes */

#define DEFAULT_LIBRARY "legacy"

static void error(int rc, const char *msg, ...)
{
    va_list args;

    fprintf(stderr, "ERROR %d, %s: ", rc, strerror(rc));
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);

    fprintf(stderr, "\n");
    exit(rc);
}

static inline void dir_path(const char *dir_root_name, int index, char **dp)
{
    asprintf(dp, "%s_%d", dir_root_name, index);
}

static void phobos_dir_add(struct admin_handle *adm, const char *path,
                           char *tag)
{
    struct media_info dir_info = {0};
    int rc;

    dir_info.rsc.id.family = PHO_RSC_DIR;
    pho_id_name_set(&dir_info.rsc.id, path, DEFAULT_LIBRARY);
    dir_info.flags.put = true;
    dir_info.flags.get = true;
    dir_info.flags.delete = true;

    if (tag)
        string_array_init(&dir_info.tags, &tag, 1);

    rc = phobos_admin_media_add(adm, &dir_info, 1);
    if (rc)
        error(-rc, "Unable to add dir medium %s", path);

    rc = phobos_admin_device_add(adm, &dir_info.rsc.id, 1, false,
                                 DEFAULT_LIBRARY);
    if (rc)
        error(-rc, "Unable to add dir device %s", path);

    /* phobos_admin_device_add is modifying id name */
    pho_id_name_set(&dir_info.rsc.id, path, DEFAULT_LIBRARY);
    rc = phobos_admin_format(adm, &dir_info.rsc.id, 1, 0, PHO_FS_POSIX, true,
                             false);
    if (rc)
        error(-rc, "Unable to format dir %s", path);

    if (tag)
        string_array_free(&dir_info.tags);
}

static void add_dirs(struct admin_handle *adm, const char *dir_root_name,
                     int nb_dirs, bool tag_from_index)
{
    int i;

    for (i = 0; i < nb_dirs; i++) {
        char *new_dir_path;
        char *tag = NULL;

        dir_path(dir_root_name, i, &new_dir_path);
        if (mkdir(new_dir_path, 0700))
            error(errno, "unable to create dir %d (%s)", i, new_dir_path);

        if (tag_from_index)
            asprintf(&tag, "%d", i);

        phobos_dir_add(adm, new_dir_path, tag);

        if (tag_from_index)
            free(tag);

        free(new_dir_path);
    }
}

static void *putter(void *_arg)
{
    char *dir_tag = (char *) _arg;

    struct pho_xfer_target target = {0};
    struct pho_xfer_desc xfer = {0};
    int i;

    /* prepare target */
    target.xt_fd = open(dir_tag, O_RDWR | O_CREAT, 0600);
    for (i = 0; i < OBJECT_SIZE; i++)
        write(target.xt_fd, &dir_tag[0], 1);

    target.xt_objid = dir_tag;
    target.xt_size = OBJECT_SIZE;

    /* prepare the xfer we reuse with overwrite == true */
    xfer.xd_op = PHO_XFER_OP_PUT;
    xfer.xd_params.put.family = PHO_RSC_DIR;
    string_array_init(&xfer.xd_params.put.tags, &dir_tag, 1);
    xfer.xd_params.put.profile = "simple";
    xfer.xd_params.put.overwrite = true;
    xfer.xd_ntargets = 1;
    xfer.xd_targets = &target;

    /* put loop */
    for (i = 0; i < NB_PUT; i++) {
        int rc;

        lseek(target.xt_fd, 0, SEEK_SET);
        rc = phobos_put(&xfer, 1, NULL, NULL);
        if (rc)
            error(-rc, "Putter %s error on put %d", dir_tag, i);

        pho_xfer_clean(xfer.xd_targets);
    }

    close(target.xt_fd);
    pho_xfer_desc_clean(&xfer);
    pthread_exit(NULL);
}

struct dir_deleter_arg {
    struct admin_handle *adm;
    const char *dir_root_name;
};

static void *dir_deleter(void *_arg)
{
    struct dir_deleter_arg *arg = (struct dir_deleter_arg *)_arg;
    int i;

    for (i = 0; i < NB_DELETED_DIR; i++) {
        struct pho_id dir_id;
        char *path;
        int rc;

        dir_path(arg->dir_root_name, i, &path);
        dir_id.family = PHO_RSC_DIR;
        pho_id_name_set(&dir_id, path, DEFAULT_LIBRARY);
        free(path);
        rc = phobos_admin_device_lock(arg->adm, &dir_id, 1, true);
        if (rc)
            error(-rc, "Unable to lock the dir %d", i);
    }

    pthread_exit(NULL);
}

int main(int argc, char **argv)
{
    struct dir_deleter_arg dir_deleter_arg;
    pthread_t putter_id[NB_PUT_DIR];
    char *dir_tag[NB_PUT_DIR];
    pthread_t dir_deleter_id;
    struct admin_handle adm;
    int rc;
    int i;

    /* moving to working directory target */
    if (argc < 2)
        error(EINVAL, "usage: %s working_directory_path", argv[0]);

    if (chdir(argv[1]))
        error(errno,
              "Unable to change current working directory to %s", argv[1]);

    /* phobos initialization */
    rc = phobos_init();
    if (rc)
        error(rc, "Error when initializing phobos");

    rc = phobos_admin_init(&adm, true, NULL);
    if (rc)
        error(rc, "Error when initializing phobos admin");

    /* dirs initialization */
    add_dirs(&adm, DELETED_DIR_ROOT_NAME, NB_DELETED_DIR,
             false);
    add_dirs(&adm, PUT_DIR_ROOT_NAME, NB_PUT_DIR, true);
    printf("Dirs added");

    /* putter start */
    for (i = 0; i < NB_PUT_DIR; i++) {
        asprintf(&dir_tag[i], "%d", i);
        pthread_create(&putter_id[i], NULL, putter, dir_tag[i]);
    }

    /* dir_deleter start */
    dir_deleter_arg.adm = &adm;
    dir_deleter_arg.dir_root_name = DELETED_DIR_ROOT_NAME;
    pthread_create(&dir_deleter_id, NULL, dir_deleter, &dir_deleter_arg);

    /* putter join */
    for (i = 0; i < NB_PUT_DIR; i++) {
        pthread_join(putter_id[i], NULL);
        free(dir_tag[i]);
    }

    /* dir deleter join */
    pthread_join(dir_deleter_id, NULL);

    exit(EXIT_SUCCESS);
}
