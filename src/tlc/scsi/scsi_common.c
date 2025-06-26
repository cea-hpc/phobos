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
/**
 * \brief  SCSI command helper
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "scsi_common.h"
#include "pho_common.h"

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <scsi/sg.h>
#include <scsi/sg_io_linux.h>
#include <scsi/scsi.h>
#include <assert.h>

#define SCSI_MSG_LEN    256

/** Convert internal direction to SG equivalent */
static inline int scsi_dir2sg(enum scsi_direction direction)
{
    switch (direction) {
    case SCSI_GET: return SG_DXFER_FROM_DEV;
    case SCSI_PUT: return SG_DXFER_TO_DEV;
    case SCSI_NONE: return SG_DXFER_NONE;
    }
    return -1;
}

/** Convert SCSI host_status to -errno code */
static void fill_host_status_scsi_error(uint16_t host_status,
                                        struct scsi_error *err)
{
    switch (host_status) {
    case SG_LIB_DID_OK:
        err->status = SCSI_SUCCESS;
        err->rc = 0;
        break;

    case SG_LIB_DID_NO_CONNECT:
        err->status = SCSI_FATAL_ERROR;
        err->rc = -ECONNABORTED;
        break;

    case SG_LIB_DID_TIME_OUT:
        err->status = SCSI_RETRY_LONG;
        err->rc = -ETIMEDOUT;
        break;

    case SG_LIB_DID_BAD_TARGET:
        err->status = SCSI_FATAL_ERROR;
        err->rc = -EINVAL;
        break;

    case SG_LIB_DID_ABORT:
    case SG_LIB_DID_RESET:
        err->status = SCSI_FATAL_ERROR;
        err->rc = -ECANCELED;
        break;

    case SG_LIB_DID_BAD_INTR:
        err->status = SCSI_RETRY_SHORT;
        err->rc = -EINTR;
        break;

    case SG_LIB_DID_SOFT_ERROR:
    case SG_LIB_DID_IMM_RETRY:
        /* retry immediately */
        err->status = SCSI_RETRY_SHORT;
        err->rc = -EAGAIN;
        break;

    case SG_LIB_DID_BUS_BUSY:
    case SG_LIB_DID_REQUEUE:
        /* need retry after a while */
        err->status = SCSI_RETRY_LONG;
        err->rc = -EBUSY;
        break;

    case SG_LIB_DID_PARITY:
    case SG_LIB_DID_ERROR:
    case SG_LIB_DID_PASSTHROUGH:  /* ? */
    default:
        err->status = SCSI_RETRY_LONG;
        err->rc = -EIO;
    }
}

/** Convert SCSI request errors to -errno code, check
 * https://www.ibm.com/support/pages/sites/default/files/inline-files/%24FILE/SC27-4641-00_1.pdf
 * for more information.
 */
static void fill_sense_key_scsi_error(struct scsi_req_sense *sbp,
                                      struct scsi_error *err)
{
    switch (sbp->sense_key) {
    case SPC_SK_NO_SENSE:
        err->status = SCSI_SUCCESS;
        err->rc = 0;
        break;

    case SPC_SK_RECOVERED_ERROR:
    case SPC_SK_UNIT_ATTENTION:
        err->status = SCSI_RETRY_SHORT;
        err->rc = -EAGAIN;
        break;

    case SPC_SK_NOT_READY:
        switch (sbp->additional_sense_code) {
        case 0x4:
            switch (sbp->additional_sense_code_qualifier) {
            /* In progress, almost ready, for example, scanning magazines */
            case 0x1:
                err->status = SCSI_RETRY_SHORT;
                err->rc = -EBUSY;
                break;
            /* All other errors: cause not reportable, offline, ... */
            default:
                err->status = SCSI_RETRY_LONG;
                err->rc = -EIO;
            }
            return;
        default:
            err->status = SCSI_RETRY_LONG;
            err->rc = -EIO;
        }
        break;

    case SPC_SK_ILLEGAL_REQUEST:
        err->status = SCSI_FATAL_ERROR;
        err->rc = -EINVAL;
        break;

    case SPC_SK_DATA_PROTECT:
        err->status = SCSI_FATAL_ERROR;
        err->rc = -EPERM;
        break;

    case SPC_SK_BLANK_CHECK:
    case SPC_SK_COPY_ABORTED:
    case SPC_SK_ABORTED_COMMAND:
    case SPC_SK_VOLUME_OVERFLOW:
    case SPC_SK_MISCOMPARE:
    case SPC_SK_MEDIUM_ERROR:
    case SPC_SK_HARDWARE_ERROR:
    default:
        err->status = SCSI_RETRY_LONG;
        err->rc = -EIO;
    }
}

/** Convert SCSI masked_status to -errno code */
static void fill_masked_status_scsi_error(uint8_t masked_status,
                                          struct scsi_error *err)
{
    switch (masked_status) {
    case GOOD:
    case CONDITION_GOOD:
    case INTERMEDIATE_GOOD:
    case INTERMEDIATE_C_GOOD:
        err->status = SCSI_SUCCESS;
        err->rc = 0;
        break;
    case BUSY:
    case RESERVATION_CONFLICT:
    case QUEUE_FULL:
        err->status = SCSI_RETRY_LONG;
        err->rc = -EBUSY;
        break;
    case COMMAND_TERMINATED:
    case CHECK_CONDITION:
    default:
        err->status = SCSI_RETRY_LONG;
        err->rc = -EIO;
        break;
    }
}

/** check if the SCSI request was errorneous */
static inline bool scsi_error_check(const struct sg_io_hdr *hdr)
{
    assert(hdr != NULL);

    return (hdr->masked_status != 0 || hdr->host_status != 0
        || hdr->driver_status != 0);
}

static void scsi_error_trace(const struct sg_io_hdr *hdr, json_t *message)
{
    json_t *log_object = json_object();
    const struct scsi_req_sense *sbp;
    char msg[SCSI_MSG_LEN];

    assert(hdr != NULL);

    pho_warn("SCSI ERROR: scsi_masked_status=%#hhx, adapter_status=%#hx, "
             "driver_status=%#hx", hdr->masked_status,
             hdr->host_status, hdr->driver_status);
    json_insert_element(log_object, "scsi_masked_status",
                        json_integer(hdr->masked_status));
    json_insert_element(log_object, "adapter_status",
                        json_integer(hdr->host_status));
    json_insert_element(log_object, "driver_status",
                        json_integer(hdr->driver_status));

    sbp = (const struct scsi_req_sense *)hdr->sbp;
    if (sbp == NULL) {
        pho_warn("sbp=NULL");
    } else {
        pho_warn("    req_sense_error=%#hhx, sense_key=%#hhx (%s)",
                 sbp->error_code, sbp->sense_key,
                 sg_get_sense_key_str(sbp->sense_key, sizeof(msg), msg));
        pho_warn("    asc=%#hhx, ascq=%#hhx (%s)", sbp->additional_sense_code,
                 sbp->additional_sense_code_qualifier,
                 sg_get_asc_ascq_str(sbp->additional_sense_code,
                                     sbp->additional_sense_code_qualifier,
                                     sizeof(msg), msg));

        json_insert_element(log_object, "req_sense_error",
                            json_integer(sbp->error_code));
        json_insert_element(log_object, "sense_key",
                            json_integer(sbp->sense_key));
        json_insert_element(log_object, "sense_key_str",
            json_string(sg_get_sense_key_str(sbp->sense_key, sizeof(msg),
                                             msg)));

        json_insert_element(log_object, "asc",
                            json_integer(sbp->additional_sense_code));
        json_insert_element(log_object, "ascq",
                            json_integer(sbp->additional_sense_code_qualifier));
        json_insert_element(log_object, "asc_ascq_str",
            json_string(sg_get_asc_ascq_str(sbp->additional_sense_code,
                sbp->additional_sense_code_qualifier, sizeof(msg), msg)));
    }

    json_object_set_new(message, "SCSI ERROR", log_object);
}

int scsi_execute(struct scsi_error *err, int fd, enum scsi_direction direction,
                 unsigned char *cdb, int cdb_len, struct scsi_req_sense *sbp,
                 int sb_len, void *dxferp, int dxfer_len,
                 unsigned int timeout_msec, json_t *message)
{
    struct phobos_global_context *context = phobos_context();
    struct sg_io_hdr hdr = {0};
    int rc;

    hdr.interface_id = 'S'; /* S for generic SCSI */
    hdr.dxfer_direction = scsi_dir2sg(direction);
    hdr.cmdp = cdb;
    hdr.cmd_len = cdb_len;
    hdr.sbp = (unsigned char *)sbp;
    hdr.mx_sb_len = sb_len;
    /* hdr.iovec_count = 0 implies no scatter gather */
    hdr.dxferp = dxferp;
    hdr.dxfer_len = dxfer_len;
    hdr.timeout = timeout_msec;
    /* hdr.flags = 0: default */

    if (context->mocks.mock_ioctl == NULL)
        context->mocks.mock_ioctl = ioctl;

    rc = context->mocks.mock_ioctl(fd, SG_IO, &hdr);
    if (rc) {
        err->rc = -errno;
        err->status = SCSI_FATAL_ERROR;
        json_insert_element(message, "SCSI ERROR",
                            json_string("ioctl() failed"));
        LOG_RETURN(rc = -errno, "ioctl() failed");
    }

    if (scsi_error_check(&hdr))
        scsi_error_trace(&hdr, message);

    if (hdr.masked_status == CHECK_CONDITION) {
        /* check sense_key value */
        fill_sense_key_scsi_error(sbp, err);
        if (err->rc)
            LOG_RETURN(err->rc,
                       "Sense key %#hhx (converted to %d)", sbp->sense_key,
                       err->rc);
    } else {
        fill_masked_status_scsi_error(hdr.masked_status, err);
        if (err->rc)
            LOG_RETURN(err->rc, "SCSI error %#hhx (converted to %d)",
                       hdr.masked_status, err->rc);
    }

    fill_host_status_scsi_error(hdr.host_status, err);
    if (err->rc)
        LOG_RETURN(err->rc,
                   "Adapter error %#hx (converted to %d)", hdr.host_status,
                   err->rc);

    return 0;
}
