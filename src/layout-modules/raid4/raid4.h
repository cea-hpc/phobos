#ifndef _PHO_RAID4_H
#define _PHO_RAID4_H

#include "pho_types.h" /* struct layout_info */
#include "raid_common.h"

/**
 * Implementation of raid_ops::write_split
 */
int raid4_write_split(struct pho_encoder *enc, size_t split_size,
                      int target_idx);

int raid4_read_split(struct pho_encoder *dec);

int raid4_get_block_size(struct pho_encoder *enc, size_t *block_size);

void buffer_xor(struct pho_buff *buff1, struct pho_buff *buff2,
                struct pho_buff *xor, size_t count);

#endif
