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
#include "pho_test_utils.h"
#include "pho_test_xfer_utils.h"
#include "phobos_admin.h"
#include "phobos_store.h"
#include "../dss/dss_lock.h"

#define PHO_TMP_DIR_TEMPLATE "/tmp/pho_XXXXXX"
#define LOCK_OWNER "generic_lock_owner"
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
    TMP_DIR = xmalloc(sizeof(PHO_TMP_DIR_TEMPLATE));
    strcpy(TMP_DIR, PHO_TMP_DIR_TEMPLATE);
    assert(mkdtemp(TMP_DIR) != NULL);
    atexit(rm_tmp_dir);
    return TMP_DIR;
}

static void reinit_xfer(struct pho_xfer_desc *xfer, const char *path,
                        const char *objpath, enum pho_xfer_op op)
{
    free(xfer->xd_targets->xt_objid);
    free(xfer->xd_targets->xt_objuuid);
    close(xfer->xd_targets->xt_fd);

    xfer_desc_open_path(xfer, path, op, 0);
    xfer->xd_op = op;
    xfer->xd_targets->xt_objid = realpath(objpath, NULL);
    if (op == PHO_XFER_OP_PUT)
        xfer->xd_params.put.family = PHO_RSC_INVAL;
    else if (op == PHO_XFER_OP_GET)
        xfer->xd_params.get.copy_name = NULL;
}

static void add_dir_or_drive(struct admin_handle *adm, struct dss_handle *dss,
                             const char *path, struct dev_info *dev,
                             struct media_info *media)
{
    struct dev_adapter_module *adapter = NULL;
    struct ldm_dev_state dev_st = {0};
    struct pho_id dev_id = {};
    char hostname[256];

    gethostname(hostname, sizeof(hostname));
    strtok(hostname, ".");

    /* Add dir media */
    if (media) {
        pho_id_name_set(&media->rsc.id, path, "legacy");
        media->rsc.id.family = PHO_RSC_DIR;
        media->rsc.adm_status = PHO_RSC_ADM_ST_LOCKED;
        media->fs.type = PHO_FS_POSIX;
        media->addr_type = PHO_ADDR_HASH1;
        media->flags.put = true;
        media->flags.get = true;
        media->flags.delete = true;
        ASSERT_RC(dss_media_insert(dss, media, 1));
    }

    /* Get dir device */
    get_dev_adapter(media ? PHO_RSC_DIR : PHO_RSC_TAPE, &adapter);
    ASSERT_RC(ldm_dev_query(adapter, path, &dev_st));

    pho_id_name_set(&dev->rsc.id, dev_st.lds_serial ? : "", "legacy");
    dev->rsc.id.family = dev_st.lds_family;
    dev->rsc.model = dev_st.lds_model ? xstrdup(dev_st.lds_model) : NULL;
    dev->rsc.adm_status = PHO_RSC_ADM_ST_UNLOCKED;
    dev->path = (char *) path;
    dev->host = hostname;

    ldm_dev_state_fini(&dev_st);

    /* Add dir device */
    pho_id_name_set(&dev_id, path, "legacy");
    dev_id.family = dev->rsc.id.family;
    ASSERT_RC(phobos_admin_device_add(adm, &dev_id, 1, false, "legacy"));

    /* Format and unlock media */
    if (media)
        ASSERT_RC(phobos_admin_format(adm, &media->rsc.id, 1, 0, PHO_FS_POSIX,
                                      true, false));
}

static void add_tape(struct admin_handle *adm, struct dss_handle *dss,
                     const char *tape_id, const char *model,
                     struct media_info *media)
{
    /* Add dir media */
    pho_id_name_set(&media->rsc.id, tape_id, "legacy");
    media->rsc.id.family = PHO_RSC_TAPE;
    media->rsc.model = xstrdup(model);
    media->rsc.adm_status = PHO_RSC_ADM_ST_UNLOCKED;
    media->fs.type = PHO_FS_LTFS;
    media->addr_type = PHO_ADDR_HASH1;
    media->flags.put = true;
    media->flags.get = true;
    media->flags.delete = true;
    ASSERT_RC(dss_media_insert(dss, media, 1));

    /* This can fail if the tape has already been formatted */
    phobos_admin_format(adm, &media->rsc.id, 1, 0, PHO_FS_LTFS, true, false);
    free(media->rsc.model);
}

/** Test that phobos_get work properly */
static void test_get(struct pho_xfer_desc *xfer, const char *path)
{
    ASSERT_RC(phobos_get(xfer, 1, NULL, NULL));
    ASSERT_RC(xfer->xd_rc);
    unlink(path);
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
    _dss_unlock(&args->dss, DSS_DEVICE, args->dev, 1, LOCK_OWNER, getpid());
    _dss_unlock(&args->dss, DSS_MEDIA, args->media, 1, LOCK_OWNER, getpid());
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
    _dss_lock(&wait_unlock_args.dss, DSS_DEVICE, dev, 1, LOCK_OWNER, getpid(),
              false, NULL);
    _dss_lock(&wait_unlock_args.dss, DSS_MEDIA, media, 1, LOCK_OWNER, getpid(),
              false, NULL);

    /* In another thread, sleep for some time and unlock the device */
    assert(pthread_create(&wait_unlock_thread, NULL, wait_unlock_device,
                          &wait_unlock_args) == 0);

    /*
     * Start putting, il should hang waiting for a device to become available,
     * when the other thread unlocks it the put will suceed.
     */
    ASSERT_RC(phobos_put(xfer, 1, NULL, NULL));
    ASSERT_RC(xfer->xd_rc);

    /* Join spawned thread */
    assert(pthread_join(wait_unlock_thread, NULL) == 0);
}

int main(int argc, char **argv)
{
    struct admin_handle     adm;
    struct dss_handle       dss = {0};
    struct pho_xfer_desc    xfer = {0};
    struct pho_xfer_target  target = {0};
    struct dev_info         dev = { { {0} } };
    struct media_info       media = { { {0} } };
    char                   *tmp_dir = setup_tmp_dir();
    char                   *default_family;
    char                    dst_path[256];

    assert(system("../../setup_db.sh drop_tables") == 0);
    assert(system("../../setup_db.sh setup_tables") == 0);
    test_env_initialize();

    xfer.xd_targets = &target;
    xfer.xd_ntargets = 1;
    reinit_xfer(&xfer, argv[0], argv[0], PHO_XFER_OP_PUT);

    dss_init(&dss);
    phobos_admin_init(&adm, true, NULL);

    default_family = getenv("PHOBOS_STORE_default_family");
    if (default_family && strcmp(default_family, "tape") == 0) {
        int rc;
        /* Tape based tests */

        /* We need to get any unknown tape out of the drive for phobos to be
         * able to use it. First, unmount and wait a bit for ltfs to exit
         * properly, then unload the drive if necessary.
         */

        rc = system("umount /mnt/phobos-st0; sleep 1");
        rc = system("mtx -f /dev/changer unload");
        (void)rc;

        /* Add drive and tape (hardcoded for simplicity). The tape used here is
         * known not to be used by acceptance.sh, it can therefore be formatted.
         */

        /* Tape based tests */
        add_dir_or_drive(&adm, &dss, "/dev/st0", &dev, NULL);
        add_tape(&adm, &dss, "P00003L5", "LTO5", &media);

        /* Test put retry */
        test_put_retry(&xfer, &dev, &media);

        /* Put retry again to ensure no new error is raised */
        reinit_xfer(&xfer, argv[0], argv[0], PHO_XFER_OP_PUT);
        xfer.xd_targets->xt_objid[0] = '0';
        test_put_retry(&xfer, &dev, &media);
    } else {
        /* Dir based tests */

        /* Add directory drive and media */
        add_dir_or_drive(&adm, &dss, tmp_dir, &dev, &media);

        /* Simple put */
        ASSERT_RC(phobos_put(&xfer, 1, NULL, NULL));
        ASSERT_RC(xfer.xd_rc);

        /* Two successive get */
        snprintf(dst_path, sizeof(dst_path), "%s/%s", tmp_dir, "dst");
        reinit_xfer(&xfer, dst_path, argv[0], PHO_XFER_OP_GET);
        test_get(&xfer, dst_path);

        reinit_xfer(&xfer, dst_path, argv[0], PHO_XFER_OP_GET);
        test_get(&xfer, dst_path);

        /* Test put retry */
        reinit_xfer(&xfer, argv[0], argv[0], PHO_XFER_OP_PUT);
        /* Artificially build a new object ID */
        xfer.xd_targets->xt_objid[0] = '0';
        test_put_retry(&xfer, &dev, &media);
    }

    free(dev.rsc.model);
    free(xfer.xd_targets->xt_objid);
    free(xfer.xd_targets->xt_objuuid);
    xfer_close_fd(xfer.xd_targets);
    phobos_admin_fini(&adm);
    dss_fini(&dss);

    return EXIT_SUCCESS;
}
