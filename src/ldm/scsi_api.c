/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/**
 * \brief  Application-friendly API to perform SCSI operations.
 */
#include "scsi_api.h"
#include "pho_common.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <malloc.h>
#include <endian.h>
#include <assert.h>
#include <scsi/scsi.h>
#include <endian.h>

/* #define DEBUG 1 */

int mode_sense(int fd, struct mode_sense_info *info)
{
    struct mode_sense_cdb req = {0};
    struct mode_sense_result_header *res_hdr;
    struct mode_sense_result_EAAP   *res_element_addr;
    struct scsi_req_sense error = {0};
    unsigned char buffer[MODE_SENSE_BUFF_LEN];
    int rc;

    if (!info)
        return -EINVAL;

    memset(buffer, 0, sizeof(buffer));

    pho_debug("scsi_execute: MODE_SENSE, buffer_len=%u", MODE_SENSE_BUFF_LEN);

    req.opcode = MODE_SENSE;
    req.dbd = 1;            /* disable block descriptors */
    req.page_code = PAGECODE_ELEMENT_ADDRESS;
    req.page_control = 0;   /* last/current */
    req.allocation_length = MODE_SENSE_BUFF_LEN;
    /* all other fields are zeroed */

    rc = scsi_execute(fd, SCSI_GET, (unsigned char *)&req, sizeof(req), &error,
                      sizeof(error), buffer, sizeof(buffer), QUERY_TIMEOUT_MS);
    if (rc)
        return rc;

    res_hdr = (struct mode_sense_result_header *)buffer;
    if (res_hdr->mode_data_length < sizeof(struct mode_sense_result_header) +
                                    sizeof(struct mode_sense_result_EAAP) - 1)
        LOG_RETURN(-EIO, "Unexpected result size %u < %u",
                   res_hdr->mode_data_length,
                   sizeof(struct mode_sense_result_header) +
                   sizeof(struct mode_sense_result_EAAP) - 1);

    res_element_addr = (struct mode_sense_result_EAAP *)((ptrdiff_t)res_hdr
                        + sizeof(struct mode_sense_result_header));
    if (res_element_addr->page_code != PAGECODE_ELEMENT_ADDRESS)
        LOG_RETURN(-EIO, "Invalid page_code %#x != %#x",
                   res_element_addr->page_code, PAGECODE_ELEMENT_ADDRESS);

    info->arms.first_addr
        = be16toh(res_element_addr->first_medium_transport_elt_addr);
    info->arms.nb = be16toh(res_element_addr->medium_transport_elt_nb);

    info->slots.first_addr
        = be16toh(res_element_addr->first_storage_elt_addr);
    info->slots.nb = be16toh(res_element_addr->storage_elt_nb);

    info->impexp.first_addr = be16toh(res_element_addr->first_ie_elt_addr);
    info->impexp.nb = be16toh(res_element_addr->ie_elt_nb);

    info->drives.first_addr
        = be16toh(res_element_addr->first_data_transfer_elt_addr);
    info->drives.nb = be16toh(res_element_addr->data_transfer_elt_nb);

    return 0;
}

/** convert an array of 3 bytes (big endian 24 bits)
 * to a 32bits little endian */
static inline uint32_t be24toh(uint8_t *a)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
        return ((uint32_t)(a[0]) << 16) + ((uint32_t)(a[1]) << 8)
               + (uint32_t)a[2];
#else
    #error  "Only little endian architectures are currently supported"
#endif
}

/** convert a 32bits little endian to an array of 3 bytes
 * (big endian 24 bits).
 */
static inline void htobe24(uint32_t h, uint8_t *be)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
        be[0] = (h >> 16) & 0xFF;
        be[1] = (h >> 8) & 0xFF;
        be[2] = h & 0xFF;
#else
    #error  "Only little endian architectures are currently supported"
#endif
}

/**
 * Read the next element from READ_ELEMENT_STATUS reply.
 * @param[in]     elmt          Pointer to the next element status.
 * @param[in]     page          Header of element status list.
 * @param[in,out] elem_out      Element status information to be filled in.
 * @return The number of bytes read, or < 0 on error.
 */
static int read_next_element_status(const struct element_descriptor *elmt,
                                    const struct element_status_page *page,
                                    struct element_status *elem_out)
{
    elem_out->type = page->type_code;
    elem_out->address = be16toh(elmt->address);

    elem_out->full = elmt->full;
    elem_out->impexp = elmt->impexp;
    elem_out->accessible = elmt->access;
    elem_out->exp_enabled = elmt->exp_enabled;
    elem_out->imp_enabled = elmt->imp_enabled;
    elem_out->invert = elmt->invert;

    elem_out->except = elmt->except;
    elem_out->error_code = elmt->asc;
    elem_out->error_code_qualifier = elmt->ascq;

    if (elmt->svalid) {
        elem_out->src_addr_is_set = true;
        elem_out->src_addr = be16toh(elmt->ssea);
    }

    if (page->pvoltag) {
        strncpy(elem_out->vol, elmt->pvti, VOL_ID_LEN);
        elem_out->vol[VOL_ID_LEN - 1] = '\0';
        rstrip(elem_out->vol);
    }

    if (elem_out->type == SCSI_TYPE_DRIVE) {
        /* id length (host endianess) */
        int id_len = elmt->alt_info.dev.id_len;

        if (id_len > 0) {
            strncpy(elem_out->dev_id, elmt->alt_info.dev.devid,
                    id_len);
            elem_out->dev_id[id_len] = '\0';
            rstrip(elem_out->dev_id);
        }
    }

    return be16toh(page->ed_len);
}

int element_status(int fd, enum element_type_code type,
                   uint16_t start_addr, uint16_t nb, bool allow_motion,
                   struct element_status **elmt_list, int *elmt_count)
{
    struct read_status_cdb        req = {0};
    struct element_status_header *res_hdr;
    struct                        scsi_req_sense error = {0};
    unsigned char                *buffer = NULL;
    int                           len = 0;
    int                           rc, i;
    unsigned char                *curr;
    int                           count, byte_count;

    /* length to be allocated for the result buffer */
    len = sizeof(struct element_status_header)
          + nb * sizeof(struct element_status_page)
          + nb * READ_STATUS_MAX_ELT_LEN;

    buffer = calloc(1, len);
    if (!buffer)
        return -ENOMEM;

    pho_debug("scsi_execute: READ_ELEMENT_STATUS, type=%#x, start_addr=%#hx, "
              "count=%#hu, buffer_len=%u", type, start_addr, nb, len);

    req.opcode = READ_ELEMENT_STATUS;
    req.voltag = 1; /* return volume bar-code */
    req.element_type_code = type;
    req.starting_address = htobe16(start_addr);
    req.elements_nb = htobe16(nb);
    req.curdata = allow_motion;
    req.dvcid = 1; /* return device identifier */
    htobe24(len, req.alloc_length);

    rc = scsi_execute(fd, SCSI_GET, (unsigned char *)&req, sizeof(req), &error,
                      sizeof(error), buffer, len, QUERY_TIMEOUT_MS);
    if (rc)
        goto free_buff;

    *elmt_list = calloc(nb, sizeof(struct element_status));
    if (!(*elmt_list))
        GOTO(free_buff, rc = -ENOMEM);

    /* pointer to result header */
    res_hdr = (struct element_status_header *)buffer;

#ifdef DEBUG
    pho_debug("%hu elements returned (%d bytes/%u buff len)\n",
              be16toh(res_hdr->elements_nb), be24toh(res_hdr->byte_count), len);
#endif

    /* pointer to the first element */
    curr = (unsigned char *)res_hdr + sizeof(struct element_status_header);

    /* number of elements returned */
    count = be16toh(res_hdr->elements_nb);

    /* number of bytes returned */
    byte_count = be24toh(res_hdr->byte_count);

    *elmt_count = 0;

    for (i = 0; i < count && byte_count >= sizeof(struct element_status_page);
         i++) {
        /* current element page */
        struct element_status_page *page = (struct element_status_page *)curr;

        curr += sizeof(struct element_status_page);
        byte_count -=  sizeof(struct element_status_page);

#ifdef DEBUG
        pho_debug("        type=%hhu, vol=%u, avol=%u, descriptor_len=%hu, "
               "byte_count=%u\n", page->type_code, !!page->pvoltag,
               !!page->avoltag, be16toh(page->ed_len),
               be24toh(page->byte_count));
#endif

        while (byte_count > 0) {
            rc = read_next_element_status((struct element_descriptor *)curr,
                                          page, &((*elmt_list)[*elmt_count]));
            if (rc < 0)
                goto free_buff;

            (*elmt_count)++;

            byte_count -= rc;
            curr += rc;
#ifdef DEBUG
            pho_debug("%d bytes left\n", byte_count);
#endif
        }
    }

    rc = 0;

free_buff:
    free(buffer);

    return rc;
}

void element_status_list_free(struct element_status *elmt_list)
{
    free(elmt_list);
}

int move_medium(int fd, uint16_t arm_addr, uint16_t src_addr, uint16_t tgt_addr)
{
    struct move_medium_cdb req = {0};
    struct scsi_req_sense  error = {0};

    pho_debug("scsi_execute: MOVE_MEDIUM, arm_addr=%#hx, src_addr=%#hx, "
              "tgt_addr=%#hx", arm_addr, src_addr, tgt_addr);

    req.opcode = MOVE_MEDIUM;
    req.transport_element_address = htobe16(arm_addr);
    req.source_address = htobe16(src_addr);
    req.destination_address = htobe16(tgt_addr);

    return scsi_execute(fd, SCSI_GET, (unsigned char *)&req, sizeof(req),
                        &error, sizeof(error), NULL, 0, MOVE_TIMEOUT_MS);
}
