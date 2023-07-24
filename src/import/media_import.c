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
#include <string.h>

#include "pho_common.h"
#include "pho_srl_lrs.h"
#include "pho_ldm.h"
#include "admin_utils.h"
#include "raid1.h"
#include "io_posix_common.h"


/**
 * Update media_info stats and push its new state to the DSS
 * This function was copied from lrs_device.c, but it should be put in a
 * separated file common to both dss and ldm
 */
static int _dev_media_update(struct dss_handle *dss,
                             struct media_info *media_info,
                             size_t size_written, int media_rc,
                             const char *fsroot, long long nb_new_obj)
{
    struct ldm_fs_space space = {0};
    struct fs_adapter_module *fsa;
    uint64_t fields = 0;
    int rc2, rc = 0;

    if (media_info->fs.status == PHO_FS_STATUS_EMPTY && !media_rc) {
        media_info->fs.status = PHO_FS_STATUS_USED;
        fields |= FS_STATUS;
    }

    rc2 = get_fs_adapter(media_info->fs.type, &fsa);
    if (rc2) {
        rc = rc ? : rc2;
        pho_error(rc2,
                  "Invalid filesystem type for '%s' (database may be "
                  "corrupted)", fsroot);
        media_info->rsc.adm_status = PHO_RSC_ADM_ST_FAILED;
        fields |= ADM_STATUS;
    } else {
        struct pho_log log;
        struct pho_id dev = { .family = PHO_RSC_TAPE, .name = "" };

        init_pho_log(&log, &dev, &media_info->rsc.id, PHO_LTFS_DF);

        rc2 = ldm_fs_df(fsa, fsroot, &space, &log.message);
        emit_log_after_action(dss, &log, PHO_LTFS_DF, rc2);
        if (rc2) {
            rc = rc ? : rc2;
            pho_error(rc2, "Cannot retrieve media usage information");
            media_info->rsc.adm_status = PHO_RSC_ADM_ST_FAILED;
            fields |= ADM_STATUS;
        } else {
            media_info->stats.phys_spc_used = space.spc_used;
            media_info->stats.phys_spc_free = space.spc_avail;
            fields |= PHYS_SPC_USED | PHYS_SPC_FREE;
            if (media_info->stats.phys_spc_free == 0) {
                media_info->fs.status = PHO_FS_STATUS_FULL;
                fields |= FS_STATUS;
            }
        }
    }

    if (media_rc) {
        media_info->rsc.adm_status = PHO_RSC_ADM_ST_FAILED;
        fields |= ADM_STATUS;
    } else {
        if (nb_new_obj) {
            media_info->stats.nb_obj = nb_new_obj;
            fields |= NB_OBJ_ADD;
        }

        if (size_written) {
            media_info->stats.logc_spc_used = size_written;
            fields |= LOGC_SPC_USED_ADD;
        }
    }

    /* TODO update nb_load, nb_errors, last_load */

    assert(fields);
    rc2 = dss_media_set(dss, media_info, 1, DSS_SET_UPDATE, fields);
    if (rc2)
        rc = rc ? : rc2;

    return rc;
}

/**
 * This function takes a file name, and parses it several times to retrieve
 * data contained in an extent's file name.
 *
 * @param[in]   filename            Name of the file to extract info.
 * @param[out]  lyt_info            Contains information about the layout of
 *                                  the extent to add.
 * @param[out]  extent_to_insert    Contains information about the extent to
 *                                  add.
 * @param[out]  obj_info            Contains information about the object
 *                                  related to the extent to add.
 *
 * @return      0 on success,
 *              -EINVAL on failure.
 */

static int _get_info_from_filename(char *filename,
                                   struct layout_info *lyt_info,
                                   struct extent *extent_to_insert,
                                   struct object_info *obj_info)
{
    char **subgroups;
    char *repl_count;
    char *lyt_name;
    char *ext_idx;
    char *version;
    char *buffer;
    char *uuid;
    char *oid;

    /* A typical filename looks like:
     * oid.version.r1-1_0.uuid for raid1
     */
    subgroups = parse_str(filename, ".", 4);
    if (subgroups == NULL)
        return -EINVAL;

    oid = subgroups[0];
    version = subgroups[1];
    buffer = subgroups[2];
    uuid = subgroups[3];
    free(subgroups);
    subgroups = parse_str(buffer, "-_", 3);
    if (subgroups == NULL)
        return -EINVAL;

    lyt_name = subgroups[0];
    repl_count = subgroups[1];
    ext_idx = subgroups[2];
    free(subgroups);
    pho_debug("oid:%s, vers:%s, lyt-name:%s, repl_count:%s,"
              "extent_index=%s, uuid:%s",
              oid, version, lyt_name, repl_count, ext_idx, uuid);
    lyt_info->oid = oid;
    obj_info->oid = oid;
    lyt_info->uuid = uuid;
    obj_info->uuid = uuid;
    lyt_info->version = strtol(version, NULL, 10);
    obj_info->version = strtol(version, NULL, 10);

    static struct pho_attrs md;

    pho_attr_set(&md, "repl_count", repl_count);
    struct module_desc mod = {
        .mod_name = !strcmp(lyt_name, "r1") ? "raid1" : "inconnu",
        .mod_major = 0,
        .mod_minor = 2,
        .mod_attrs = md,
    };

    lyt_info->layout_desc = mod;
    extent_to_insert->layout_idx = strtol(ext_idx, NULL, 10);
    return 0;
}

/**
 * This function initializes the layout and extent information contained in
 * the extended attributes of a file whose file descriptor is given
 *
 * @param[in]   fd      File descriptor of the extent.
 * @param[out]  lyt     Contains information about the layout of the extent
 *                      to add.
 * @param[out]  ext     Contains information about the extent to add.
 * @param[out]  obj     Contains information about the object related to the
 *                      extent to add.
 * @param[out]  offset  Offset reference of the extent relative to the object.
 *
 * @return      0 on success,
 *              -errno on failure.
 */
static int _get_info_from_xattrs(int fd, struct layout_info *lyt,
                                 struct extent *ext, struct object_info *obj,
                                 int *offset)
{
    unsigned char *hash_buffer;
    char *buffer;
    int rc = 0;

    rc = pho_getxattr(NULL, fd, PHO_EA_MD5_NAME, &buffer);
    if (rc)
        return rc;
    if (!buffer) {
        ext->with_md5 = false;
    } else {
        hash_buffer = hex2uchar(buffer, MD5_BYTE_LENGTH);
        memcpy(ext->md5, hash_buffer, (MD5_BYTE_LENGTH + 1) * sizeof(char));
        free(buffer);
        free(hash_buffer);
        ext->with_md5 = true;
    }

    rc = pho_getxattr(NULL, fd, PHO_EA_XXH128_NAME, &buffer);
    if (rc)
        return rc;
    if (!buffer) {
        ext->with_xxh128 = false;
    } else {
        hash_buffer = hex2uchar(buffer, XXH128_BYTE_LENGTH);
        memcpy(ext->xxh128, hash_buffer,
               (XXH128_BYTE_LENGTH + 1) * sizeof(char));
        free(buffer);
        free(hash_buffer);
        ext->with_xxh128 = true;
    }

    rc = pho_getxattr(NULL, fd, PHO_EA_UMD_NAME, &buffer);
    if (rc)
        return rc;
    if (!buffer)
        LOG_RETURN(rc = -EINVAL, "Could not read user md");
    obj->user_md = strdup(buffer);
    free(buffer);
    if (!obj->user_md)
        LOG_RETURN(rc = -errno, "Could not duplicate user md");

    if (!strcmp(lyt->layout_desc.mod_name, "raid1")) {
        rc = pho_getxattr(NULL, fd, PHO_EA_OBJECT_SIZE_NAME, &buffer);
        if (rc)
            return rc;
        if (!buffer)
            LOG_RETURN(rc = -EINVAL, "raid 1 object size xattr not found");

        pho_attr_set(&lyt->layout_desc.mod_attrs, "raid1.obj_size", buffer);
        lyt->wr_size = strtol(buffer, NULL, 10);
        free(buffer);

        rc = pho_getxattr(NULL, fd, PHO_EA_EXTENT_OFFSET_NAME, &buffer);
        if (rc)
            return rc;
        if (!buffer)
            LOG_RETURN(rc = -EINVAL, "raid 1 extent offset xattr not found");

        *offset = strtol(buffer, NULL, 10);
        free(buffer);
    }

    return rc;
}

/*
 * Gives the list of the objects and deprecated objects matching a given uuid
 * and version.
 * \p in_obj and \p in_depr indicate whether the object to insert already exists
 * in the object table or the deprecated_object table.
 *
 * @param[in]    dss            Dss handle.
 * @param[in]    obj_to_insert  Object with the uuid and version to use.
 * @param[out]   in_obj         True if another object with same uuid and
 *                              version was detected in the DSS,
 * @param[out]   in_depr        True if another deprecated object with same
 *                              uuid and version was detected in the DSS.
 *
 * @return       0 on success,
 *               -errno on error.
 */
static int _get_objects_with_same_uuid_version(struct dss_handle *dss,
                                               struct object_info obj_to_insert,
                                               bool *in_obj, bool *in_depr)
{
    struct object_info *objects;
    struct dss_filter filter;
    int objects_count;
    int rc = 0;

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          " {\"DSS::OBJ::uuid\": \"%s\"},"
                          " {\"DSS::OBJ::version\": %d}"
                          "]}",
                          obj_to_insert.uuid,
                          obj_to_insert.version);
    if (rc)
        return rc;

    rc = dss_object_get(dss, &filter, &objects, &objects_count);
    if (rc) {
        dss_filter_free(&filter);
        return rc;
    }

    *in_obj = (objects_count > 0);
    dss_res_free(objects, objects_count);

    rc = dss_deprecated_object_get(dss, &filter, &objects, &objects_count);
    dss_filter_free(&filter);
    if (rc)
        return rc;

    *in_depr = (objects_count > 0);
    dss_res_free(objects, objects_count);

    return 0;
}

/*
 * Gives the list of the objects and deprecated objects matching a given oid.
 * On success, both obj_get and depr_get must be freed with dss_res_free()
 * afterwards.
 *
 * @param[in]    dss            Dss handle.
 * @param[in]    obj_to_insert  Object to compare to.
 * @param[out]   obj_get        Reference to the list of the objects of the
 *                              given oid.
 * @param[out]   obj_cnt        Number of objects with the given oid.
 * @param[out]   depr_get       Reference to the list of the deprecated objects
 *                              of the given oid.
 * @param[out]   depr_cnt       Number of objects with the given oid.
 *
 * @return       0 on success,
 *               -errno on error.
 */
static int _get_objects_with_oid(struct dss_handle *dss,
                                 struct object_info obj_to_insert,
                                 struct object_info **obj_get, int *obj_cnt,
                                 struct object_info **depr_get, int *depr_cnt)
{
    char *oid = obj_to_insert.oid;
    struct dss_filter filter;
    int rc = 0;

    rc = dss_filter_build(&filter, "{\"DSS::OBJ::oid\": \"%s\"}", oid);
    if (rc)
        return rc;

    rc = dss_object_get(dss, &filter, obj_get, obj_cnt);
    if (rc) {
        dss_filter_free(&filter);
        LOG_RETURN(rc, "Could not get object based on oid '%s'", oid);
    }

    rc = dss_deprecated_object_get(dss, &filter, depr_get, depr_cnt);
    dss_filter_free(&filter);
    if (rc) {
        dss_res_free(*obj_get, *obj_cnt);
        LOG_RETURN(rc,
                   "Could not get deprecated object based on oid '%s'", oid);
    }

    return rc;
}

/*
 * Adds a given extent to the dss.
 *
 * @param[in]   dss              The dss handle.
 * @param[in]   lyt_insert       The layout_info where to insert the extent to.
 * @param[in]   extent_to_insert The extent to insert in the dss.
 *
 * @return      0 on success
 *              -errno on failure.
 */
static int _add_extent_to_dss(struct dss_handle *dss,
                              struct layout_info lyt_insert,
                              struct extent extent_to_insert)
{
    struct layout_info *lyt_get;
    struct dss_filter filter;
    struct extent *extents;
    int ext_cnt = 0;
    int ext_lines;
    int rc = 0;
    int i;

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          " {\"DSS::LYT::object_uuid\": \"%s\"},"
                          " {\"DSS::LYT::version\": %d}"
                          "]}",
                          lyt_insert.uuid,
                          lyt_insert.version);
    if (rc)
        LOG_RETURN(rc, "Could not construct filter for extent");

    rc = dss_full_layout_get(dss, &filter, NULL, &lyt_get, &ext_lines);
    dss_filter_free(&filter);
    if (rc)
        LOG_RETURN(rc, "Could not get extent '%s'", lyt_insert.oid);

    if (ext_lines > 1)
        LOG_GOTO(lyt_info_get_free, rc = -ENOTSUP,
                 "Should not occur with current database version");

    if (ext_lines == 1)
        ext_cnt = lyt_get[0].ext_count;

    for (i = 0; i < ext_cnt; i++)
        if (lyt_get[0].extents[i].layout_idx == extent_to_insert.layout_idx)
            LOG_GOTO(lyt_info_get_free, rc = -EEXIST,
                    "Already existing extent detected");

    extents = xmalloc((ext_cnt + 1) * sizeof(struct extent));

    for (i = 0; i < ext_cnt; i++) {
        extents[i] = lyt_get[0].extents[i];
        extents[i].state = PHO_EXT_ST_SYNC;
    }

    extents[ext_cnt] = extent_to_insert;
    lyt_insert.extents = extents;
    lyt_insert.ext_count = ext_cnt + 1;
    rc = dss_layout_set(dss, &lyt_insert, 1,
                        ext_cnt == 0 ? DSS_SET_FULL_INSERT : DSS_SET_UPDATE);
    free(extents);

lyt_info_get_free:
    dss_res_free(lyt_get, ext_lines);
    return rc;
}

/**
 * Adds an object inside the DSS, depending on the version of the object,
 * and the presence of other objects or deprecated objects already in the DSS.
 *
 * @param[in]   dss             DSS handler,
 * @param[in]   obj_to_insert   The object to insert,
 * @param[in]   in_obj          True if another object with same oid, uuid and
 *                              version was detected in the DSS,
 * @param[in]   in_depr         True if another deprecated object with same
 *                              oid, uuid and version was detected in the DSS.
 *
 * @return      0 on success,
 *              -errno on failure.
 */
static int _add_obj_to_dss(struct dss_handle *dss,
                           struct object_info obj_to_insert)
{
    struct object_info *depr_obj_get = NULL;
    struct object_info *obj_get = NULL;
    bool is_already_inserted = false;
    int depr_obj_cnt = 0;
    int obj_cnt = 0;
    bool in_depr;
    bool in_obj;
    int rc = 0;
    int i;
    int j;

    rc = _get_objects_with_same_uuid_version(dss, obj_to_insert,
                                             &in_obj, &in_depr);
    if (rc)
        LOG_RETURN(rc,
                   "Could not get object and depr_objects for uuid '%s' and"
                   " version '%d'",
                   obj_to_insert.uuid, obj_to_insert.version);

    if (in_obj || in_depr)
        LOG_RETURN(rc = 0,
                   "Object '%s' with uuid '%s' and version '%d' already in DSS",
                   obj_to_insert.oid, obj_to_insert.uuid,
                   obj_to_insert.version);

    rc = _get_objects_with_oid(dss, obj_to_insert,
                               &obj_get, &obj_cnt,
                               &depr_obj_get, &depr_obj_cnt);
    if (rc)
        LOG_RETURN(rc, "Could not get object and depr_objects for oid '%s'",
                   obj_to_insert.oid);

    if (obj_cnt == 0 && depr_obj_cnt == 0) {
        rc = dss_object_set(dss, &obj_to_insert, 1, DSS_SET_FULL_INSERT);
        if (rc)
            LOG_GOTO(obj_get_free, rc = rc, "Could not set object");
    } else  {
        for (i = 0; i < obj_cnt; i++) {
            if (strcmp(obj_to_insert.uuid, obj_get[i].uuid))
                LOG_GOTO(obj_get_free, rc = -EINVAL, "An object with the same "
                         "oid but of a different generation already exists in "
                         "the object table");

            if (obj_to_insert.version > obj_get[i].version) {
                rc = dss_object_move(dss,
                                     DSS_OBJECT, DSS_DEPREC, obj_get + i, 1);
                if (rc)
                    LOG_GOTO(obj_get_free, rc = rc, "Could not move the "
                             "old object to the deprecated_object table");

                rc = dss_object_set(dss, &obj_to_insert, 1,
                                    DSS_SET_FULL_INSERT);
                if (rc)
                    LOG_GOTO(obj_get_free, rc = rc, "Could not set object");
            } else if (obj_to_insert.version < obj_get[i].version) {
                rc = dss_deprecated_object_set(dss, &obj_to_insert, 1,
                                               DSS_SET_INSERT);
                if (rc)
                    LOG_GOTO(obj_get_free, rc = rc, "Could not set deprecated "
                             "object");
            } else {
                pho_error(rc = 0, "should not happen");
            }

            is_already_inserted = true;
        }

        for (j = 0; j < depr_obj_cnt; j++) {
            if (obj_to_insert.version > depr_obj_get[j].version) {
                if (!is_already_inserted) {
                    rc = dss_object_set(dss, &obj_to_insert, 1,
                                        DSS_SET_FULL_INSERT);
                    if (rc)
                        LOG_GOTO(obj_get_free, rc = rc, "Could not set object");
                }
            } else if (obj_to_insert.version < depr_obj_get[j].version) {
                if (is_already_inserted) {
                    rc = dss_object_move(dss, DSS_OBJECT, DSS_DEPREC,
                                         &obj_to_insert, 1);
                    if (rc)
                        LOG_GOTO(obj_get_free, rc = rc,
                                 "Could not move object to deprecated table");

                } else {
                    rc = dss_deprecated_object_set(dss, &obj_to_insert, 1,
                                                   DSS_SET_INSERT);
                    if (rc)
                        LOG_GOTO(obj_get_free, rc = rc,
                                 "Could not set object to deprecated table");
                }
            } else {
                pho_error(rc = 0, "should not happen");
            }
        }
    }

obj_get_free:
    dss_res_free(obj_get, obj_cnt);
    dss_res_free(depr_obj_get, depr_obj_cnt);
    return rc;
}

/**
 * Imports the file of a given file descriptor and its information contained in
 * its xattrs or in its name to the DSS (add in the extent and
 * object/deprecated_object tables)
 */
static int _import_file_to_dss(struct admin_handle *adm, int fd,
                               char *rootpath, char *filename, off_t fsize,
                               int height, struct pho_id med_id,
                               size_t *size_written, long long *nb_new_obj)
{
    struct layout_info lyt_to_insert;
    struct object_info obj_to_insert;
    struct extent ext_to_insert;
    int offset;
    int rc = 0;

    // Get info from the extent name
    rc = _get_info_from_filename(filename, &lyt_to_insert, &ext_to_insert,
                                 &obj_to_insert);
    if (rc)
        LOG_RETURN(rc, "Could not get info from filename");

    *nb_new_obj += 1;
    *size_written += fsize;
    ext_to_insert.size = fsize;
    ext_to_insert.media = med_id;
    ext_to_insert.address = PHO_BUFF_NULL;
    ext_to_insert.address.buff = rootpath;

    // Get info from the extent's xattrs
    rc = _get_info_from_xattrs(fd, &lyt_to_insert, &ext_to_insert,
                               &obj_to_insert, &offset);
    if (rc)
        LOG_RETURN(rc, "Could not get info from xattrs");

    obj_to_insert.obj_status = PHO_OBJ_STATUS_INCOMPLETE;

    rc = dss_lock(&adm->dss, DSS_OBJECT, &obj_to_insert, 1);
    if (rc)
        LOG_RETURN(rc, "Unable to lock object objid: '%s'", obj_to_insert.oid);

    // Add the object to the DSS
    rc = _add_obj_to_dss(&adm->dss, obj_to_insert);
    if (rc)
        LOG_RETURN(rc, "Could not add object to DSS");

    rc = dss_unlock(&adm->dss, DSS_OBJECT, &obj_to_insert, 1, false);
    if (rc)
        LOG_RETURN(rc, "Unable to unlock object objid: '%s'",
                   obj_to_insert.oid);

    // Add the extent to the DSS
    rc = _add_extent_to_dss(&adm->dss, lyt_to_insert, ext_to_insert);
    if (rc)
        LOG_RETURN(rc, "Could not add extent to DSS");

    return rc;
}

/**
 * Auxilary function of explore_from_path
 */
static int _explore_from_path_aux(struct admin_handle *adm,
                                  char *root_path, char *address, int height,
                                  struct pho_id med_id,
                                  int (*func)(struct admin_handle *, int,
                                              char *, char *, off_t, int,
                                              struct pho_id, size_t *,
                                              long long *),
                                  size_t *size_written, long long *nb_new_obj)
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
        char *address_buf;
        char *path;
        int fd;

        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        rc = asprintf(&path, "%s/%s", root_path, entry->d_name);
        if (rc < 0)
            LOG_GOTO(free_dirent, rc = -ENOMEM,
                     "Could not alloc memory for path");

        fd = open(path, O_RDONLY);
        if (height == 0)
            rc = asprintf(&address_buf, "%s", entry->d_name);
        else
            rc = asprintf(&address_buf, "%s/%s", address, entry->d_name);

        if (rc < 0)
            LOG_GOTO(free_dirent, rc = -ENOMEM,
                     "Could not alloc memory for address");

        if (fd < 0)
            LOG_GOTO(free_elts, rc = -errno, "Could not open the file");

        rc = fstat(fd, &stat_buf);
        if (rc)
            LOG_GOTO(free_elts, rc = -errno, "Could not stat the file");

        if (S_ISDIR(stat_buf.st_mode))
            _explore_from_path_aux(adm, path, address_buf, height + 1,
                                   med_id, (*func), size_written, nb_new_obj);
        else {
            rc = (func)(adm, fd, address_buf, entry->d_name, stat_buf.st_size,
                        height, med_id, size_written, nb_new_obj);
            if (rc)
                pho_error(rc, "Could not extract information from the file,"
                          "rc:%d", rc);
        }
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

/**
 * This function recursively explores a directory from its root path to execute
 * a specified function to the files found during the process.
 *
 * @param[in]   adm          Admin handle,
 * @param[in]   root_path    Path from which the exploration starts,
 * @param[in]   func         Function to call each time a new file is found,
 * @param[out]  size_written The total size written on this tape (sum of size of
 *                           the extents),
 * @param[out]  nb_new_obj   The number of objects written on this tape.
 *
 * @return      0 on success,
 *              -errno on failure.
 */
static int explore_from_path(struct admin_handle *adm, char *root_path,
                             struct pho_id med_id,
                             int (*func)(struct admin_handle *, int, char *,
                                         char *, off_t, int, struct pho_id,
                                         size_t *, long long *),
                             size_t *size_written, long long *nb_new_obj)
{
   return _explore_from_path_aux(adm, root_path, "", 0, med_id, (*func),
                                 size_written, nb_new_obj);
}

int pho_import_medium(struct admin_handle *adm, struct media_info medium,
                      bool check_hash)
{
    struct pho_id id = medium.rsc.id;
    enum address_type addr_type;
    long long nb_new_obj = 0;
    size_t size_written = 0;
    enum fs_type fs_type;
    size_t label_length;
    pho_resp_t *resp;
    pho_req_t *reqs;
    char *root_path;
    int rc = 0;

    // fs.label field of the tape
    label_length = (strlen(id.name) < PHO_LABEL_MAX_LEN ? strlen(id.name) :
                                                          PHO_LABEL_MAX_LEN);
    memcpy(medium.fs.label, id.name, label_length);
    medium.fs.label[label_length] = 0;

    rc = dss_media_set(&adm->dss, &medium, 1, DSS_SET_UPDATE, FS_LABEL);

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
    rc = explore_from_path(adm, root_path, id, _import_file_to_dss,
                           &size_written, &nb_new_obj);

    // fs_df to actualize the stats of the tape
    rc = _dev_media_update(&adm->dss, &medium, size_written, rc, root_path,
                           nb_new_obj);

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

    return rc;
}
