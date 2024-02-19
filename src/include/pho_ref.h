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
/**
 * \brief  Simplement reference counter
 */

#ifndef _PHO_REF_H
#define _PHO_REF_H

struct pho_ref {
    /** Number of references to \p value */
    _Atomic int count;
    /** Pointer to the value that is reference counted */
    void *value;
};

/**
 * Create a new reference count for \p value
 *
 * Initialized with a ref count of 0. Call pho_ref_acquire to get the first
 * reference.
 */
struct pho_ref *pho_ref_init(void *value);

/**
 * Free the reference counter \p ref.
 */
void pho_ref_destroy(struct pho_ref *ref);

/**
 * Acquire a reference on \p ref.
 */
void pho_ref_acquire(struct pho_ref *ref);

/**
 * Release a reference on \p ref.
 */
void pho_ref_release(struct pho_ref *ref);

#endif
