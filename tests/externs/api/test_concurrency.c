#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <phobos_store.h>
#include <pho_cfg.h>
#include <pho_common.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

typedef int (*phobos_store_action_t)(struct pho_xfer_desc *xfers, size_t n,
                                     pho_completion_cb_t cb, void *udata);

struct thread_data {
    struct pho_xfer_desc   xfer;
    pthread_t              tid;
    phobos_store_action_t  action;
    pthread_barrier_t     *barrier;
    char                  *file;
    int                    rc;
};

struct conf {
    int                   num_threads;
    phobos_store_action_t action;
    int                   type;
    char                 *file;
    bool                  load_first;
};

static void *action_thread(void *tdata)
{
    struct thread_data *data = (struct thread_data *)tdata;

    /* the file descriptor has to be unique to each thread,
     * otherwise, read operations cannot be concurrent on the
     * same file descriptor.
     */
    data->xfer.xd_fd = open(data->file, O_RDONLY);
    /* Let phobos fail if open failed as this should not happen anyway */

    pthread_barrier_wait(data->barrier);
    data->rc = data->action(&data->xfer, 1, NULL, NULL);

    return &data->rc;
}

static int start_action(struct thread_data *td)
{
    int rc;

    rc = pthread_create(&td->tid, NULL, action_thread, (void *)td);
    if (rc)
        return -rc;

    return 0;
}

static int str2action(char *type, struct conf *conf)
{
    if (!strcmp(type, "put")) {
        conf->action = phobos_put;
        conf->type = PHO_XFER_OP_PUT;
    } else if (!strcmp(type, "get")) {
        conf->action = phobos_get;
        conf->type = PHO_XFER_OP_GET;
    } else if (!strcmp(type, "getmd")) {
        conf->action = phobos_getmd;
        conf->type = PHO_XFER_OP_GETMD;
    } else {
        return -EINVAL;
    }

    return 0;
}

static struct option cliopts[] = {
    {
        .name = "num-threads",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 'N',
    },
    {
        .name = "action",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 'A',
    },
    {
        .name = "file",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 'F',
    },
    {
        .name = "load-first",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'L',
    },
    {
        .name = "help",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'h',
    },
    { 0 }
};

static void usage(char *progname)
{
    printf(
        "Usage: %s [--load-first] [--num-threads <n>] "
        "[--action <put|get|getmd>] --file <file>\n"
        "Run <n> synchronized store actions concurrently to check for race "
        "conditions\n"
        "\n"
        "    --action      one of put, get, getmd (only put supported)\n"
        "    --load-first  load the config file before starting threads\n"
        "    --num-threads number of concurrent operation run simultaneously\n"
        "    --file        name of the file to read for put or write for get\n",
        progname);
}

static int parse_args(int argc, char **argv, struct conf *conf)
{
    int rc;
    char c;

    while ((c = getopt_long(argc, argv, "F:A:N:h", cliopts, NULL)) != -1) {
        switch (c) {
        case 'A':
            rc = str2action(optarg, conf);
            if (rc)
                return rc;

            break;
        case 'N':
            conf->num_threads = atoi(optarg);
            if (!conf->num_threads)
                return -EINVAL;

            break;
        case 'L':
            conf->load_first = true;
            break;
        case 'F':
            conf->file = optarg;
            break;
        case 'h':
            usage(argv[0]);
            exit(EXIT_SUCCESS);
        case '?':
        default:
            return -EINVAL;
        }
    }

    if (!conf->file)
        return -EINVAL;

    if (!conf->num_threads)
        conf->num_threads = 2;

    if (!conf->action)
        conf->action = phobos_put;

    return 0;
}

static char *make_oid(char *file, int i)
{
    size_t filelength = strlen(file);
    char *oid = NULL;
    int sz;

    if (i > 9999 || i < 0)
        exit(ERANGE);

    /* 5: 9999\0 */
    oid = calloc(filelength + 5, sizeof(*oid));
    if (!oid)
        exit(errno);

    strcpy(oid, file);
    sz = snprintf(oid + filelength, 5, "%d", i);
    oid[filelength + sz] = '\0';

    return oid;
}

static int file_size(char *file, off_t *size)
{
    struct stat st;

    if (stat(file, &st) == -1) {
        *size = 0;
        return -errno;
    }

    *size = st.st_size;

    return 0;
}

int main(int argc, char **argv)
{
    struct thread_data *threads = NULL;
    pthread_barrier_t barrier;
    struct conf conf = {0};
    off_t size;
    int rc, i;

    pho_context_init();
    atexit(pho_context_fini);

    conf.load_first = false;

    rc = parse_args(argc, argv, &conf);
    if (rc)
        return rc;

    threads = calloc(conf.num_threads, sizeof(*threads));
    if (!threads)
        return errno;

    pthread_barrier_init(&barrier, NULL, conf.num_threads + 1);
    printf("Will perform %d concurrent %ss...\n",
           conf.num_threads,
           conf.type == PHO_XFER_OP_PUT ? "put" :
           conf.type == PHO_XFER_OP_GET ? "get" :
           conf.type == PHO_XFER_OP_GETMD ? "getmd" :
           "???");

    rc = file_size(conf.file, &size);
    if (rc)
        return -rc;

    if (conf.load_first) {
        rc = pho_cfg_init_local(NULL);
        if (rc)
            return -rc;
    }

    for (i = 0; i < conf.num_threads; i++) {
        struct pho_xfer_desc *xfer = &threads[i].xfer;
        struct pho_attrs *lyt_params =
            &xfer->xd_params.put.lyt_params;

        xfer->xd_op = conf.type;
        xfer->xd_objid = make_oid(conf.file, i);
        xfer->xd_objuuid = NULL;
        xfer->xd_version = 0;
        xfer->xd_flags = 0;
        xfer->xd_params.put.size = size;
        xfer->xd_params.put.family = PHO_RSC_DIR;
        xfer->xd_params.put.overwrite = true;
        xfer->xd_params.put.layout_name = "raid1";
        pho_attr_set(lyt_params, "repl_count", "3");

        threads[i].action = conf.action;
        threads[i].barrier = &barrier;
        threads[i].file = conf.file;

        printf("starting thread %d\n", i);
        start_action(&threads[i]);
    }

    pthread_barrier_wait(&barrier);

    for (i = 0; i < conf.num_threads; i++) {
        int *threadrc = NULL;

        pthread_join(threads[i].tid, (void **)&threadrc);
        pho_xfer_desc_clean(&threads[i].xfer);
        free(threads[i].xfer.xd_objid);

        rc = rc ? : *threadrc;
    }

    free(threads);
    pthread_barrier_destroy(&barrier);

    return rc;
}
