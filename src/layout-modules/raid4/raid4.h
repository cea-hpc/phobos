#ifndef _PHO_RAID4_H
#define _PHO_RAID4_H

#include "pho_types.h" /* struct layout_info */
#include "raid_common.h"

/**
 * Implementation of raid_ops::write_split
 */
int raid4_write_split(struct pho_encoder *enc, int input_fd,
                      size_t split_size);

int raid4_read_split(struct pho_encoder *dec, int out_fd);

void buffer_xor(struct pho_buff *buff1, struct pho_buff *buff2,
                struct pho_buff *xor, size_t count);

#endif
