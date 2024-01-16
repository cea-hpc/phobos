/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2022 CEA/DAM.
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

typedef PhoResourceId pho_rsc_id_t;

/******************************************************************************/
/* Convenient functions *******************************************************/
/******************************************************************************/

/**
 * Copy content of \a model in \a dest resource ID
 *
 * \param[out] dest  Resource ID to set conforming to \provided model
 * \param[in]  model Resource ID to copy
 */
static inline void rsc_id_cpy(pho_rsc_id_t *dest, const pho_rsc_id_t *model)
{
    dest->family = model->family;
    dest->name = xstrdup(model->name);
}

static inline void med_ids_switch(pho_rsc_id_t **med_ids, size_t first_index,
                                  size_t second_index)
{
    enum rsc_family saved_medium_family;
    char *saved_medium_name;

    saved_medium_family = (enum rsc_family)med_ids[first_index]->family;
    saved_medium_name = med_ids[first_index]->name;
    med_ids[first_index]->family = med_ids[second_index]->family;
    med_ids[first_index]->name = med_ids[second_index]->name;
    med_ids[second_index]->family = saved_medium_family;
    med_ids[second_index]->name = saved_medium_name;
}

#endif /* _PHO_SRL_COMMON_H */
