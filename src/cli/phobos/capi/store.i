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
 * \brief  Phobos object store interface file for SWIG.
 */

%module store
%include "typemaps.i"

%{
#define SWIG_FILE_WITH_INIT

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <phobos_store.h>
#include <pho_attrs.h>
#include <pho_types.h>
#include <pho_common.h>

/**
 * Callback to fill a python dict from a phobos attribute set.
 * Invoked on each item of the set a PyDict instance as udata.
 */
static int _attr_dict_build(const char *key, const char *value, void *udata)
{
    PyObject *dict = udata;

    PyDict_SetItem(dict, PyString_FromString(key), PyString_FromString(value));
    return 0;
}

/**
 * Internal function to use as xfer completion handler. The user data pointer it
 * takes is a reference to a python function (provided by the CLI) to call in
 * turn with a result tuple in parameter.
 *
 * The python function receives the following arguments:
 * - the object id (always present)
 * - the file path (can be null for GETATTR)
 * - the attribute set, as a flat python dict of strings
 * - the xfer return code (absolute value)
 */
static void python_cb_forwarder(void *uptr, const struct pho_xfer_desc *xd, int ret)
{
    PyObject    *attr = PyDict_New();
    PyObject    *func = uptr;
    PyObject    *args;
    PyObject    *res;

    if (xd->xd_attrs != NULL)
        pho_attrs_foreach(xd->xd_attrs, _attr_dict_build, attr);

    args = Py_BuildValue("(ssOi)", xd->xd_objid, xd->xd_fpath, attr, ret);

    pho_debug("Calling handler for objid:'%s' with rc=%d", xd->xd_objid, ret);
    res = PyEval_CallObject(func, args);

    Py_DECREF(args);
    Py_DECREF(attr);
    Py_XDECREF(res);
}
%}

/**
 * Always use our python_cb_forwarder as a completion callback.
 * The typemap hides the parameter and set it internally.
 */
%typemap(in, numinputs=0) (pho_completion_cb_t cb) {
    $1 = python_cb_forwarder;
}

/**
 * We pass in our python completion handler through the udata pointer.
 * The cast seems useless but is actually needed because SWIG is fascist.
 */
%typemap(in) (void *udata) {
    $1 = (void *)$input;
}

/**
 * Bulk operations (MPUT) work on a linear array of xfer descriptors.
 * Allow the python callers to use a list, which is way more convenient
 * and rebuild an array internally.
 */
%typemap(in) (const struct pho_xfer_desc *desc, size_t n) {
    int i;

    if (!PyList_Check($input)) {
        PyErr_SetString(PyExc_TypeError, "must be a list");
        return NULL;
    }

    $2 = PyList_Size($input);
    if ($2 > 0) {
        $1 = malloc($2 * sizeof(struct pho_xfer_desc));
        for (i = 0; i < $2; i++) {
            struct pho_xfer_desc    *xd;

            SWIG_ConvertPtr(PyList_GET_ITEM($input, i),
                            (void **)&xd,
                            $descriptor(struct pho_xfer_desc *),
                            SWIG_POINTER_EXCEPTION);
            $1[i] = *xd;
        }
    } else {
        $1 = NULL;
        $2 = 0;
    }
}

/**
 * Free the array of struct pho_xfer_desc allocated in the typemap above.
 */
%typemap(freearg) (const struct pho_xfer_desc *desc, size_t n) {
    free($1);
}

%include <phobos_store.h>
%include <pho_attrs.h>
