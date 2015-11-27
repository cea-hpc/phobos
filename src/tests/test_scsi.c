/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
#include "pho_test_utils.h"
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
    case SCSI_TYPE_ARM:  return "arm";
    case SCSI_TYPE_SLOT: return "slot";
    case SCSI_TYPE_IMPEXP: return "import/export";
    case SCSI_TYPE_DRIVE: return "drive";
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
    case SCSI_TYPE_DRIVE:
        if (full_drive == UNSET && element->full)
            full_drive = element->address;
        else if (empty_drive == UNSET && !element->full)
            empty_drive = element->address;
        break;

    case SCSI_TYPE_SLOT:
        if (free_slot == UNSET && !element->full)
            free_slot = element->address;
        else if (full_slot == UNSET && element->full)
            full_slot = element->address;
        break;

    case SCSI_TYPE_ARM:
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
    GString *gstr = g_string_new("");
    bool     first = true;

    save_test_elements(element);

    g_string_append_printf(gstr, "type: %s; ", type2str(element->type));
    g_string_append_printf(gstr, "address: %#hX; ", element->address);
    g_string_append_printf(gstr, "status: %s; ",
                           element->full ? "full" : "empty");

    if (element->full && element->vol[0])
        g_string_append_printf(gstr, "volume=%s; ", element->vol);

    if (element->src_addr_is_set)
        g_string_append_printf(gstr, "source_addr: %#hX; ", element->src_addr);

    if (element->except) {
        g_string_append_printf(gstr, "error: code=%hhu, qualifier=%hhu; ",
                               element->error_code,
                               element->error_code_qualifier);
    }

    if (element->dev_id[0])
        g_string_append_printf(gstr, "device_id: '%s'; ", element->dev_id);

    g_string_append_printf(gstr, "flags: ");

    if (element->type == SCSI_TYPE_IMPEXP) {
        g_string_append_printf(gstr, "%s%s", first ? "" : ",",
                               element->impexp ? "import" : "export");
        first = false;
    }
    if (element->accessible) {
        g_string_append_printf(gstr, "%saccess", first ? "" : ",");
        first = false;
    }
    if (element->exp_enabled) {
        g_string_append_printf(gstr, "%sexp_enab", first ? "" : ",");
        first = false;
    }
    if (element->imp_enabled) {
        g_string_append_printf(gstr, "%simp_enab", first ? "" : ",");
        first = false;
    }
    if (element->invert) {
        g_string_append_printf(gstr, "%sinvert", first ? "" : ",");
        first = false;
    }

    pho_debug("%s", gstr->str);
    g_string_free(gstr, TRUE);
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

    rc = element_status(fd, SCSI_TYPE_ALL, addr, 1, false, &list, &lcount);
    if (rc)
        LOG_RETURN(rc, "status ERROR %d", rc);

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

    test_env_initialize();

    fd = open("/dev/changer", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        pho_error(errno, "Cannot open /dev/changer");
        exit(EXIT_FAILURE);
    }

    rc = mode_sense(fd, &msi);
    if (rc) {
        pho_error(rc, "mode_sense error");
        exit(EXIT_FAILURE);
    }

    pho_info("arms: first=%#hX, nb=%d", msi.arms.first_addr, msi.arms.nb);

    rc = element_status(fd, SCSI_TYPE_ARM, msi.arms.first_addr, msi.arms.nb,
                false, &list, &lcount);
    if (rc) {
        pho_error(rc, "element_status error");
        exit(EXIT_FAILURE);
    }
    print_elements(list, lcount);
    free(list);

    pho_info("slots: first=%#hX, nb=%d", msi.slots.first_addr, msi.slots.nb);

    rc = element_status(fd, SCSI_TYPE_SLOT, msi.slots.first_addr,  msi.slots.nb,
                false, &list, &lcount);
    if (rc) {
        pho_error(rc, "element_status error");
        exit(EXIT_FAILURE);
    }

    print_elements(list, lcount);
    free(list);

    pho_info("imp/exp: first=%#hX, nb=%d",
             msi.impexp.first_addr, msi.impexp.nb);

    rc = element_status(fd, SCSI_TYPE_IMPEXP, msi.impexp.first_addr,
                        msi.impexp.nb, false, &list, &lcount);
    if (rc) {
        pho_error(rc, "element_status ERROR %d");
        exit(EXIT_FAILURE);
    }
    print_elements(list, lcount);
    free(list);

    pho_info("drives: first=%#hX, nb=%d", msi.drives.first_addr, msi.drives.nb);

    rc = element_status(fd, SCSI_TYPE_DRIVE, msi.drives.first_addr,
                        msi.drives.nb, false, &list, &lcount);
    if (rc) {
        pho_error(rc, "element_status error");
        exit(EXIT_FAILURE);
    }

    print_elements(list, lcount);
    free(list);

    if (full_drive != UNSET && free_slot != UNSET) {
        single_element_status(fd, full_drive);
        single_element_status(fd, free_slot);

        pho_info("Unloading drive %#x to slot %#x", full_drive, free_slot);

        rc = move_medium(fd, arm_addr, full_drive, free_slot);
        if (rc) {
            pho_error(rc, "move_medium error");
            exit(EXIT_FAILURE);
        }
        single_element_status(fd, full_drive);
        single_element_status(fd, free_slot);
    }

    if (empty_drive != UNSET && full_slot != UNSET) {
        single_element_status(fd, full_slot);
        single_element_status(fd, empty_drive);

        pho_info("Loading tape from slot %#x to drive %#x",
                 full_slot, empty_drive);

        rc = move_medium(fd, arm_addr, full_slot, empty_drive);
        if (rc) {
            pho_error(rc, "move_medium error");
            exit(EXIT_FAILURE);
        }
        single_element_status(fd, full_slot);
        single_element_status(fd, empty_drive);
    } else {
        single_element_status(fd, full_drive);
        single_element_status(fd, free_slot);

        pho_info("Loading back tape from slot %#x to drive %#x",
                 free_slot, full_drive);

        rc = move_medium(fd, arm_addr, free_slot, full_drive);
        if (rc) {
            pho_error(rc, "move_medium error");
            exit(EXIT_FAILURE);
        }
        single_element_status(fd, full_drive);
        single_element_status(fd, free_slot);
    }

    exit(EXIT_SUCCESS);
}
