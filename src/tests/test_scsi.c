/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
#define _GNU_SOURCE
#include "pho_test_utils.h"
#include "../ldm/scsi_api.h"
#include "pho_ldm.h"

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

static char *one_serial;
static char *one_label;

static char *dup_serial(const char *id)
{
    char *c = strrchr(id, ' ');
    if (c)
        return strdup(c + 1);

    return strdup(id);
}


/** Fill the previous variables for next test scenarios */
static void save_test_elements(const struct element_status *element)
{
    if (element->full && one_label == NULL)
        one_label = strdup(element->vol);

    switch (element->type) {
    case SCSI_TYPE_DRIVE:
        if (element->dev_id[0] != '\0' && element->full) {
            free(one_serial);
            one_serial = dup_serial(element->dev_id);
        }

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

static int single_element_status(int fd, uint16_t addr, bool expect_full)
{
    int rc;

    struct element_status *list = NULL;
    int lcount = 0;

    rc = scsi_element_status(fd, SCSI_TYPE_ALL, addr, 1,
                             ESF_GET_LABEL | ESF_GET_DRV_ID,
                             &list, &lcount);
    if (rc) {
        pho_error(rc, "status ERROR %d", rc);
        exit(EXIT_FAILURE);
    }

    if (lcount > 0 && list->full != expect_full) {
        pho_warn("Element at addr %#hx is expected to be full",
                 addr);
        exit(EXIT_FAILURE);
    }

    print_elements(list, lcount);
    free(list);
    return 0;
}

/** tests of the lib adapter API */
static void test_lib_adapter(void)
{
    int rc;
    struct lib_adapter lib = {0};
    struct lib_drv_info drv_info;
    struct lib_item_addr med_addr;


    rc = get_lib_adapter(PHO_LIB_SCSI, &lib);
    if (rc)
        exit(EXIT_FAILURE);

    rc = ldm_lib_open(&lib, "/dev/changer");
    if (rc)
        exit(EXIT_FAILURE);

    if (one_serial) {
        rc = ldm_lib_drive_lookup(&lib, one_serial, &drv_info);
        if (rc)
            exit(EXIT_FAILURE);
    }

    if (one_label) {
        rc = ldm_lib_media_lookup(&lib, one_label, &med_addr);
        if (rc)
            exit(EXIT_FAILURE);
    }

    /* unload the drive to any slot if it's full */
    if (one_serial && drv_info.ldi_full) {
        rc = ldm_lib_media_move(&lib, &drv_info.ldi_addr, NULL);
        if (rc)
            exit(EXIT_FAILURE);
    }

    ldm_lib_close(&lib);
}

static int val;
static int incr_val1(void)
{
    switch (val++) {
    case 0:
        /* short retry */
        return -EAGAIN;
    case 1:
        /* longer retry */
        return -EBUSY;
    case 2:
        /* no error */
        return 0;
    default:
        /* no later retry */
        exit(EXIT_FAILURE);
    }
}

static int incr_val2(void)
{
    /* all retries fail */
    val++;
    return -EAGAIN;
}

static int test1(void *hint)
{
    int rc;

    /* test retry loop */
    val = 0;
    PHO_RETRY_LOOP(rc, scsi_retry_func, NULL, 5, incr_val1);
    /* rc should be 0 */
    if (rc != 0) {
        fprintf(stderr, "1) rc should be 0\n");
        return -1;
    }
    /* val should be 3 */
    if (val != 3) {
        fprintf(stderr, "2) val should be 3\n");
        return -1;
    }
    return 0;
}

static int test2(void *hint)
{
    int rc;

    val = 0;
    PHO_RETRY_LOOP(rc, scsi_retry_func, NULL, 3, incr_val2);
    if (rc != -EAGAIN) {
        fprintf(stderr, "3) rc should be -EAGAIN\n");
        return -1;
    }
    /* val should be 4 (inital call + 3 retries) */
    if (val != 4) {
        fprintf(stderr, "4) val should be 4\n");
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct mode_sense_info msi = { {0} };
    int fd, rc;
    struct element_status *list = NULL;
    int lcount = 0;
    char *val = NULL;
    bool was_loaded = false;

    test_env_initialize();

    /* tests of retry loop */
    run_test("Test1: retry loop with success", test1, NULL, PHO_TEST_SUCCESS);
    run_test("Test2: retry loop with failure", test2, NULL, PHO_TEST_SUCCESS);

    fd = open("/dev/changer", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        pho_error(errno, "Cannot open /dev/changer");
        exit(EXIT_FAILURE);
    }

    rc = scsi_mode_sense(fd, &msi);
    if (rc) {
        pho_error(rc, "mode_sense error");
        exit(EXIT_FAILURE);
    }

    pho_info("arms: first=%#hX, nb=%d", msi.arms.first_addr, msi.arms.nb);

    rc = scsi_element_status(fd, SCSI_TYPE_ARM, msi.arms.first_addr,
                             msi.arms.nb, ESF_GET_LABEL, &list, &lcount);
    if (rc) {
        pho_error(rc, "element_status error");
        exit(EXIT_FAILURE);
    }
    print_elements(list, lcount);
    free(list);

    pho_info("slots: first=%#hX, nb=%d", msi.slots.first_addr, msi.slots.nb);

    rc = scsi_element_status(fd, SCSI_TYPE_SLOT, msi.slots.first_addr,
                             msi.slots.nb, ESF_GET_LABEL, &list, &lcount);
    if (rc) {
        pho_error(rc, "element_status error");
        exit(EXIT_FAILURE);
    }

    print_elements(list, lcount);
    free(list);

    /* try with a limited chunk size (force splitting in 4 chunks) */
    if (asprintf(&val, "%u", msi.slots.nb / 4) == -1 || val == NULL) {
        pho_error(errno, "asprintf failed");
        exit(EXIT_FAILURE);
    }

    if (setenv("PHOBOS_SCSI_max_element_status", val, 1)) {
        pho_error(errno, "setenv failed");
        exit(EXIT_FAILURE);
    }

    rc = scsi_element_status(fd, SCSI_TYPE_SLOT, msi.slots.first_addr,
                             msi.slots.nb, ESF_GET_LABEL, &list, &lcount);
    if (rc) {
        pho_error(rc, "element_status error");
        exit(EXIT_FAILURE);
    }

    if (lcount != msi.slots.nb) {
        pho_error(rc, "Invalid count returned: %d != %d", lcount, msi.slots.nb);
        exit(EXIT_FAILURE);
    }
    free(list);

    pho_info("imp/exp: first=%#hX, nb=%d",
             msi.impexp.first_addr, msi.impexp.nb);

    rc = scsi_element_status(fd, SCSI_TYPE_IMPEXP, msi.impexp.first_addr,
                             msi.impexp.nb, ESF_GET_LABEL, &list, &lcount);
    if (rc) {
        pho_error(rc, "element_status error");
        exit(EXIT_FAILURE);
    }
    print_elements(list, lcount);
    free(list);

    pho_info("drives: first=%#hX, nb=%d", msi.drives.first_addr, msi.drives.nb);

    rc = scsi_element_status(fd, SCSI_TYPE_DRIVE, msi.drives.first_addr,
                             msi.drives.nb, ESF_GET_LABEL, &list, &lcount);
    if (rc) {
        pho_error(rc, "element_status error");
        exit(EXIT_FAILURE);
    }

    print_elements(list, lcount);
    free(list);

    if (full_drive != UNSET && free_slot != UNSET) {
        single_element_status(fd, full_drive, true);
        single_element_status(fd, free_slot, false);

        pho_info("Unloading drive %#x to slot %#x", full_drive, free_slot);

        rc = scsi_move_medium(fd, arm_addr, full_drive, free_slot);
        if (rc) {
            pho_error(rc, "move_medium error");
            exit(EXIT_FAILURE);
        }
        single_element_status(fd, full_drive, false);
        single_element_status(fd, free_slot, true);

        was_loaded = true;
    }

    if (empty_drive != UNSET && full_slot != UNSET) {
        single_element_status(fd, full_slot, true);
        single_element_status(fd, empty_drive, false);

        pho_info("Loading tape from slot %#x to drive %#x",
                 full_slot, empty_drive);

        rc = scsi_move_medium(fd, arm_addr, full_slot, empty_drive);
        if (rc) {
            pho_error(rc, "move_medium error");
            exit(EXIT_FAILURE);
        }
        single_element_status(fd, full_slot, false);
        single_element_status(fd, empty_drive, true);
    } else if (was_loaded) {
        single_element_status(fd, full_drive, false);
        single_element_status(fd, free_slot, true);

        pho_info("Loading back tape from slot %#x to drive %#x",
                 free_slot, full_drive);

        rc = scsi_move_medium(fd, arm_addr, free_slot, full_drive);
        if (rc) {
            pho_error(rc, "move_medium error");
            exit(EXIT_FAILURE);
        }
        single_element_status(fd, full_drive, true);
        single_element_status(fd, free_slot, false);
    }

    /* test of the lib adapter API */
    test_lib_adapter();

    /* same test with PHO_CFG_LIB_SCSI_sep_sn_query=1 */
    if (setenv("PHOBOS_LIB_SCSI_sep_sn_query", "1", 1)) {
        pho_error(errno, "setenv failed");
        exit(EXIT_FAILURE);
    }
    test_lib_adapter();

    exit(EXIT_SUCCESS);
}
