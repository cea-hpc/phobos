#ifndef _PHO_RAID4_H
#define _PHO_RAID4_H

#include "pho_types.h" /* struct layout_info */
#include "raid_common.h"

int raid4_read_into_buff(struct pho_data_processor *proc);
int raid4_write_from_buff(struct pho_data_processor *proc);
int raid4_extra_attrs(struct pho_data_processor *proc);

void buffer_xor(struct pho_buff *buff1, struct pho_buff *buff2,
                struct pho_buff *xor, size_t count);

/**
 * Update the parity buff with its XOR with the data buff.
 */
void xor_in_place(const char *data_buff, char *parity_buff, size_t count);

#endif
