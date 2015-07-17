/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos Local Resource Scheduler (LRS)
 */
#ifndef _PHO_LRS_H
#define _PHO_LRS_H

#include "pho_types.h"

/**
 * Query to write a given amount of data with a given layout.
 * @param(in) dss_hdl handle to initialized DSS.
 * @param(in) size   size of the object to be written.
 * @param(in) layout the requested layout
 * @param(out) loc   data locations (for write)
 *  (future: several extents if the file is splitted, striped...)
 * @return 0 on success, -1 * posix error code on failure
 */
int lrs_write_intent(void *dss_hdl, size_t size,
                     const struct layout_descr *layout, struct data_loc *loc);

/**
 * Query to read from a given set of media.
 * @param(in) dss_hdl handle to initialized DSS.
 * @param(in) layout data layout description
 * @param(in, out) loc  Location to read the data. Extent must be set as input.
 *                      loc->root_path is set by the function.
 *  (future: several locations if the file is splitted, striped...
 *   Moreover, the object may have several locations and layouts if it is
 *   duplicated).
 * @return 0 on success, -1 * posix error code on failure
 */
int lrs_read_intent(void *dss_hdl, const struct layout_descr *layout,
                    struct data_loc *loc);

/**
 * Declare the current operation (read/write) as finished.
 * @param(in) dss_hdl handle to initialized DSS.
 * @param(in) loc the location where the operation was done.
 * @return 0 on success, -1 * posix error code on failure
 */
int lrs_done(void *dss_hdl, struct data_loc *loc);

#endif
