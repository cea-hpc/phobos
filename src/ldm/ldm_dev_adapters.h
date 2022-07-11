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
 * \brief  External declaration of LDM dev adapters.
 */

#ifndef _LDM_DEV_ADAPTERS_H
#define _LDM_DEV_ADAPTERS_H

/*
 * Use external references for now.
 * They can easily be replaced later by dlopen'ed symbols.
 */
extern const struct dev_adapter dev_adapter_scsi_tape;
#ifdef RADOS_ENABLED
extern const struct dev_adapter dev_adapter_rados_pool;
#endif

#endif
