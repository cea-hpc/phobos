/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos Local Device Manager.
 *
 * This modules implements low level device control on local host.
 */
#ifndef _PHO_LDM_H
#define _PHO_LDM_H

#include "pho_types.h"

/**
 * Retrieve device information from system.
 * @param(in) dev_type family of device to query.
 *                 Caller can pass PHO_DEV_UNSPEC if it doesn't know.
 *                 The function will then try to guess the type of device,
 *                 but the call is more expensive.
 * @param(in)  dev_path path to the device.
 * @param(out) dev_st   information about the device.
 *
 * @return 0 on success, -errno on failure.
 */
int ldm_device_query(enum dev_family dev_type, const char *dev_path,
                     struct dev_state *dev_st);

/**
 * Load a media into a device.
 * @param(in)  dev_type     Family of device.
 *                 Caller can pass PHO_DEV_UNSPEC if it doesn't know.
 *                 The function will then try to guess the type of device,
 *                 but the call is more expensive.
 * @param(in)  dev_path     Path to the device.
 * @param(in)  media_id     Id of the media to be loaded.
 *
 * @return 0 on success, -errno on failure.
 */
int ldm_device_load(enum dev_family dev_type, const char *dev_path,
                    const struct media_id *media_id);
/**
 * Unload a media from a device.
 * @param(in)  dev_type     Family of device.
 *                 Caller can pass PHO_DEV_UNSPEC if it doesn't know.
 *                 The function will then try to guess the type of device,
 *                 but the call is more expensive.
 * @param(in)  dev_path     Path to the device.
 * @param(in)  media_id     Id of the media to be unloaded.
 *
 * @return 0 on success, -errno on failure.
 */
int ldm_device_unload(enum dev_family dev_type, const char *dev_path,
                      const struct media_id *media_id);

/**
 * Mount a device as a given filesystem type.
 * @param(in) fs        type of filesystem.
 * @param(in) dev_path  path to the device.
 * @param(in) mnt_point mount point of the filesystem.
 *
 * @return 0 on success, -errno on failure.
 */
int ldm_fs_mount(enum fs_type fs, const char *dev_path, const char *mnt_point);

/**
 * Unmount a filesystem.
 * @param(in) fs        type of filesystem.
 * @param(in) dev_path  path to the device.
 * @param(in) mnt_point mount point of the filesystem.
 *
 * @return 0 on success, -errno on failure.
 */
int ldm_fs_umount(enum fs_type fs, const char *dev_path,
                   const char *mnt_point);

/**
 * Format a media to the desired filesystem type.
 * @param(in) dev_path  Path to the device where the desired media is loaded.
 * @param(in) label     Media label to apply.
 * @param(in) fs        Type of filesystem.
 *
 * @return 0 on success, -errno on failure.
 */
int ldm_fs_format(enum fs_type fs, const char *dev_path, const char *label);


/**
 * Populate drive serial / name cache in memory.
 */
int lintape_map_load(void);

/**
 * Free the device serial cache.
 */
void lintape_map_free(void);

/**
 * Retrieve device path like "/dev/IBMtape0" for a given lin_tape drive.
 * This will trigger ldm_serial_cache_load() if the cache was not
 * previously initialized.
 *
 * @param(in)   serial     Device serial, like "1234567".
 * @param(out)  path       Destination buffer for device path.
 * @param(in)   path_size  Destination buffer size.
 *
 * @return 0 on success, -errno on failure.
 */
int lintape_dev_lookup(const char *serial, char *path, size_t path_size);

/**
 * Retrieve device serial for a given lin_tape device name.
 * This will trigger ldm_serial_cache_load() if the cache was not
 * previously initialized.
 *
 * @param(in)   name         Device nale, like "IBMtape0".
 * @param(out)  serial       Destination buffer for serial.
 * @param(in)   serial_size  Destination buffer size.
 *
 * @return 0 on success, -errno on failure.
 */
int lintape_dev_rlookup(const char *name, char *serial, size_t serial_size);

#endif
