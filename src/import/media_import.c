/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2023 CEA/DAM.
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
 * \brief  Phobos Administration interface
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "phobos_admin.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>

#include "pho_common.h"
#include "pho_srl_lrs.h"
#include "admin_utils.h"

/**
 * This function takes a file name, and parses it several times to retrieve
 * data contained in an extent's file name.
 *
 * @param[in] filename  Name of the file to extract info.
 */
static void _get_info_from_file_name(char *filename)
{
    /* This function is incomplete and will be modified in the near future */
    char *extent_index;
    char *repl_count;
    char **subgroups;
    char *lyt_name;
    char *version;
    char *buffer;
    char *uuid;
    char *oid;

    /* A typical filename looks like:
     * oid.version.r1-1_0.uuid for raid1
     */
    subgroups = parse_str(filename, ".", 4);
    if (subgroups == NULL)
        return;

    oid = subgroups[0];
    version = subgroups[1];
    buffer = subgroups[2];
    uuid = subgroups[3];
    free(subgroups);
    subgroups = parse_str(buffer, "-_", 3);
    if (subgroups == NULL)
        return;

    lyt_name = subgroups[0];
    repl_count = subgroups[1];
    extent_index = subgroups[2];
    free(subgroups);
    pho_debug("oid:%s, vers:%s, lyt-name:%s, repl_count:%s,"
              "extent_index=%s, uuid:%s",
              oid, version, lyt_name, repl_count, extent_index, uuid);
}

/**
 * This function recursively explores a directory from its root path to execute
 * a specified function to the files found during the process.
 *
 * @param[in]   root_path   Path from which the exploration starts,
 * @param[in]   func        Function to call each time a new file is found.
 *
 * @return      0 on success,
 *              -errno on failure.
 */
static int explore_from_path(char *root_path, void (*func)(char *))
{
    struct dirent *entry;
    int rc2 = 0;
    int rc = 0;
    DIR *dir;

    dir = opendir(root_path);
    if (!dir)
        return -errno;

    while ((entry = readdir(dir)) != NULL) {
        struct stat stat_buf;
        char *path;
        int fd;

        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        rc = asprintf(&path, "%s/%s", root_path, entry->d_name);
        if (rc < 0)
            LOG_GOTO(free_dirent, rc = -ENOMEM,
                     "Could not alloc memory for path");

        fd = open(path, O_PATH);
        if (fd < 0)
            LOG_GOTO(free_elts, rc = -errno, "Could not open the file");

        rc = fstat(fd, &stat_buf);
        if (rc)
            LOG_GOTO(free_elts, rc = -errno, "Could not stat the file");

        if (S_ISDIR(stat_buf.st_mode))
            explore_from_path(path, (*func));
        else
            (func)(entry->d_name);

free_elts:
        rc2 = close(fd);
        if (rc2)
            pho_error(-errno, "Could not close the file");

        free(path);
        if (rc || rc2)
            break;
    }

free_dirent:
    rc = rc ? : rc2;
    rc2 = closedir(dir);
    if (rc2) {
        rc2 = rc2 ? : -errno;
        pho_error(rc2, "Could not close dir");
    }

    // The first error encountered is kept
    rc = rc ? : rc2;
    return rc;
}

int pho_import_medium(struct admin_handle *adm, struct media_info medium,
                      bool check_hash)
{
    struct pho_id id = medium.rsc.id;
    enum address_type addr_type;
    enum fs_type fs_type;
    pho_resp_t *resp;
    pho_req_t *reqs;
    char *root_path;
    int rc = 0;

    // One request to read the tape, one request to release it afterwards
    reqs = malloc(2 * sizeof(pho_req_t));
    if (reqs == NULL)
        LOG_RETURN(-ENOMEM, "Could not allocate memory for requests");

    pho_srl_request_read_alloc(reqs, 1);

    reqs[0].id = 0;
    reqs[0].ralloc->n_required = 1;
    reqs[0].ralloc->med_ids[0]->family = id.family;
    reqs[0].ralloc->med_ids[0]->name = strdup(id.name);

    rc = _send_and_receive(&adm->phobosd_comm, &reqs[0], &resp);
    if (rc)
        LOG_GOTO(request_free, rc,
                 "Failed to send or receive read request for %s",
                 id.name);

    if (pho_response_is_error(resp)) {
        rc = resp->error->rc;
        LOG_GOTO(response_free, rc, "Received error response to read request");
    } else if (!pho_response_is_read(resp) ||
               reqs[0].id != resp->req_id) {
        LOG_GOTO(response_free, -EINVAL,
                 "Received a wrong response to the read request");
    } else if (resp->ralloc->n_media != 1) {
        LOG_GOTO(response_free, -EINVAL, "1 medium required");
    }

    // The medium has been successfully read
    root_path = resp->ralloc->media[0]->root_path;
    fs_type = resp->ralloc->media[0]->fs_type;
    addr_type = resp->ralloc->media[0]->addr_type;

    pho_verb("Successfully mounted tape %s to %s",
             id.name, root_path);
    pho_debug("fs_type:%s, med_id:%s, addr_type:%s",
              fs_type2str(fs_type),
              resp->ralloc->media[0]->med_id->name,
              address_type2str(addr_type));

    // Exploration of the tape
    rc = explore_from_path(root_path, _get_info_from_file_name);

    // Release of the medium
    pho_srl_request_release_alloc(reqs + 1, 1);

    reqs[1].id = 1;
    reqs[1].release->media[0]->med_id->family = id.family;
    reqs[1].release->media[0]->med_id->name = strdup(id.name);
    reqs[1].release->media[0]->size_written = 0;
    reqs[1].release->media[0]->rc = 0;
    reqs[1].release->media[0]->to_sync = false;

    rc = _send(&adm->phobosd_comm, &reqs[1]);
    if (rc)
        pho_error(rc, "Failed to send release request");
    pho_srl_request_free(reqs + 1, false);

response_free:
    pho_srl_response_free(resp, false);

request_free:
    pho_srl_request_free(reqs, false);
    free(reqs);

    return -ENOSYS;
}
