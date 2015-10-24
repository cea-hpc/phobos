/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
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
static int scsi_host_status2errno(uint16_t host_status)
{
    switch (host_status) {
    case SG_LIB_DID_OK:           return 0;
    case SG_LIB_DID_NO_CONNECT:   return -ECONNABORTED;
    case SG_LIB_DID_BUS_BUSY:     return -EBUSY;
    case SG_LIB_DID_TIME_OUT:     return -ETIMEDOUT;
    case SG_LIB_DID_BAD_TARGET:   return -EINVAL;
    case SG_LIB_DID_ABORT:        return -ECANCELED;
    case SG_LIB_DID_PARITY:       return -EIO;
    case SG_LIB_DID_ERROR:        return -EIO;
    case SG_LIB_DID_RESET:        return -ECANCELED;
    case SG_LIB_DID_BAD_INTR:     return -EINTR;
    case SG_LIB_DID_PASSTHROUGH:  return -EIO; /* ? */
    case SG_LIB_DID_SOFT_ERROR:   return -EAGAIN;
    case SG_LIB_DID_IMM_RETRY:    return -EAGAIN;
    case SG_LIB_DID_REQUEUE:      return -EAGAIN;
    default:                      return -EIO;
    }
}

/** Convert SCSI masked_status to -errno code */
static int scsi_masked_status2errno(uint8_t masked_status)
{
    switch (masked_status) {
    case GOOD:
    case CONDITION_GOOD:
    case INTERMEDIATE_GOOD:
    case INTERMEDIATE_C_GOOD:
        return 0;
    case BUSY:
    case RESERVATION_CONFLICT:
    case QUEUE_FULL:
        return -EBUSY;
    case COMMAND_TERMINATED:
    case CHECK_CONDITION:
    default:
        return -EIO;
    }
}

int scsi_execute(int fd, enum scsi_direction direction,
                 unsigned char *cdb, int cdb_len,
                 struct scsi_req_sense *sbp, int sb_len,
                 void *dxferp, int dxfer_len,
                 unsigned int timeout_msec)
{
    int rc;
    struct sg_io_hdr hdr = {0};

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

    rc = ioctl(fd, SG_IO, &hdr);
    if (rc)
        LOG_RETURN(rc = -errno, "ioctl() failed");

    if (hdr.masked_status != 0 || hdr.host_status != 0
        || hdr.driver_status != 0)
        pho_warn("scsi_masked_status=%#hhx, adapter_status=%#hx, "
                 "driver_status=%#hx, req_sense_error=%#hhx, "
                 "sense_key=%#hhx", hdr.masked_status, hdr.host_status,
                 hdr.driver_status, sbp->error_code, sbp->sense_key);

    rc = scsi_masked_status2errno(hdr.masked_status);
    if (rc)
        LOG_RETURN(rc, "SCSI error %#hhx (converted to %d)", hdr.masked_status,
                   rc);

    rc = scsi_host_status2errno(hdr.host_status);
    if (rc)
        LOG_RETURN(rc, "Adapter error %#hx (converted to %d)", hdr.host_status,
                   rc);
    return 0;
}
