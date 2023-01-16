#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>

#include <sys/wait.h>
#include <unistd.h>

#include "pho_srl_lrs.h"
#include "phobos_admin.h"

#include "admin_utils.h"

#include <cmocka.h>

static int setup(void **state)
{
    pho_resp_t *resp;

    resp = malloc(sizeof(*resp));
    if (!resp)
        return -1;

    pho_srl_response_ping_alloc(resp);
    resp->req_id = 1;

    *state = resp;

    return 0;
}

static int teardown(void **state)
{
    if (*state) {
        pho_srl_response_free(*state, false);
        free(*state);
    }
    return 0;
}

static void phobos_admin_ping_success(void **state)
{
    struct admin_handle handle;
    pho_resp_t *resp = (pho_resp_t *)*state;
    int rc;

    will_return(_send_and_receive, resp);
    will_return(_send_and_receive, 0);
    rc = phobos_admin_ping(&handle);
    assert_int_equal(rc, 0);
}

static void phobos_admin_ping_no_daemon(void **state)
{
    struct admin_handle handle;
    pho_resp_t *resp = (pho_resp_t *)*state;
    int rc;

    will_return(_send_and_receive, resp);
    will_return(_send_and_receive, -ENOTCONN);
    rc = phobos_admin_ping(&handle);
    assert_int_equal(rc, -ENOTCONN);
}

static void phobos_admin_ping_wrong_socket_path(void **state)
{
    struct admin_handle handle;
    pho_resp_t *resp = (pho_resp_t *)*state;
    int rc;

    will_return(_send_and_receive, resp);
    will_return(_send_and_receive, -ENOTSOCK);
    rc = phobos_admin_ping(&handle);
    assert_int_equal(rc, -ENOTSOCK);
}

static void phobos_admin_ping_bad_response(void **state)
{
    struct admin_handle handle;
    pho_resp_t *resp = (pho_resp_t *)*state;
    int rc;

    resp->has_ping = false;

    will_return(_send_and_receive, resp);
    will_return(_send_and_receive, 0);
    rc = phobos_admin_ping(&handle);
    assert_int_equal(rc, -EBADMSG);
}

int main(void)
{
    const struct CMUnitTest phobos_ping_test_cases[] = {
        cmocka_unit_test(phobos_admin_ping_success),
        cmocka_unit_test(phobos_admin_ping_no_daemon),
        cmocka_unit_test(phobos_admin_ping_wrong_socket_path),
        cmocka_unit_test(phobos_admin_ping_bad_response),
    };

    pho_context_init();
    atexit(pho_context_fini);

    return cmocka_run_group_tests(phobos_ping_test_cases, setup, teardown);
}
