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

int _send_and_receive(struct pho_comm_info *comm, pho_req_t *lrs_req,
                      pho_resp_t **lrs_resp)
{
    int rc;

    rc = (int)mock();

    if (rc)
        return rc;

    *lrs_resp = (pho_resp_t *)mock();
    return rc;
}
