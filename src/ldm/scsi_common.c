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
#include <scsi/sg.h>
#include <sys/ioctl.h>

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

int scsi_execute(int fd, enum scsi_direction direction,
                 unsigned char *cdb, int cdb_len,
                 struct scsi_req_sense *sbp, int sb_len,
                 void *dxferp, int dxfer_len)
{
    int rc;
    struct sg_io_hdr hdr = {0};

    hdr.interface_id = 'S'; /* S for generic SCSI */
    hdr.dxfer_direction = scsi_dir2sg(direction);
    hdr.cmdp = cdb;
    hdr.cmd_len = cdb_len;
    hdr.sbp = (unsigned char *)sbp;
    hdr.mx_sb_len = sb_len;
    hdr.dxferp = dxferp;
    hdr.dxfer_len = dxfer_len;

    rc = ioctl(fd, SG_IO, &hdr);
    if (rc)
        LOG_RETURN(rc = -errno, "ioctl() failed");
    return 0;
}
