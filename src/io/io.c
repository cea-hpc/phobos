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
 * \brief  Phobos I/O adapters.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>

#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_io.h"
#include "pho_module_loader.h"

#define IO_BLOCK_SIZE_ATTR_KEY "io_block_size"
#define FS_BLOCK_SIZE_ATTR_KEY "fs_block_size"

/**
 * List of configuration parameters for this module
 */
enum pho_cfg_params_io {
    /* Actual parameters */
    PHO_CFG_IO_io_block_size,
    PHO_CFG_IO_fs_block_size,

    /* Delimiters, update when modifying options */
    PHO_CFG_IO_FIRST = PHO_CFG_IO_io_block_size,
    PHO_CFG_IO_LAST  = PHO_CFG_IO_fs_block_size,
};

const struct pho_config_item cfg_io[] = {
    [PHO_CFG_IO_io_block_size] = {
        .section = "io",
        .name    = IO_BLOCK_SIZE_ATTR_KEY,
        .value   = "dir=0,tape=0,rados_pool=0" /** default value = not set */
    },
    [PHO_CFG_IO_fs_block_size] = {
        .section = "io",
        .name    = FS_BLOCK_SIZE_ATTR_KEY,
        .value   = "dir=1024,tape=524288,rados_pool=1024"
    },
};

int get_cfg_io_block_size(size_t *size, enum rsc_family family)
{
    char *string_io_block_size;
    int64_t sz;
    int rc;

    rc = PHO_CFG_GET_SUBSTRING_VALUE(cfg_io, PHO_CFG_IO, io_block_size,
                                     family, &string_io_block_size);
    if (rc == 0) {
        sz = str2int64(string_io_block_size);
        if (sz < 0) {
            *size = 0;
            rc = -EINVAL;
            pho_error(rc, "Invalid value '%s' for parameter 'io_block_size_%s'",
                      string_io_block_size, rsc_family2str(family));
        } else {
            *size = sz;
        }
        free(string_io_block_size);

    } else if (rc == -ENODATA) {
        /* If not forced by configuration, the io adapter will retrieve it
         * from the backend storage system.
         */
        *size = 0;
        rc = 0;
    }

    return rc;
}

int get_cfg_fs_block_size(enum rsc_family family, size_t *size)
{
    char *value;
    int rc;

    rc = PHO_CFG_GET_SUBSTRING_VALUE(cfg_io, PHO_CFG_IO, fs_block_size,
                                     family, &value);
    if (rc == 0) {
        *size = str2int64(value);
        free(value);
        if (size < 0)
            LOG_RETURN(-EINVAL,
                       "Invalid value for fs_block_size with '%s' family",
                       rsc_family2str(family));
    } else if (rc == -ENODATA) {
        *size = 0;
        rc = 0;
    }

    return rc;
}

void update_io_size(struct pho_io_descr *iod, size_t *io_size)
{
    if (*io_size != 0)
        /* io_size already specified in the configuration */
        return;

    *io_size = ioa_preferred_io_size(iod->iod_ioa, iod);
    if (*io_size <= 0)
        /* fallback: get the system page size */
        *io_size = sysconf(_SC_PAGESIZE);
}

void get_preferred_io_block_size(size_t *io_size, enum rsc_family family,
                                 const struct io_adapter_module *ioa,
                                 struct pho_io_descr *iod)
{
    ssize_t sz;

    get_cfg_io_block_size(io_size, family);
    if (*io_size != 0)
        return;

    sz = ioa_preferred_io_size(ioa, iod);
    if (sz > 0) {
        *io_size = sz;
        return;
    }

    /* fallback: get the system page size */
    *io_size = sysconf(_SC_PAGESIZE);
}

/** retrieve IO functions for the given filesystem and addressing type */
int get_io_adapter(enum fs_type fstype, struct io_adapter_module **ioa)
{
    int rc = 0;

    switch (fstype) {
    case PHO_FS_POSIX:
        rc = load_module("io_adapter_posix", sizeof(**ioa), phobos_context(),
                         (void **)ioa);
        break;
    case PHO_FS_LTFS:
        rc = load_module("io_adapter_ltfs", sizeof(**ioa), phobos_context(),
                         (void **)ioa);
        break;
    case PHO_FS_RADOS:
        rc = load_module("io_adapter_rados", sizeof(**ioa), phobos_context(),
                         (void **)ioa);
        break;
    default:
        pho_error(-EINVAL, "Invalid FS type %#x", fstype);
        return -EINVAL;
    }

    return rc;
}

int copy_extent(struct io_adapter_module *ioa_source,
                struct pho_io_descr *iod_source,
                struct io_adapter_module *ioa_target,
                struct pho_io_descr *iod_target,
                enum rsc_family family)
{
    size_t left_to_read;
    size_t buf_size;
    char *buffer;
    int rc2;
    int rc;

    /* retrieve the preferred IO size to allocate the buffer */
    get_preferred_io_block_size(&buf_size, family, ioa_target, iod_target);

    buffer = xcalloc(buf_size, sizeof(*buffer));

    /* prepare the retrieval of source xattrs */
    pho_json_to_attrs(&iod_source->iod_attrs,
                      "{\"id\":\"\", \"user_md\":\"\", \"md5\":\"\"}");

    /* open source IO descriptor then copy address to the target */
    rc = ioa_open(ioa_source, NULL, iod_source, false);
    if (rc) {
        iod_source->iod_rc = rc;
        LOG_GOTO(memory, rc, "Unable to open source object");
    }

    iod_target->iod_loc->addr_type = iod_source->iod_loc->addr_type;
    iod_target->iod_loc->extent->address.size =
        iod_source->iod_loc->extent->address.size;
    iod_target->iod_loc->extent->address.buff =
        xstrdup(iod_source->iod_loc->extent->address.buff);
    iod_target->iod_attrs = iod_source->iod_attrs;

    left_to_read = iod_source->iod_size;

    /* open target IO descriptor */
    rc = ioa_open(ioa_target, NULL, iod_target, true);
    if (rc) {
        iod_target->iod_rc = rc;
        LOG_GOTO(close_source, rc, "Unable to open target object");
    }

    rc = ioa_set_md(ioa_target, NULL, iod_target);
    if (rc) {
        iod_target->iod_rc = rc;
        LOG_GOTO(close_source, rc, "Unable to set attrs to target object");
    }

    /* do the actual copy */
    while (left_to_read) {
        size_t iter_size = buf_size < left_to_read ? buf_size : left_to_read;
        ssize_t nb_read_bytes;

        nb_read_bytes = ioa_read(ioa_source, iod_source, buffer, iter_size);
        if (nb_read_bytes < 0) {
            iod_source->iod_rc = rc;
            LOG_GOTO(close, nb_read_bytes, "Unable to read %zu bytes",
                     iter_size);
        }

        left_to_read -= nb_read_bytes;

        rc = ioa_write(ioa_target, iod_target, buffer, nb_read_bytes);
        if (rc != 0) {
            iod_target->iod_rc = rc;
            LOG_GOTO(close, rc, "Unable to write %zu bytes", nb_read_bytes);
        }
    }

close:
    rc2 = ioa_close(ioa_target, iod_target);
    if (rc)
        rc2 = ioa_del(ioa_target, iod_target);
    if (!rc && rc2) {
        iod_target->iod_rc = rc;
        rc = rc2;
    }

close_source:
    rc2 = ioa_close(ioa_source, iod_source);
    if (!rc && rc2) {
        iod_source->iod_rc = rc;
        rc = rc2;
    }

memory:
    free(buffer);

    return rc;
}

int set_object_md(const struct io_adapter_module *ioa, struct pho_io_descr *iod,
                  struct object_metadata *object_md)
{
    struct extent *extent = iod->iod_loc->extent;
    char str_buffer[64];
    GString *user_md;
    int rc = 0;

    /**
     * Build the extent attributes from the object ID and the user provided
     * attributes. This information will be attached to backend objects for
     * "self-description"/"rebuild" purpose.
     */
    user_md = g_string_new(NULL);
    rc = pho_attrs_to_json(&object_md->object_attrs, user_md,
                           PHO_ATTR_BACKUP_JSON_FLAGS);
    if (rc) {
        g_string_free(user_md, true);
        LOG_RETURN(rc, "Unable to construct user attrs");
    }

    pho_attr_set(&iod->iod_attrs, PHO_EA_UMD_NAME, user_md->str);
    g_string_free(user_md, true);

    if (extent->with_md5) {
        const char *md5_buffer = uchar2hex(extent->md5, MD5_BYTE_LENGTH);

        if (!md5_buffer)
            LOG_RETURN(rc = -ENOMEM, "Unable to construct hex md5");

        pho_attr_set(&iod->iod_attrs, PHO_EA_MD5_NAME, md5_buffer);
        free((char *)md5_buffer);
    }

    if (extent->with_xxh128) {
        const char *xxh128_buffer = uchar2hex(extent->xxh128,
                                              sizeof(extent->xxh128));
        if (!xxh128_buffer)
            LOG_RETURN(rc = -ENOMEM, "Unable to construct hex xxh128");

        pho_attr_set(&iod->iod_attrs, PHO_EA_XXH128_NAME, xxh128_buffer);
        free((char *)xxh128_buffer);
    }

    rc = snprintf(str_buffer, sizeof(str_buffer),
                  "%lu", object_md->object_size);
    if (rc < 0)
        LOG_RETURN(-errno, "Unable to construct object size buffer");

    pho_attr_set(&iod->iod_attrs, PHO_EA_OBJECT_SIZE_NAME, str_buffer);

    rc = snprintf(str_buffer, sizeof(str_buffer), "%lu", extent->offset);
    if (rc < 0)
        LOG_RETURN(-errno, "Unable to construct offset buffer");

    pho_attr_set(&iod->iod_attrs, PHO_EA_EXTENT_OFFSET_NAME, str_buffer);

    rc = snprintf(str_buffer, sizeof(str_buffer),
                  "%d", object_md->object_version);
    if (rc < 0)
        LOG_RETURN(-errno, "Unable to construct version buffer");

    pho_attr_set(&iod->iod_attrs, PHO_EA_VERSION_NAME, str_buffer);

    pho_attr_set(&iod->iod_attrs, PHO_EA_LAYOUT_NAME, object_md->layout_name);
    pho_attr_set(&iod->iod_attrs, PHO_EA_OBJECT_UUID_NAME,
                 object_md->object_uuid);
    pho_attr_set(&iod->iod_attrs, PHO_EA_COPY_NAME, object_md->copy_name);


    rc = ioa_set_md(ioa, NULL, iod);
    pho_attrs_free(&iod->iod_attrs);

    return rc;
}
