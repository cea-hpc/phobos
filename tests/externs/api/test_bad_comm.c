/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2022 CEA/DAM.
 *
 *  This file is part of Phobos.
 *
 *  Phobos is free software: you can redistribute it and/or modify it under
 *  the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  Phobos is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with Phobos. If not, see <http://www.gnu.org/licenses/>.
 */
/**
 * \brief  Integration test: phobosd management of bad requests
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include "pho_comm.h"
#include "pho_dss.h"
#include "pho_srl_lrs.h"
#include "pho_test_utils.h"
#include "pho_type_utils.h"

static int _send_and_receive(struct pho_comm_info *ci, pho_req_t *req,
                             pho_resp_t **resp)
{
    struct pho_comm_data *data_in = NULL;
    struct pho_comm_data data_out;
    int n_data_in = 0;
    int rc;
    int i;

    data_out = pho_comm_data_init(ci);
    pho_srl_request_pack(req, &data_out.buf);
    rc = pho_comm_send(&data_out);
    free(data_out.buf.buff);
    if (rc)
        return rc;

    rc = pho_comm_recv(ci, &data_in, &n_data_in);
    if (rc || n_data_in != 1) {
        if (data_in)
            for (i = 0; i < n_data_in; ++i)
                free(data_in[i].buf.buff);
        free(data_in);
        if (rc)
            return rc;
        return -EINVAL;
    }

    *resp = pho_srl_response_unpack(&data_in->buf);
    free(data_in);
    if (!*resp)
        return -EINVAL;
    return 0;
}

static int _send_request(struct pho_comm_info *ci, pho_req_t *req)
{
    struct pho_comm_data data;
    int rc;

    data = pho_comm_data_init(ci);
    pho_srl_request_pack(req, &data.buf);
    rc = pho_comm_send(&data);
    free(data.buf.buff);

    return rc;
}

static int _check_error(pho_resp_t *resp, const char *msg_prefix,
                                 int expected_rc)
{
    if (!pho_response_is_error(resp))
        LOG_RETURN(PHO_TEST_FAILURE, "%s did not return an error", msg_prefix);
    if (resp->error->rc != expected_rc)
        LOG_RETURN(PHO_TEST_FAILURE, "%s did not return the expected rc "
                                     "(exp: %d, got: %d)", msg_prefix,
                                     expected_rc, resp->error->rc);
    return PHO_TEST_SUCCESS;
}

static int test_bad_put(void *arg)
{
    struct pho_comm_info *ci = (struct pho_comm_info *)arg;
    size_t n_tags[1] = {1};
    pho_resp_t *resp;
    pho_req_t req;
    int rc = 0;

    // Bad resource family
    pho_srl_request_write_alloc(&req, 1, n_tags);
    req.id = 0;
    req.walloc->family = PHO_RSC_INVAL;
    req.walloc->media[0]->size = 1;
    req.walloc->media[0]->tags[0] = xstrdup("ratatouille");
    assert(!_send_and_receive(ci, &req, &resp));
    rc = _check_error(resp, "Walloc -- bad resource family", -EINVAL);
    if (rc)
        goto out_fail;

    // Family not available
    pho_srl_response_free(resp, true);
    ++req.id;
    req.walloc->family = PHO_RSC_TAPE;
    assert(!_send_and_receive(ci, &req, &resp));
    rc = _check_error(resp, "Walloc -- family not available", -EINVAL);
    if (rc)
        goto out_fail;

    // Bad tag request
    pho_srl_response_free(resp, true);
    ++req.id;
    req.walloc->family = PHO_RSC_DIR;
    assert(!_send_and_receive(ci, &req, &resp));
    rc = _check_error(resp, "Walloc -- bad tag request", -ENOSPC);

out_fail:
    pho_srl_request_free(&req, false);
    pho_srl_response_free(resp, true);

    return rc;
}

static int fetch_medium_info(struct media_info **medium,
                             const struct pho_id *id)
{
    struct media_info *res = NULL;
    struct dss_filter filter;
    struct dss_handle dss;
    int count = 0;

    int rc;

    rc = dss_filter_build(&filter,
                          "{\"$AND\": ["
                          "  {\"DSS::MDA::family\": \"%s\"},"
                          "  {\"DSS::MDA::id\": \"%s\"},"
                          "  {\"DSS::MDA::library\": \"%s\"}"
                          "]}",
                          rsc_family2str(id->family), id->name, id->library);
    if (rc)
        return rc;

    rc = dss_init(&dss);
    if (rc)
        return rc;

    rc = dss_media_get(&dss, &filter, &res, &count);
    dss_filter_free(&filter);
    dss_fini(&dss);
    if (rc)
        return rc;

    assert(count == 1);

    *medium = media_info_dup(res);
    dss_res_free(res, count);

    return 0;
}

static void fill_pho_id(struct pho_id *id, pho_resp_t *write_resp)
{
    assert(pho_response_is_write(write_resp));

    id->family = write_resp->walloc->media[0]->med_id->family;
    pho_id_name_set(id, write_resp->walloc->media[0]->med_id->name,
                    write_resp->walloc->media[0]->med_id->library);
}

static int send_write_and_release_with_rc(struct pho_comm_info *ci,
                                          int client_rc)
{
    struct media_info *current_info;
    struct media_info *new_info;
    int rc = PHO_TEST_SUCCESS;
    size_t n_tags[1] = {0};
    struct pho_id med_id;
    pho_resp_t *resp;
    size_t size = 1;
    pho_req_t req;

    pho_srl_request_write_alloc(&req, 1, n_tags);
    req.id = 0;
    req.walloc->family = PHO_RSC_DIR;
    req.walloc->media[0]->size = size;
    assert(!_send_and_receive(ci, &req, &resp));
    if (!pho_response_is_write(resp))
        GOTO(out, rc = PHO_TEST_FAILURE);

    fill_pho_id(&med_id, resp);
    rc = fetch_medium_info(&current_info, &med_id);
    if (rc)
        goto out;

    pho_srl_request_free(&req, false);
    pho_srl_request_release_alloc(&req, 1);
    req.release->media[0]->med_id->family = PHO_RSC_DIR;
    req.release->media[0]->med_id->name =
        xstrdup(resp->walloc->media[0]->med_id->name);
    req.release->media[0]->med_id->library =
        xstrdup(resp->walloc->media[0]->med_id->library);
    req.release->media[0]->to_sync = true;
    req.release->media[0]->size_written = size;
    req.release->media[0]->rc = client_rc;

    pho_srl_response_free(resp, true);
    assert(!_send_and_receive(ci, &req, &resp));
    rc = fetch_medium_info(&new_info, &med_id);
    if (rc)
        goto out;

    if (client_rc == 0) {
        if (current_info->stats.nb_obj + 1 != new_info->stats.nb_obj)
            LOG_GOTO(out, rc = PHO_TEST_FAILURE,
                     "Total number of objects was not increased by 1");

    } else {
        struct dss_handle dss;

        dss_init(&dss);

        if (new_info->rsc.adm_status != PHO_RSC_ADM_ST_FAILED)
            LOG_GOTO(out, rc = PHO_TEST_FAILURE,
                     "Medium is not set to failed after ENOSPC error");

        if (current_info->stats.nb_obj != new_info->stats.nb_obj)
            LOG_GOTO(out, rc = PHO_TEST_FAILURE,
                     "Number of objects was increased but no object was "
                     "written");

        /* set the status back to make other tests pass */
        new_info->rsc.adm_status = current_info->rsc.adm_status;
        rc = dss_media_update(&dss, new_info, 1, ADM_STATUS);
        dss_fini(&dss);
    }

out:
    pho_srl_request_free(&req, false);
    pho_srl_response_free(resp, true);

    media_info_free(current_info);
    media_info_free(new_info);

    return rc;
}

static int test_put_io_error(void *arg)
{
    int rc;

    rc = send_write_and_release_with_rc(arg, 0);
    if (rc)
        return rc;

    return send_write_and_release_with_rc(arg, -ENOSPC);
}

static int test_bad_mput(void *arg)
{
    struct pho_comm_info *ci = (struct pho_comm_info *)arg;
    size_t tags[1] = {1};
    pho_resp_t *resps[2];
    pho_req_t reqs[2];
    int rc;
    int i;

    tags[0] = 0;
    pho_srl_request_write_alloc(&reqs[0], 1, tags);
    tags[0] = 1;
    pho_srl_request_write_alloc(&reqs[1], 1, tags);

    reqs[0].walloc->media[0]->tags = NULL;
    reqs[1].walloc->media[0]->tags[0] = xstrdup("invalid-tag");

    for (i = 0; i < 2; i++) {
        reqs[i].id = i;
        reqs[i].walloc->family = PHO_RSC_DIR;
        reqs[i].walloc->media[0]->size = 1;
        rc = _send_and_receive(ci, &reqs[i], &resps[i]);
        if (rc)
            goto out;

        printf("i=%d, req_id=%d\n", i, resps[i]->req_id);
        assert(resps[i]->req_id == i);
    }

    assert(pho_response_is_write(resps[0]));
    assert(pho_response_is_error(resps[1]));

    pho_srl_request_free(&reqs[0], false);

    pho_srl_request_release_alloc(&reqs[0], 1);
    reqs[0].id = 0;
    reqs[0].release->media[0]->med_id->family = PHO_RSC_DIR;
    reqs[0].release->media[0]->med_id->name =
        xstrdup(resps[0]->walloc->media[0]->med_id->name);
    reqs[0].release->media[0]->med_id->library =
        xstrdup(resps[0]->walloc->media[0]->med_id->library);
    reqs[0].release->media[0]->to_sync = false;

    rc = _send_request(ci, &reqs[0]);

out:
    for (i = 0; i < 2; i++) {
        pho_srl_request_free(&reqs[i], false);
        pho_srl_response_free(resps[i], true);
    }

    return rc;
}

static int test_bad_get(void *arg)
{
    struct pho_comm_info *ci = (struct pho_comm_info *)arg;
    pho_resp_t *resp;
    pho_req_t req;
    int rc = 0;

    // Bad resource family
    pho_srl_request_read_alloc(&req, 1);
    req.id = 0;
    req.ralloc->n_required = 1;
    req.ralloc->med_ids[0]->family = PHO_RSC_INVAL;
    req.ralloc->med_ids[0]->name = xstrdup("/tmp/test.pho.1");
    req.ralloc->med_ids[0]->library = xstrdup("legacy");
    assert(!_send_and_receive(ci, &req, &resp));
    rc = _check_error(resp, "Get -- bad resource family", -EINVAL);
    if (rc)
        goto out_fail;

    // Bad resource name
    pho_srl_response_free(resp, true);
    ++req.id;
    req.ralloc->med_ids[0]->family = PHO_RSC_DIR;
    free(req.ralloc->med_ids[0]->name);
    free(req.ralloc->med_ids[0]->library);
    req.ralloc->med_ids[0]->name = xstrdup("/tmp/not/a/med");
    req.ralloc->med_ids[0]->library = xstrdup("legacy");
    assert(!_send_and_receive(ci, &req, &resp));
    rc = _check_error(resp, "Get -- bad resource name", -ENXIO);

out_fail:
    pho_srl_request_free(&req, false);
    pho_srl_response_free(resp, true);

    return rc;
}

static int test_bad_mget(void *arg)
{
    struct pho_comm_info *ci = (struct pho_comm_info *)arg;
    pho_resp_t *resps[2];
    pho_req_t reqs[2];
    int rc;
    int i;

    pho_srl_request_read_alloc(&reqs[0], 1);
    pho_srl_request_read_alloc(&reqs[1], 1);

    reqs[0].ralloc->med_ids[0]->name = xstrdup("/tmp/test.pho.1");
    reqs[0].ralloc->med_ids[0]->library = xstrdup("legacy");
    reqs[1].ralloc->med_ids[0]->name = xstrdup("/not/a/dir");
    reqs[1].ralloc->med_ids[0]->library = xstrdup("legacy");

    for (i = 0; i < 2; i++) {
        reqs[i].id = i;
        reqs[i].ralloc->n_required = 1;
        reqs[i].ralloc->med_ids[0]->family = PHO_RSC_DIR;
        rc = _send_and_receive(ci, &reqs[i], &resps[i]);
        if (rc)
            goto out;

        printf("i=%d, req_id=%d\n", i, resps[i]->req_id);
        assert(resps[i]->req_id == i);
    }

    assert(pho_response_is_read(resps[0]));
    assert(pho_response_is_error(resps[1]));

    pho_srl_request_free(&reqs[0], false);

    pho_srl_request_release_alloc(&reqs[0], 1);
    reqs[0].id = 0;
    reqs[0].release->media[0]->med_id->family = PHO_RSC_DIR;
    reqs[0].release->media[0]->med_id->name =
        xstrdup(resps[0]->ralloc->media[0]->med_id->name);
    reqs[0].release->media[0]->med_id->library =
        xstrdup(resps[0]->ralloc->media[0]->med_id->library);
    reqs[0].release->media[0]->to_sync = false;

    rc = _send_request(ci, &reqs[0]);

out:
    for (i = 0; i < 2; i++) {
        pho_srl_request_free(&reqs[i], false);
        pho_srl_response_free(resps[i], true);
    }

    return rc;
}

static int test_bad_release(void *arg)
{
    struct pho_comm_info *ci = (struct pho_comm_info *)arg;
    pho_resp_t *resp;
    pho_req_t req;
    int rc = 0;

    // Bad resource name
    pho_srl_request_release_alloc(&req, 1);
    req.id = 0;
    req.release->media[0]->med_id->family = PHO_RSC_DIR;
    req.release->media[0]->med_id->name = xstrdup("/tmp/not/a/med");
    req.release->media[0]->med_id->library = xstrdup("legacy");
    req.release->media[0]->to_sync = true;
    assert(!_send_and_receive(ci, &req, &resp));
    rc = _check_error(resp, "Release -- bad resource name", -ENODEV);

    pho_srl_request_free(&req, false);
    pho_srl_response_free(resp, true);

    return rc;
}

static int test_bad_format(void *arg)
{
    struct pho_comm_info *ci = (struct pho_comm_info *)arg;
    pho_resp_t *resp;
    pho_req_t req;
    int rc = 0;

    // Bad file system
    pho_srl_request_format_alloc(&req);
    req.id = 0;
    req.format->fs = PHO_FS_INVAL;
    req.format->med_id->family = PHO_RSC_DIR;
    req.format->med_id->name = xstrdup("/tmp/test.pho.3");
    req.format->med_id->library = xstrdup("legacy");
    assert(!_send_and_receive(ci, &req, &resp));
    rc = _check_error(resp, "Format -- bad file system", -ENOTSUP);
    if (rc)
        goto out_fail;

    // Bad resource family
    pho_srl_response_free(resp, true);
    ++req.id;
    req.format->fs = PHO_FS_POSIX;
    req.format->med_id->family = PHO_RSC_INVAL;
    assert(!_send_and_receive(ci, &req, &resp));
    rc = _check_error(resp, "Format -- bad resource family", -EINVAL);
    if (rc)
        goto out_fail;

    // Bad resource name
    pho_srl_response_free(resp, true);
    ++req.id;
    req.format->med_id->family = PHO_RSC_DIR;
    free(req.format->med_id->name);
    free(req.format->med_id->library);
    req.format->med_id->name = xstrdup("/tmp/not/a/med");
    req.format->med_id->library = xstrdup("legacy");
    assert(!_send_and_receive(ci, &req, &resp));
    rc = _check_error(resp, "Format -- bad resource name", -ENXIO);

out_fail:
    pho_srl_request_free(&req, false);
    pho_srl_response_free(resp, true);

    return rc;
}

static int test_bad_notify(void *arg)
{
    struct pho_comm_info *ci = (struct pho_comm_info *)arg;
    pho_resp_t *resp;
    pho_req_t req;
    int rc = 0;

    // Bad operation
    pho_srl_request_notify_alloc(&req);
    req.id = 0;
    req.notify->op = PHO_NTFY_OP_INVAL;
    req.notify->wait = true;
    assert(!_send_and_receive(ci, &req, &resp));
    rc = _check_error(resp, "Notify -- bad operation", -EINVAL);
    if (rc)
        goto out_fail;

    // Bad resource family
    pho_srl_response_free(resp, true);
    ++req.id;
    req.notify->op = PHO_NTFY_OP_DEVICE_ADD;
    req.notify->rsrc_id->family = PHO_RSC_INVAL;
    assert(!_send_and_receive(ci, &req, &resp));
    rc = _check_error(resp, "Notify -- bad family", -EINVAL);
    if (rc)
        goto out_fail;

    // Bad resource name
    pho_srl_response_free(resp, true);
    ++req.id;
    req.notify->rsrc_id->family = PHO_RSC_DIR;
    req.notify->rsrc_id->name = xstrdup("/tmp/not/a/dev");
    req.notify->rsrc_id->library = xstrdup("legacy");
    assert(!_send_and_receive(ci, &req, &resp));
    rc = _check_error(resp, "Notify -- bad resource name", -ENXIO);
    if (rc)
        goto out_fail;

out_fail:
    pho_srl_request_free(&req, false);
    pho_srl_response_free(resp, true);

    return rc;
}

static int test_bad_configure(void *arg)
{
    struct pho_comm_info *ci = (struct pho_comm_info *)arg;
    pho_resp_t *resp;
    pho_req_t req;
    int rc = 0;

    // No configuration
    pho_srl_request_configure_alloc(&req);
    req.id = 0;
    req.configure->op = PHO_CONF_OP_SET;
    req.configure->configuration = NULL;
    assert(!_send_and_receive(ci, &req, &resp));
    rc = _check_error(resp, "Configure -- no configuration", -EPROTO);
    if (rc)
        goto out_fail;

    // Invalid JSON string
    pho_srl_response_free(resp, true);
    ++req.id;
    req.configure->configuration = xstrdup("this is not JSON");
    assert(!_send_and_receive(ci, &req, &resp));
    rc = _check_error(resp, "Configure -- invalid JSON string", -EINVAL);
    if (rc)
        goto out_fail;

    // Not a JSON array
    free(req.configure->configuration);
    pho_srl_response_free(resp, true);
    ++req.id;
    req.configure->configuration = xstrdup("{}");
    assert(!_send_and_receive(ci, &req, &resp));
    rc = _check_error(resp, "Configure -- not a JSON array", -EINVAL);
    if (rc)
        goto out_fail;

    // not an object
    free(req.configure->configuration);
    pho_srl_response_free(resp, true);
    ++req.id;
    req.configure->configuration = xstrdup("[ 1 ]");
    assert(!_send_and_receive(ci, &req, &resp));
    rc = _check_error(resp, "Configure -- not an object", -EINVAL);
    if (rc)
        goto out_fail;

    // missing key 'value'
    free(req.configure->configuration);
    pho_srl_response_free(resp, true);
    ++req.id;
    req.configure->configuration =
        xstrdup("[{\"section\": \"s\", \"key\": \"k\"}]");
    assert(!_send_and_receive(ci, &req, &resp));
    rc = _check_error(resp, "Configure -- missing key 'value'", -EINVAL);
    if (rc)
        goto out_fail;

    // invalid value for 'section'
    free(req.configure->configuration);
    pho_srl_response_free(resp, true);
    ++req.id;
    req.configure->configuration =
        xstrdup("[{\"key\": \"k\", \"section\": 1, \"value\": \"v\"}]");
    assert(!_send_and_receive(ci, &req, &resp));
    rc = _check_error(resp, "Configure -- invalid value for 'section'",
                      -EINVAL);
    if (rc)
        goto out_fail;

    // second value missing 'key'
    free(req.configure->configuration);
    pho_srl_response_free(resp, true);
    ++req.id;
    req.configure->configuration =
        xstrdup("["
                "    {\"section\": \"s\", \"key\": \"k\", \"value\": \"v\"}, "
                "    {\"section\": \"s\", \"value\": \"v\"} "
                "]");
    assert(!_send_and_receive(ci, &req, &resp));
    rc = _check_error(resp, "Configure -- second value missing 'key'", -EINVAL);
    if (rc)
        goto out_fail;

out_fail:
    pho_srl_request_free(&req, false);
    pho_srl_response_free(resp, true);

    return rc;
}

static int test_bad_ping(void *arg)
{
    struct pho_comm_info *ci = (struct pho_comm_info *)arg;
    union pho_comm_addr addr;
    pho_resp_t *resp;
    pho_req_t req;
    int rc = 0;

    pho_srl_request_ping_alloc(&req);
    req.id = 0;
    assert(!_send_request(ci, &req));
    pho_comm_close(ci);
    addr.af_unix.path = "/tmp/socklrs";
    assert(!pho_comm_open(ci, &addr, PHO_COMM_UNIX_CLIENT));

    rc = _send_and_receive(ci, &req, &resp);
    if (!rc)
        pho_srl_response_free(resp, true);

    /* The first read on the new connection will return ECONNRESET as we closed
     * the socket before reading the response from the LRS
     */
    assert(rc == -ECONNRESET || rc == 0);

    /* Make sure that we can still ping the LRS */
    assert(!_send_and_receive(ci, &req, &resp));
    pho_srl_response_free(resp, true);

    pho_srl_request_free(&req, false);
}

int main(int argc, char **argv)
{
    union pho_comm_addr addr;
    struct pho_comm_info ci;

    pho_context_init();
    atexit(pho_context_fini);

    pho_cfg_init_local(NULL);
    atexit(pho_cfg_local_fini);

    addr.af_unix.path = "/tmp/socklrs";
    assert(!pho_comm_open(&ci, &addr, PHO_COMM_UNIX_CLIENT));

    pho_run_test("Test: bad ping", test_bad_ping, &ci, PHO_TEST_SUCCESS);
    pho_run_test("Test: bad put", test_bad_put, &ci, PHO_TEST_SUCCESS);
    pho_run_test("Test: bad mput", test_bad_mput, &ci, PHO_TEST_SUCCESS);

    pho_run_test("Test: bad get", test_bad_get, &ci, PHO_TEST_SUCCESS);
    pho_run_test("Test: bad mget", test_bad_mget, &ci, PHO_TEST_SUCCESS);
    pho_run_test("Test: bad release", test_bad_release, &ci, PHO_TEST_SUCCESS);
    pho_run_test("Test: bad format", test_bad_format, &ci, PHO_TEST_SUCCESS);

    pho_run_test("Test: bad notify", test_bad_notify, &ci, PHO_TEST_SUCCESS);
    /* run last as the state of the device used is set to failed at the end */
    pho_run_test("Test: put I/O error", test_put_io_error, &ci,
                 PHO_TEST_SUCCESS);
    pho_run_test("Test: bad config", test_bad_configure, &ci, PHO_TEST_SUCCESS);

    pho_comm_close(&ci);
}
