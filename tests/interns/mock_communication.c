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

int _send_and_receive(struct admin_handle *adm, pho_req_t *req,
                      pho_resp_t **resp)
{
    *resp = (pho_resp_t *)mock();

    return (int)mock();
}
