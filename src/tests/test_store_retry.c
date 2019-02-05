#include "config.h"

#include <ftw.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "pho_cfg.h"
#include "pho_dss.h"
#include "pho_ldm.h"
#include "pho_lrs.h"
#include "pho_test_utils.h"
#include "phobos_store.h"

#define PHO_TMP_DIR_TEMPLATE "/tmp/pho_XXXXXX"
#define RM_OPEN_FDS 16

/** Assert an rc is 0 or print a pretty error and exit */
#define ASSERT_RC(rc_expr) do {                                         \
        int rc = (rc_expr);                                             \
        if (rc) {                                                       \
            pho_error(rc, "%s:%d:%s", __FILE__, __LINE__, #rc_expr);    \
            exit(EXIT_FAILURE);                                         \
        }                                                               \
    } while (0)


static char *TMP_DIR;

static int rm_anything(const char *pathname, const struct stat *sbuf, int type,
                       struct FTW *ftwb)
{
    if (remove(pathname) < 0) {
        perror("ERROR: remove");
        return -1;
    }
    return 0;
}

static void rm_tmp_dir(void)
{
    if (!TMP_DIR)
        return;
    /* Recursive rm */
    nftw(TMP_DIR, rm_anything, RM_OPEN_FDS, FTW_DEPTH | FTW_MOUNT | FTW_PHYS);
    free(TMP_DIR);
}

static char *setup_tmp_dir(void)
{
    TMP_DIR = malloc(sizeof(PHO_TMP_DIR_TEMPLATE));
    strcpy(TMP_DIR, PHO_TMP_DIR_TEMPLATE);
    assert(mkdtemp(TMP_DIR) != NULL);
    atexit(rm_tmp_dir);
    return TMP_DIR;
}

/**
 * Test that two successive phobos_get work properly
 */
static void test_double_get(struct pho_xfer_desc *xfer)
{
    ASSERT_RC(phobos_get(xfer, 1, NULL, NULL));
    unlink(xfer->xd_fpath);
    ASSERT_RC(phobos_get(xfer, 1, NULL, NULL));
}

static void add_dir(struct dss_handle *dss, const char *path)
{
    struct ldm_dev_state dev_st = {0};
    struct dev_adapter adapter = {0};
    struct dev_info dev = {0};
    struct media_info media = { {0} };
    char hostname[256];

    gethostname(hostname, sizeof(hostname));
    strtok(hostname, ".");

    /* Add dir media */
    media.id.type = PHO_DEV_DIR;
    strncpy(media.id.id, path, PHO_URI_MAX);
    media.fs.type = PHO_FS_POSIX;
    media.addr_type = PHO_ADDR_HASH1;
    media.adm_status = PHO_MDA_ADM_ST_LOCKED;
    ASSERT_RC(dss_media_set(dss, &media, 1, DSS_SET_INSERT));

    /* Add dir device */
    get_dev_adapter(PHO_DEV_DIR, &adapter);
    /* FIXME: some struct ldm_dev_state attributes are leaked */
    ASSERT_RC(ldm_dev_query(&adapter, path, &dev_st));

    dev.family = dev_st.lds_family;
    dev.model = dev_st.lds_model;
    dev.path = (char *) path;
    dev.host = hostname;
    dev.serial = dev_st.lds_serial;
    dev.adm_status = PHO_DEV_ADM_ST_UNLOCKED;

    ASSERT_RC(dss_device_set(dss, &dev, 1, DSS_SET_INSERT));

    /* Add device to lrs */
    ASSERT_RC(lrs_device_add(dss, &dev));

    /* Format and unlock media */
    ASSERT_RC(lrs_format(dss, &media.id, PHO_FS_POSIX, true));
}

int main(int argc, char **argv)
{
    struct dss_handle       dss = {0};
    struct pho_xfer_desc    xfer = {0};
    char                   *tmp_dir = setup_tmp_dir();
    char                    dst_path[256];

    setenv("PHOBOS_LRS_default_family", "dir", 1);

    test_env_initialize();
    pho_cfg_init_local(NULL);
    assert(system("./setup_db.sh drop_tables setup_tables") == 0);

    dss_init(&dss);
    add_dir(&dss, tmp_dir);

    xfer.xd_objid = realpath(argv[0], NULL);
    xfer.xd_fpath = argv[0];
    xfer.xd_flags = 0;
    /* Ignore rc: it's okay if the object is already in phobos */
    phobos_put(&xfer, 1, NULL, NULL);

    snprintf(dst_path, sizeof(dst_path), "%s/%s", tmp_dir, "dst");
    xfer.xd_fpath = dst_path;

    test_double_get(&xfer);
    dss_fini(&dss);

    return EXIT_SUCCESS;
}
