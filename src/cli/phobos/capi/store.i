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


static void _xfer_report_cb(void *uptr, const struct pho_xfer_desc *xd, int ret)
{
    PyObject *results = uptr;

    PyDict_SetItemString(results, xd->xd_objid, PyInt_FromLong((long)ret));
}
%}


%typemap(in, numinputs=0) (pho_completion_cb_t cb, void *udata) {
    $1 = _xfer_report_cb;
    $2 = PyDict_New();
}

%typemap(freearg) (pho_completion_cb_t cb, void *udata) {
    Py_CLEAR($2);
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

%typemap(argout) (const struct pho_xfer_desc *desc, size_t n,
                  pho_completion_cb_t cb, void *udata) {
    PyObject *out_list = PyList_New($2);
    int i;

    for (i = 0; i < $2; i++) {
        const char  *objid = $1[i].xd_objid;
        PyObject    *res = PyDict_GetItemString($4, objid);

        if (!PyInt_Check(res)) {
            pho_warn("Missing return value for '%s', assuming EIO", objid);
            res = PyInt_FromLong(-EIO);
        }

        PyList_SetItem(out_list, i, res);
    }

    $result = out_list;
}

%include <phobos_store.h>
%include <pho_attrs.h>
