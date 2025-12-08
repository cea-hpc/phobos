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
#include <time.h>

#include "pho_comm_wrapper.h"
#include "pho_common.h"
#include "pho_attrs.h"
#include "pho_dss_wrapper.h"
#include "pho_srl_lrs.h"
#include "pho_layout.h"
#include "pho_ldm.h"
#include "pho_type_utils.h"
#include "pho_cfg.h"

#include "import.h"
#include "io_posix_common.h"

/**
 * Update media_info stats and push its new state to the DSS
 *
 * \param[in] dss           DSS's handle
 * \param[in] media_info    Medium with the stats to update
 * \param[in] size_written  Size written on the medium
 * \param[in] media_rc      If non-zero, the medium should be marked as failed
 * \param[in] fsroot        Root of the medium's filesystem
 * \param[in] nb_new_obj    Number of objects on the medium
 *
 * \return 0 if the stat retrieval and update were successfull,
 *         non-zero otherwise
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

    if (media_info->fs.status == PHO_FS_STATUS_IMPORTING && !media_rc) {
        media_info->fs.status = (nb_new_obj == 0 ? PHO_FS_STATUS_EMPTY :
                                                   PHO_FS_STATUS_USED);
        fields |= FS_STATUS;
    }

    rc = get_fs_adapter(media_info->fs.type, &fsa);
    if (rc) {
        pho_error(rc,
                  "Invalid filesystem type for '%s' (database may be "
                  "corrupted)", fsroot);
        media_info->rsc.adm_status = PHO_RSC_ADM_ST_FAILED;
        fields |= ADM_STATUS;
    } else {
        struct pho_log log;
        struct pho_id dev = { .family = PHO_RSC_TAPE, .name = "",
                              .library = ""};

        init_pho_log(&log, &dev, &media_info->rsc.id, PHO_LTFS_DF);

        rc = ldm_fs_df(fsa, fsroot, &space, &log.message);
        emit_log_after_action(dss, &log, PHO_LTFS_DF, rc);
        if (rc) {
            pho_error(rc, "Cannot retrieve media usage information");
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
    rc2 = dss_media_update(dss, media_info, media_info, 1, fields);
    if (rc2)
        rc = rc ? : rc2;

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
static int
_objects_with_same_uuid_version_exist(struct dss_handle *dss,
                                      struct object_info *object_to_find,
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
                          object_to_find->uuid,
                          object_to_find->version);
    if (rc)
        return rc;

    rc = dss_object_get(dss, &filter, &objects, &objects_count, NULL);
    if (rc) {
        dss_filter_free(&filter);
        return rc;
    }

    *in_obj = (objects_count > 0);
    if (*in_obj)
        object_to_find->oid = xstrdup(objects[0].oid);

    dss_res_free(objects, objects_count);

    rc = dss_deprecated_object_get(dss, &filter, &objects, &objects_count,
                                   NULL);
    dss_filter_free(&filter);
    if (rc)
        return rc;

    *in_depr = (objects_count > 0);
    if (*in_depr)
        object_to_find->oid = xstrdup(objects[0].oid);

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
                                 struct object_info *obj_to_insert,
                                 struct object_info **obj_get, int *obj_cnt,
                                 struct object_info **depr_get, int *depr_cnt)
{
    char *oid = obj_to_insert->oid;
    struct dss_filter filter;
    int rc = 0;

    rc = dss_filter_build(&filter, "{\"DSS::OBJ::oid\": \"%s\"}", oid);
    if (rc)
        return rc;

    rc = dss_object_get(dss, &filter, obj_get, obj_cnt, NULL);
    if (rc) {
        dss_filter_free(&filter);
        LOG_RETURN(rc, "Could not get object based on oid '%s'", oid);
    }

    rc = dss_deprecated_object_get(dss, &filter, depr_get, depr_cnt, NULL);
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
                              struct layout_info *lyt_insert,
                              struct extent *extent_to_insert)
{
    struct layout_info *lyt_get;
    struct dss_filter filter;
    int layout_count;
    int ext_cnt = 0;
    int rc = 0;
    int i;

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                              "{\"DSS::LYT::object_uuid\": \"%s\"}, "
                              "{\"DSS::LYT::version\": \"%d\"},"
                              "{\"DSS::LYT::copy_name\": \"%s\"}"
                          "]}", lyt_insert->uuid, lyt_insert->version,
                          lyt_insert->copy_name);
    if (rc)
        LOG_RETURN(rc, "Could not construct filter for extent");

    rc = dss_full_layout_get(dss, &filter, NULL, &lyt_get, &layout_count, NULL);
    dss_filter_free(&filter);
    if (rc)
        LOG_RETURN(rc, "Could not get extent '%s'", lyt_insert->oid);

    if (layout_count > 1)
        LOG_GOTO(lyt_info_get_free, rc = -ENOTSUP,
                 "UUID '%s', version '%d' and copy_name '%s' should uniquely "
                 "identify a layout, found '%d' layouts matching",
                 lyt_insert->uuid, lyt_insert->version, lyt_insert->copy_name,
                 layout_count);

    if (layout_count == 1)
        ext_cnt = lyt_get[0].ext_count;

    for (i = 0; i < ext_cnt; i++)
        if (lyt_get[0].extents[i].layout_idx == extent_to_insert->layout_idx)
            LOG_GOTO(lyt_info_get_free, rc = -EEXIST,
                     "Already existing extent detected");

    lyt_insert->extents = extent_to_insert;
    lyt_insert->ext_count = 1;

    rc = dss_extent_insert(dss, extent_to_insert, 1, DSS_SET_FULL_INSERT);
    if (rc)
        LOG_GOTO(lyt_info_get_free, rc,
                 "Failed to insert extent '%s'", extent_to_insert->uuid);

    rc = dss_layout_insert(dss, lyt_insert, 1);

lyt_info_get_free:
    dss_res_free(lyt_get, layout_count);
    return rc;
}

static int
insert_object_with_different_uuid(struct dss_handle *dss,
                                  struct object_info *obj_to_insert,
                                  struct copy_info *copy_to_insert,
                                  struct object_info *depr_obj_get,
                                  int depr_obj_cnt)
{
    time_t current_time = time(NULL);
    bool found_in_depr = false;
    char *modified_oid = NULL;
    struct object_info *obj;
    int rc;
    int i;

    for (i = 0; i < depr_obj_cnt; ++i) {
        if (strcmp(obj_to_insert->uuid, depr_obj_get[i].uuid) == 0) {
            found_in_depr = true;
            break;
        }
    }

    if (found_in_depr) {
        rc = dss_deprecated_object_insert(dss, obj_to_insert, 1);
        if (rc)
            pho_error(rc, "Could not set deprecated object");

        rc = dss_copy_insert(dss, copy_to_insert, 1);
        if (rc)
            pho_error(rc, "Could not set copy");

        return rc;
    }

    // This will be freed after the extent is inserted
    rc = asprintf(&modified_oid, "%s.import-%ld",
                  obj_to_insert->oid, (intmax_t) current_time);
    if (rc < 1)
        LOG_RETURN(rc = -ENOMEM, "Could not create new object name");

    // This will be changed back after the extent is inserted
    obj_to_insert->oid = modified_oid;

    rc = dss_object_insert(dss, obj_to_insert, 1, DSS_SET_FULL_INSERT);
    if (rc)
        pho_error(rc, "Could not create new object");

    rc = dss_lazy_find_object(dss, obj_to_insert->oid, NULL, 0, &obj);
    if (rc)
        LOG_RETURN(rc, "Could not get new object imported");

    copy_to_insert->object_uuid = obj->uuid;
    rc = dss_copy_insert(dss, copy_to_insert, 1);
    if (rc)
        pho_error(rc, "Could not set copy");

    object_info_free(obj);

    return rc;
}

/**
 * Adds an object inside the DSS, depending on the version of the object,
 * and the presence of other objects or deprecated objects already in the DSS.
 *
 * @param[in]   dss             DSS handler,
 * @param[in]   obj_to_insert   The object to insert,
 *
 * @return      0 on success,
 *              -errno on failure.
 */
static int _add_object_to_dss(struct dss_handle *dss,
                              struct object_info *object_to_insert,
                              struct copy_info *copy_to_insert)
{
    struct object_info *deprecated_objects = NULL;
    struct object_info *objects = NULL;
    int deprecated_objects_count = 0;
    int objects_count = 0;
    bool in_depr;
    bool in_obj;
    int rc = 0;

    rc = _objects_with_same_uuid_version_exist(dss, object_to_insert,
                                               &in_obj, &in_depr);
    if (rc)
        LOG_RETURN(rc,
                   "Could not get object and depr_objects for uuid '%s' and"
                   " version '%d'",
                   object_to_insert->uuid, object_to_insert->version);

    if (in_obj || in_depr) {
        pho_verb("Object '%s' with uuid '%s' and version '%d' already in DSS",
                 object_to_insert->oid, object_to_insert->uuid,
                 object_to_insert->version);
        return 0;
    }

    rc = _get_objects_with_oid(dss, object_to_insert,
                               &objects, &objects_count,
                               &deprecated_objects, &deprecated_objects_count);
    if (rc)
        LOG_RETURN(rc, "Could not get object and depr_objects for oid '%s'",
                   object_to_insert->oid);

    if (objects_count == 0 && deprecated_objects_count == 0) {
        rc = dss_object_insert(dss, object_to_insert, 1, DSS_SET_FULL_INSERT);
        if (rc)
            pho_error(rc,
                      "Could not insert object with oid '%s', uuid '%s' and "
                      "version '%d'", object_to_insert->oid,
                      object_to_insert->uuid, object_to_insert->version);

        rc = dss_copy_insert(dss, copy_to_insert, 1);
        if (rc)
            pho_error(rc,
                      "Could not insert copy with uuid '%s', version '%d' and "
                      "copy_name '%s'", copy_to_insert->object_uuid,
                      copy_to_insert->version, copy_to_insert->copy_name);

        goto objects_free;
    }

    if (objects_count == 1) {
        if (strcmp(object_to_insert->uuid, objects->uuid)) {
            rc = insert_object_with_different_uuid(dss, object_to_insert,
                                                   copy_to_insert,
                                                   deprecated_objects,
                                                   deprecated_objects_count);
            if (rc)
                LOG_GOTO(objects_free, rc,
                         "Could not insert object '%s' with different uuid: "
                         "uuid to insert = '%s' vs uuid of object = '%s'",
                         object_to_insert->oid, object_to_insert->uuid,
                         objects->uuid);
        } else {
            if (object_to_insert->version > objects->version) {
                rc = dss_move_object_to_deprecated(dss, objects, 1);
                if (rc)
                    LOG_GOTO(objects_free, rc,
                             "Could not move old object '%s' to deprecated",
                             objects->oid);

                rc = dss_object_insert(dss, object_to_insert, 1,
                                       DSS_SET_FULL_INSERT);
                if (rc)
                    LOG_GOTO(objects_free, rc,
                             "Could not insert object '%s' after moving one "
                             "with same oid to deprecated",
                             object_to_insert->oid);
            } else {
                rc = dss_deprecated_object_insert(dss, object_to_insert, 1);
                if (rc)
                    LOG_GOTO(objects_free, rc,
                             "Could not insert deprecated object '%s'",
                             object_to_insert->oid);
            }

            rc = dss_copy_insert(dss, copy_to_insert, 1);
            if (rc)
                LOG_GOTO(objects_free, rc, "Could not insert copy");
        }

        goto objects_free;
    }

    assert(objects_count == 0);

    rc = dss_object_insert(dss, object_to_insert, 1, DSS_SET_INSERT);
    if (rc)
        LOG_GOTO(objects_free, rc, "Could not insert deprecated object '%s'",
                 object_to_insert->oid);

    rc = dss_copy_insert(dss, copy_to_insert, 1);
    if (rc)
        LOG_GOTO(objects_free, rc, "Could not insert copy");

objects_free:
    dss_res_free(objects, objects_count);
    dss_res_free(deprecated_objects, deprecated_objects_count);
    return rc;
}

/**
 * Imports the file of a given file descriptor and its information contained in
 * its xattrs or in its name to the DSS (add in the extent and
 * object/deprecated_object tables)
 */
static int _import_file_to_dss(struct admin_handle *adm, int fd,
                               char *rootpath, char *filename, off_t fsize,
                               struct timespec f_ctime, int height,
                               struct pho_id med_id, size_t *size_written,
                               long long *nb_new_obj)
{
    struct object_info obj_to_insert = {0};
    struct extent ext_to_insert = {0};
    struct layout_info lyt_to_insert;
    struct copy_info copy_to_insert;
    struct io_adapter_module *ioa;
    struct pho_io_descr iod;
    struct pho_ext_loc loc;
    char *save_oid;
    int rc = 0;

    rc = get_io_adapter(PHO_FS_LTFS, &ioa);
    if (rc)
        LOG_RETURN(rc,
                   "Failed to get LTFS I/O adapter to import tape (name '%s', "
                   "library '%s')",
                   med_id.name, med_id.library);

    iod.iod_size = fsize;
    iod.iod_fd = fd;
    loc.addr_type = PHO_ADDR_PATH;
    loc.root_path = rootpath;
    loc.extent = &ext_to_insert;
    iod.iod_loc = &loc;
    ext_to_insert.address.buff = filename;

    rc = ioa_get_common_xattrs_from_extent(ioa, &iod, &lyt_to_insert,
                                           &ext_to_insert, &obj_to_insert);
    if (rc)
        LOG_RETURN(rc,
                   "Failed to retrieve every common xattrs from file '%s/%s', "
                   "the object and extent will not be added to the DSS",
                   rootpath, filename);

    rc = layout_get_specific_attrs(&iod, ioa, &ext_to_insert, &lyt_to_insert);
    if (rc)
        LOG_RETURN(rc,
                   "Failed to retrieve every layout specific xattrs from file "
                   "'%s/%s', the object and extent will not be added to the "
                   "DSS",
                   rootpath, filename);

    *nb_new_obj += 1;
    *size_written += fsize;
    ext_to_insert.size = fsize;
    ext_to_insert.media = med_id;
    ext_to_insert.address = PHO_BUFF_NULL;
    ext_to_insert.address.buff = xstrdup(rootpath);
    ext_to_insert.state = PHO_EXT_ST_SYNC;
    ext_to_insert.creation_time.tv_sec = f_ctime.tv_sec;
    ext_to_insert.creation_time.tv_usec = f_ctime.tv_nsec / 1000;

    copy_to_insert.copy_name = lyt_to_insert.copy_name;
    copy_to_insert.object_uuid = obj_to_insert.uuid;
    copy_to_insert.version = obj_to_insert.version;
    copy_to_insert.copy_status = PHO_COPY_STATUS_INCOMPLETE;

    rc = dss_lock(&adm->dss, DSS_OBJECT, &obj_to_insert, 1);
    if (rc)
        LOG_RETURN(rc, "Unable to lock object objid: '%s'", obj_to_insert.oid);

    save_oid = obj_to_insert.oid;

    rc = _add_object_to_dss(&adm->dss, &obj_to_insert, &copy_to_insert);
    if (rc)
        LOG_RETURN(rc, "Could not add object to DSS");

    lyt_to_insert.oid = obj_to_insert.oid;

    rc = _add_extent_to_dss(&adm->dss, &lyt_to_insert, &ext_to_insert);
    if (rc)
        LOG_RETURN(rc, "Could not add extent to DSS");

    if (obj_to_insert.oid != save_oid) {
        free(obj_to_insert.oid);
        obj_to_insert.oid = save_oid;
        lyt_to_insert.oid = obj_to_insert.oid;
    }

    rc = dss_unlock(&adm->dss, DSS_OBJECT, &obj_to_insert, 1, false);
    if (rc)
        LOG_RETURN(rc, "Unable to unlock object objid: '%s'",
                   obj_to_insert.oid);

    return 0;
}

/**
 * Auxilary function of explore_from_path
 */
static int _explore_from_path_aux(struct admin_handle *adm,
                                  char *root_path, char *address, int height,
                                  struct pho_id med_id,
                                  int (*func)(struct admin_handle *, int,
                                              char *, char *, off_t,
                                              struct timespec, int,
                                              struct pho_id, size_t *,
                                              long long *),
                                  size_t *size_written, long long *nb_new_obj)
{
    struct dirent *entry;
    int rc2 = 0;
    int rc = 0;
    int fddir;
    DIR *dir;

    fddir = open(root_path, O_NOATIME);
    if (fddir == -1)
        return -errno;

    dir = fdopendir(fddir);
    if (!dir) {
        rc = -errno;
        rc2 = close(fddir);
        if (rc2)
            pho_error(-errno, "Could not close dir fd");
        return rc;
    }

    while ((entry = readdir(dir)) != NULL) {
        struct stat stat_buf;
        char *address_buf;
        char *path;
        int fd;

        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") ||
            !strcmp(entry->d_name, ".phobos_dir_label"))
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
                        stat_buf.st_ctim, height, med_id, size_written,
                        nb_new_obj);
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
        rc2 = -errno;
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
                                         char *, off_t, struct timespec,
                                         int, struct pho_id,
                                         size_t *, long long *),
                             size_t *size_written, long long *nb_new_obj)
{
   return _explore_from_path_aux(adm, root_path, "", 0, med_id, (*func),
                                 size_written, nb_new_obj);
}

int import_medium(struct admin_handle *adm, struct media_info *medium,
                  bool check_hash)
{
    struct pho_id id = medium->rsc.id;
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
    memcpy(medium->fs.label, id.name, label_length);
    medium->fs.label[label_length] = 0;

    rc = dss_media_update(&adm->dss, medium, medium, 1, FS_LABEL);
    if (rc)
        LOG_RETURN(rc,
                   "Failed to update filesystem label of the tape (name '%s', "
                   "library '%s') to '%s' in DSS",
                   id.name, id.library, medium->fs.label);

    // One request to read the tape, one request to release it afterwards
    reqs = malloc(2 * sizeof(pho_req_t));
    if (reqs == NULL)
        LOG_RETURN(-ENOMEM, "Could not allocate memory for requests");

    pho_srl_request_read_alloc(reqs, 1);

    reqs[0].id = 0;
    reqs[0].ralloc->n_required = 1;
    reqs[0].ralloc->operation = PHO_READ_TARGET_ALLOC_OP_READ;
    reqs[0].ralloc->med_ids[0]->family = id.family;
    reqs[0].ralloc->med_ids[0]->name = strdup(id.name);
    reqs[0].ralloc->med_ids[0]->library = strdup(id.library);

    rc = comm_send_and_recv(&adm->phobosd_comm, &reqs[0], &resp);
    if (rc)
        LOG_GOTO(request_free, rc,
                 "Failed to send or receive read request for medium (family "
                 "'%s', name '%s', library '%s')",
                 rsc_family2str(id.family), id.name, id.library);

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

    pho_verb("Successfully mounted tape (name '%s', library '%s') to %s",
             id.name, id.library, root_path);
    pho_debug("fs_type:%s, med_id:%s, library:%s, addr_type:%s",
              fs_type2str(fs_type),
              resp->ralloc->media[0]->med_id->name,
              resp->ralloc->media[0]->med_id->library,
              address_type2str(addr_type));

    // Exploration of the tape
    rc = explore_from_path(adm, root_path, id, _import_file_to_dss,
                           &size_written, &nb_new_obj);

    // fs_df to actualize the stats of the tape
    rc = _dev_media_update(&adm->dss, medium, size_written, rc, root_path,
                           nb_new_obj);

    // Release of the medium
    pho_srl_request_release_alloc(reqs + 1, 1, true);

    reqs[1].id = 1;
    reqs[1].release->media[0]->med_id->family = id.family;
    reqs[1].release->media[0]->med_id->name = strdup(id.name);
    reqs[1].release->media[0]->med_id->library = strdup(id.library);
    reqs[1].release->media[0]->size_written = 0;
    reqs[1].release->media[0]->nb_extents_written = 0;
    reqs[1].release->media[0]->rc = 0;
    reqs[1].release->media[0]->to_sync = false;

    rc = comm_send(&adm->phobosd_comm, &reqs[1]);
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

int reconstruct_copy(struct admin_handle *adm, struct copy_info *copy)
{
    struct dss_filter filter;
    struct layout_info *lyt;
    int lyt_cnt;
    int rc = 0;

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                              "{\"DSS::LYT::object_uuid\": \"%s\"}, "
                              "{\"DSS::LYT::version\": \"%d\"},"
                              "{\"DSS::LYT::copy_name\": \"%s\"}"
                          "]}", copy->object_uuid, copy->version,
                          copy->copy_name);
    if (rc)
        return rc;

    rc = dss_full_layout_get(&adm->dss, &filter, NULL, &lyt, &lyt_cnt, NULL);
    dss_filter_free(&filter);
    if (rc)
        return rc;

    // Works like this in the current database version
    assert(lyt_cnt <= 1);

    if (lyt_cnt == 0) {
        copy->copy_status = PHO_COPY_STATUS_INCOMPLETE;
        goto end;
    }

    rc = layout_reconstruct(lyt[0], copy);

end:
    dss_res_free(lyt, lyt_cnt);
    if (rc)
        return rc;

    rc = dss_copy_update(&adm->dss, copy, copy, 1,
                         DSS_COPY_UPDATE_COPY_STATUS);
    return rc;
}
