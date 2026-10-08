/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2026 CEA/DAM.
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
 * \brief Check the handling of the input strings that are not valid UTF-8:
 *        the user metadata is silently sanitized, the object IDs are
 *        rejected with an explicit error.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <phobos_store.h>

/* The Unicode replacement character U+FFFD, as encoded in UTF-8. */
#define U_FFFD "\xEF\xBF\xBD"

/*
 * "clé" and "piège" with the accentuated characters encoded as single
 * Latin-1 bytes: these byte sequences are not valid UTF-8.
 */
#define BAD_KEY   "cl\xE9"
#define BAD_VALUE "pi\xE8ge"
#define BAD_OID   "bad\xE8oid"

static void test_fail(const char *msg, int fd)
{
    close(fd);
    printf("Error: %s\n", msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    struct pho_xfer_target md_target = {0};
    struct pho_xfer_target target = {0};
    struct pho_xfer_desc md_xfer = {0};
    struct pho_xfer_desc xfer = {0};
    struct pho_attrs *md_attrs;
    struct stat statbuf;
    const char *value;
    int fd;
    int rc;

    if (argc != 3) {
        printf("usage: %s input_path object_name\n", argv[0]);
        exit(EINVAL);
    }

    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        printf("Error: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    rc = fstat(fd, &statbuf);
    if (rc)
        test_fail(strerror(errno), fd);

    phobos_init();

    /*
     * The metadata keys and values that are not valid UTF-8 are silently
     * sanitized: the put must succeed and store the sanitized form.
     */
    xfer.xd_op = PHO_XFER_OP_PUT;
    xfer.xd_params.put.family = PHO_RSC_DIR;
    xfer.xd_ntargets = 1;
    target.xt_objid = argv[2];
    target.xt_fd = fd;
    target.xt_size = statbuf.st_size;
    xfer.xd_targets = &target;

    pho_attr_set(&target.xt_attrs, BAD_KEY, BAD_VALUE);

    rc = phobos_put(&xfer, 1, NULL, NULL);
    if (rc)
        test_fail("the put of a metadata that is not valid UTF-8 must "
                  "succeed, as the metadata is silently sanitized", fd);

    pho_xfer_desc_clean(&xfer);

    /*
     * Read the metadata back: both the key and the value must have been
     * sanitized to U+FFFD.
     */
    md_xfer.xd_ntargets = 1;
    md_target.xt_objid = argv[2];
    md_xfer.xd_targets = &md_target;

    rc = phobos_getmd(&md_xfer, 1);
    if (rc)
        test_fail("cannot get the metadata of the object back", fd);

    md_attrs = &md_xfer.xd_targets[0].xt_attrs;

    value = pho_attr_get(md_attrs, "cl" U_FFFD);
    if (value == NULL || strcmp(value, "pi" U_FFFD "ge") != 0)
        test_fail("the stored metadata is not the sanitized form of the "
                  "input metadata", fd);

    if (pho_attr_get(md_attrs, BAD_KEY) != NULL)
        test_fail("the metadata key must have been sanitized too", fd);

    pho_xfer_desc_clean(&md_xfer);

    /*
     * The object ID is not sanitized: a put with an object ID that is not
     * valid UTF-8 must fail with an explicit error.
     */
    xfer.xd_op = PHO_XFER_OP_PUT;
    xfer.xd_params.put.family = PHO_RSC_DIR;
    xfer.xd_ntargets = 1;
    target.xt_objid = BAD_OID;
    xfer.xd_targets = &target;

    rc = phobos_put(&xfer, 1, NULL, NULL);
    if (rc == 0)
        test_fail("the put of an object ID that is not valid UTF-8 must "
                  "fail with an explicit error", fd);

    pho_xfer_desc_clean(&xfer);

    phobos_fini();
    close(fd);

    exit(EXIT_SUCCESS);
}
