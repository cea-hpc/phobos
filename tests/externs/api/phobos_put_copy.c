#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <phobos_store.h>

int main(int argc, char **argv)
{
    struct pho_xfer_target target_copy = {0};
    struct pho_xfer_target target = {0};
    struct pho_xfer_desc xfer_copy = {0};
    struct pho_xfer_desc xfer = {0};
    struct stat statbuf;
    int fd;
    int rc;

    if (argc != 6) {
        printf("usage: %s input_path object_name copy_name\n", argv[0]);
        exit(EINVAL);
    }

    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        rc = errno;
        printf("Error at input open '%s': %d, %s .\n",
               argv[1], rc, strerror(rc));
        exit(rc);
    }

    rc = fstat(fd, &statbuf);
    if (rc) {
        rc = errno;
        printf("Error at fstat: %d, %s .\n", rc, strerror(rc));
        exit(rc);
    }

    /* first object/copy without grouping */
    xfer.xd_op = PHO_XFER_OP_PUT;
    xfer.xd_params.put.family = PHO_RSC_DIR;
    xfer.xd_ntargets = 1;
    target.xt_objid = argv[2];
    target.xt_fd = fd;
    target.xt_size = statbuf.st_size;
    xfer.xd_targets = &target;

    phobos_init();
    rc = phobos_put(&xfer, 1, NULL, NULL);
    if (rc) {
        printf("Error at put: %d, %s .\n", -rc, strerror(-rc));
        exit(-rc);
    }

    pho_xfer_desc_clean(&xfer);

    xfer_copy.xd_op = PHO_XFER_OP_COPY;
    xfer_copy.xd_params.copy = (struct pho_xfer_copy_params){0};
    xfer_copy.xd_params.copy.get.scope = DSS_OBJ_ALIVE;
    xfer_copy.xd_params.copy.put.family = PHO_RSC_DIR;
    xfer_copy.xd_params.copy.put.copy_name = argv[3];
    target_copy.xt_objid = argv[2];
    xfer_copy.xd_ntargets = 1;
    xfer_copy.xd_targets = &target_copy;

    rc = phobos_copy(&xfer_copy, 1, NULL, NULL);
    if (rc) {
        printf("Error at copy: %d, %s .\n", -rc, strerror(-rc));
        exit(-rc);
    }

    pho_xfer_desc_clean(&xfer_copy);

    /* second object/copy with grouping */
    rc = lseek(fd, 0, SEEK_SET);
    if (rc == -1) {
        rc = errno;
        printf("Error at lseek: %d, %s .\n", rc, strerror(rc));
        exit(rc);
    }

    xfer.xd_op = PHO_XFER_OP_PUT;
    xfer.xd_params.put.family = PHO_RSC_DIR;
    xfer.xd_params.put.grouping = argv[5];
    xfer.xd_ntargets = 1;
    target.xt_objid = argv[4];
    target.xt_fd = fd;
    target.xt_size = statbuf.st_size;
    xfer.xd_targets = &target;

    phobos_init();
    rc = phobos_put(&xfer, 1, NULL, NULL);
    if (rc) {
        printf("Error at put: %d, %s .\n", -rc, strerror(-rc));
        exit(-rc);
    }

    pho_xfer_desc_clean(&xfer);

    xfer_copy.xd_op = PHO_XFER_OP_COPY;
    xfer_copy.xd_params.copy = (struct pho_xfer_copy_params){0};
    xfer_copy.xd_params.copy.get.scope = DSS_OBJ_ALIVE;
    xfer_copy.xd_params.copy.put.family = PHO_RSC_DIR;
    xfer_copy.xd_params.copy.put.copy_name = argv[3];
    target_copy.xt_objid = argv[4];
    xfer_copy.xd_ntargets = 1;
    xfer_copy.xd_targets = &target_copy;

    rc = phobos_copy(&xfer_copy, 1, NULL, NULL);
    if (rc) {
        printf("Error at copy: %d, %s .\n", -rc, strerror(-rc));
        exit(-rc);
    }

    pho_xfer_desc_clean(&xfer_copy);

    close(fd);
    exit(EXIT_SUCCESS);
}
