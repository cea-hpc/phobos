/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos backend adapters
 */
#ifndef _PHO_IO_H
#define _PHO_IO_H

#include "pho_types.h"
#include <errno.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>

#define PHO_IO_SYNC_FILE (1 << 0)
#define PHO_IO_SYNC_FS   (1 << 1)
#define PHO_IO_NO_REUSE  (1 << 2)

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
    int (*ioa_id2addr)(const char *id, const char *tag, struct pho_buff *addr);
    int (*ioa_open)(const struct data_loc *loc, int flags, void **hdl);
    int (*ioa_close)(void *hdl, int io_flags);
    int (*ioa_sync)(const struct data_loc *loc, int io_flags);
    ssize_t (*ioa_sendfile_r)(int tgt_fd, void *src_hdl, off_t *src_offset,
                          size_t count);
    ssize_t (*ioa_sendfile_w)(void *tgt_hdl, int src_fd, off_t *src_offset,
                          size_t count);
    ssize_t (*ioa_pread)(void *hdl, void *buf, size_t count, off_t offset);
    ssize_t (*ioa_pwrite)(void *hdl, const void *buf, size_t count,
                          off_t offset);
    ssize_t (*ioa_read)(void *hdl, void *buf, size_t count);
    ssize_t (*ioa_write)(void *hdl, const void *buf, size_t count);
    int (*ioa_fsetxattr)(void *hdl, const char *name, const void *value,
                     size_t size, int flags);
    int (*ioa_fstat)(void *hdl, struct stat *st);
    int (*ioa_remove)(const struct data_loc *loc);
};


/**
 * Retrieve IO functions for the given filesystem and addressing type.
 */
int get_io_adapter(enum fs_type fstype, enum address_type addrtype,
                   struct io_adapter *ioa);

/**
 * Check whether an IOA matches the requirements. It is up to the caller to
 * decide what to do on error, but the program might fail on assertions if an
 * invalid IOA is actually used.
 *
 * In order to be valid, an IOA has to provide all mandatory functions and at
 * least one way to do I/O, IOW any of:
 *   - ioa_sendfile_r / ioa_sendfile_w
 *   - ioa_pread / ioa_pwrite
 *   - ioa_read / ioa_write
 */
bool io_adapter_is_valid(const struct io_adapter *ioa);

/**
 * Convert  object id + extent tag to an address in the media.
 * All I/O adapters must implement this call.
 *
 * \param[in]   ioa     Suitable I/O adapter for the media
 * \param[in]   id      Null-terminated object ID to uniquely identify an extent
 * \param[in]   tag     Null-terminated tag
 * \param[out]  addr    Destination buffer to be released by the caller after
 *                      use, if the call has succeeded
 *
 * \return 0 on success, negative error code on failure
 */
static inline int ioa_id2addr(const struct io_adapter *ioa, const char *id,
                              const char *tag, struct pho_buff *addr)
{
    assert(ioa != NULL);
    assert(ioa->ioa_id2addr != NULL);
    return ioa->ioa_id2addr(id, tag, addr);
}

/**
 * Open an opaque descriptor to an object in the backend.
 * All I/O adapters must implement this call.
 *
 * \param[in]   ioa     Suitable I/O adapter for the media
 * \param[in]   loc     Object location identifier
 * \param[in]   flags   Standard open(2) flags
 * \param[out]  hdl     Opaque descriptor to be released with ioa_close()
 *                      after use, if this call has succeeded
 *
 * \return 0 on success, negative error code on failure
 */
static inline int ioa_open(const struct io_adapter *ioa,
                           const struct data_loc *loc, int flags, void **hdl)
{
    assert(ioa != NULL);
    assert(ioa->ioa_open != NULL);
    return ioa->ioa_open(loc, flags, hdl);
}

/**
 * Close a descriptor to an object in the backend.
 * All I/O adapters must implement this call.
 *
 * \param[in]       ioa     Suitable I/O adapter for the media
 * \param[in]       loc     Object location identifier
 * \param[in]       flags   PHO_IO_* flags
 * \param[in,out]   hdl     Opaque descriptor acquired from ioa_open()
 *
 * \return 0 on success, negative error code on failure
 */
static inline int ioa_close(const struct io_adapter *ioa, void *hdl,
                            int io_flags)
{
    assert(ioa != NULL);
    assert(ioa->ioa_close != NULL);
    return ioa->ioa_close(hdl, io_flags);
}

/**
 * Flush an object to stable storage.
 * I/O adapters may implement this call.
 *
 * \param[in]   ioa     Suitable I/O adapter for the media
 * \param[in]   loc     Object location identifier
 * \param[in]   flags   PHO_IO_SYNC_* flag
 *
 * \retval -ENOTSUP the I/O adapter does not provide this function
 * \return 0 on success, negative error code on failure
 */
static inline int ioa_sync(const struct io_adapter *ioa,
                           const struct data_loc *loc, int io_flags)
{
    assert(ioa != NULL);
    if (ioa->ioa_sync == NULL)
        return -ENOTSUP;

    return ioa->ioa_sync(loc, io_flags);
}

/**
 * Sendfile(2)-like operation from an object.
 * I/O adapters may implement this call.
 *
 * \param[in]     ioa         Suitable I/O adapter for the media
 * \param[in]     tgt_fd      Data stream destination file descriptor
 * \param[in,out] src_hdl     Data stream source opaque handle, from ioa_open()
 * \param[in,out] src_offset  File offset as interpreted by sendfile(2)
 * \param[in]     count       Bytes to transfer
 *
 * \retval -ENOTSUP the I/O adapter does not provide this function
 * \return number of read bytes, negative error code on failure
 */
static inline ssize_t ioa_sendfile_r(const struct io_adapter *ioa, int tgt_fd,
                                     void *src_hdl, off_t *src_offset,
                                     size_t count)
{
    assert(ioa != NULL);
    if (ioa->ioa_sendfile_r == NULL)
        return -ENOTSUP;

    return ioa->ioa_sendfile_r(tgt_fd, src_hdl, src_offset, count);
}

/**
 * Sendfile(2)-like operation to an object.
 * I/O adapters may implement this call.
 *
 * \param[in]     ioa         Suitable I/O adapter for the media
 * \param[in,out] tgt_hdl     Data stream destination opaque handle as
 *                            acquired from ioa_open()
 * \param[in]     src_fd      Data stream destination file descriptor
 * \param[in,out] src_offset  File offset as interpreted by sendfile(2)
 * \param[in]     count       Bytes to transfer
 *
 * \retval -ENOTSUP the I/O adapter does not provide this function
 * \return number of written bytes, negative error code on failure
 */
static inline ssize_t ioa_sendfile_w(const struct io_adapter *ioa,
                                     void *tgt_hdl, int src_fd,
                                     off_t *src_offset, size_t count)
{
    assert(ioa != NULL);
    if (ioa->ioa_sendfile_w == NULL)
        return -ENOTSUP;

    return ioa->ioa_sendfile_w(tgt_hdl, src_fd, src_offset, count);
}

/**
 * Read from an object without updating the current file offset.
 * I/O adapters may implement this call.
 *
 * \param[in]       ioa     Suitable I/O adapter for the media
 * \param[in,out]   hdl     Data stream source opaque handle, from ioa_open()
 * \param[out]      buf     Destination buffer
 * \param[in]       count   Maximum number of bytes to read
 * \param[in]       offset  Read start offset
 *
 * \retval -ENOTSUP the I/O adapter does not provide this function
 * \return number of read bytes, negative error code on failure
 */
static inline ssize_t ioa_pread(const struct io_adapter *ioa, void *hdl,
                                void *buf, size_t count, off_t offset)
{
    assert(ioa != NULL);
    if (ioa->ioa_pread == NULL)
        return -ENOTSUP;

    return ioa->ioa_pread(hdl, buf, count, offset);
}

/**
 * Write to an object without updating the current file offset.
 * I/O adapters may implement this call.
 *
 * \param[in]       ioa     Suitable I/O adapter for the media
 * \param[in,out]   hdl     Data stream destination handle, from ioa_open()
 * \param[in]       buf     Source buffer
 * \param[in]       count   Maximum number of bytes to write
 * \param[in]       offset  Write start offset
 *
 * \retval -ENOTSUP the I/O adapter does not provide this function
 * \return number of written bytes, negative error code on failure
 */
static inline ssize_t ioa_pwrite(const struct io_adapter *ioa, void *hdl,
                                 const void *buf, size_t count, off_t offset)
{
    assert(ioa != NULL);
    if (ioa->ioa_pwrite == NULL)
        return -ENOTSUP;

    return ioa->ioa_pwrite(hdl, buf, count, offset);
}


/**
 * Read linearly from an object.
 * I/O adapters may implement this call.
 *
 * \param[in]       ioa     Suitable I/O adapter for the media
 * \param[in,out]   hdl     Data stream source opaque handle, from ioa_open()
 * \param[out]      buf     Destination buffer
 * \param[in]       count   Maximum number of bytes to read
 *
 * \retval -ENOTSUP the I/O adapter does not provide this function
 * \return number of read bytes, negative error code on failure
 */
static inline ssize_t ioa_read(const struct io_adapter *ioa, void *hdl,
                               void *buf, size_t count)
{
    assert(ioa != NULL);
    if (ioa->ioa_read == NULL)
        return -ENOTSUP;

    return ioa->ioa_read(hdl, buf, count);
}

/**
 * Write linearly to an object.
 * I/O adapters may implement this call.
 *
 * \param[in]       ioa     Suitable I/O adapter for the media
 * \param[in,out]   hdl     Data stream destination handle, from ioa_open()
 * \param[out]      buf     Source buffer
 * \param[in]       count   Maximum number of bytes to write
 *
 * \retval -ENOTSUP the I/O adapter does not provide this function
 * \return number of written bytes, negative error code on failure
 */
static inline ssize_t ioa_write(const struct io_adapter *ioa, void *hdl,
                                const void *buf, size_t count)
{
    assert(ioa != NULL);
    if (ioa->ioa_write == NULL)
        return -ENOTSUP;

    return ioa->ioa_write(hdl, buf, count);
}

/**
 * Set an extended attribute
 * Note: name is without xattr prefix (let the backend choose it)
 * I/O adapters may implement this call.
 *
 * \param[in]       ioa     Suitable I/O adapter for the media
 * \param[in,out]   hdl     Target handle, from ioa_open()
 * \param[in]       name    EA name as a null-terminated string
 * \param[in]       value   EA value, may be binary
 * \param[in]       size    EA value length
 * \param[in]       flags   Standard fsetxattr(2) flags
 *
 * \retval -ENOTSUP the I/O adapter does not provide this function
 * \return 0 on success, negative error code on failure
 */
static inline int ioa_fsetxattr(const struct io_adapter *ioa, void *hdl,
                                const char *name, const void *value,
                                size_t size, int flags)
{
    assert(ioa != NULL);
    if (ioa->ioa_fsetxattr == NULL)
        return -ENOTSUP;

    return ioa->ioa_fsetxattr(hdl, name, value, size, flags);
}

/**
 * Fill struct stat from handle.
 * All I/O adapters must implement this call.
 *
 * \param[in]       ioa Suitable I/O adapter for the media
 * \param[in,out]   hdl Handle to the target object
 * \param[out]      st  Destination structure to fill
 *
 * \return 0 on success, negative error code on failure
 */
static inline int ioa_fstat(const struct io_adapter *ioa, void *hdl,
                            struct stat *st)
{
    assert(ioa != NULL);
    assert(ioa->ioa_fstat != NULL);
    return ioa->ioa_fstat(hdl, st);
}

/**
 * Remove object from the backend.
 * All I/O adapters must implement this call.
 *
 * \param[in]       ioa Suitable I/O adapter for the media
 * \param[in,out]   loc Location of the object to remove
 *
 * \return 0 on success, negative error code on failure
 */
static inline int ioa_remove(const struct io_adapter *ioa,
                             const struct data_loc *loc)
{
    assert(ioa != NULL);
    assert(ioa->ioa_remove == NULL);
    return ioa->ioa_remove(loc);
}

#endif
