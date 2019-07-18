#include "config.h"

#include <ftw.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "pho_cfg.h"
#include "pho_dss.h"
#include "pho_ldm.h"
#include "pho_lrs.h"
#include "pho_test_utils.h"
#include "phobos_store.h"

#define PHO_TMP_DIR_TEMPLATE "/tmp/pho_XXXXXX"
#define RM_OPEN_FDS 16
#define WAIT_UNLOCK_SLEEP 2

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

static void add_dir(struct lrs *lrs, const char *path, struct dev_info *dev,
                    struct media_info *media)
{
    struct ldm_dev_state dev_st = {0};
    struct dev_adapter adapter = {0};
    char hostname[256];

    gethostname(hostname, sizeof(hostname));
    strtok(hostname, ".");

    /* Add dir media */
    media->id.type = PHO_DEV_DIR;
    media_id_set(&media->id, path);
    media->fs.type = PHO_FS_POSIX;
    media->addr_type = PHO_ADDR_HASH1;
    media->adm_status = PHO_MDA_ADM_ST_LOCKED;
    ASSERT_RC(dss_media_set(lrs->dss, media, 1, DSS_SET_INSERT));

    /* Add dir device */
    get_dev_adapter(PHO_DEV_DIR, &adapter);
    /* FIXME: some struct ldm_dev_state attributes are leaked */
    ASSERT_RC(ldm_dev_query(&adapter, path, &dev_st));

    dev->family = dev_st.lds_family;
    dev->model = dev_st.lds_model;
    dev->path = (char *) path;
    dev->host = hostname;
    dev->serial = dev_st.lds_serial;
    dev->adm_status = PHO_DEV_ADM_ST_UNLOCKED;

    ASSERT_RC(dss_device_set(lrs->dss, dev, 1, DSS_SET_INSERT));

    /* Add device to lrs */
    ASSERT_RC(lrs_device_add(lrs, dev));

    /* Format and unlock media */
    ASSERT_RC(lrs_format(lrs, &media->id, PHO_FS_POSIX, true));
}

/* TODO: factorize with add_dir */
static void add_drive(struct lrs *lrs, const char *path, struct dev_info *dev)
{
    struct ldm_dev_state dev_st = {0};
    struct dev_adapter adapter = {0};
    char hostname[256];

    gethostname(hostname, sizeof(hostname));
    strtok(hostname, ".");

    /* Add drive device */
    get_dev_adapter(PHO_DEV_TAPE, &adapter);
    ASSERT_RC(ldm_dev_query(&adapter, path, &dev_st));

    dev->family = dev_st.lds_family;
    dev->model = dev_st.lds_model;
    dev->path = (char *) path;
    dev->host = hostname;
    dev->serial = dev_st.lds_serial;
    dev->adm_status = PHO_DEV_ADM_ST_UNLOCKED;

    ASSERT_RC(dss_device_set(lrs->dss, dev, 1, DSS_SET_INSERT));

    /* Add device to lrs */
    ASSERT_RC(lrs_device_add(lrs, dev));
}

static void add_tape(struct lrs *lrs, const char *tape_id,
                     const char *model, struct media_info *media)
{
    /* Add dir media */
    media->id.type = PHO_DEV_TAPE;
    media_id_set(&media->id, tape_id);
    media->model = strdup(model);
    media->fs.type = PHO_FS_LTFS;
    media->addr_type = PHO_ADDR_HASH1;
    media->adm_status = PHO_MDA_ADM_ST_UNLOCKED;
    ASSERT_RC(dss_media_set(lrs->dss, media, 1, DSS_SET_INSERT));

    /* This can fail if the tape has already been formatted */
    lrs_format(lrs, &media->id, PHO_FS_LTFS, true);
    free(media->model);
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

struct wait_unlock_device_args {
    struct dss_handle dss;
    struct dev_info *dev;
    struct media_info *media;
};

static void *wait_unlock_device(void *f_args)
{
    struct wait_unlock_device_args *args = f_args;

    sleep(WAIT_UNLOCK_SLEEP);
    dss_device_unlock(&args->dss, args->dev, 1);
    dss_media_unlock(&args->dss, args->media, 1);
    dss_fini(&args->dss);
    return NULL;
}

/**
 * Test retry mechanism on EAGAIN
 */
static void test_put_retry(struct pho_xfer_desc *xfer, struct dev_info *dev,
                           struct media_info *media)
{
    struct wait_unlock_device_args wait_unlock_args = { {0} };
    pthread_t wait_unlock_thread;

    wait_unlock_args.dev = dev;
    wait_unlock_args.media = media;

    /* Get dss handle to lock/unlock the device */
    dss_init(&wait_unlock_args.dss);

    /* First lock the only available device and media */
    dss_device_lock(&wait_unlock_args.dss, dev, 1);
    dss_media_lock(&wait_unlock_args.dss, media, 1);

    /* In another thread, sleep for some time and unlock the device */
    assert(pthread_create(&wait_unlock_thread, NULL, wait_unlock_device,
                          &wait_unlock_args) == 0);

    /*
     * Start putting, il should hang waiting for a device to become available,
     * when the other thread unlocks it the put will suceed.
     */
    ASSERT_RC(phobos_put(xfer, 1, NULL, NULL));

    /* Join spawned thread */
    assert(pthread_join(wait_unlock_thread, NULL) == 0);
}

int main(int argc, char **argv)
{
    struct dss_handle       dss = {0};
    struct lrs              lrs = {0};
    struct pho_xfer_desc    xfer = {0};
    struct dev_info         dev = {0};
    struct media_info       media = { {0} };
    char                   *tmp_dir = setup_tmp_dir();
    char                   *default_family;
    char                    dst_path[256];

    assert(system("./setup_db.sh drop_tables setup_tables") == 0);
    test_env_initialize();
    pho_cfg_init_local(NULL);

    xfer.xd_objid = realpath(argv[0], NULL);
    xfer.xd_fpath = argv[0];
    xfer.xd_flags = 0;

    dss_init(&dss);
    lrs_init(&lrs, &dss);

    default_family = getenv("PHOBOS_LRS_default_family");
    if (default_family && strcmp(default_family, "tape") == 0) {
        int rc;
        /* Tape based tests */

        /* We need to get any unknown tape out of the drive for phobos to be
         * able to use it. First, unmount and wait a bit for ltfs to exit
         * properly, then unload the drive if necessary.
         */

        rc = system("umount /mnt/phobos-IBMtape0; sleep 1");
        rc = system("mtx -f /dev/changer unload");
        (void)rc;

        /* Add drive and tape (hardcoded for simplicity). The tape used here is
         * known not to be used by acceptance.sh, it can therefore be formatted.
         */

        /* Tape based tests */
        add_drive(&lrs, "/dev/IBMtape0", &dev);
        add_tape(&lrs, "P00003L5", "LTO5", &media);

        /* Test put retry */
        test_put_retry(&xfer, &dev, &media);

        /* Put retry again to ensure no new error is raised */
        xfer.xd_objid[0] = '0';
        test_put_retry(&xfer, &dev, &media);
    } else {
        /* Dir based tests */
        setenv("PHOBOS_LRS_default_family", "dir", 1);

        /* Add directory drive and media */
        add_dir(&lrs, tmp_dir, &dev, &media);

        /* Simple put */
        ASSERT_RC(phobos_put(&xfer, 1, NULL, NULL));

        /* Two successive get */
        snprintf(dst_path, sizeof(dst_path), "%s/%s", tmp_dir, "dst");
        sprintf(dst_path, "%s/%s", tmp_dir, "dst");
        xfer.xd_fpath = dst_path;
        test_double_get(&xfer);

        /* Test put retry */
        xfer.xd_objid[0] = '0';  /* Artificially build a new object ID */
        xfer.xd_fpath = argv[0];
        test_put_retry(&xfer, &dev, &media);
    }

    free(xfer.xd_objid);
    lrs_fini(&lrs);
    dss_fini(&dss);

    return EXIT_SUCCESS;
}
