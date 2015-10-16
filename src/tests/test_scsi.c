/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
#include "../ldm/scsi_api.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static const char *type2str(enum element_type_code code)
{
    switch (code) {
    case TYPE_ARM:  return "arm";
    case TYPE_SLOT: return "slot";
    case TYPE_IMPEXP: return "import/export";
    case TYPE_DRIVE: return "drive";
    default:    return "?";
    }
}

static void print_element(const struct element_status *element)
{
    bool first;

    printf("type: %s; ", type2str(element->type));
    printf("address: %#hX; ", element->address);

    printf("status: %s; ", element->full ? "full" : "empty");
    if (element->full && element->vol[0])
        printf("volume=%s; ", element->vol);

    if (element->src_addr_is_set)
        printf("source_addr: %#hX; ", element->src_addr);

    if (element->except) {
        printf("error: code=%hhu, qualifier=%hhu; ", element->error_code,
               element->error_code_qualifier);
    }

    if (element->dev_id[0])
        printf("device_id: '%s'; ", element->dev_id);

    printf("flags: ");

    first = true;

    if (element->type == TYPE_IMPEXP) {
        printf("%s%s", first ? "" : ",",
               element->impexp ? "import" : "export");
        first = false;
    }
    if (element->accessible) {
        printf("%saccess", first ? "" : ",");
        first = false;
    }
    if (element->exp_enabled) {
        printf("%sexp_enab", first ? "" : ",");
        first = false;
    }
    if (element->imp_enabled) {
        printf("%simp_enab", first ? "" : ",");
        first = false;
    }
    if (element->invert) {
        printf("%sinvert", first ? "" : ",");
        first = false;
    }

    printf("\n");

}

static void print_elements(const struct element_status *list, int nb)
{
    int i;

    for (i = 0; i < nb ; i++)
        print_element(&list[i]);
}

int main(int argc, char **argv)
{
    struct mode_sense_info msi = { {0} };
    int fd, rc;
    struct element_status *list = NULL;
    int lcount = 0;

    fd = open("/dev/changer", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "ERROR %d\n", errno);
        exit(1);
    }

    rc = mode_sense(fd, &msi);
    if (rc) {
        fprintf(stderr, "mode_sense ERROR %d\n", rc);
        exit(rc);
    }

    printf("arms: first=%#hX, nb=%d\n",
        msi.arms.first_addr,
        msi.arms.nb);
    rc = element_status(fd, TYPE_ARM, msi.arms.first_addr, msi.arms.nb,
                false, &list, &lcount);
    if (rc) {
        fprintf(stderr, "element_status ERROR %d\n", rc);
        exit(rc);
    }
    print_elements(list, lcount);
    free(list);

    printf("slots: first=%#hX, nb=%d\n",
        msi.slots.first_addr,
        msi.slots.nb);
    rc = element_status(fd, TYPE_SLOT, msi.slots.first_addr,  msi.slots.nb,
                false, &list, &lcount);
    if (rc) {
        fprintf(stderr, "element_status ERROR %d\n", rc);
        exit(rc);
    }
    print_elements(list, lcount);
    free(list);

    printf("imp/exp: first=%#hX, nb=%d\n",
        msi.impexp.first_addr,
        msi.impexp.nb);
    rc = element_status(fd, TYPE_IMPEXP, msi.impexp.first_addr,  msi.impexp.nb,
                false, &list, &lcount);
    if (rc) {
        fprintf(stderr, "element_status ERROR %d\n", rc);
        exit(rc);
    }
    print_elements(list, lcount);
    free(list);

    printf("drives: first=%#hX, nb=%d\n",
        msi.drives.first_addr,
        msi.drives.nb);
    rc = element_status(fd, TYPE_DRIVE, msi.drives.first_addr,  msi.drives.nb,
                false, &list, &lcount);
    if (rc) {
        fprintf(stderr, "element_status ERROR %d\n", rc);
        exit(rc);
    }
    print_elements(list, lcount);
    free(list);

}
