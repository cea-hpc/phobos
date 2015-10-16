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

/** Request sense description */
struct scsi_req_sense {
    unsigned char error_code:7;                           /* Byte 0 Bits 0-6 */
    unsigned char valid:1;                                /* Byte 0 Bit 7 */

    unsigned char segment_number;                         /* Byte 1 */

    unsigned char sense_key:4;                            /* Byte 2 Bits 0-3 */
    unsigned char reserved1:1;                            /* Byte 2 Bit 4 */
    unsigned char ILI:1;                                  /* Byte 2 Bit 5 */
    unsigned char EOM:1;                                  /* Byte 2 Bit 6 */
    unsigned char filemark:1;                             /* Byte 2 Bit 7 */

    unsigned char information[4];                         /* Bytes 3-6 */
    unsigned char additional_sense_length;                /* Byte 7 */
    unsigned char command_specific_information[4];        /* Bytes 8-11 */
    unsigned char additional_sense_code;                  /* Byte 12 */
    unsigned char additional_sense_code_qualifier;        /* Byte 13 */
    unsigned char field_replaceable_unit_code;            /* Byte 14 */
    unsigned char bit_pointer:3;                          /* Byte 15 */
    unsigned char BPV:1;
    unsigned char reserved3:2;
    unsigned char command_data:1;
    unsigned char SKSV:1;
    unsigned char field_data[2];                          /* Byte 16,17 */
    unsigned char acsii_data[34];                         /* Bytes 18-51 */
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
    unsigned char opcode;       /* 1Ah */

    unsigned char reserved2:3;
    unsigned char dbd:1;        /* disable block descriptors */
    unsigned char reserved1:1;
    unsigned char obsolete:3;

    unsigned char page_code:6;  /* 3Fh: all pages
                                 * 1Dh: element address assignment
                                 * 1Eh: transport geometry
                                 * 1Fh: capabilities */
    unsigned char page_control:2; /* 00b: last/current, 01b: changeable,
                                   * 10b: default, 11b: saved */

    unsigned char reserved3;
    unsigned char allocation_length; /* spectra: 48, mtx: 136 */
    unsigned char reserved4;
} __attribute__((packed));

/** Response header for Mode Sense */
struct mode_sense_result_header {
    unsigned char mode_data_length; /* result length, including this header */
    unsigned char reserved[3];
} __attribute__((packed));

/** Element Address Assignment Page */
struct mode_sense_result_EAAP {
        unsigned char page_code:6; /* 1Dh */
        unsigned char reserved1:1;
        unsigned char ps:1; /* pages saveable: 1 */

        unsigned char parameter_length; /* bytes after this one */
        unsigned short int first_medium_transport_elt_addr;
        unsigned short int medium_transport_elt_nb;
        unsigned short int first_storage_elt_addr;
        unsigned short int storage_elt_nb;
        unsigned short int first_ie_elt_addr;
        unsigned short int ie_elt_nb;
        unsigned short int first_data_transfer_elt_addr;
        unsigned short int data_transfer_elt_nb;
        unsigned short reserved2;
} __attribute__((packed));

#define MODE_SENSE_BUFF_LEN 136


/*--------------------------------------
 *     ELEMENT STATUS TYPES
 *--------------------------------------*/
/** Read Element Status CDB */
struct read_status_cdb {
    unsigned char opcode;

    unsigned char element_type_code:4;
    unsigned char voltag:1;
    unsigned char obs1:3;

    unsigned short int starting_address;

    unsigned short int elements_nb;

    unsigned char dvcid:1;
    unsigned char curdata:1;
    unsigned char reserved1:6;

    unsigned char alloc_length[3];

    unsigned char reserved2;
    unsigned char reserved3;
} __attribute__((packed));


/** Element Status Header */
struct element_status_header {
    unsigned short int first_address;

    unsigned short int elements_nb;

    unsigned char reserved[1];
    unsigned char byte_count[3];

} __attribute__((packed));


/** Element Status Page */
struct element_status_page {
    unsigned char type_code;

    unsigned char reserved1:6;
    unsigned char avoltag:1;
    unsigned char pvoltag:1;

    unsigned short int ed_len;

    unsigned char reserved[1];
    unsigned char byte_count[3];

} __attribute__((packed));

/**
 * Element Descriptor.
 *
 * This structure is the merge of:
 * Transport Element descriptor, Storage Element descriptor,
 * Data Transfer Element descriptor, import/export element descriptor.
 */
struct element_descriptor {
    unsigned short int address; /* bytes 0-1 */

    unsigned char full:1;       /* byte 2 (LSB) */
    unsigned char impexp:1;
    unsigned char except:1;
    unsigned char access:1;
    unsigned char exp_enabled:1;
    unsigned char imp_enabled:1;
    unsigned char reserved1:2; /*  byte 2 (MSB) */

    unsigned char reserved2;    /* byte 3 */

    unsigned char asc;          /* byte 4 */

    unsigned char ascq;         /* byte 5 */

    unsigned char reserved3[3]; /* bytes 6-8 */

    unsigned char reserved4:6;  /* byte 9 (LSB) */
    unsigned char invert:1;
    unsigned char svalid:1;     /* byte 9 (MSB) */

    unsigned short int ssea; /* Source Storage Element Address (bytes 10-11) */

    char pvti[36];           /* Physical Volume Tag (bytes 12-47) */

    union {
    struct dev_i {
        unsigned char code_set:4;   /* byte 48 (LSB) */
        unsigned char reserved6:4;  /* byte 48 (MSB) */

        unsigned char id_type:4;    /* byte 49 (LSB) */
        unsigned char reserved7:4;  /* byte 49 (MSB) */

        unsigned char reserved8;    /* byte 50 */

        unsigned char id_len;       /* byte 51 */

        char devid[32]; /* 36 minus 4 */
    } dev;  /* Device identifier information */
    char avti[36]; /* Alternate Volume Tag */
     } alt_info;

} __attribute__((packed));

/** library dependant. larger ever seen are 84 bytes length... */
#define READ_STATUS_MAX_ELT_LEN 128

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
 * @param fd     file descriptor to the device.
 * @param cdb    command buffer.
 * @param sdb    sense data buffer.
 * @param dxferp transfer buffer.
 */
int scsi_execute(int fd, enum scsi_direction direction,
                 unsigned char *cdb, int cdb_len,
                 struct scsi_req_sense *sbp, int sb_len,
                 void *dxferp, int dxfer_len);

#endif
