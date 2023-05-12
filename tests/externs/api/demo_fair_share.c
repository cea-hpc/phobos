#include "pho_comm.h"
#include "pho_type_utils.h"
#include "pho_common.h"
#include "pho_srl_common.h"

#include "../../../src/lrs/io_sched.h"
#include "../../../src/lrs/lrs_cfg.h"

#include <string.h>
#include <termios.h>
#include <unistd.h>

enum req_status {
    READY,
    SENT,
};

struct request {
    pho_req_t req;
    enum req_status status;
};

struct requests {
    GArray *reads;
    GArray *writes;
    GArray *formats;
};

struct context {
    size_t inflight;
    size_t nb_reads;
    size_t nb_writes;
    size_t nb_formats;
    struct requests requests;
    GPtrArray *tapes_to_format;
    GPtrArray *tapes_to_read;
    struct pho_comm_info *comm;
    struct dss_handle dss;
};

static int send_request(struct pho_comm_info *comm,
                        struct request *request)
{
    struct pho_comm_data msg;
    int rc;

    if (pho_request_is_write(&request->req))
        pho_info("write");
    else if (pho_request_is_read(&request->req))
        pho_info("read: %s", request->req.ralloc->med_ids[0]->name);
    else if (pho_request_is_format(&request->req))
        pho_info("format: %s", request->req.format->med_id->name);

    msg = pho_comm_data_init(comm);
    rc = pho_srl_request_pack(&request->req, &msg.buf);
    if (rc)
        return rc;

    rc = pho_comm_send(&msg);
    free(msg.buf.buff);
    if (rc)
        return rc;

    return 0;
}

static int handle_read_response(struct context *context,
                                struct pho_comm_info *comm,
                                pho_resp_t *resp)
{
    struct request *read_req;
    struct request request;
    int rc;

    pho_info("read: %s", resp->ralloc->media[0]->med_id->name);

    rc = pho_srl_request_release_alloc(&request.req, 1);
    if (rc)
        return rc;

    request.req.id = resp->req_id;
    request.req.release->media[0]->med_id->family = PHO_RSC_TAPE;
    rsc_id_cpy(request.req.release->media[0]->med_id,
               resp->ralloc->media[0]->med_id);
    request.req.release->media[0]->to_sync = false;
    request.req.release->media[0]->rc = 0;

    send_request(comm, &request);

    read_req = &g_array_index(context->requests.reads,
                              struct request,
                              resp->req_id);
    read_req->status = READY;

    return 0;
}

static int handle_write_response(struct context *context,
                                 struct pho_comm_info *comm,
                                 pho_resp_t *resp)
{
    struct request *write_req;
    struct request request;
    int rc;

    pho_info("write: %s", resp->walloc->media[0]->med_id->name);
    rc = pho_srl_request_release_alloc(&request.req, 1);
    if (rc)
        return rc;

    request.req.id = resp->req_id;
    request.req.release->media[0]->med_id->family = PHO_RSC_TAPE;
    rsc_id_cpy(request.req.release->media[0]->med_id,
               resp->walloc->media[0]->med_id);
    request.req.release->media[0]->to_sync = false;
    request.req.release->media[0]->rc = 0;

    send_request(comm, &request);

    write_req = &g_array_index(context->requests.writes,
                               struct request,
                               resp->req_id);
    write_req->status = READY;

    return 0;
}

static int handle_format_response(struct context *context,
                                  struct pho_comm_info *comm,
                                  pho_resp_t *resp)
{
    struct media_info *medium;
    struct dss_filter filter;
    struct request *format_req;
    struct pho_id id;
    enum fs_type fs;
    int count;
    int rc;

    pho_info("format: %s", resp->format->med_id->name);
    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          "  {\"DSS::MDA::family\": \"%s\"},"
                          "  {\"DSS::MDA::id\": \"%s\"}"
                          "]}",
                          rsc_family2str(resp->format->med_id->family),
                          resp->format->med_id->name);
    if (rc)
        return rc;

    rc = dss_media_get(&context->dss, &filter, &medium, &count);
    dss_filter_free(&filter);
    if (rc)
        return rc;

    assert(count == 1);

    /* set the medium back to blanc to keep the state of the system */
    medium->fs.status = PHO_FS_STATUS_BLANK;

    rc = dss_media_set(&context->dss, medium, 1, DSS_SET_UPDATE, FS_STATUS);
    if (rc)
        return rc;

    dss_res_free(medium, 1);

    format_req = &g_array_index(context->requests.formats,
                               struct request,
                               resp->req_id);
    format_req->status = READY;

    return 0;
}

static int build_read_request(struct context *context)
{
    struct media_info *medium;
    struct request req;
    int rc;

    pho_info("context->requests.reads->len: %d, "
             "context->tapes_to_read->len: %d",
             context->requests.reads->len,
             context->tapes_to_read->len);

    // TODO make sure that each medium is read at least once
    medium = g_ptr_array_index(context->tapes_to_read,
                               context->requests.reads->len %
                               context->tapes_to_read->len);

    rc = pho_srl_request_read_alloc(&req.req, 1);
    if (rc)
        return rc;

    req.req.ralloc->med_ids[0]->name = strdup(medium->rsc.id.name);
    req.req.ralloc->med_ids[0]->family = PHO_RSC_TAPE;
    req.req.ralloc->n_required = 1;
    req.req.id = context->requests.reads->len;
    req.status = READY;

    g_array_append_val(context->requests.reads, req);

    return 0;
}

static int build_write_request(struct context *context)
{
    struct request req;
    size_t n = 0;
    int rc;

    rc = pho_srl_request_write_alloc(&req.req, 1, &n);
    if (rc)
        return rc;

    req.req.walloc->media[0]->size = 0;
    req.req.walloc->family = PHO_RSC_TAPE;
    req.req.id = context->requests.writes->len;
    req.status = READY;

    g_array_append_val(context->requests.writes, req);

    return 0;
}

static int build_format_request(struct context *context)
{
    struct media_info *medium;
    struct request req;
    int rc;

    // TODO make sure that each medium is formatted at least once
    medium = g_ptr_array_index(context->tapes_to_format,
                               context->requests.reads->len %
                               context->tapes_to_format->len);

    rc = pho_srl_request_format_alloc(&req.req);
    if (rc)
        return rc;

    req.req.format->fs = PHO_FS_LTFS;
    req.req.format->unlock = false;
    req.req.format->force = true;
    req.req.format->med_id->family = PHO_RSC_TAPE;
    req.req.format->med_id->name = strdup(medium->rsc.id.name);
    req.req.id = context->requests.formats->len;

    g_array_append_val(context->requests.formats, req);

    return 0;
}

static int build_and_send_requests(struct context *context,
                                   struct pho_comm_info *comm)
{
    int rc;
    int i;

    while (context->nb_reads > context->requests.reads->len) {
        rc = build_read_request(context);
        if (rc)
            return rc;
    }

    while (context->nb_writes > context->requests.writes->len) {
        rc = build_write_request(context);
        if (rc)
            return rc;
    }

    while (context->nb_formats > context->requests.formats->len) {
        rc = build_format_request(context);
        if (rc)
            return rc;
    }

    for (i = 0; i < context->requests.reads->len && context->nb_reads;
         i++) {
        struct request *req;

        req = &g_array_index(context->requests.reads, struct request, i);

        if (req->status == READY) {
            send_request(comm, req);
            context->inflight++;
            req->status = SENT;
        }
    }

    for (i = 0; i < context->requests.writes->len && i < context->nb_writes;
         i++) {
        struct request *req;

        req = &g_array_index(context->requests.writes, struct request, i);

        if (req->status == READY) {
            send_request(comm, req);
            context->inflight++;
            req->status = SENT;
        }
    }

    for (i = 0; i < context->requests.formats->len && i < context->nb_formats;
         i++) {
        struct request *req;

        req = &g_array_index(context->requests.formats, struct request, i);

        if (req->status == READY) {
            send_request(comm, req);
            context->inflight++;
            req->status = SENT;
        }
    }

    return 0;
}

static void *send_requests(void *data)
{
    struct context *context = data;
    const char *socket_path;
    int rc;

    context->comm = malloc(sizeof(*context->comm));
    if (!context->comm)
        pthread_exit(NULL);

    socket_path = PHO_CFG_GET(cfg_lrs, PHO_CFG_LRS, server_socket);
    rc = pho_comm_open(context->comm, socket_path, false);
    if (rc) {
        printf("failed to open socket: %s\n", strerror(-rc));
        pthread_exit(NULL);
    }

    while (true) {
        struct pho_comm_data *responses = NULL;
        int n_responses = 0;
        int i;

        rc = build_and_send_requests(context, context->comm);
        if (rc)
            pthread_exit(NULL);

        if (context->inflight == 0) {
            /* Do not call receive as client receive is blocking */
            usleep(1000000);
            continue;
        }

        rc = pho_comm_recv(context->comm, &responses, &n_responses);
        if (rc) {
            fprintf(stderr, "failed to recv data: %s\n", strerror(-rc));
            pthread_exit(NULL);
        }

        if (n_responses == 0)
            continue;

        for (i = 0; i < n_responses; i++) {
            pho_resp_t *resp;

            resp = pho_srl_response_unpack(&responses[i].buf);
            context->inflight--;

            if (pho_response_is_read(resp))
                handle_read_response(context, context->comm, resp);
            else if (pho_response_is_write(resp))
                handle_write_response(context, context->comm, resp);
            else if (pho_response_is_format(resp))
                handle_format_response(context, context->comm, resp);
            else if (pho_response_is_error(resp))
                fprintf(stderr, "received an error response: %s\n",
                        strerror(-resp->error->rc));
            else
                pthread_exit(NULL);
        }
    }
}

#define array_size(arr) (sizeof(arr) / sizeof((arr)[0]))

struct splitter {
    char *str;     /* string to split (duplicated from the input string) */
    char *token;   /* the current token */
    char *saveptr; /* internal state */
    char split_char; /* the caracter on which to split */
};

struct splitter *make_splitter(const char *str, char c)
{
    char delim[2] = { c, '\0' };
    struct splitter *splitter;

    splitter = malloc(sizeof(*splitter));
    if (!splitter)
        return NULL;

    splitter->str = strdup(str);
    if (!splitter->str) {
        free(splitter);
        return NULL;
    }

    splitter->split_char = c;
    splitter->token = strtok_r(splitter->str, delim, &splitter->saveptr);

    return splitter;
}

void splitter_fini(struct splitter *splitter)
{
    free(splitter->str);
    free(splitter);
}

const char *splitter_next(struct splitter *splitter)
{
    char delim[2] = { splitter->split_char, '\0' };

    splitter->token = strtok_r(NULL, delim, &splitter->saveptr);

    return splitter->token;
}

static void set_nb_requests(const char *command,
                            enum io_request_type type,
                            struct context *context)
{
    struct splitter *splitter = make_splitter(command, ' ');
    const char *cmdname = splitter->token;
    const char *strnum;
    int64_t value;

    strnum = splitter_next(splitter);
    if (splitter_next(splitter)) {
        fprintf(stderr, "Too many arguments to '%s'\n", cmdname);
        goto free_splitter;
    }

    if (!strnum) {
        fprintf(stderr, "Missing integer argument to '%s'\n", cmdname);
        goto free_splitter;
    }

    value = str2int64(strnum);
    if (value == INT64_MIN || value < 0) {
        fprintf(stderr, "Invalid number '%s', expected integer >= 0\n", strnum);
        return;
    }

    switch (type) {
    case IO_REQ_READ:
        context->nb_reads = value;
        break;
    case IO_REQ_WRITE:
        context->nb_writes = value;
        break;
    case IO_REQ_FORMAT:
        context->nb_formats = value;
        break;
    }
    printf("reads: %lu, write: %lu, formats: %lu\n",
           context->nb_reads,
           context->nb_writes,
           context->nb_formats);

free_splitter:
    splitter_fini(splitter);
}

static int fetch_tapes(struct context *context)
{
    struct media_info *media = NULL;
    struct dss_filter filter;
    int count = 0;
    int rc;
    int i;

    rc = dss_init(&context->dss);
    if (rc)
        return rc;

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          "  {\"DSS::MDA::family\": \"%s\"},"
                          "  {\"DSS::MDA::adm_status\": \"%s\"}"
                          "]}",
                          rsc_family2str(PHO_RSC_TAPE),
                          rsc_adm_status2str(PHO_RSC_ADM_ST_UNLOCKED));
    if (rc)
        return rc;

    rc = dss_media_get(&context->dss, &filter, &media, &count);
    dss_filter_free(&filter);
    if (rc)
        return rc;

    if (count == 0)
        return 0;

    for (i = 0; i < count; i++) {
        if (media[i].fs.status == PHO_FS_STATUS_BLANK) {
            g_ptr_array_add(context->tapes_to_format,
                            media_info_dup(&media[i]));
            pho_info("format: %s", media[i].rsc.id.name);
        } else if ((media[i].fs.status == PHO_FS_STATUS_EMPTY ||
                    /* Since we are not realy going to read anything, we don't
                     * care if the tape is empty.
                     */
                    media[i].fs.status == PHO_FS_STATUS_USED ||
                    media[i].fs.status == PHO_FS_STATUS_FULL) &&
                   media[i].flags.get) {
            g_ptr_array_add(context->tapes_to_read,
                            media_info_dup(&media[i]));
            pho_info("read: %s", media[i].rsc.id.name);
        }
    }

    return 0;
}

int main(void)
{
    struct context context = {0};
    char command[32];
    pthread_t sender;
    int rc;

    // TODO add -w -r -f to specify the repartition using CLI arguments
    // TODO count the number of errors and return it
    // TODO count the number of requests that where never answered
    // TODO on error find the type of request and decrement the inflight count
    // TODO remove put access from readable tapes
    // TODO make sure that each request is sent regularly

    context.requests.reads = g_array_new(false, false,
                                         sizeof(struct request));
    context.requests.writes = g_array_new(false, false,
                                          sizeof(struct request));
    context.requests.formats = g_array_new(false, false,
                                           sizeof(struct request));
    context.tapes_to_format = g_ptr_array_new();
    context.tapes_to_read = g_ptr_array_new();
    context.inflight = 0;

    pho_context_init();
    atexit(pho_context_fini);

    pho_cfg_init_local(NULL);

    fetch_tapes(&context);

    rc = pthread_create(&sender, NULL, send_requests, &context);
    if (rc)
        return rc;

    while (true) {
        size_t cmdlen;
        char *space;
        char *read;
        char next;
        char *cr;

        fputs("> ", stdout);
        read = fgets(command, array_size(command) - 1, stdin);
        if (!read)
            return 0;

        cr = strchr(command, '\n');
        if (cr)
            *cr = '\0';

        if (strlen(command) == 0)
            continue;

        space = strchr(command, ' ');
        if (!space)
            space = cr;

        if (!strcmp(command, "quit")) {
            if (context.inflight == 0)
                break;
            pho_info("Cannot stop the client, some requests are still ongoing");
        }

        if (!strncmp(command, "reads", min(5, space - command)))
            set_nb_requests(command, IO_REQ_READ, &context);
        else if (!strncmp(command, "writes", min(6, space - command)))
            set_nb_requests(command, IO_REQ_WRITE, &context);
        else if (!strncmp(command, "formats", min(7, space - command)))
            set_nb_requests(command, IO_REQ_FORMAT, &context);
        else
            fprintf(stderr, "Unknown command '%.*s'\n",
                    space - command, command);
    }
    pho_comm_close(context.comm);
    dss_fini(&context.dss);

    return 0;
}
