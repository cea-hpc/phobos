/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos DSS interface file for SWIG.
 */

%module dss
%include "typemaps.i"

%{
#define SWIG_FILE_WITH_INIT

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pho_dss.h>
#include <pho_types.h>
%}

%include <pho_types.h>

/**
 * typemap(in): python -> c
 * allow crit lists to be passed in as Python Lists of crit() objects.
 */
%typemap(in) (struct dss_crit *crit, int crit_cnt) {
    int i;
    if (!PyList_Check($input)) {
        PyErr_SetString(PyExc_TypeError, "must be a list");
        return NULL;
    }
    $2 = PyList_Size($input);
    if ($2 > 0) {
        $1 = malloc($2 * sizeof(struct dss_crit));
        for (i = 0; i < $2; i++) {
            struct dss_crit *c;

            SWIG_ConvertPtr(PyList_GET_ITEM($input, i),
                            (void **)&c,
                            $descriptor(struct dss_crit *),
                            SWIG_POINTER_EXCEPTION);
            $1[i] = *c;
        }
    } else {
        $1 = NULL;
        $2 = 0;
    }
}
/**
 * typemap(freearg): free resources allocated in the corresponding typemap(in)
 */
%typemap(freearg) (struct dss_crit *crit, int crit_cnt) {
    free($1);
}


/* --- DEVICE GET --- */
/**
 * typemap(in): python -> c
 * Hide the output parameter which is used by the C function. Use a local
 * variable instead.
 */
%typemap(in, numinputs=0) (struct dev_info **dev_ls, int *dev_cnt) {
    struct dev_info *temp = NULL;
    int sz = 0;
    $1 = &temp;
    $2 = &sz;
}
/**
 * typemap(argout): c -> python
 * The output parameter was hidden, return values as a python list.
 */
%typemap(argout) (struct dev_info **dev_ls, int *dev_cnt) {
    struct dev_info *itr = *$1;
    PyObject *out_list = PyList_New(*$2);
    int i;
    for (i = 0; i < *$2; i++) {
        PyObject *elt = SWIG_NewPointerObj(SWIG_as_voidptr(&itr[i]),
                                           $descriptor(struct dev_info *), 0);
        PyList_SetItem(out_list, i, elt);
    }
    $result = out_list;
}


/* --- MEDIA GET --- */
/**
 * typemap(in): python -> c
 * Hide the output parameter which is used by the C function. Use a local
 * variable instead.
 */
%typemap(in, numinputs=0) (struct media_info **med_ls, int *med_cnt) {
    struct media_info *temp = NULL;
    int sz = 0;
    $1 = &temp;
    $2 = &sz;
}
/**
 * typemap(argout): c -> python
 * The output parameter was hidden, return values as a python list.
 */
%typemap(argout) (struct media_info **med_ls, int *med_cnt) {
    struct media_info *itr = *$1;
    PyObject *out_list = PyList_New(*$2);
    int i;
    for (i = 0; i < *$2; i++) {
        PyObject *elt = SWIG_NewPointerObj(SWIG_as_voidptr(&itr[i]),
                                           $descriptor(struct media_info *), 0);
        PyList_SetItem(out_list, i, elt);
    }
    $result = out_list;
}


/* --- EXTENT GET --- */
/**
 * typemap(in): python -> c
 * Hide the output parameter which is used by the C function. Use a local
 * variable instead.
 */
%typemap(in, numinputs=0) (struct layout_info **lyt_ls, int *lyt_cnt) {
    struct layout_info *temp = NULL;
    int sz = 0;
    $1 = &temp;
    $2 = &sz;
}
/**
 * typemap(argout): c -> python
 * The output parameter was hidden, return values as a python list.
 */
%typemap(argout) (struct layout_info **lyt_ls, int *lyt_cnt) {
    struct layout_info *itr = *$1;
    PyObject *out_list = PyList_New(*$2);
    int i;
    for (i = 0; i < *$2; i++) {
        PyObject *elt = SWIG_NewPointerObj(SWIG_as_voidptr(&itr[i]),
                                           $descriptor(struct layout_info *), 0);
        PyList_SetItem(out_list, i, elt);
    }
    $result = out_list;
}


/* --- OBJECT GET --- */
/**
 * typemap(in): python -> c
 * Hide the output parameter which is used by the C function. Use a local
 * variable instead.
 */
%typemap(in, numinputs=0) (struct object_info **obj_ls, int *obj_cnt) {
    struct layout_info *temp = NULL;
    int sz = 0;
    $1 = &temp;
    $2 = &sz;
}
/**
 * typemap(argout): c -> python
 * The output parameter was hidden, return values as a python list.
 */
%typemap(argout) (struct object_info **obj_ls, int *obj_cnt) {
    struct object_info *itr = *$1;
    PyObject *out_list = PyList_New(*$2);
    int i;
    for (i = 0; i < *$2; i++) {
        PyObject *elt = SWIG_NewPointerObj(SWIG_as_voidptr(&itr[i]),
                                           $descriptor(struct object_info *), 0);
        PyList_SetItem(out_list, i, elt);
    }
    $result = out_list;
}


/* --- DEVICE SET --- */
/**
 * typemap(in): python -> c
 * allow device lists to be passed in as Python Lists of dev_info() objects.
 */
%typemap(in) (struct dev_info *dev_ls, int dev_cnt) {
    int i;
    if (!PyList_Check($input)) {
        PyErr_SetString(PyExc_TypeError, "must be a list");
        return NULL;
    }
    $2 = PyList_Size($input);
    if ($2 > 0) {
        $1 = malloc($2 * sizeof(struct dev_info));
        for (i = 0; i < $2; i++) {
            struct dev_info *t;

            SWIG_ConvertPtr(PyList_GET_ITEM($input, i),
                            (void **)&t,
                            $descriptor(struct dev_info *),
                            SWIG_POINTER_EXCEPTION);
            $1[i] = *t;
        }
    } else {
        $1 = NULL;
        $2 = 0;
    }
}
/**
 * typemap(freearg): free resources allocated in the corresponding typemap(in)
 */
%typemap(freearg) (struct dev_info *dev_ls, int dev_cnt) {
    free($1);
}


/* --- MEDIA SET --- */
/**
 * typemap(in): python -> c
 * allow device lists to be passed in as Python Lists of dev_info() objects.
 */
%typemap(in) (struct media_info *med_ls, int med_cnt) {
    int i;
    if (!PyList_Check($input)) {
        PyErr_SetString(PyExc_TypeError, "must be a list");
        return NULL;
    }
    $2 = PyList_Size($input);
    if ($2 > 0) {
        $1 = malloc($2 * sizeof(struct media_info));
        for (i = 0; i < $2; i++) {
            struct media_info *t;

            SWIG_ConvertPtr(PyList_GET_ITEM($input, i),
                            (void **)&t,
                            $descriptor(struct media_info *),
                            SWIG_POINTER_EXCEPTION);
            $1[i] = *t;
        }
    } else {
        $1 = NULL;
        $2 = 0;
    }
}
/**
 * typemap(freearg): free resources allocated in the corresponding typemap(in)
 */
%typemap(freearg) (struct media_info *med_ls, int med_cnt) {
    free($1);
}


/* --- EXTENT SET --- */
/**
 * typemap(in): python -> c
 * allow device lists to be passed in as Python Lists of dev_info() objects.
 */
%typemap(in) (struct layout_info *lyt_ls, int lyt_cnt) {
    int i;
    if (!PyList_Check($input)) {
        PyErr_SetString(PyExc_TypeError, "must be a list");
        return NULL;
    }
    $2 = PyList_Size($input);
    if ($2 > 0) {
        $1 = malloc($2 * sizeof(struct layout_info));
        for (i = 0; i < $2; i++) {
            struct layout_info *t;

            SWIG_ConvertPtr(PyList_GET_ITEM($input, i),
                            (void **)&t,
                            $descriptor(struct layout_info *),
                            SWIG_POINTER_EXCEPTION);
            $1[i] = *t;
        }
    } else {
        $1 = NULL;
        $2 = 0;
    }
}
/**
 * typemap(freearg): free resources allocated in the corresponding typemap(in)
 */
%typemap(freearg) (struct layout_info *lyt_ls, int lyt_cnt) {
    free($1);
}


/* --- OBJECT SET --- */
/**
 * typemap(in): python -> c
 * allow device lists to be passed in as Python Lists of dev_info() objects.
 */
%typemap(in) (struct object_info *obj_ls, int obj_cnt) {
    int i;
    if (!PyList_Check($input)) {
        PyErr_SetString(PyExc_TypeError, "must be a list");
        return NULL;
    }
    $2 = PyList_Size($input);
    if ($2 > 0) {
        $1 = malloc($2 * sizeof(struct object_info));
        for (i = 0; i < $2; i++) {
            struct object_info *t;

            SWIG_ConvertPtr(PyList_GET_ITEM($input, i),
                            (void **)&t,
                            $descriptor(struct object_info *),
                            SWIG_POINTER_EXCEPTION);
            $1[i] = *t;
        }
    } else {
        $1 = NULL;
        $2 = 0;
    }
}
/**
 * typemap(freearg): free resources allocated in the corresponding typemap(in)
 */
%typemap(freearg) (struct object_info *obj_ls, int obj_cnt) {
    free($1);
}


%include <pho_dss.h>
