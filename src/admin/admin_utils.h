#ifndef _ADMIN_UTILS
#define _ADMIN_UTILS

#include "pho_srl_lrs.h"
#include "pho_srl_tlc.h"

enum request_type {
    INVAL_REQUEST_TYPE = -1,
    LRS_REQUEST = 0,
    TLC_REQUEST = 1,
    LAST_REQUEST_TYPE = 2,
};

static const char * const request_type_names[] = {
    [LRS_REQUEST] = "LRS",
    [TLC_REQUEST] = "TLC",
};

static inline const char *request_type2str(enum request_type type)
{
    if (type >= LAST_REQUEST_TYPE || type < 0)
        return NULL;
    return request_type_names[type];
}

struct proto_req {
    enum request_type type;
    union req {
        pho_req_t *lrs_req;
        pho_tlc_req_t *tlc_req;
    } msg;
};

struct proto_resp {
    enum request_type type;
    union resp {
        pho_resp_t *lrs_resp;
        pho_tlc_resp_t *tlc_resp;
    } msg;
};

int _send_and_receive(struct pho_comm_info *comm, struct proto_req proto_req,
                      struct proto_resp *proto_resp);

#endif /* _ADMIN_UTILS */
