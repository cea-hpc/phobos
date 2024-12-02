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

#include "io_posix_common.h"
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

#define TERA (1024LL * 1024LL * 1024LL * 1024LL)
#define MAX_NULL_IO 10

/* Check if the content of the file is similar to the buffer */
static int check_file_content(const char *fpath, const unsigned char *ibuff,
                              size_t count, int repeat_count)
{
    size_t size          = repeat_count * count;
    struct stat extent_file_stat;
    unsigned char *obuff = NULL;
    int zero_read_count;
    size_t read_bytes;
    int fd = -1;
    int rc = 0;

    obuff = xmalloc(size);

    /* stat extent file to check size */
    if (stat(fpath, &extent_file_stat))
        LOG_GOTO(clean, rc = -errno,
                 "Unable to stat '%s' file to check size", fpath);

    /* check extent file size */
    if (extent_file_stat.st_size != size)
        LOG_GOTO(clean, rc = -EINVAL, "Extent file size is %zu insted of %zu",
                 extent_file_stat.st_size, size);

    /* open extent file for reading */
    fd = open(fpath, O_RDONLY);
    if (fd < 0)
        LOG_GOTO(clean, rc = -errno,
                 "Error on opening '%s' file after closing it", fpath);

    /* read size bytes */
    for (read_bytes = 0, zero_read_count = 0;
         read_bytes < size && zero_read_count < MAX_NULL_IO;) {
        ssize_t read_count;

        read_count = read(fd, obuff + read_bytes, size - read_bytes);
        if (read_count < 0)
            LOG_GOTO(clean, rc = -errno,
                     "Fail to read data in '%s' file", fpath);

        if (read_count < count - read_bytes) {
            pho_warn("Partial read : %zu of %zu",
                     read_count, size - read_bytes);
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
    for (int i = 0; i < repeat_count; i++) {
        if (memcmp(ibuff, obuff + i*count, count))
            LOG_GOTO(clean, rc = -EINVAL, "Wrong extent file content");
    }

    /* cleaning */
clean:
    if (fd >= 0) {
        if (close(fd))
            pho_error(rc = rc ? : -errno, "Fail to unlink extent file");
    }

    free(obuff);
    return rc;
}

static int check_files_are_equal(const char *fpath_a, const char *fpath_b)
{
    char buf_a[4096] = {0}, buf_b[4096] = {0};
    FILE *stream_a, *stream_b;
    int rc_a, rc_b;

    stream_a = fopen(fpath_a, "r");
    if (stream_a == NULL)
        LOG_RETURN(-errno, "Cannot open source test file for comparison");
    stream_b = fopen(fpath_b, "r");
    if (stream_b == NULL) {
        fclose(stream_a);
        LOG_RETURN(-errno, "Cannot open target test file for comparison");
    }

    while (1) {
        rc_a = fscanf(stream_a, "%4096c", buf_a);
        rc_b = fscanf(stream_b, "%4096c", buf_b);

        /* scans did not return the same number of items */
        if (rc_a != rc_b)
            return 1;

        if (rc_a == EOF) {
            /* both scans end without error */
            if (ferror(stream_a) == 0 && ferror(stream_b) == 0)
                break;

            /* at least one scan failed */
            errno = ferror(stream_a) == 0 ? ferror(stream_b) : ferror(stream_a);
            LOG_RETURN(-errno, "Read error (err: %d, %d)",
                       ferror(stream_a), ferror(stream_b));
        }

        /* buffers differ */
        if (strncmp(buf_a, buf_b, 4096) != 0)
            return 1;
    }

    return 0;
}

#define REPEAT_COUNT 3

static int test_posix_open_write_close(void *hint)
{
    char test_dir[] = "/tmp/test_posix_open_write_closeXXXXXX";
    char *put_extent_address = "put_extent";
    struct io_adapter_module *ioa = {0};
    struct posix_io_ctx *pioctx = NULL;
    struct pho_io_descr iod = {0};
    struct pho_ext_loc loc = {0};
    struct stat extent_file_stat;
    unsigned char *ibuff = NULL;
    struct extent ext = {0};
    char *fpath = NULL;
    char *id = NULL;
    size_t count;
    int rc;
    int i;

    /**
     *  INIT
     */
    /* create test dir */
    if (mkdtemp(test_dir) == NULL)
        LOG_RETURN(-errno, "Unable to create test dir");

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
    rc = ioa_open(ioa, id, &iod, true);
    if (rc)
        LOG_GOTO(free_path, rc, "Error on opening extent");

    /* get preferred IO size to allocate the IO buffer */
    count = ioa_preferred_io_size(ioa, &iod);
    pho_debug("Preferred I/O size=%zu", count);

    /* AFAIK, no storage system use such small/large IO size */
    if (count < 512 || count >= TERA)
        LOG_GOTO(clean_extent, -EINVAL, "Invalid or inconsistent IO size");

    /* init buffers */
    ibuff = xmalloc(count);

    for (i = 0; i < count; i++)
        ibuff[i] = (unsigned char)i;

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
     * WRITE x3 / CLOSE / CHECK CONTENT
     */
    /* try to write with pho_posix_write */
    for (i = 0; i < REPEAT_COUNT; i++) {
        rc = ioa_write(ioa, &iod, ibuff, count);
        if (rc)
            LOG_GOTO(clean_extent, rc,
                     "Error on writting with pho_posix_write");
    }

    /* try to close with pho_posix_close */
    rc = ioa_close(ioa, &iod);
    if (rc)
        LOG_GOTO(clean_extent, rc,
                 "Fail to close iod with pho_posix_close");

    /* check iod_ctx is NULL */
    if (iod.iod_ctx)
        LOG_GOTO(clean_extent, rc = -EINVAL,
                 "pho_posix_close didn't clean private io ctx");

    /* check written extent file content */
    rc = check_file_content(fpath, ibuff, count, REPEAT_COUNT);

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
    /* free NULL is safe */
    free(ibuff);
    free(fpath);

clean_test_dir:
    if (rmdir(test_dir))
        pho_error(rc = rc ? : -errno, "Unable to remove test dir");

    return rc;
}

/**
 * TO DO
static int test_posix_open_to_get_close(void *hint)

static int test_posix_open_to_put_md(void *hint)

static int test_posix_open_to_get_md(void *hint)
*/

static int test_copy_extent(void *state)
{
    char test_dir_source[] = "/tmp/test_copy_extentXXXXXX";
    char test_dir_target[] = "/tmp/test_copy_extentXXXXXX";
    struct io_adapter_module *ioa_source = {0};
    struct io_adapter_module *ioa_target = {0};
    char copy_extent_address[] = "copy_extent";
    struct pho_io_descr iod_source = {0};
    struct pho_io_descr iod_target = {0};
    struct pho_ext_loc loc_source = {0};
    struct pho_ext_loc loc_target = {0};
    char *fpath_source = NULL;
    char *fpath_target = NULL;
    struct extent ext = {0};
    char command[256];
    int rc = 0;

    (void)state;

    /* create test dirs */
    if (mkdtemp(test_dir_source) == NULL)
        LOG_RETURN(-errno, "Unable to create test dir source");
    if (mkdtemp(test_dir_target) == NULL)
        LOG_GOTO(clean_test_dir_source, -errno,
                 "Unable to create test dir target");

    /* build fpaths to test the value */
    rc = asprintf(&fpath_source, "%s/%s", test_dir_source, copy_extent_address);
    if (rc < 0)
        LOG_GOTO(clean_test_dirs, rc = -ENOMEM,
                 "Unable to allocate tested fpath source");
    rc = asprintf(&fpath_target, "%s/%s", test_dir_target, copy_extent_address);
    if (rc < 0)
        LOG_GOTO(free_path, rc = -ENOMEM,
                 "Unable to allocate tested fpath target");

    /* get posix ioas */
    rc = get_io_adapter(PHO_FS_POSIX, &ioa_source);
    if (rc)
        LOG_GOTO(free_path, rc, "Unable to get posix ioa source");
    rc = get_io_adapter(PHO_FS_POSIX, &ioa_target);
    if (rc)
        LOG_GOTO(free_path, rc, "Unable to get posix ioa target");

    /* init open contexts with an already set extent address */
    ext.address.buff = copy_extent_address;
    loc_source.extent = &ext;
    loc_source.root_path = test_dir_source;
    iod_source.iod_loc = &loc_source;
    iod_source.iod_size = 10 * 1024;

    loc_target.extent = &ext;
    loc_target.root_path = test_dir_target;
    iod_target.iod_loc = &loc_target;

    /* create the test file in source directory */
    sprintf(command, "dd bs=1k count=10 if=/dev/urandom of=%s", fpath_source);
    rc = system(command);
    if (rc)
        LOG_GOTO(free_path, rc, "File creation failed");

    /* copy and check */
    rc = copy_extent(ioa_source, &iod_source, ioa_target, &iod_target,
                     PHO_RSC_DIR);
    if (rc)
        LOG_GOTO(remove_source, rc, "Extent copy failed");

    rc = check_files_are_equal(fpath_source, fpath_target);

    free(loc_target.extent->address.buff);

    remove(fpath_target);

remove_source:
    remove(fpath_source);

free_path:
    free(fpath_target);
    free(fpath_source);

clean_test_dirs:
    if (rmdir(test_dir_target))
        pho_error(rc = rc ? : -errno, "Unable to remove test dir target");

clean_test_dir_source:
    if (rmdir(test_dir_source))
        pho_error(rc = rc ? : -errno, "Unable to remove test dir source");

    return rc;
}

int main(int argc, char **argv)
{
    test_env_initialize();

    pho_run_test("Posix open, write and close",
                 test_posix_open_write_close, NULL, PHO_TEST_SUCCESS);
    /**
     * TO DO
    pho_run_test("Posix open to get and close",
             test_posix_open_to_get_close, NULL, PHO_TEST_SUCCESS);
    pho_run_test("Posix open to put only metadata",
             test_posix_open_to_put_md, NULL, PHO_TEST_SUCCESS);
    pho_run_test("Posix open to get only metadata",
             test_posix_open_to_get_md, NULL, PHO_TEST_SUCCESS);
    */

    pho_run_test("Posix copy",
                 test_copy_extent, NULL, PHO_TEST_SUCCESS);

    pho_info("Unit IO posix open/write/close: All tests succeeded");
    exit(EXIT_SUCCESS);
}
