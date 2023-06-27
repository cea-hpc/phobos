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
    pho_tlc_resp_t tlc_resp_up;
    pho_tlc_resp_t tlc_resp_down;
    pho_tlc_resp_t tlc_resp_no_ping;
};

static int setup(void **state)
{
    struct resp_state *resp_state;

    resp_state = malloc(sizeof(*resp_state));
    if (!resp_state)
        return -1;

    pho_srl_response_ping_alloc(&resp_state->lrs_resp);
    resp_state->lrs_resp.req_id = 1;

    pho_srl_tlc_response_ping_alloc(&resp_state->tlc_resp_up);
    resp_state->tlc_resp_up.ping->library_is_up = true;
    resp_state->tlc_resp_up.req_id = 1;

    pho_srl_tlc_response_ping_alloc(&resp_state->tlc_resp_down);
    resp_state->tlc_resp_down.ping->library_is_up = false;
    resp_state->tlc_resp_down.req_id = 1;

    pho_tlc_response__init(&resp_state->tlc_resp_no_ping);
    resp_state->tlc_resp_no_ping.ping = NULL;

    *state = resp_state;

    return 0;
}

static int teardown(void **state)
{
    struct resp_state *resp_state = (struct resp_state *)*state;

    if (resp_state) {
        pho_srl_response_free(&resp_state->lrs_resp, false);
        pho_srl_tlc_response_free(&resp_state->tlc_resp_up, false);
        pho_srl_tlc_response_free(&resp_state->tlc_resp_down, false);
        pho_srl_tlc_response_free(&resp_state->tlc_resp_no_ping, false);
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

static void phobos_admin_ping_tlc_success_up(void **state)
{
    struct resp_state *resp_state = (struct resp_state *)*state;
    pho_tlc_resp_t *tlc_resp_up = &resp_state->tlc_resp_up;
    struct admin_handle handle;
    bool library_is_up;
    int rc;

    will_return(_send_and_receive, 0);
    will_return(_send_and_receive, TLC_REQUEST);
    will_return(_send_and_receive, tlc_resp_up);
    rc = phobos_admin_ping_tlc(&handle, &library_is_up);
    assert_int_equal(rc, 0);
    assert_int_equal(library_is_up, true);
}

static void phobos_admin_ping_tlc_success_down(void **state)
{
    struct resp_state *resp_state = (struct resp_state *)*state;
    pho_tlc_resp_t *tlc_resp_down = &resp_state->tlc_resp_down;
    struct admin_handle handle;
    bool library_is_up;
    int rc;

    will_return(_send_and_receive, 0);
    will_return(_send_and_receive, TLC_REQUEST);
    will_return(_send_and_receive, tlc_resp_down);
    rc = phobos_admin_ping_tlc(&handle, &library_is_up);
    assert_int_equal(rc, 0);
    assert_int_equal(library_is_up, false);
}

static void phobos_admin_ping_lrs_no_daemon(void **state)
{
    struct admin_handle handle;
    int rc;

    will_return(_send_and_receive, -ENOTCONN);
    rc = phobos_admin_ping_lrs(&handle);
    assert_int_equal(rc, -ENOTCONN);
}

static void phobos_admin_ping_tlc_no_daemon(void **state)
{
    struct admin_handle handle;
    bool library_is_up;
    int rc;

    will_return(_send_and_receive, -ENOTCONN);
    rc = phobos_admin_ping_tlc(&handle, &library_is_up);
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

static void phobos_admin_ping_tlc_wrong_socket_path(void **state)
{
    struct admin_handle handle;
    bool library_is_up;
    int rc;

    will_return(_send_and_receive, -ENOTSOCK);
    rc = phobos_admin_ping_tlc(&handle, &library_is_up);
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

static void phobos_admin_ping_tlc_bad_response(void **state)
{
    struct resp_state *resp_state = (struct resp_state *)*state;
    pho_tlc_resp_t *tlc_resp_no_ping;
    struct admin_handle handle;
    bool library_is_up;
    int rc;

    tlc_resp_no_ping = &resp_state->tlc_resp_no_ping;
    will_return(_send_and_receive, 0);
    will_return(_send_and_receive, TLC_REQUEST);
    will_return(_send_and_receive, tlc_resp_no_ping);
    rc = phobos_admin_ping_tlc(&handle, &library_is_up);
    assert_int_equal(rc, -EBADMSG);
}

int main(void)
{
    const struct CMUnitTest phobos_ping_test_cases[] = {
        cmocka_unit_test(phobos_admin_ping_lrs_success),
        cmocka_unit_test(phobos_admin_ping_tlc_success_up),
        cmocka_unit_test(phobos_admin_ping_tlc_success_down),
        cmocka_unit_test(phobos_admin_ping_lrs_no_daemon),
        cmocka_unit_test(phobos_admin_ping_tlc_no_daemon),
        cmocka_unit_test(phobos_admin_ping_lrs_wrong_socket_path),
        cmocka_unit_test(phobos_admin_ping_tlc_wrong_socket_path),
        cmocka_unit_test(phobos_admin_ping_lrs_bad_response),
        cmocka_unit_test(phobos_admin_ping_tlc_bad_response),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(phobos_ping_test_cases, setup, teardown);
}
