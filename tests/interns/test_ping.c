#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>

#include <sys/wait.h>
#include <unistd.h>

#include "pho_srl_lrs.h"
#include "pho_srl_tlc.h"
#include "phobos_admin.h"

#include "admin_utils.h"

#include <cmocka.h>

struct resp_state {
    pho_resp_t lrs_resp;
};

static int setup(void **state)
{
    struct resp_state *resp_state;

    resp_state = xmalloc(sizeof(*resp_state));

    pho_srl_response_ping_alloc(&resp_state->lrs_resp);
    resp_state->lrs_resp.req_id = 1;

    *state = resp_state;

    return 0;
}

static int teardown(void **state)
{
    struct resp_state *resp_state = (struct resp_state *)*state;

    if (resp_state) {
        pho_srl_response_free(&resp_state->lrs_resp, false);
        free(resp_state);
    }
    return 0;
}

static void phobos_admin_ping_lrs_success(void **state)
{
    struct resp_state *resp_state = (struct resp_state *)*state;
    pho_resp_t *lrs_resp = &resp_state->lrs_resp;
    struct admin_handle handle;
    int rc;

    will_return(_send_and_receive, 0);
    will_return(_send_and_receive, LRS_REQUEST);
    will_return(_send_and_receive, lrs_resp);
    rc = phobos_admin_ping_lrs(&handle);
    assert_int_equal(rc, 0);
}

static void phobos_admin_ping_lrs_no_daemon(void **state)
{
    struct admin_handle handle;
    int rc;

    will_return(_send_and_receive, -ENOTCONN);
    rc = phobos_admin_ping_lrs(&handle);
    assert_int_equal(rc, -ENOTCONN);
}

static void phobos_admin_ping_lrs_wrong_socket_path(void **state)
{
    struct admin_handle handle;
    int rc;

    will_return(_send_and_receive, -ENOTSOCK);
    rc = phobos_admin_ping_lrs(&handle);
    assert_int_equal(rc, -ENOTSOCK);
}

static void phobos_admin_ping_lrs_bad_response(void **state)
{
    struct resp_state *resp_state = (struct resp_state *)*state;
    pho_resp_t *lrs_resp = &resp_state->lrs_resp;
    struct admin_handle handle;
    int rc;

    lrs_resp->has_ping = false;
    will_return(_send_and_receive, 0);
    will_return(_send_and_receive, LRS_REQUEST);
    will_return(_send_and_receive, lrs_resp);
    rc = phobos_admin_ping_lrs(&handle);
    assert_int_equal(rc, -EBADMSG);
}

int main(void)
{
    const struct CMUnitTest phobos_ping_test_cases[] = {
        cmocka_unit_test(phobos_admin_ping_lrs_success),
        cmocka_unit_test(phobos_admin_ping_lrs_no_daemon),
        cmocka_unit_test(phobos_admin_ping_lrs_wrong_socket_path),
        cmocka_unit_test(phobos_admin_ping_lrs_bad_response),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(phobos_ping_test_cases, setup, teardown);
}
