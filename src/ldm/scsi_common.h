/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  SCSI protocol structures to query drives and libraries.
 */

#ifndef _SCSI_COMMANDS_H
#define _SCSI_COMMANDS_H

#include <stdint.h>

#define MOVE_TIMEOUT_MS    300000 /* 5min */
#define QUERY_TIMEOUT_MS     1000 /* 1sec */

/** Request sense description */
struct scsi_req_sense {
    uint8_t error_code:7;                           /* Byte 0 Bits 0-6 */
    uint8_t valid:1;                                /* Byte 0 Bit 7 */

    uint8_t segment_number;                         /* Byte 1 */

    uint8_t sense_key:4;                            /* Byte 2 Bits 0-3 */
    uint8_t reserved1:1;                            /* Byte 2 Bit 4 */
    uint8_t ILI:1;                                  /* Byte 2 Bit 5 */
    uint8_t EOM:1;                                  /* Byte 2 Bit 6 */
    uint8_t filemark:1;                             /* Byte 2 Bit 7 */

    uint8_t information[4];                         /* Bytes 3-6 */
    uint8_t additional_sense_length;                /* Byte 7 */
    uint8_t command_specific_information[4];        /* Bytes 8-11 */
    uint8_t additional_sense_code;                  /* Byte 12 */
    uint8_t additional_sense_code_qualifier;        /* Byte 13 */
    uint8_t field_replaceable_unit_code;            /* Byte 14 */
    uint8_t bit_pointer:3;                          /* Byte 15 */
    uint8_t BPV:1;
    uint8_t reserved2:2;
    uint8_t command_data:1;
    uint8_t SKSV:1;
    uint8_t field_data[2];                          /* Byte 16,17 */
    uint8_t acsii_data[34];                         /* Bytes 18-51 */
} __attribute__((packed));


/*--------------------------------------
 *     MODE SENSE TYPES
 *--------------------------------------*/
#define PAGECODE_ALL_PAGES       0x3F
#define PAGECODE_ELEMENT_ADDRESS 0x1D
#define PAGECODE_TRANSPORT_GEOM  0x1E
#define PAGECODE_CAPABILITIES    0x1F

/** Mode Sense CDB */
struct mode_sense_cdb {
    uint8_t opcode;       /* 1Ah */

    uint8_t reserved1:3;
    uint8_t dbd:1;        /* disable block descriptors */
    uint8_t reserved2:1;
    uint8_t obsolete:3;

    uint8_t page_code:6;        /* 3Fh: all pages
                                 * 1Dh: element address assignment
                                 * 1Eh: transport geometry
                                 * 1Fh: capabilities */
    uint8_t page_control:2;     /* 00b: last/current, 01b: changeable,
                                   * 10b: default, 11b: saved */

    uint8_t reserved3;
    uint8_t allocation_length;  /* spectra: 48, mtx: 136 */
    uint8_t reserved4;
} __attribute__((packed));

/** Response header for Mode Sense */
struct mode_sense_result_header {
    uint8_t mode_data_length; /* result length, including this header */
    uint8_t reserved[3];
} __attribute__((packed));

/** Element Address Assignment Page */
struct mode_sense_result_EAAP {
        uint8_t page_code:6; /* 1Dh */
        uint8_t reserved1:1;
        uint8_t ps:1; /* pages saveable: 1 */

        uint8_t parameter_length; /* bytes after this one */
        uint16_t first_medium_transport_elt_addr;
        uint16_t medium_transport_elt_nb;
        uint16_t first_storage_elt_addr;
        uint16_t storage_elt_nb;
        uint16_t first_ie_elt_addr;
        uint16_t ie_elt_nb;
        uint16_t first_data_transfer_elt_addr;
        uint16_t data_transfer_elt_nb;
        uint16_t reserved2;
} __attribute__((packed));

#define MODE_SENSE_BUFF_LEN 136

/*--------------------------------------
 *     ELEMENT STATUS TYPES
 *--------------------------------------*/
/** Read Element Status CDB */
struct read_status_cdb {
    uint8_t opcode;

    uint8_t element_type_code:4;
    uint8_t voltag:1;
    uint8_t obs1:3;

    uint16_t starting_address;

    uint16_t elements_nb;

    uint8_t dvcid:1;
    uint8_t curdata:1;
    uint8_t reserved1:6;

    uint8_t alloc_length[3];

    uint8_t reserved2;
    uint8_t reserved3;
} __attribute__((packed));


/** Element Status Header */
struct element_status_header {
    uint16_t first_address;

    uint16_t elements_nb;

    uint8_t reserved[1];
    uint8_t byte_count[3];

} __attribute__((packed));


/** Element Status Page */
struct element_status_page {
    uint8_t type_code;

    uint8_t reserved1:6;
    uint8_t avoltag:1;
    uint8_t pvoltag:1;

    uint16_t ed_len;

    uint8_t reserved2[1];
    uint8_t byte_count[3];

} __attribute__((packed));

/**
 * Element Descriptor.
 *
 * This structure is the merge of:
 * Transport Element descriptor, Storage Element descriptor,
 * Data Transfer Element descriptor, import/export element descriptor.
 */
struct element_descriptor {
    uint16_t address; /* bytes 0-1 */

    uint8_t full:1;       /* byte 2 (LSB) */
    uint8_t impexp:1;
    uint8_t except:1;
    uint8_t access:1;
    uint8_t exp_enabled:1;
    uint8_t imp_enabled:1;
    uint8_t reserved1:2; /*  byte 2 (MSB) */

    uint8_t reserved2;    /* byte 3 */

    uint8_t asc;          /* byte 4 */

    uint8_t ascq;         /* byte 5 */

    uint8_t reserved3[3]; /* bytes 6-8 */

    uint8_t reserved4:6;  /* byte 9 (LSB) */
    uint8_t invert:1;
    uint8_t svalid:1;     /* byte 9 (MSB) */

    uint16_t ssea; /* Source Storage Element Address (bytes 10-11) */

    char pvti[36];           /* Physical Volume Tag (bytes 12-47) */

    union {
    struct dev_i {
        uint8_t code_set:4;   /* byte 48 (LSB) */
        uint8_t reserved5:4;  /* byte 48 (MSB) */

        uint8_t id_type:4;    /* byte 49 (LSB) */
        uint8_t reserved6:4;  /* byte 49 (MSB) */

        uint8_t reserved7;    /* byte 50 */

        uint8_t id_len;       /* byte 51 */

        char devid[32]; /* 36 minus 4 */
    } dev;  /* Device identifier information */
    char avti[36]; /* Alternate Volume Tag */
     } alt_info;

} __attribute__((packed));

/** library dependant. larger ever seen are 84 bytes length... */
#define READ_STATUS_MAX_ELT_LEN 128

/*--------------------------------------
 *     MOVE MEDIUM TYPES
 *--------------------------------------*/
/** Move Medium CDB */
struct move_medium_cdb {
    uint8_t opcode;

    uint8_t reserved1;

    uint16_t transport_element_address;
    uint16_t source_address;
    uint16_t destination_address;

    uint16_t reserved2;

    uint8_t invert:1;
    uint8_t reserved3:7;

    uint8_t control;

} __attribute__((packed));


/*--------------------------------------
 *     SCSI command helper
 *--------------------------------------*/

/** SCSI request direction */
enum scsi_direction {
    SCSI_NONE,
    SCSI_GET,
    SCSI_PUT
};

/**
 * Execute a SCSI command.
 * @param fd           File descriptor to the device.
 * @param cdb          Command buffer.
 * @param sdb          Sense data buffer.
 * @param dxferp       Transfer buffer.
 * @param timeout_msec Timeout in milliseconds (MAX_UINT: no timeout).
 */
int scsi_execute(int fd, enum scsi_direction direction,
                 uint8_t *cdb, int cdb_len,
                 struct scsi_req_sense *sbp, int sb_len,
                 void *dxferp, int dxfer_len,
                 unsigned int timeout_msec);

#endif
