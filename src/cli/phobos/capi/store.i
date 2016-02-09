/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2016 CEA/DAM. All Rights Reserved.
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


static int _attr_dict_build(const char *key, const char *value, void *udata)
{
    PyObject *dict = udata;

    PyDict_SetItem(dict, PyString_FromString(key), PyString_FromString(value));
    return 0;
}

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

%typemap(in, numinputs=0) (pho_completion_cb_t cb) {
    $1 = python_cb_forwarder;
}

%typemap(in) (void *udata) {
    $1 = (void *)$input;
}

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

%typemap(freearg) (const struct pho_xfer_desc *desc, size_t n) {
    free($1);
}

%include <phobos_store.h>
%include <pho_attrs.h>
