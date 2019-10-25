/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2019 CEA/DAM.
 *
 *  This file is part of Phobos.
 *
 *  Phobos is free software: you can redistribute it and/or modify it under
 *  the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  Phobos is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with Phobos. If not, see <http://www.gnu.org/licenses/>.
 */
/**
 * \brief  Phobos common communication data structure helper.
 *         'srl' stands for SeRiaLizer.
 */
#ifndef _PHO_SRL_COMMON_H
#define _PHO_SRL_COMMON_H

#include <string.h>
#include "pho_types.h"
#include "pho_proto_common.pb-c.h"

/******************************************************************************/
/* Typedefs *******************************************************************/
/******************************************************************************/

typedef PhoMediumInfo pho_med_info_t;

/******************************************************************************/
/* Convenient functions *******************************************************/
/******************************************************************************/

/**
 * Copy content of \a model in \a dest medium info
 *
 * \param[out] dest  Medium info to set conforming to \provided model
 * \param[in]  model Medium info to copy
 */
static inline void med_info_cpy(pho_med_info_t *dest,
                                const pho_med_info_t *model)
{
    dest->type = model->type;
    dest->id = strdup(model->id);
}

#endif /* _PHO_SRL_COMMON_H */
