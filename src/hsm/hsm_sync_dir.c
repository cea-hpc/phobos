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
#include <unistd.h>

#include "phobos_store.h"

#include "pho_common.h"
#include "pho_dss.h"
#include "pho_ldm.h"
#include "pho_types.h"
#include "pho_type_utils.h"

static void print_usage(void)
{
    printf("usage: %s [-h/--help] [-v/--verbose] [-q/--quiet] [-d/--dry-run] "
           "source_copy_name destination_copy_name\n",
           program_invocation_short_name);
}

struct params {
    const char *source_copy_name;
    const char *destination_copy_name;
    bool dry_run;
    int log_level;
};

#define DEFAULT_PARAMS {NULL, NULL, false, PHO_LOG_INFO}

static struct params parse_args(int argc, char **argv)
{
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"verbose", no_argument, 0, 'v'},
        {"quiet", no_argument, 0, 'q'},
        {"dry-run", no_argument, 0, 'd'},
        {0, 0, 0, 0}
    };
    struct params params = DEFAULT_PARAMS;
    int c;

    while ((c = getopt_long(argc, argv, "hvqd", long_options, NULL)) != -1) {
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
            params.dry_run = true;
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

static void sync_object(const char *oid, const char *object_uuid, int version,
                        const char *source_copy_name,
                        const char *destination_copy_name, bool dry_run)
{
    struct pho_xfer_target target = {0};
    struct pho_xfer_desc xfer = {0};
    char *target_uuid;
    int rc;

    pho_info("Syncing object ('%s' oid, '%s' uuid, '%d' version) from copy "
             "'%s' to copy '%s'%s", oid, object_uuid, version, source_copy_name,
             destination_copy_name,
             dry_run ? " (DRY RUN MODE, NO SYNC DONE)" : "");
    if (dry_run)
        return;

    target_uuid = xstrdup(object_uuid);
    xfer.xd_op = PHO_XFER_OP_COPY;
    target.xt_objid = xstrdup(oid);
    target.xt_objuuid = target_uuid;
    target.xt_version = version;
    xfer.xd_ntargets = 1;
    xfer.xd_targets = &target;
    xfer.xd_params.copy.get.copy_name = source_copy_name;
    /* only sync alive object */
    xfer.xd_params.copy.get.scope = DSS_OBJ_ALIVE;
    /* destination family is given by destination_copy_name profile */
    xfer.xd_params.copy.put.family = PHO_RSC_INVAL;
    xfer.xd_params.copy.put.copy_name = destination_copy_name;
    xfer.xd_params.copy.put.grouping = NULL;

    rc = phobos_copy(&xfer, 1, NULL, NULL);
    if (rc)
        pho_warn("Error %d (%s) when syncing object '%s' to copy '%s'",
                 -rc, strerror(-rc), oid, destination_copy_name);

    pho_xfer_desc_clean(&xfer);
    free(target.xt_objid);
    free(target_uuid);
}

int main(int argc, char **argv)
{
    struct dev_info *dev_list = NULL;
    const char *hostname = NULL;
    struct dss_filter filter;
    struct dss_handle dss;
    struct params params;
    int dev_count = 0;
    int rc;
    int i;

    params = parse_args(argc, argv);

    rc = phobos_init();
    if (rc)
        return rc;

    rc = pho_cfg_init_local(NULL);
    if (rc && rc != -EALREADY) {
        pho_error(rc, "Cannot init access to local config parameters");
        phobos_fini();
        return rc;
    }

    pho_log_level_set(params.log_level);

    rc = dss_init(&dss);
    if (rc) {
        pho_cfg_local_fini();
        phobos_fini();
        return rc;
    }

    /* only target local unlocked dir */
    hostname = get_hostname();
    if (!hostname)
        GOTO(end, rc = -errno);

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          "  {\"DSS::DEV::host\": \"%s\"},"
                          "  {\"DSS::DEV::family\": \"%s\"},"
                          "  {\"DSS::DEV::adm_status\": \"%s\"}"
                          "]}",
                          hostname, rsc_family2str(PHO_RSC_DIR),
                          rsc_adm_status2str(PHO_RSC_ADM_ST_UNLOCKED));
    if (rc)
        goto end;

    rc = dss_device_get(&dss, &filter, &dev_list, &dev_count, NULL);
    dss_filter_free(&filter);
    if (rc)
        goto end;

    /* target extent for each device dir */
    for (i = 0; i < dev_count; i++) {
        struct lib_drv_info drv_info;
        struct extent *extent_list;
        struct lib_handle lib_hdl;
        int extent_count;
        int j;

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

        rc = dss_filter_build(&filter,
                              "{\"$AND\": ["
                              "  {\"DSS::EXT::medium_family\": \"%s\"},"
                              "  {\"DSS::EXT::medium_id\": \"%s\"},"
                              "  {\"DSS::EXT::medium_library\": \"%s\"},"
                              "  {\"DSS::EXT::state\": \"%s\"}"
                              "]}",
                              rsc_family2str(PHO_RSC_DIR),
                              drv_info.ldi_medium_id.name,
                              dev_list[i].rsc.id.library,
                              extent_state2str(PHO_EXT_ST_SYNC));
        if (rc)
            goto close_lib_hdl;

        rc = dss_extent_get(&dss, &filter, &extent_list, &extent_count);
        dss_filter_free(&filter);
        if (rc)
            goto close_lib_hdl;

        /* check source copy */
        for (j = 0; j < extent_count; j++) {
            struct layout_info *layout_list;
            int layout_count;
            int k;

            rc = dss_filter_build(&filter,
                                  "{\"$AND\": ["
                                  "  {\"DSS::LYT::extent_uuid\": \"%s\"},"
                                  "  {\"DSS::LYT::copy_name\": \"%s\"}"
                                  "]}",
                                  extent_list[j].uuid, params.source_copy_name);
            if (rc)
                continue;

            rc = dss_layout_get(&dss, &filter, &layout_list, &layout_count);
            dss_filter_free(&filter);
            if (rc)
                continue;

            /* check destination copy */
            for (k = 0; k < layout_count; k++) {
                struct object_info *object_list;
                struct copy_info *copy_list;
                int object_count;
                int copy_count;

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
                if (copy_count)
                    continue;

                /* check object is a living one and get oid */
                rc = dss_filter_build(&filter,
                                      "{\"$AND\": ["
                                      "  {\"DSS::OBJ::uuid\": \"%s\"},"
                                      "  {\"DSS::OBJ::version\": \"%d\"}"
                                      "]}",
                                      layout_list[k].uuid,
                                      layout_list[k].version);
                if (rc)
                    continue;

                rc = dss_object_get(&dss, &filter, &object_list, &object_count,
                                    NULL);
                dss_filter_free(&filter);
                if (rc)
                    continue;

                if (object_count)
                    sync_object(object_list[0].oid, layout_list[k].uuid,
                                layout_list[k].version, params.source_copy_name,
                                params.destination_copy_name, params.dry_run);

                dss_res_free(object_list, object_count);
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
end:
    dss_fini(&dss);
    pho_cfg_local_fini();
    phobos_fini();
    return rc;
}
