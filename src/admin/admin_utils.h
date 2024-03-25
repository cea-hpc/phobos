#ifndef _ADMIN_UTILS
#define _ADMIN_UTILS

#include "pho_srl_lrs.h"

int _send_and_receive(struct pho_comm_info *comm, pho_req_t *req,
                      pho_resp_t **resp);

#endif /* _ADMIN_UTILS */
