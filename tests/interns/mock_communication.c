#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>

#include <sys/wait.h>
#include <unistd.h>

#include "pho_srl_lrs.h"
#include "phobos_admin.h"
#include "pho_comm_wrapper.h"

#include <cmocka.h>

int comm_send_and_recv(struct pho_comm_info *comm, pho_req_t *lrs_req,
                       pho_resp_t **lrs_resp)
{
    int rc;

    rc = (int)mock();

    if (rc)
        return rc;

    *lrs_resp = (pho_resp_t *)mock();
    return rc;
}
