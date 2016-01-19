/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2016 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos Local Device Manager.
 *
 * This modules implements low level device control on local host.
 */
#ifndef _PHO_LDM_H
#define _PHO_LDM_H

#include "pho_types.h"
#include <assert.h>

/**
 * \defgroup dev_adapter (Device Adapter API)
 * @{
 */

/** device information */
struct ldm_dev_state {
    enum dev_family      lds_family; /**< device family */
    char                *lds_model;  /**< device model */
    char                *lds_serial; /**< device serial */
    bool                 lds_loaded; /**< whether a media is loaded in the
                                          device */
};

/**
 * A device adapter is a vector of functions to operate on a device.
 * They should be invoked via their corresponding wrappers. Refer to
 * them for more precise explanation about each call.
 *
 * dev_query are dev_lookup are mandatory.
 * Other calls can be NULL if no operation is required, and do noop.
 */
struct dev_adapter {
    int (*dev_lookup)(const char *dev_id, char *dev_path, size_t path_size);
    int (*dev_query)(const char *dev_path, struct ldm_dev_state *lds);
    int (*dev_load)(const char *dev_path);
    int (*dev_eject)(const char *dev_path);
};

/**
 * Retrieve device adapter for the given device type.
 * @param[in]  dev_type Family of device.
 * @param[out] dev      Vector of functions to handle the given device type.
 *
 * @return 0 on success, negative error code on failure.
 */
int get_dev_adapter(enum dev_family dev_type, struct dev_adapter *dev);

/**
 * Get device path from its identifier (eg. serial number).
 * @param[in]   dev         Device adapter.
 * @param[in]   dev_id      Drive identifier.
 * @param[out]  dev_path    Path to access the device.
 * @param[in]   path_size   Max bytes that can be written to dev_path.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_dev_lookup(const struct dev_adapter *dev,
                                 const char *dev_id, char *dev_path,
                                 size_t path_size)
{
    assert(dev != NULL);
    assert(dev->dev_lookup != NULL);
    return dev->dev_lookup(dev_id, dev_path, path_size);
}

/**
 * Query a device.
 * @param[in]   dev         Device adapter.
 * @param[in]   dev_path    Device path.
 * @param[out]  lds         Device status.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_dev_query(const struct dev_adapter *dev,
                                const char *dev_path,
                                struct ldm_dev_state *lds)
{
    assert(dev != NULL);
    assert(dev->dev_query != NULL);
    return dev->dev_query(dev_path, lds);
}

/**
 * Load a device with a medium on front of it.
 * @param[in]   dev         Device adapter.
 * @param[in]   dev_path    Device path.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_dev_load(const struct dev_adapter *dev,
                               const char *dev_path)

{
    assert(dev != NULL);
    if (dev->dev_load == NULL)
        return 0;
    return dev->dev_load(dev_path);
}

/**
 * Eject the medium currently loaded in the device.
 * @param[in]   dev         Device adapter.
 * @param[in]   dev_path    Device path.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_dev_eject(const struct dev_adapter *dev,
                                const char *dev_path)

{
    assert(dev != NULL);
    if (dev->dev_eject == NULL)
        return 0;
    return dev->dev_eject(dev_path);
}

/** @}*/

/* No python bindings for the following methods unless we need them. */
#ifndef SWIG

/**
 * \defgroup lib_adapter (Library Adapter API)
 * @{
 */

/**
 * Opaque library handler.
 */
struct lib_handle {
    void *lh_lib;
};

/**
 * Type of location in a library.
 */
enum med_location {
    MED_LOC_UNKNOWN = 0,
    MED_LOC_DRIVE   = 1,
    MED_LOC_SLOT    = 2,
    MED_LOC_ARM     = 3,
    MED_LOC_IMPEXP  = 4
};

/**
 * Location descriptor in a library.
 *
 * lia_addr examples:
 * SCSI library: 16 bits integer handled as 64 bits.
 * STK library: 4 integers (e.g. 0,1,10,5) encoded as 4x16 bits.
 */
struct lib_item_addr {
    enum med_location    lia_type; /**< type of location */
    uint64_t             lia_addr; /**< address of location */
};

/**
 * Device information in a library.
 */
struct lib_drv_info {
    struct lib_item_addr ldi_addr;     /**< location of the drive */
    bool                 ldi_full;     /**< true if a media is in the drive */
    struct media_id      ldi_media_id; /**< media label, if drive is full */
};

/**
 * A library adapter is a vector of functions to control a tape library.
 * They should be invoked via their corresponding wrappers. Refer to
 * them for more precise explanation about each call.
 *
 * lib_open, lib_close and lib_media_move do noop if they are NULL.
 *
 * @TODO Instead each lib_adapter includes a full vector of functions,
 * this should be turned to a lib_adapter object including a lib_handle
 * and a pointer to a vector of functions.
 */
struct lib_adapter {
    /* adapter functions */
    int (*lib_open)(struct lib_handle *lib, const char *dev);
    int (*lib_close)(struct lib_handle *lib);
    int (*lib_drive_lookup)(struct lib_handle *lib, const char *drive_serial,
                            struct lib_drv_info *drv_info);
    int (*lib_media_lookup)(struct lib_handle *lib, const char *media_label,
                            struct lib_item_addr *med_addr);
    int (*lib_media_move)(struct lib_handle *lib,
                          const struct lib_item_addr *src_addr,
                          const struct lib_item_addr *tgt_addr);

    /* adapter private state */
    struct lib_handle lib_hdl;
};

/**
 * Retrieve library adapter for the given library type.
 * @param[in]  lib_type Family of library.
 * @param[out] lib      Vector of functions to handle the library.
 *
 * @return 0 on success, negative error code on failure.
 */
int get_lib_adapter(enum lib_type lib_type, struct lib_adapter *lib);

/**
 * Open a library handler.
 * Library access may rely on a caching of item addresses.
 * A library should be closed and reopened to refresh this cache
 * in case a change or inconsistency is detected.
 * @param[in,out] lib library adapter.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_lib_open(struct lib_adapter *lib, const char *dev)
{
    assert(lib != NULL);
    if (lib->lib_open == NULL)
        return 0;
    return lib->lib_open(&lib->lib_hdl, dev);
}

/**
 * Close a library handler.
 * Close access to the library and clean address cache.
 * @param[in,out] lib   Opened library adapter.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_lib_close(struct lib_adapter *lib)
{
    assert(lib != NULL);
    if (lib->lib_close == NULL)
        return 0;
    return lib->lib_close(&lib->lib_hdl);
}

/**
 * Get the location of a device in library from its serial number.
 * @param[in,out] lib          Opened library handler.
 * @param[in]     drive_serial Serial number of the drive to lookup.
 * @param[out]    drv_info     Information about the drive.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_lib_drive_lookup(struct lib_adapter *lib,
                                       const char *drive_serial,
                                       struct lib_drv_info *drv_info)
{
    assert(lib != NULL);
    assert(lib->lib_drive_lookup != NULL);
    return lib->lib_drive_lookup(&lib->lib_hdl, drive_serial, drv_info);
}

/**
 * Get the location of a media in library from its label.
 * @param[in,out] Lib           Library adapter.
 * @param[in]     media_label   Label of the media.
 * @param[out]    media_addr    Location of the media in library.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_lib_media_lookup(struct lib_adapter *lib,
                                       const char *media_label,
                                       struct lib_item_addr *med_addr)
{
    assert(lib != NULL);
    assert(lib->lib_media_lookup != NULL);
    return lib->lib_media_lookup(&lib->lib_hdl, media_label, med_addr);
}

/**
 * Move a media in library from a source location to a target location.
 * @param[in,out] lib       Opened library adapter.
 * @param[in]     src_addr  Source address of the move.
 * @param[in]     tgt_addr  Target address of the move.
 */
static inline int ldm_lib_media_move(struct lib_adapter *lib,
                                     const struct lib_item_addr *src_addr,
                                     const struct lib_item_addr *tgt_addr)
{
    assert(lib != NULL);
    if (lib->lib_media_move == NULL)
        return 0;
    return lib->lib_media_move(&lib->lib_hdl, src_addr, tgt_addr);
}

/** @}*/

/**
 * \defgroup fs_adapter (FileSystem Adapter API)
 * @{
 */

/**
 * A FS adapter is a vector of functions to manage a FileSystem.
 * Managing a filesystem requires a media is loaded into a device.
 *
 * Functions should be invoked via their corresponding wrappers. Refer to
 * them for more precise explanation about each call.
 *
 * fs_format do noop if NULL.
 */
struct fs_adapter {
    int (*fs_mount)(const char *dev_path, const char *mnt_path);
    int (*fs_umount)(const char *dev_path, const char *mnt_path);
    int (*fs_format)(const char *dev_path, const char *label);
    int (*fs_mounted)(const char *dev_path, char *mnt_path,
                      size_t mnt_path_size);
    int (*fs_df)(const char *mnt_path, size_t *spc_used, size_t *spc_free);
};

/**
 * Retrieve adapter for the filesystem type.
 *
 * @return 0 on success, negative error code on failure.
 */
int get_fs_adapter(enum fs_type fs_type, struct fs_adapter *fsa);

/**
 * Mount a device as a given filesystem type.
 * @param[in] fsa       File system adapter.
 * @param[in] dev_path  Path to the device.
 * @param[in] mnt_path  Mount point for the filesystem.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_fs_mount(const struct fs_adapter *fsa,
                               const char *dev_path, const char *mnt_point)
{
    assert(fsa != NULL);
    if (fsa->fs_mount == NULL)
        return 0;
    return fsa->fs_mount(dev_path, mnt_point);
}

/**
 * Unmount a filesystem.
 * @param[in] fsa       File system adapter.
 * @param[in] dev_path  Path to the device.
 * @param[in] mnt_path  Mount point for the filesystem.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_fs_umount(const struct fs_adapter *fsa,
                                const char *dev_path, const char *mnt_point)
{
    assert(fsa != NULL);
    if (fsa->fs_umount == NULL)
        return 0;
    return fsa->fs_umount(dev_path, mnt_point);
}

/**
 * Format a media to the desired filesystem type.
 * @param[in] fsa       File system adapter.
 * @param[in] dev_path  Path to the device.
 * @param[in] label     Media label to apply.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_fs_format(const struct fs_adapter *fsa,
                                const char *dev_path, const char *label)
{
    assert(fsa != NULL);
    if (fsa->fs_format == NULL)
        return 0;
    return fsa->fs_format(dev_path, label);
}

/**
 * Indicate if a device is currently mounted as a filesystem.
 * @param[in]  fsa           File system adapter.
 * @param[in]  dev_path      Path to the device.
 * @param[out] mnt_path      Mount point for the filesystem, if mounted.
 * @param[in]  mnt_path_size Size of mnt_path argument.
 *
 * @return 0 if the device is mounted, negative error code on failure.
 * @retval 0            the device is mounted.
 * @retval -ENOENT      the device is not mounted.
 * @retval -EMEDIUMTYPE the device is mounted with an unexpected FS type.
 */
static inline int ldm_fs_mounted(const struct fs_adapter *fsa,
                                 const char *dev_path, char *mnt_path,
                                 size_t mnt_path_size)
{
    assert(fsa != NULL);
    assert(fsa->fs_mounted != NULL);
    return fsa->fs_mounted(dev_path, mnt_path, mnt_path_size);
}

/**
 * Get used and available space in a filesystem.
 * @param[in] fsa        File system adapter.
 * @param[in] mnt_path   Moint point of the filesystem.
 * @param[out] spc_used  Used space in the filesystem (physical).
 * @param[out] spc_free  Available space in the filesystem.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_fs_df(const struct fs_adapter *fsa, const char *mnt_path,
                            size_t *spc_used, size_t *spc_free)
{
    assert(fsa != NULL);
    assert(fsa->fs_df != NULL);
    return fsa->fs_df(mnt_path, spc_used, spc_free);
}

#endif /* ^SWIG */

/** @}*/
#endif
