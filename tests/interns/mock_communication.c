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

int _send_and_receive(struct pho_comm_info *comm, struct proto_req proto_req,
                      struct proto_resp *proto_resp)
{
    int rc;

    rc = (int)mock();

    if (rc)
        return rc;

    proto_resp->type = (enum request_type)mock();
    switch (proto_resp->type) {
    case LRS_REQUEST:
        proto_resp->msg.lrs_resp = (pho_resp_t *)mock();
        break;
    case TLC_REQUEST:
        proto_resp->msg.tlc_resp = (pho_tlc_resp_t *)mock();
        break;
    default:
        return -1;
    }

    return rc;
}
