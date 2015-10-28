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

/* save address of some full/empty drives or slots to run test scenarios */
#define UNSET -1
static int16_t full_drive = UNSET;
static int16_t empty_drive = UNSET;
static int16_t full_slot = UNSET;
static int16_t free_slot = UNSET;
static int16_t arm_addr = UNSET;

/** Fill the previous variables for next test scenarios */
static void save_test_elements(const struct element_status *element)
{
    switch (element->type) {
    case TYPE_DRIVE:
        if (full_drive == UNSET && element->full)
            full_drive = element->address;
        else if (empty_drive == UNSET && !element->full)
            empty_drive = element->address;
        break;

    case TYPE_SLOT:
        if (free_slot == UNSET && !element->full)
            free_slot = element->address;
        else if (full_slot == UNSET && element->full)
            full_slot = element->address;
        break;

    case TYPE_ARM:
        if (arm_addr == UNSET)
            arm_addr = element->address;
        break;

    default:
        /* nothing interesting to save (noop) */
        ;
    }
}

static void print_element(const struct element_status *element)
{
    bool first;

    save_test_elements(element);

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

static int single_element_status(int fd, uint16_t addr)
{
    int rc;

    struct element_status *list = NULL;
    int lcount = 0;

    rc = element_status(fd, TYPE_ALL, addr, 1, false, &list, &lcount);
    if (rc) {
        fprintf(stderr, "status ERROR %d\n", rc);
        return rc;
    }
    print_elements(list, lcount);
    free(list);
    return 0;
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

    if (full_drive != UNSET && free_slot != UNSET) {
            printf("-------------------\n");
            single_element_status(fd, full_drive);
            single_element_status(fd, free_slot);

            printf("unloading drive %#x to slot %#x...\n", full_drive,
                   free_slot);
            rc = move_medium(fd, arm_addr, full_drive, free_slot);
            if (rc) {
                    fprintf(stderr, "move_medium ERROR %d\n", rc);
                    exit(rc);
            }
            single_element_status(fd, full_drive);
            single_element_status(fd, free_slot);
    }
    if (empty_drive != UNSET && full_slot != UNSET) {
            printf("-------------------\n");
            single_element_status(fd, full_slot);
            single_element_status(fd, empty_drive);

            printf("loading tape from slot %#x to drive %#x...\n", full_slot,
                   empty_drive);
            rc = move_medium(fd, arm_addr, full_slot, empty_drive);
            if (rc) {
                    fprintf(stderr, "move_medium ERROR %d\n", rc);
                    exit(rc);
            }

            single_element_status(fd, full_slot);
            single_element_status(fd, empty_drive);
    } else {
            printf("-------------------\n");
            single_element_status(fd, full_drive);
            single_element_status(fd, free_slot);

            printf("loading back tape from slot %#x to drive %#x...\n",
                   free_slot, full_drive);
            rc = move_medium(fd, arm_addr, free_slot, full_drive);
            if (rc) {
                    fprintf(stderr, "move_medium ERROR %d\n", rc);
                    exit(rc);
            }
            single_element_status(fd, full_drive);
            single_element_status(fd, free_slot);
    }

    return 0;
}
