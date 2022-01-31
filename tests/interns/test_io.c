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
 * \brief test io module
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_io.h"
#include "pho_types.h"
#include "pho_test_utils.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/* private posix context structure : copied in this file to test it */
struct posix_io_ctx {
    int   fd;
    char *fpath;
};

#define MEGA (1024 * 1024)
#define MAX_NULL_IO 10

/* Check if the content of the file is similar to the buffer */
static int check_file_content(const char *fpath, const unsigned char *ibuff,
                              size_t count)
{
    int rc = 0;
    unsigned char *obuff = NULL;
    struct stat extent_file_stat;
    int fd = -1;
    size_t read_bytes;
    int zero_read_count;

    obuff = malloc(count);
    if (!obuff)
        LOG_RETURN(rc = -ENOMEM, "Unable to allocate output buff");

    /* stat extent file to check size */
    if (stat(fpath, &extent_file_stat))
        LOG_GOTO(clean, rc = -errno,
                 "Unable to stat '%s' file to check size", fpath);

    /* check extent file size */
    if (extent_file_stat.st_size != count)
        LOG_GOTO(clean, rc = -EINVAL, "Extent file size is %zu insted of %zu",
                 extent_file_stat.st_size, count);

    /* open extent file for reading */
    fd = open(fpath, O_RDONLY);
    if (fd < 0)
        LOG_GOTO(clean, rc = -errno,
                 "Error on opening '%s' file after closing it", fpath);

    /* read count bytes */
    for (read_bytes = 0, zero_read_count = 0;
         read_bytes < count && zero_read_count < MAX_NULL_IO;) {
        ssize_t read_count;

        read_count = read(fd, obuff + read_bytes, count - read_bytes);
        if (read_count < 0)
            LOG_GOTO(clean, rc = -errno,
                     "Fail to read data in '%s' file", fpath);

        if (read_count < count - read_bytes) {
            pho_warn("Partial read : %zu of %zu",
                     read_count, count - read_bytes);
            if (read_count == 0)
                zero_read_count++;
        }

        read_bytes += read_count;
    }

    /* check "zero" bytes read */
    if (zero_read_count >= MAX_NULL_IO)
        LOG_GOTO(clean, rc = -EIO,
                 "Error : too many \"zero\" reads when checking '%s' file",
                 fpath);

    /* check read content */
    if (memcmp(ibuff, obuff, count))
        LOG_GOTO(clean, rc = -EINVAL, "Wrong extent file content");

    /* cleaning */
clean:
    if (fd >= 0) {
        if (close(fd))
            pho_error(rc = rc ? : -errno, "Fail to unlink extent file");
    }

    free(obuff);
    return rc;
}

static int test_posix_open_write_close(void *hint)
{
    char test_dir[] = "/tmp/test_posix_open_write_closeXXXXXX";
    char *put_extent_address = "put_extent";
    char *fpath;
    int rc;
    struct io_adapter ioa = {0};
    struct extent ext = {0};
    struct pho_ext_loc loc = {0};
    struct pho_io_descr iod = {0};
    struct posix_io_ctx *pioctx = NULL;
    char *id = NULL;
    char *tag = NULL;
    struct stat extent_file_stat;
    unsigned char *ibuff = NULL;
    size_t count = MEGA;
    int i;

    /**
     *  INIT
     */
    /* init buffers */
    ibuff = malloc(count);
    if (!ibuff)
        LOG_RETURN(-ENOMEM, "Unable to allocate input buff");

    for (i = 0; i < count; i++)
        ibuff[i] = (unsigned char)i;

    /* create test dir */
    if (mkdtemp(test_dir) == NULL)
        LOG_GOTO(free_ibuff, rc = -errno, "Unable to create test dir");

    /* build fpath to test the value */
    rc = asprintf(&fpath, "%s/%s", test_dir, put_extent_address);
    if (rc < 0)
        LOG_GOTO(clean_test_dir, rc = -ENOMEM,
                 "Unable to allocate tested fpath");

    /* get posix ioa */
    rc = get_io_adapter(PHO_FS_POSIX, &ioa);
    if (rc)
        LOG_GOTO(free_path, rc, "Unable to get posix ioa");

    /* init open context with an already set extent address */
    ext.address.buff = put_extent_address;
    loc.extent = &ext;
    loc.root_path = test_dir;
    iod.iod_loc = &loc;

    /**
     *  OPEN
     */
    /* try to open for put with pho_posix_open */
    rc = ioa_open(&ioa, id, tag, &iod, true);
    if (rc)
        LOG_GOTO(free_path, rc, "Error on opening extent");

    /* Is iod->io_ctx built ? */
    if (iod.iod_ctx == NULL)
        LOG_GOTO(clean_extent, rc = -EINVAL,
                 "No private context set by pho_posix_open");

    pioctx =  iod.iod_ctx;
    /* Is fpath set ? */
    if (!pioctx->fpath)
        LOG_GOTO(clean_extent, rc = -EINVAL, "No fpath set by pho_posix_open");

    /* Is fpath set to the correct value ? */
    if (strcmp(pioctx->fpath, fpath))
        LOG_GOTO(clean_extent, rc = -EINVAL,
                 "fpath is set to %s instead of %s", pioctx->fpath, fpath);

    /* Is fd set ? */
    if (pioctx->fd < 0)
        LOG_GOTO(clean_extent, rc = -EINVAL,
                 "fd set by pho_posix_open is not valid : %d", pioctx->fd);

    /* stat extent file */
    if (fstat(pioctx->fd, &extent_file_stat))
        LOG_GOTO(clean_extent, rc = -errno, "Unable to stat extent file");

    /* Is extent a regular file ? */
    if (!S_ISREG(extent_file_stat.st_mode))
        LOG_GOTO(clean_extent, rc = -EINVAL, "Extent is not a regular file");

    /* test fd owner write access */
    if (!(extent_file_stat.st_mode & 0200))
        LOG_GOTO(clean_extent, rc = -EINVAL,
                 "Extent file has no owner write access");

    /**
     * WRITE / CLOSE / CHECK CONTENT
     */
    /* try to write with pho_posix_write */
    rc = ioa_write(&ioa, &iod, ibuff, count);
    if (rc)
        LOG_GOTO(clean_extent, rc,
                 "Error on writting with pho_posix_write");

    /* try to close with pho_posix_close */
    rc = ioa_close(&ioa, &iod);
    if (rc)
        LOG_GOTO(clean_extent, rc,
                 "Fail to close iod with pho_posix_close");

    /* check iod_ctx is NULL */
    if (iod.iod_ctx)
        LOG_GOTO(clean_extent, rc = -EINVAL,
                 "pho_posix_close didn't clean private io ctx");

    /* check written extent file content */
    rc = check_file_content(fpath, ibuff, count);

clean_extent:
    if (iod.iod_ctx) {
        pioctx = iod.iod_ctx;
        if (pioctx->fd >= 0) {
            if (close(pioctx->fd))
                pho_error(rc = rc ? : -errno, "Fail to close extent file");
        }
    }

    if (unlink(fpath))
        pho_error(rc = rc ? : -errno, "Fail to unlink extent file");

free_path:
    free(fpath);

clean_test_dir:
    if (rmdir(test_dir))
        pho_error(rc = rc ? : -errno, "Unable to remove test dir");

free_ibuff:
    free(ibuff);

    return rc;
}

/**
 * TO DO
static int test_posix_open_to_get_close(void *hint)

static int test_posix_open_to_put_md(void *hint)

static int test_posix_open_to_get_md(void *hint)
*/

int main(int argc, char **argv)
{
    test_env_initialize();

    run_test("Posix open, write and close",
             test_posix_open_write_close, NULL, PHO_TEST_SUCCESS);
    /**
     * TO DO
    run_test("Posix open to get and close",
             test_posix_open_to_get_close, NULL, PHO_TEST_SUCCESS);
    run_test("Posix open to put only metadata",
             test_posix_open_to_put_md, NULL, PHO_TEST_SUCCESS);
    run_test("Posix open to get only metadata",
             test_posix_open_to_get_md, NULL, PHO_TEST_SUCCESS);
    */

    pho_info("Unit IO posix open/write/close: All tests succeeded");
    exit(EXIT_SUCCESS);
}
