/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2025 CEA/DAM.
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
 * \brief  phobos_hsm_sync_dir creates copies of objects from local dir.
 *
 * The phobos_hsm_sync_dir command targets objects with extents on the media
 * of the dir family owned by the local host.
 *
 * This command takes two input parameters: a source_copy_name and a
 * destination_copy_name. Only living objects with a source_copy_name copy with
 * extents on dir owned by the local host and no existing destination_copy_name
 * copy will have a new destination_copy_name copy.
 *
 * All parameters of the created copies will be inherited from the
 * destination_copy_name copy.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "phobos_store.h"

#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_ldm.h"
#include "pho_types.h"
#include "pho_type_utils.h"

#include "hsm_common.h"

static int sync_object(const struct object_info *obj,
                       const struct hsm_params *params)
{
    struct pho_xfer_target target = {0};
    struct pho_xfer_desc xfer = {0};
    char *target_uuid;
    int rc;

    rc = hsm_write_candidate(HSM_SYNC, obj, params);
    if (rc || !params->achieve)
        return rc;

    target_uuid = xstrdup(obj->uuid);
    xfer.xd_op = PHO_XFER_OP_COPY;
    target.xt_objid = xstrdup(obj->oid);
    target.xt_objuuid = target_uuid;
    target.xt_version = obj->version;
    xfer.xd_ntargets = 1;
    xfer.xd_targets = &target;
    xfer.xd_params.copy.get.copy_name = params->source_copy_name;
    /* only sync alive object */
    xfer.xd_params.copy.get.scope = DSS_OBJ_ALIVE;
    /* destination family is given by destination_copy_name profile */
    xfer.xd_params.copy.put.family = PHO_RSC_INVAL;
    xfer.xd_params.copy.put.copy_name = params->destination_copy_name;
    xfer.xd_params.copy.put.grouping = NULL;

    rc = phobos_copy(&xfer, 1, NULL, NULL);
    if (rc)
        hsm_log_error(HSM_SYNC, rc, obj, params);

    pho_xfer_desc_clean(&xfer);
    free(target.xt_objid);
    free(target_uuid);
    return rc;
}

/* a GDestroyNotify function to clean an object_info */
static void free_object_info(gpointer data)
{
    struct object_info *obj = data;

    object_info_free(obj);
}

/* A GHashFunc for object_info */
static guint hash_object_info(gconstpointer key)
{
    const struct object_info *obj = key;

    return g_str_hash(obj->oid) ^ g_str_hash(obj->uuid) ^
           g_int_hash(&obj->version);
}

/* A GEqualFunc for object_info */
static gboolean equal_object_info(gconstpointer a, gconstpointer b)
{
    const struct object_info *obj_a = a;
    const struct object_info *obj_b = b;

    return g_str_equal(obj_a->oid, obj_b->oid) &&
           g_str_equal(obj_a->uuid, obj_b->uuid) &&
           obj_a->version == obj_b->version;

}

struct sync_params {
    int rc;
    struct hsm_params *params;
};

static void sync_object_info(gpointer data, gpointer user_data)
{
    struct object_info *obj = data;
    struct sync_params *sp = user_data;
    int rc;

    rc = sync_object(obj, sp->params);
    if (rc && !sp->rc)
        sp->rc = rc;
}

#define NO_GROUPING "NGRP"

/* object to sync are grouped per "grouping" */
struct grouping_to_sync {
    char *grouping;
    GQueue *object_to_sync_queue; /* Queue of object_info (also stored into
                                   * object_to_sync_hashtable to detect
                                   * duplicate), head is the object with the
                                   * older 'source_copy_name' copy ctime
                                   */
    GHashTable *object_to_sync_hashtable; /* Hashtable of object_info (also
                                           * stored into object_to_sync_queue
                                           * to track ctime order), hashed per
                                           * (oid, uuid, version)
                                           * object_info are owned and freed
                                           * by the unref of
                                           * object_to_sync_hashtable.
                                           */
};

/* a GDestroyNotify function to clean a grouping_to_sync */
static void free_grouping_to_sync(gpointer data)
{
    struct grouping_to_sync *gts = data;

    free(gts->grouping);
    g_queue_free(gts->object_to_sync_queue);
    g_hash_table_unref(gts->object_to_sync_hashtable);
    free(gts);
}

/* A GFunc to sync a grouping_to_sync */
static void sync_grouping(gpointer data, gpointer params)
{
    struct grouping_to_sync *gts = data;

    g_queue_foreach(gts->object_to_sync_queue, sync_object_info, params);
}

static void add_object_to_sync(const struct object_info *obj,
                               GQueue *grouping_queue,
                               GHashTable *grouping_hashtable)
{
    const char *grouping = obj->grouping ? : NO_GROUPING;
    struct grouping_to_sync *gts = g_hash_table_lookup(grouping_hashtable,
                                                       grouping);

    if (!gts) {
        gts = xmalloc(sizeof(*gts));

        gts->grouping = xstrdup(grouping);
        gts->object_to_sync_queue = g_queue_new();
        gts->object_to_sync_hashtable =
            g_hash_table_new_full(hash_object_info, equal_object_info,
                                  NULL, free_object_info);
        g_queue_push_tail(grouping_queue, gts);
        g_hash_table_insert(grouping_hashtable, gts->grouping, gts);
    }

    if (!g_hash_table_lookup(gts->object_to_sync_hashtable, obj)) {
        struct object_info *new_obj = object_info_dup(obj);

        g_queue_push_tail(gts->object_to_sync_queue, new_obj);
        g_hash_table_add(gts->object_to_sync_hashtable, new_obj);
    }
}

/** Open synced ctime file in a+ mode and set synced_time
 *
 *  The synced ctime file must contains a 'date +"%Y-%m-%d %H:%M:%S.%6N"' value,
 *  of format "YYYY-mm-dd HH:MM:SS.uuuuuu".
 *  example: ""2025-09-26 18:17:07.548048", always 26 characters
 */
static int set_synced_ctime(struct timeval *synced_ctime,
                            const char *hsm_cfg_section_name,
                            const char **synced_ctime_path)
{
    char synced_ctime_string[CTIME_STRING_LENGTH + 1] = {0};
    FILE *synced_ctime_stream;
    struct stat statbuf;
    size_t nb_char_read;
    int rc;

    rc = pho_cfg_get_val(hsm_cfg_section_name,
                         cfg_hsm[PHO_CFG_HSM_synced_ctime_path].name,
                         synced_ctime_path);
    if (rc == -ENODATA) {
        pho_warn("No synced_ctime_path value in config section '%s', we get "
                 "default value '%s'", hsm_cfg_section_name,
                 cfg_hsm[PHO_CFG_HSM_synced_ctime_path].value);
        *synced_ctime_path = cfg_hsm[PHO_CFG_HSM_synced_ctime_path].value;
    } else if (rc) {
        LOG_RETURN(-EINVAL,
                   "Unable to get synced_ctime_path for config section '%s'",
                   hsm_cfg_section_name);
    }

    rc = stat(*synced_ctime_path, &statbuf);
    if (rc) {
        rc = -errno;
        if (rc == -ENOENT) {
            pho_warn("Sync-ctime file '%s' does not exist, setting the last "
                     "synced time to the default 1970/01/01",
                     *synced_ctime_path);
            synced_ctime->tv_sec = 1;
            synced_ctime->tv_usec = 0;
            return 0;
        } else {
            LOG_RETURN(rc, "Unable to stat synced ctime file %s",
                       *synced_ctime_path);
        }
    }

    synced_ctime_stream = fopen(*synced_ctime_path, "r");
    if (!synced_ctime_stream)
        LOG_RETURN(rc = -errno,
                   "Error when opening synced ctime file %s to load",
                   *synced_ctime_path);

    nb_char_read = fread(synced_ctime_string, sizeof(char), CTIME_STRING_LENGTH,
                         synced_ctime_stream);
    if (ferror(synced_ctime_stream))
        LOG_GOTO(error_close, rc = -errno,
                 "Error when reading synced ctime in file %s",
                 *synced_ctime_path);

    if (nb_char_read != CTIME_STRING_LENGTH)
        LOG_GOTO(error_close, rc = -EINVAL,
                 "%s must contain a synced ctime of at least %d characters, "
                 "with the \"YYYY-mm-dd HH:MM:SS.uuuuuu\" format of the "
                 "'date +\"%%Y-%%m-%%d %%H:%%M:%%S.%%6N\"' command",
                 *synced_ctime_path, CTIME_STRING_LENGTH);

    rc = str2timeval(synced_ctime_string, synced_ctime);
    if (rc)
        LOG_GOTO(error_close, rc,
                 "Error when parsing synced ctime from file %s, %s is not "
                 "consistent with the \"YYYY-mm-dd HH:MM:SS.uuuuuu\" format of "
                 "the 'date +\"%%Y-%%m-%%d %%H:%%M:%%S.%%6N\"' command",
                 *synced_ctime_path, synced_ctime_string);

error_close:
    fclose(synced_ctime_stream);
    return rc;
}

static int set_tosync_ctime(const char *hsm_cfg_section_name,
                            struct timeval *tosync_ctime)
{
    const char *sync_delay_second_string;
    int sync_delay_second;
    int rc;

    rc = gettimeofday(tosync_ctime, NULL);
    if (rc)
        LOG_RETURN(rc = -errno, "Error when getting current time");

    rc = pho_cfg_get_val(hsm_cfg_section_name,
                         cfg_hsm[PHO_CFG_HSM_sync_delay_second].name,
                         &sync_delay_second_string);
    if (rc == -ENODATA) {
        pho_warn("No %s parameter for config section '%s', we use the default "
                 "value '%s'",
                 cfg_hsm[PHO_CFG_HSM_sync_delay_second].name,
                 hsm_cfg_section_name,
                 cfg_hsm[PHO_CFG_HSM_sync_delay_second].value);
        sync_delay_second_string = cfg_hsm[PHO_CFG_HSM_sync_delay_second].value;
    } else if (rc) {
        LOG_RETURN(rc,
                   "Unable to get %s config value for config section '%s'",
                   cfg_hsm[PHO_CFG_HSM_sync_delay_second].name,
                   hsm_cfg_section_name);
    }

    sync_delay_second = atoi(sync_delay_second_string);
    if (sync_delay_second < 0)
        LOG_RETURN(-EINVAL,
                   "hsm sync_delay_second config value can not be negative, %d",
                   sync_delay_second);

    if (sync_delay_second > tosync_ctime->tv_sec)
        LOG_RETURN(-EINVAL,
                   "hsm sync_delay_second %d can not be greater than current "
                   "time %ld", sync_delay_second, tosync_ctime->tv_sec);

    tosync_ctime->tv_sec -= sync_delay_second;
    return 0;
}

static void print_usage(void)
{
    printf("usage: %s [-h/--help] [-v/--verbose] [-q/--quiet] [-g/--grouping] "
           "[-c/--create] source_copy_name destination_copy_name\n"
           "\n"
           "This command detects which new copy `destination_copy_name` "
           "must be created to replicate the data referenced by "
           "`source_copy_name`. Each new copy to create is written on "
           "stdout with the following line:\n"
           "CREATE \"oid\" \"uuid\" \"version\" \"copy_name\"\n"
           "\n"
           "The source copy must have data on directories to be copied, and "
           "the directories must be owned the local host.\n"
           "\n"
           "Only the source copies with a creation time younger than the last "
           "synced time recorded into the 'synced_timed_path' file and older "
           "than 'now - sync_delay_second' are replicated to destination "
           "copies.\n"
           "'synced_timed_path' and 'sync_delay_second' can be set from the "
           "phobos configuration.\n"
           "\n"
           "If the '-g/--grouping' option is set, sync are grouped per "
           "grouping (Warning: with this option, the first sync starts only "
           "when all the objects to sync had been listed from the DSS.).\n"
           "\n"
           "If the '-c/--create' option is set, new copies written on STDOUT "
           "are sequentially created.\n",
           program_invocation_short_name);
}

static struct hsm_params parse_args(int argc, char **argv)
{
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"verbose", no_argument, 0, 'v'},
        {"quiet", no_argument, 0, 'q'},
        {"grouping", no_argument, 0, 'g'},
        {"create", no_argument, 0, 'c'},
        {0, 0, 0, 0}
    };
    struct hsm_params params = DEFAULT_HSM_PARAMS;
    int c;

    while ((c = getopt_long(argc, argv, "hvqdgc", long_options, NULL)) != -1) {
        switch (c) {
        case 'h':
            print_usage();
            exit(EXIT_SUCCESS);
        case 'v':
            ++params.log_level;
            break;
        case 'q':
            --params.log_level;
            break;
        case 'g':
            params.grouping = true;
            break;
        case 'c':
            params.achieve = true;
            break;
        default:
            print_usage();
            exit(EXIT_FAILURE);
        }
    }

    if (argc - optind != 2) {
        print_usage();
        exit(EXIT_FAILURE);
    }

    params.source_copy_name = argv[optind];
    params.destination_copy_name = argv[optind + 1];

    if (params.log_level < PHO_LOG_DISABLED)
        params.log_level = PHO_LOG_DISABLED;

    if (params.log_level > PHO_LOG_DEBUG)
        params.log_level = PHO_LOG_DEBUG;

    return params;
}

static int update_synced_ctime(const char *synced_ctime_path,
                               const char *synced_ctime_string)
{
    FILE *synced_ctime_stream;
    size_t nb_char_write;
    int rc = 0;

    synced_ctime_stream = fopen(synced_ctime_path, "w");
    if (!synced_ctime_stream)
        LOG_RETURN(rc = -errno,
                   "Error when opening synced ctime file %s to update",
                   synced_ctime_path);

    nb_char_write = fwrite(synced_ctime_string, sizeof(char),
                           CTIME_STRING_LENGTH, synced_ctime_stream);
    if (ferror(synced_ctime_stream))
        LOG_GOTO(end, rc = -errno,
                 "Error when writing synced ctime %s in file %s",
                 synced_ctime_string, synced_ctime_path);

    if (nb_char_write != CTIME_STRING_LENGTH)
        LOG_GOTO(end, rc = -EINVAL,
                 "Error when writing synced ctime %s in file %s, "
                 "%zu written characters instead of %d",
                 synced_ctime_string, synced_ctime_path, nb_char_write,
                 CTIME_STRING_LENGTH);
end:
    fclose(synced_ctime_stream);
    return rc;
}

int main(int argc, char **argv)
{
    /* Hashtable of grouping_to_sync (also stored into grouping_queue to track
     * ctime order), hashed per grouping
     * grouping_to_sync are owned and freed by the unref of grouping_hashtable.
     */
    GHashTable *grouping_hashtable = NULL;
    /* Queue of grouping_to_sync (also stored into grouping_hashtable to detect
     * duplicate), head is the grouping with the older 'source_copy_name' copy
     * ctime
     */
    GQueue *grouping_queue = NULL;
    char synced_ctime_string[CTIME_STRING_LENGTH + 1] = {0};
    char tosync_ctime_string[CTIME_STRING_LENGTH + 1] = {0};
    const char *synced_ctime_path = NULL;
    char *hsm_cfg_section_name = NULL;
    struct timeval synced_ctime = {0};
    struct timeval tosync_ctime = {0};
    struct dev_info *dev_list = NULL;
    const char *hostname = NULL;
    struct dss_filter filter;
    struct hsm_params params;
    bool update_sync = true;
    struct dss_handle dss;
    int dev_count = 0;
    int rc, rc2;
    int i;

    params = parse_args(argc, argv);

    rc = phobos_init();
    if (rc)
        return rc;

    rc = pho_cfg_init_local(NULL);
    if (rc && rc != -EALREADY)
        LOG_GOTO(fini_end, rc, "Cannot init access to local config parameters");

    pho_log_level_set(params.log_level);

    rc = dss_init(&dss);
    if (rc)
        goto cfg_end;

    /* build hsm cfg section */
    rc = asprintf(&hsm_cfg_section_name, HSM_SECTION_CFG,
                  params.source_copy_name, params.destination_copy_name);
    if (rc < 0) {
        hsm_cfg_section_name = NULL;
        goto dss_end;
    }

    /* Setting time window */
    rc = set_synced_ctime(&synced_ctime, hsm_cfg_section_name,
                          &synced_ctime_path);
    if (rc)
        goto dss_end;

    timeval2str(&synced_ctime, synced_ctime_string);
    rc = set_tosync_ctime(hsm_cfg_section_name, &tosync_ctime);
    if (rc)
        goto dss_end;

    timeval2str(&tosync_ctime, tosync_ctime_string);
    if (tosync_ctime.tv_sec < synced_ctime.tv_sec ||
        (tosync_ctime.tv_sec == synced_ctime.tv_sec &&
            tosync_ctime.tv_usec < synced_ctime.tv_usec))
        LOG_GOTO(dss_end, rc = -EINVAL,
                 "Empty window time, synced_ctime '%s' is older than "
                 "tosync_ctime '%s'", synced_ctime_string, tosync_ctime_string);

    rc = open_error_log_file(hsm_cfg_section_name, &params.error_log_file);
    if (rc)
        goto dss_end;

    pho_info("Checking new object copies from %s to %s",
             synced_ctime_string, tosync_ctime_string);

    /* only target local unlocked dir */
    hostname = get_hostname();
    if (!hostname)
        GOTO(log_end, rc = -errno);

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          "  {\"DSS::DEV::host\": \"%s\"},"
                          "  {\"DSS::DEV::family\": \"%s\"},"
                          "  {\"DSS::DEV::adm_status\": \"%s\"}"
                          "]}",
                          hostname, rsc_family2str(PHO_RSC_DIR),
                          rsc_adm_status2str(PHO_RSC_ADM_ST_UNLOCKED));
    if (rc)
        goto log_end;

    rc = dss_device_get(&dss, &filter, &dev_list, &dev_count, NULL);
    dss_filter_free(&filter);
    if (rc)
        goto log_end;

    if (params.grouping) {
        grouping_queue = g_queue_new();
        grouping_hashtable = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                   NULL, free_grouping_to_sync);
    }

    /* target extent for each device dir */
    for (i = 0; i < dev_count; i++) {
        struct lib_drv_info drv_info;
        struct dss_sort sort = {0};
        struct extent *extent_list;
        struct lib_handle lib_hdl;
        int extent_count;
        int j;

        rc2 = get_lib_adapter(PHO_LIB_DUMMY, &lib_hdl.ld_module);
        if (rc2) {
            pho_error(rc2, "Failed to get dir library adapter");
            update_sync = false;
            rc = rc ? : rc2;
            continue;
        }

        rc2 = ldm_lib_open(&lib_hdl, dev_list[i].rsc.id.library);
        if (rc2) {
            pho_error(rc2, "Failed to load dir library handle");
            update_sync = false;
            rc = rc ? : rc2;
            continue;
        }

        rc2 = ldm_lib_drive_lookup(&lib_hdl, dev_list[i].rsc.id.name,
                                   &drv_info);
        if (rc2) {
            update_sync = false;
            rc = rc ? : rc2;
            continue;
        }

        rc2 = dss_filter_build(&filter,
                               "{\"$AND\": ["
                               "  {\"DSS::EXT::medium_family\": \"%s\"},"
                               "  {\"DSS::EXT::medium_id\": \"%s\"},"
                               "  {\"DSS::EXT::medium_library\": \"%s\"},"
                               "  {\"DSS::EXT::state\": \"%s\"},"
                               "  {\"$GTE\": "
                               "    {\"DSS::EXT::creation_time\": \"%s\"}},"
                               "  {\"$LTE\": "
                               "    {\"DSS::EXT::creation_time\": \"%s\"}}"
                               "]}",
                               rsc_family2str(PHO_RSC_DIR),
                               drv_info.ldi_medium_id.name,
                               dev_list[i].rsc.id.library,
                               extent_state2str(PHO_EXT_ST_SYNC),
                               synced_ctime_string, tosync_ctime_string);
        if (rc2) {
            update_sync = false;
            rc = rc ? : rc2;
            goto close_lib_hdl;
        }

        sort.attr = dss_fields_pub2implem("DSS::EXT::creation_time");
        sort.psql_sort = true;

        rc2 = dss_extent_get(&dss, &filter, &extent_list, &extent_count, &sort);
        dss_filter_free(&filter);
        if (rc2) {
            update_sync = false;
            rc = rc ? : rc2;
            goto close_lib_hdl;
        }

        /* check source copy */
        for (j = 0; j < extent_count; j++) {
            struct layout_info *layout_list;
            int layout_count;
            int k;

            rc2 = dss_filter_build(&filter,
                                   "{\"$AND\": ["
                                   "  {\"DSS::LYT::extent_uuid\": \"%s\"},"
                                   "  {\"DSS::LYT::copy_name\": \"%s\"}"
                                   "]}",
                                   extent_list[j].uuid,
                                   params.source_copy_name);
            if (rc2) {
                update_sync = false;
                rc = rc ? : rc2;
                continue;
            }

            rc2 = dss_layout_get(&dss, &filter, &layout_list, &layout_count);
            dss_filter_free(&filter);
            if (rc2) {
                update_sync = false;
                rc = rc ? : rc2;
                continue;
            }

            /* check destination copy */
            for (k = 0; k < layout_count; k++) {
                struct object_info *object_list;
                struct copy_info *copy_list;
                int object_count;
                int copy_count;

                /* check source copy window time */
                rc2 = dss_filter_build(&filter,
                                       "{\"$AND\": ["
                                       "  {\"DSS::COPY::object_uuid\": \"%s\"},"
                                       "  {\"DSS::COPY::version\": \"%d\"},"
                                       "  {\"DSS::COPY::copy_name\": \"%s\"}"
                                       "]}",
                                       layout_list[k].uuid,
                                       layout_list[k].version,
                                       params.source_copy_name);
                if (rc2) {
                    update_sync = false;
                    rc = rc ? : rc2;
                    continue;
                }

                rc = dss_copy_get(&dss, &filter, &copy_list, &copy_count, NULL);
                dss_filter_free(&filter);
                if (rc2) {
                    update_sync = false;
                    rc = rc ? : rc2;
                    continue;
                }

                dss_res_free(copy_list, copy_count);
                if (!copy_count)
                    continue;

                /* check that no destination copy exists */
                rc2 = dss_filter_build(&filter,
                                       "{\"$AND\": ["
                                       "  {\"DSS::COPY::object_uuid\": \"%s\"},"
                                       "  {\"DSS::COPY::version\": \"%d\"},"
                                       "  {\"DSS::COPY::copy_name\": \"%s\"}"
                                       "]}",
                                       layout_list[k].uuid,
                                       layout_list[k].version,
                                       params.destination_copy_name);
                if (rc2) {
                    update_sync = false;
                    rc = rc ? : rc2;
                    continue;
                }

                rc2 = dss_copy_get(&dss, &filter, &copy_list, &copy_count,
                                   NULL);
                dss_filter_free(&filter);
                if (rc2) {
                    update_sync = false;
                    rc = rc ? : rc2;
                    continue;
                }

                dss_res_free(copy_list, copy_count);
                if (copy_count)
                    continue;

                /* check object is a living one and get oid */
                rc2 = dss_filter_build(&filter,
                                      "{\"$AND\": ["
                                      "  {\"DSS::OBJ::uuid\": \"%s\"},"
                                      "  {\"DSS::OBJ::version\": \"%d\"}"
                                      "]}",
                                      layout_list[k].uuid,
                                      layout_list[k].version);
                if (rc2) {
                    update_sync = false;
                    rc = rc ? : rc2;
                    continue;
                }

                rc2 = dss_object_get(&dss, &filter, &object_list, &object_count,
                                    NULL);
                dss_filter_free(&filter);
                if (rc2) {
                    update_sync = false;
                    rc = rc ? : rc2;
                    continue;
                }

                if (object_count) {
                    if (!params.grouping) {
                        rc2 = sync_object(object_list, &params);
                        if (rc2) {
                            update_sync = false;
                            rc = rc ? : rc2;
                        }
                    } else {
                        add_object_to_sync(object_list, grouping_queue,
                                           grouping_hashtable);
                    }
                }

                dss_res_free(object_list, object_count);
            }

            dss_res_free(layout_list, layout_count);
        }

        dss_res_free(extent_list, extent_count);
close_lib_hdl:
        rc2 = ldm_lib_close(&lib_hdl);
        if (rc2) {
            pho_error(rc2, "Failed to close dir library handle");
            update_sync = false;
            rc = rc ? : rc2;
        }
    }

    dss_res_free(dev_list, dev_count);

    /* do grouped sync */
    if (params.grouping) {
        struct sync_params sp = {0, &params};

        g_queue_foreach(grouping_queue, sync_grouping, &sp);
        if (sp.rc) {
            update_sync = false;
            rc = rc ? : sp.rc;
        }
    }

    /* update synced ctime */
    if (update_sync) {
        rc2 = update_synced_ctime(synced_ctime_path, tosync_ctime_string);
        if (rc2)
            rc = rc ? : rc2;
    }

log_end:
    fclose(params.error_log_file);

dss_end:
    if (params.grouping) {
        g_queue_free(grouping_queue);
        g_hash_table_unref(grouping_hashtable);
    }

    dss_fini(&dss);
    free(hsm_cfg_section_name);
cfg_end:
    pho_cfg_local_fini();
fini_end:
    phobos_fini();
    return rc;
}
