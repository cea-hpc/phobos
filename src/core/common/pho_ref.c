/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2024 CEA/DAM.
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
#include "pho_ref.h"

#include "pho_common.h"

struct pho_ref *pho_ref_init(void *value)
{
    struct pho_ref *ref;

    ref = xmalloc(sizeof(*ref));
    ref->value = value;
    ref->count = 0;

    return ref;
}

void pho_ref_destroy(struct pho_ref *ref)
{
    free(ref);
}

void pho_ref_acquire(struct pho_ref *ref)
{
    ref->count++;
}

void pho_ref_release(struct pho_ref *ref)
{
    ref->count--;
}

