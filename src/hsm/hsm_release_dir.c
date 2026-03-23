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
 * \brief  phobos_hsm_release_dir deletes copies of objects on the local dirs.
 *
 * If the fill rate of one local dir is above the higher threshold, the
 * phobos_hsm_release_dir command deletes copies of object with extents on this
 * dir to decrease the fill rate under the lower threshold.
 *
 * To be deleted, a "to release" copy must have an existing backend copy.
 *
 * The older copies are deleted first.
 *
 * "To release" copies with a creation time younger than
 * "current_time - release_delay_second" are not deleted.
 *
 * The "source_copy_name" and "destination_copy_name" are two mandatory command
 * line parameters.
 * The "dir_release_higher_threshold", "dir_release_lower_threshold" and
 * "release_delay_second" are config file parameters.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <getopt.h>
#include <jansson.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "phobos_store.h"

#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_dss.h"
#include "pho_dss_wrapper.h"
#include "pho_ldm.h"
#include "pho_types.h"
#include "pho_type_utils.h"

#include "hsm_common.h"

static void print_usage(void)
{
    printf("usage: %s [-h/--help] [-v/--verbose] [-q/--quiet] [-d/--delete] "
           "[-m/--metadata key1[,key2,[...]]] "
           "source_copy_name destination_copy_name\n"
           "\n"
           "This command detects which source_copy_name copies of objects on "
           "the local dirs should be deleted. Each new copy to delete is "
           "written on stdout with the following line:\n"
           "DELETE \"oid\" \"uuid\" \"version\" \"copy_name\" "
           "[\"key1\"=\"value1\"] [\"key2\"=\"value2\"]\n"
           "\n"
           "If the fill rate of one local dir is above the higher threshold, "
           "the phobos_hsm_release_dir command selects copies of object with "
           "extents on this dir to decrease the fill rate under the lower "
           "threshold.\n"
           "\n"
           "To be selected for deletion, a 'source_copy_name' copy must have "
           "an existing 'destination_copy_name' copy.\n"
           "\n"
           "The older copies are selected first.\n"
           "\n"
           "'source_copy_name' copies with a creation time younger than "
           "\"current_time - release_delay_second\" are not selected.\n"
           "\n"
           "The 'source_copy_name' and 'destination_copy_name' are two "
           "mandatory command line parameters.\n"
           "The 'dir_release_higher_threshold', 'dir_release_lower_threshold' "
           "and 'release_delay_second' are config file parameters.\n"
           "If the '-d/--delete' option is set, new copies written on STDOUT "
           "are sequentially deleted.\n"
           "\n"
           "If the '-m/--metadata key1[,key2,[...]]' option is set, a "
           "'\"keyX\"=\"value\"' is put on the line of each object which has a "
           "'keyX' metadata.\n",
           program_invocation_short_name);
}

#define DEFAULT_PARAMS {NULL, NULL, false, PHO_LOG_INFO}

static struct hsm_params parse_args(int argc, char **argv)
{
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"verbose", no_argument, 0, 'v'},
        {"quiet", no_argument, 0, 'q'},
        {"delete", no_argument, 0, 'd'},
        {"metadata", required_argument, 0, 'm'},
        {0, 0, 0, 0}
    };
    struct hsm_params params = DEFAULT_PARAMS;
    int c;

    while ((c = getopt_long(argc, argv, "hvqdm:", long_options, NULL)) != -1) {
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
        case 'd':
            params.achieve = true;
            break;
        case 'm':
            str2string_array(optarg, &params.wanted_keys);
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

static int release_copy(const struct object_info *obj,
                        const struct hsm_params *params)
{
    struct pho_xfer_target target = {0};
    struct pho_xfer_desc xfer = {0};
    char *target_uuid;
    char *copy_name;
    int rc = 0;

    rc = hsm_write_candidate(HSM_RELEASE, obj, params);
    if (rc || !params->achieve)
        return rc;

    xfer.xd_op = PHO_XFER_OP_DEL;
    copy_name = strdup(params->source_copy_name);
    xfer.xd_params.delete.copy_name = copy_name;
    xfer.xd_params.delete.scope = DSS_OBJ_ALL;
    xfer.xd_flags = PHO_XFER_COPY_HARD_DEL;
    xfer.xd_ntargets = 1;
    xfer.xd_targets = &target;

    target_uuid = xstrdup(obj->uuid);
    target.xt_objid = xstrdup(obj->oid);
    target.xt_objuuid = target_uuid;
    target.xt_version = obj->version;

    rc = phobos_delete(&xfer, 1);
    if (rc)
        hsm_log_error(HSM_RELEASE, rc, obj, params);

    pho_xfer_desc_clean(&xfer);
    free(target.xt_objid);
    free(target_uuid);
    free(copy_name);
    return rc;
}

static int set_torelease_ctime(const char *hsm_cfg_section_name,
                               struct timeval *torelease_ctime)
{
    const char *release_delay_second_string;
    int release_delay_second;
    int rc;

    rc = gettimeofday(torelease_ctime, NULL);
    if (rc)
        LOG_RETURN(rc = -errno, "Error when getting current time");

    rc = pho_cfg_get_val(hsm_cfg_section_name,
                         cfg_hsm[PHO_CFG_HSM_release_delay_second].name,
                         &release_delay_second_string);
    if (rc == -ENODATA) {
        pho_warn("No %s parameter for config section '%s', we use the default "
                 "value '%s'", cfg_hsm[PHO_CFG_HSM_release_delay_second].name,
                 hsm_cfg_section_name,
                 cfg_hsm[PHO_CFG_HSM_release_delay_second].value);
        release_delay_second_string =
            cfg_hsm[PHO_CFG_HSM_release_delay_second].value;
    } else if (rc) {
        LOG_RETURN(rc, "Unable to get %s config value for config section '%s'",
                   cfg_hsm[PHO_CFG_HSM_release_delay_second].name,
                   hsm_cfg_section_name);
    }

    release_delay_second = atoi(release_delay_second_string);
    if (release_delay_second < 0)
        LOG_RETURN(-EINVAL,
                   "hsm release_delay_second config value can not be negative, "
                   "%d", release_delay_second);

    if (release_delay_second > torelease_ctime->tv_sec)
        LOG_RETURN(-EINVAL,
                   "hsm release_delay_second %d can not be greater than "
                   "current time %ld", release_delay_second,
                   torelease_ctime->tv_sec);

    torelease_ctime->tv_sec -= release_delay_second;
    return 0;
}

/**
 *  The to_release ctime string follows the syntax of a PSQL timestamp of format
 *  "YYYY-mm-dd HH:MM:SS.uuuuuu".
 *  example: ""2025-09-26 18:17:07.548048", always 26 characters
 */

static int set_higher_threshold(const char *hsm_cfg_section_name,
                                int *higher_threshold)
{
    const char *higher_threshold_string;
    int rc;

    rc = pho_cfg_get_val(hsm_cfg_section_name,
                         cfg_hsm[PHO_CFG_HSM_dir_release_higher_threshold].name,
                         &higher_threshold_string);
    if (rc == -ENODATA) {
        pho_warn("No %s value in the config section '%s', we use the default "
                 "value '%s'",
                 cfg_hsm[PHO_CFG_HSM_dir_release_higher_threshold].name,
                 hsm_cfg_section_name,
                 cfg_hsm[PHO_CFG_HSM_dir_release_higher_threshold].value);
        higher_threshold_string =
            cfg_hsm[PHO_CFG_HSM_dir_release_higher_threshold].value;
    } else if (rc) {
        LOG_RETURN(rc, "Unable to get %s in the config section '%s'",
                   cfg_hsm[PHO_CFG_HSM_dir_release_higher_threshold].name,
                   hsm_cfg_section_name);
    }

    *higher_threshold = atoi(higher_threshold_string);
    if (*higher_threshold < 1 || *higher_threshold > 100) {
        LOG_RETURN(-EINVAL,
                 "The %d%% dir_release_higher_threshold configuration value is "
                 "invalid and must be a percentage integer between 1 and 100, "
                 "strictly higher than dir_release_lower_threshold.",
                 *higher_threshold);
    }

    if (*higher_threshold == 100)
        pho_warn("dir_release_higher_threshold is set to 100%%, no release "
                 "will happen.");

    return 0;
}

static int set_lower_threshold(const char *hsm_cfg_section_name,
                               int *lower_threshold)
{
    const char *lower_threshold_string;
    int rc;

    rc = pho_cfg_get_val(hsm_cfg_section_name,
                         cfg_hsm[PHO_CFG_HSM_dir_release_lower_threshold].name,
                         &lower_threshold_string);
    if (rc == -ENODATA) {
        pho_warn("No %s value in the config section '%s', we use the default "
                 "value '%s'",
                 cfg_hsm[PHO_CFG_HSM_dir_release_lower_threshold].name,
                 hsm_cfg_section_name,
                 cfg_hsm[PHO_CFG_HSM_dir_release_lower_threshold].value);
        lower_threshold_string =
            cfg_hsm[PHO_CFG_HSM_dir_release_lower_threshold].value;
    } else if (rc) {
        LOG_RETURN(rc, "Unable to get %s in the config section '%s'",
                   cfg_hsm[PHO_CFG_HSM_dir_release_lower_threshold].name,
                   hsm_cfg_section_name);
    }

    *lower_threshold = atoi(lower_threshold_string);
    if (*lower_threshold < 0 || *lower_threshold > 99) {
        LOG_RETURN(rc = -EINVAL,
                  "The %d%% dir_release_lower_threshold configuration value is "
                  "invalid and must be a percentage integer between 0 and 99, "
                  "strictly lower than dir_release_higher_threshold.",
                  *lower_threshold);
    }

    if (*lower_threshold == 0)
        pho_warn("dir_release_lower_threshold is set to 0%%. If a purge "
                 "starts, every selectable copy will be released.");

    return 0;
}

int main(int argc, char **argv)
{
    char torelease_ctime_string[CTIME_STRING_LENGTH + 1] = {0};
    struct timeval torelease_ctime = {0};
    char *hsm_cfg_section_name = NULL;
    struct dev_info *dev_list = NULL;
    const char *hostname = NULL;
    struct dss_filter filter;
    struct hsm_params params;
    struct dss_handle dss;
    int higher_threshold;
    int lower_threshold;
    int dev_count = 0;
    int rc;
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

    rc = set_torelease_ctime(hsm_cfg_section_name, &torelease_ctime);
    if (rc)
        goto dss_end;

    timeval2str(&torelease_ctime, torelease_ctime_string);

    pho_info("Checking new object copies to release older than %s",
             torelease_ctime_string);

    rc = set_higher_threshold(hsm_cfg_section_name, &higher_threshold);
    if (rc)
        goto dss_end;

    rc = set_lower_threshold(hsm_cfg_section_name, &lower_threshold);
    if (rc)
        goto dss_end;

    if (lower_threshold > higher_threshold) {
        pho_warn("dir_release_lower_threshold %d%% is upper than "
                 "dir_release_higher_threshold %d%%, no release will happen.",
                 lower_threshold, higher_threshold);
        goto dss_end;
    }

    rc = open_error_log_file(hsm_cfg_section_name, &params.error_log_file);
    if (rc)
        goto dss_end;

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

    for (i = 0; i < dev_count; i++) {
        struct dev_adapter_module *dev_adapter;
        struct media_info *medium_info;
        struct fs_adapter_module *fsa;
        struct lib_drv_info drv_info;
        struct dss_sort sort = {0};
        struct extent *extent_list;
        struct ldm_fs_space fs_spc;
        struct lib_handle lib_hdl;
        ssize_t size_to_release;
        char fsroot[PATH_MAX];
        json_t *error_message;
        double fill_threshold;
        int extent_count;
        int j;

        /* get drive from lib */
        rc = get_lib_adapter(PHO_LIB_DUMMY, &lib_hdl.ld_module);
        if (rc) {
            pho_error(rc, "Failed to get dir library adapter");
            continue;
        }

        rc = ldm_lib_open(&lib_hdl, dev_list[i].rsc.id.library);
        if (rc) {
            pho_error(rc, "Failed to load dir library handle");
            continue;
        }

        rc = ldm_lib_drive_lookup(&lib_hdl, dev_list[i].rsc.id.name, &drv_info);
        if (rc)
            continue;

        /* get dev path */
        rc = get_dev_adapter(dev_list[i].rsc.id.family, &dev_adapter);
        if (rc)
            continue;

        rc = ldm_dev_lookup(dev_adapter, dev_list[i].rsc.id.name, fsroot,
                            sizeof(fsroot));

        /* get fs adapter */
        rc = dss_one_medium_get_from_id(&dss, &drv_info.ldi_medium_id,
                                        &medium_info);
        if (rc) {
            dss_res_free(medium_info, 1);
            pho_warn("Unable to get medium info of dir "FMT_PHO_ID,
                     PHO_ID(dev_list[i].rsc.id));
            continue;
        }

        rc = get_fs_adapter(medium_info->fs.type, &fsa);
        dss_res_free(medium_info, 1);
        if (rc) {
            pho_error(rc, "Unable to get fs adapter of dir "FMT_PHO_ID,
                      PHO_ID(dev_list[i].rsc.id));
            continue;
        }

        /* get fill rate */
        rc = ldm_fs_df(fsa, fsroot, &fs_spc, &error_message);
        if (rc) {
            if (error_message) {
                json_decref(error_message);
                error_message = NULL;
            }

            continue;
        }

        fill_threshold = ((double) fs_spc.spc_used /
                          ((double)fs_spc.spc_used + (double)fs_spc.spc_avail))
                         * (double)100;

        if (fill_threshold < higher_threshold) {
            pho_debug("current fill threshold %lf%% of dir "FMT_PHO_ID" is "
                      "inferior to higher threshold %d%%",
                      fill_threshold, PHO_ID(dev_list[i].rsc.id),
                      higher_threshold);
            continue;
        }

        size_to_release = (ssize_t)((double)fs_spc.spc_used -
                                    (double)lower_threshold *
                                    ((double)fs_spc.spc_used +
                                     (double)fs_spc.spc_avail) / (double)100);
        pho_info("%zd bytes must be released from dir "FMT_PHO_ID", its "
                 "current threshold %lf%% is greater than the higher threshold "
                 "%d%% and must be reduced to lower threshold %d%%",
                 size_to_release, PHO_ID(dev_list[i].rsc.id), fill_threshold,
                 higher_threshold, lower_threshold);


        /* getting extents of the dir */
        rc = dss_filter_build(&filter,
                              "{\"$AND\": ["
                              "  {\"DSS::EXT::medium_family\": \"%s\"},"
                              "  {\"DSS::EXT::medium_id\": \"%s\"},"
                              "  {\"DSS::EXT::medium_library\": \"%s\"},"
                              "  {\"DSS::EXT::state\": \"%s\"},"
                              "  {\"$LTE\": "
                              "    {\"DSS::EXT::creation_time\": \"%s\"}}"
                              "]}",
                              rsc_family2str(PHO_RSC_DIR),
                              drv_info.ldi_medium_id.name,
                              dev_list[i].rsc.id.library,
                              extent_state2str(PHO_EXT_ST_SYNC),
                              torelease_ctime_string);
        if (rc)
            goto close_lib_hdl;

        sort.attr = dss_fields_pub2implem("DSS::EXT::creation_time");
        sort.psql_sort = true;

        rc = dss_extent_get(&dss, &filter, &extent_list, &extent_count, &sort);
        dss_filter_free(&filter);
        if (rc)
            goto close_lib_hdl;

        /* check release copy */
        for (j = 0; j < extent_count && size_to_release > 0; j++) {
            struct layout_info *layout_list;
            int layout_count;
            int k;

            rc = dss_filter_build(&filter,
                                  "{\"$AND\": ["
                                  "  {\"DSS::LYT::extent_uuid\": \"%s\"},"
                                  "  {\"DSS::LYT::copy_name\": \"%s\"}"
                                  "]}",
                                  extent_list[j].uuid,
                                  params.source_copy_name);
            if (rc)
                continue;

            rc = dss_layout_get(&dss, &filter, &layout_list, &layout_count);
            dss_filter_free(&filter);
            if (rc)
                continue;

            /* check backend copy */
            for (k = 0; k < layout_count; k++) {
                struct copy_info *copy_list;
                struct object_info *obj;
                int copy_count;

                /* check release copy window time */
                rc = dss_filter_build(&filter,
                                      "{\"$AND\": ["
                                      "  {\"DSS::COPY::object_uuid\": \"%s\"},"
                                      "  {\"DSS::COPY::version\": \"%d\"},"
                                      "  {\"DSS::COPY::copy_name\": \"%s\"}"
                                      "]}",
                                      layout_list[k].uuid,
                                      layout_list[k].version,
                                      params.source_copy_name);
                if (rc)
                    continue;

                rc = dss_copy_get(&dss, &filter, &copy_list, &copy_count, NULL);
                dss_filter_free(&filter);
                if (rc)
                    continue;

                dss_res_free(copy_list, copy_count);
                if (!copy_count)
                    continue;

                /* check backend copy exists */
                rc = dss_filter_build(&filter,
                                      "{\"$AND\": ["
                                      "  {\"DSS::COPY::object_uuid\": \"%s\"},"
                                      "  {\"DSS::COPY::version\": \"%d\"},"
                                      "  {\"DSS::COPY::copy_name\": \"%s\"}"
                                      "]}",
                                      layout_list[k].uuid,
                                      layout_list[k].version,
                                      params.destination_copy_name);
                if (rc)
                    continue;

                rc = dss_copy_get(&dss, &filter, &copy_list, &copy_count, NULL);
                dss_filter_free(&filter);
                if (rc)
                    continue;

                dss_res_free(copy_list, copy_count);
                if (copy_count < 1)
                    continue;

                /* get OID */
                rc = dss_lazy_find_object(&dss, NULL, layout_list[k].uuid,
                                          layout_list[k].version, &obj);
                if (rc)
                    continue;

                rc = release_copy(obj, &params);
                object_info_free(obj);
                if (!rc)
                    size_to_release -= extent_list[j].size;
            }

            dss_res_free(layout_list, layout_count);
        }

        dss_res_free(extent_list, extent_count);
close_lib_hdl:
        rc = ldm_lib_close(&lib_hdl);
        if (rc)
            pho_error(rc, "Failed to close dir library handle");
    }

    dss_res_free(dev_list, dev_count);

log_end:
    fclose(params.error_log_file);
dss_end:
    dss_fini(&dss);
    free(hsm_cfg_section_name);
cfg_end:
    pho_cfg_local_fini();
fini_end:
    phobos_fini();
    clean_hsm_params(&params);
    return rc;
}
