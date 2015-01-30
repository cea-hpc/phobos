/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Handling of layout and extent structures.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_extents.h"
#include <errno.h>

/* tags:
 * - single contiguous part: no tag
 * - list of contiguous parts:
 *      p<k> (eg. p2 = part 2). Need to store at least
             the total nbr of parts in tape MD.
 * - stripes:
 *      s<k> (eg. s2 = stripe 2). Need to store stripe count and stripe size
        in tape MD.
 * - raid:
 *      r<k> (eg. r2 = raid element 2)  need to store raid info in tape MD.
 * - mirrors: need tags to identify multiple copies?
*/

int layout2tag(const struct layout_descr *layout,
               unsigned int layout_idx, char *tag)
{
    switch (layout->type) {
    case PHO_LYT_SIMPLE:
        tag[0] = '\0'; /* no tag */
        return 0;
    default:
        /* invalid / not implemented */
        return -EINVAL;
    }
}
