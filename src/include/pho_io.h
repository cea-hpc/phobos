/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2017 CEA/DAM.
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
 * \brief  Phobos backend adapters
 */
#ifndef _PHO_IO_H
#define _PHO_IO_H

#include "pho_types.h"
#include "pho_attrs.h"
#include <errno.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>

enum pho_io_flags {
    PHO_IO_MD_ONLY    = (1 << 0),   /**< Only operate on object MD */
    PHO_IO_REPLACE    = (1 << 1),   /**< Replace the entry if it exists */
    PHO_IO_SYNC_FILE  = (1 << 2),   /**< Sync file data to media on close */
    PHO_IO_NO_REUSE   = (1 << 3),   /**< Drop file contents from system cache */
    PHO_IO_DELETE     = (1 << 4),   /**< Delete extent from media */
};

/** IO information passed to IO completion callback */
struct io_cb_data {
    int status;
};

/** IO callback function prototype */
typedef int (*io_callback_t)(const struct io_cb_data *cb_data, void *user_data);


/**
 * Describe an I/O operation. This can be either or both data and metadata
 * request.
 * For MD GET request, the metadata buffer is expected to contain the requested
 * keys. The associated values will be ignored and replaced by the retrieved
 * ones.
 */
struct pho_io_descr {
    enum pho_io_flags    iod_flags;    /**< PHO_IO_* flags */
    int                  iod_fd;       /**< Local FD */
    off_t                iod_off;      /**< Operation offset */
    size_t               iod_size;     /**< Operation size */
    struct pho_ext_loc  *iod_loc;      /**< Extent location */
    struct pho_attrs     iod_attrs;    /**< In/Out metadata operations buffer */
};


/**
 * An I/O adapter (IOA) is a vector of functions that provide access to a media.
 * They should be invoked via their corresponding wrappers below. Refer to
 * them for more precise explanation about each call.
 *
 * Some calls are strictly mandatory, and assertion failures may happen if they
 * are found to be missing. Others aren't for which the wrappers will return
 * -ENOTSUP if they are not implemented in an IOA.
 *
 * Refer to the documentation of wrappers for what exactly is expected, done and
 * returned by the functions.
 */
struct io_adapter {
    int (*ioa_put)(const char *id, const char *tag, struct pho_io_descr *iod,
                   io_callback_t io_cb, void *user_data);

    int (*ioa_get)(const char *id, const char *tag, struct pho_io_descr *iod,
                   io_callback_t io_cb, void *user_data);

    int (*ioa_del)(const char *id, const char *tag, struct pho_ext_loc *loc);

    int (*ioa_flush)(const struct pho_ext_loc *loc);
};

/**
 * Retrieve IO functions for the given filesystem type.
 */
int get_io_adapter(enum fs_type fstype, struct io_adapter *ioa);

/**
 * Check whether an IOA matches the requirements. It is up to the caller to
 * decide what to do on error, but the program might fail on assertions if an
 * invalid IOA is actually used.
 *
 * In order to be valid, an IOA has to provide mandatory functions: put/get/del.
 */
bool io_adapter_is_valid(const struct io_adapter *ioa);

/**
 * Put an object to a media.
 * All I/O adapters must implement this call.
 *
 * The I/O descriptor must be filled as follow:
 * iod_fd       Source data stream file descriptor
 * iod_off      Start offset in source fd.
 * iod_size     Amount of data to be written.
 * iod_loc      Extent location identifier. The call must set
 *              loc->extent.address to indicate where the extent has been
 *              written.
 * iod_attrs    List of metadata blob k/v to set for the extent. If a value is
 *              NULL, previous value of the attribute is removed.
 * iod_flags    Bitmask of PHO_IO_* flags.
 *
 *
 * \param[in]       ioa     Suitable I/O adapter for the media
 * \param[in]       id      Null-terminated object ID
 * \param[in]       tag     Null-terminated extent tag (may be NULL)
 *
 * \param[in,out]   iod     I/O descriptor (see above)
 * \param[in]       io_cb   Callback for asynchronous put operation
 *                          If io_cb is NULL the put operation is performed
                            synchronously
 * \param[in,out]   cb_data User data to be passed to the callback function
 *
 * \return 0 on success, negative error code on failure
 */
static inline int ioa_put(const struct io_adapter *ioa, const char *id,
                          const char *tag, struct pho_io_descr *iod,
                          io_callback_t io_cb, void *cb_data)
{
    assert(ioa != NULL);
    assert(ioa->ioa_put != NULL);
    return ioa->ioa_put(id, tag, iod, io_cb, cb_data);
}

/**
 * Get an object from a media.
 * All I/O adapters must implement this call.
 *
 * The I/O descriptor must be filled as follow:
 * iod_fd       Destination data stream file descriptor
 *              (must be positionned at desired write offset)
 * iod_size     Amount of data to be read.
 * iod_loc      Extent location identifier. The call may set
 *              loc->extent.address if it is missing
 * iod_attrs    List of metadata blob names to be retrieved. The function fills
 *              the value fields with the retrieved values.
 * iod_flags    Bitmask of PHO_IO_* flags
 *
 *
 * \param[in]       ioa     Suitable I/O adapter for the media
 * \param[in]       id      Null-terminated object ID
 * \param[in]       tag     Null-terminated extent tag (may be NULL)
 * \param[in,out]   iod     I/O descriptor (see above)
 * \param[in]       io_cb   Callback for asynchronous get operation.
 *                          If io_cb is NULL the operation is performed
                            synchronously
 * \param[in,out]   cb_data User data to be passed to the callback function
 *
 * \return 0 on success, negative error code on failure
 */
static inline int ioa_get(const struct io_adapter *ioa, const char *id,
                          const char *tag, struct pho_io_descr *iod,
                          io_callback_t io_cb, void *cb_data)
{
    assert(ioa != NULL);
    assert(ioa->ioa_get != NULL);
    return ioa->ioa_get(id, tag, iod, io_cb, cb_data);
}

/**
 * Remove object from the backend.
 * All I/O adapters must implement this call.
 *
 * \param[in]  ioa  Suitable I/O adapter for the media.
 * \param[in]  id   Null-terminated object ID.
 * \param[in]  tag  Null-terminated extent tag (may be NULL).
 * \param[in]  loc  Location of the extent to remove. The call may set
 *                  loc->extent.address if it is missing.
 *
 * \return 0 on success, negative error code on failure.
 */
static inline int ioa_del(const struct io_adapter *ioa, const char *id,
                          const char *tag, struct pho_ext_loc *loc)
{
    assert(ioa != NULL);
    assert(ioa->ioa_del != NULL);
    return ioa->ioa_del(id, tag, loc);
}

/**
 * Flush all pending IOs to stable storage.
 * I/O adapters may implement this call.
 *
 * \param[in]   ioa     Suitable I/O adapter for the media
 * \param[in]   loc     Extent location identifier
 *
 * \retval -ENOTSUP the I/O adapter does not provide this function
 * \return 0 on success, negative error code on failure
 */

static inline int ioa_flush(const struct io_adapter *ioa,
                            const struct pho_ext_loc *loc)
{
    assert(ioa != NULL);
    if (ioa->ioa_flush == NULL)
        return -ENOTSUP;

    return ioa->ioa_flush(loc);
}
#endif
