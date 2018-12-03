/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2017 CEA/DAM.
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
 * \brief  Phobos Data Layout management.
 */
#ifndef _PHO_LAYOUT_H
#define _PHO_LAYOUT_H

#include <stdio.h>
#include <assert.h>
#include <stdbool.h>


/**
 * Operation names for dynamic loading with dlsym()
 * This is the publicly exposed API that layout modules provide.
 *
 * See below for the corresponding prototypes and additional information.
 */
#define PLM_OP_INIT         "pho_layout_mod_register"
#define PLM_OP_FINI         "pho_layout_mod_deregister"

struct layout_composer;
struct layout_module;
struct pho_io_descr;
struct layout_info;
struct dss_handle;

enum layout_action {
    LA_ENCODE = 0,
    LA_DECODE,
};

typedef void (*comp_priv_dtor_t)(struct layout_composer *);

struct layout_composer {
    struct dss_handle   *lc_dss;        /**< Cached DSS handle */
    enum layout_action   lc_action;     /**< Encode / Decode sub-streams */
    GHashTable          *lc_layouts;    /**< Objid::Layout descr mapping */
    void                *lc_private;    /**< Module private data pointer */
    comp_priv_dtor_t     lc_private_dtor;
    /*
     * FIXME: this is used to pass information to lrs_write_prepare in the
     * layout_module_compose method of layout modules. This is only a temporary
     * solution until a refactoring of the store/layout/lrs interactions happens
     * (hopefully soon).
     * -- FM 2018/11/29
     */
    struct tags          lc_tags;       /**< Tags to select a media to write */
};


/**
 * Layout module public interface.
 * See the comments above wrappers for each function below.
 */
struct layout_operations {
    int (*lmo_compose)(struct layout_module *, struct layout_composer *);
    int (*lmo_io_submit)(struct layout_module *, struct layout_composer *,
                         const char *, struct pho_io_descr *);
    int (*lmo_io_commit)(struct layout_module *, struct layout_composer *, int);
};

/**
 * Phobos layout management modules are external libraries, loaded dynamically
 * when requested.
 * It provides functions to instanciate and manipulate specific object layout.
 */
struct layout_module {
    enum layout_action               lm_mode;
    void                            *lm_dl_handle;
    struct module_desc               lm_desc;
    const struct layout_operations  *lm_ops;
    void                            *lm_private;
};

/**
 * Generate a composite layout from subrequests contained in \a comp.
 * Typically, layouts can be grouped together on a single media and/or
 * expanded for redundancy and spread over multiple ones.
 *
 * Get associated LRS resources and update subrequests layout_info accordingly.
 */
static inline int layout_module_compose(struct layout_module *self,
                                        struct layout_composer *comp)
{
    assert(self);
    assert(self->lm_ops);
    assert(self->lm_ops->lmo_compose);
    return self->lm_ops->lmo_compose(self, comp);
}

/**
 * I/O encoding/decoding loop.
 */
static inline int layout_module_io_submit(struct layout_module *self,
                                          struct layout_composer *comp,
                                          const char *objid,
                                          struct pho_io_descr *iod)
{
    assert(self);
    assert(self->lm_ops);
    assert(self->lm_ops->lmo_io_submit);
    return self->lm_ops->lmo_io_submit(self, comp, objid, iod);
}

/**
 * Finalize I/O
 */
static inline int layout_module_io_commit(struct layout_module *self,
                                          struct layout_composer *comp, int rc)
{
    assert(self);
    assert(self->lm_ops);
    assert(self->lm_ops->lmo_io_commit);
    return self->lm_ops->lmo_io_commit(self, comp, rc);
}

/**
 * @defgroup layout_comp Layout Composition API
 * @{
 */

/**
 * Initialize a layout compositor.
 * Takes a reference to the given DSS handle for use by the following functions.
 */
int layout_init(struct dss_handle *dss, struct layout_composer *comp,
                enum layout_action action);

/**
 * Declare a new object to the compositor.
 */
int layout_declare(struct layout_composer *comp, struct layout_info *layout);

/**
 * Compute actual layouts to use for the declared objects and acquire storage
 * resources required to encode/decode them.
 */
int layout_acquire(struct layout_composer *comp);

/**
 * Encode or Decode layout for the given object from compositor.
 */
int layout_io(struct layout_composer *comp, const char *objid,
              struct pho_io_descr *iod);

/**
 * Commit all open transactions.
 */
int layout_commit(struct layout_composer *comp, int errcode);

/**
 * Release all resources associated to this layout compositor.
 */
int layout_fini(struct layout_composer *comp);

/** @} end of layout_comp group */


/**
 * @defgroup pho_layout_mod Public API for layout modules
 * @{
 */

/**
 * Not for direct call.
 * Exposed by layout modules.
 *
 * The function fills the module description (.lm_desc) and operation (.lm_ops)
 * fields for the given layout_action working mode.
 *
 * Global initialization operations can be performed here if need be.
 */
int pho_layout_mod_register(struct layout_module *self, enum layout_action act);

/**
 * Not for direct call.
 * Exposed by layout modules.
 *
 * This function is optional. If provided, it is invoked by the generic layout
 * layer before shutting down. No other function of the module will be invoked
 * afterwards.
 */
int pho_layout_mod_deregister(struct layout_module *self);

/** @} end of pho_layout_mod group */

#endif
