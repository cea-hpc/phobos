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
 * \brief  Phobos constants exposed to the outside world
 */

/* keep this one first */
#include <Python.h>

/* Project internals */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pho_common.h>
#include <pho_dss.h>
#include <pho_type_utils.h>
#include <pho_types.h>
#include <phobos_store.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

/**
 * The goal of this module is to expose phobos constants and inline utility
 * functions to the python CLI without having to keep them synchronised.
 *
 * Therefore, we include the necessary headers and re-expose the desired
 * constants and functions.
 */


static PyObject *py_extent_state2str(PyObject *self, PyObject *args)
{
    enum extent_state state;
    const char *str_repr;

    if (!PyArg_ParseTuple(args, "i", &state))
        return NULL;

    str_repr = extent_state2str(state);

    return Py_BuildValue("s", str_repr);
}

static PyObject *py_rsc_family2str(PyObject *self, PyObject *args)
{
    enum rsc_family family;
    const char *str_repr;

    if (!PyArg_ParseTuple(args, "i", &family))
        return NULL;

    str_repr = rsc_family2str(family);

    return Py_BuildValue("s", str_repr);
}

static PyObject *ValueError;

static PyObject *py_str2rsc_family(PyObject *self, PyObject *args)
{
    enum rsc_family family;
    const char *str_repr;

    if (!PyArg_ParseTuple(args, "s", &str_repr)) {
        PyErr_SetString(ValueError, "Unrecognized family");
        return Py_BuildValue("i", PHO_RSC_INVAL);
    }

    family = str2rsc_family(str_repr);

    return Py_BuildValue("i", family);
}

static PyObject *py_rsc_adm_status2str(PyObject *self, PyObject *args)
{
    enum rsc_adm_status status;
    const char *str_repr;

    if (!PyArg_ParseTuple(args, "i", &status))
        return NULL;

    str_repr = rsc_adm_status2str(status);

    return Py_BuildValue("s", str_repr);
}

static PyObject *py_fs_status2str(PyObject *self, PyObject *args)
{
    enum fs_status status;
    const char *str_repr;

    if (!PyArg_ParseTuple(args, "i", &status))
        return NULL;

    str_repr = fs_status2str(status);

    return Py_BuildValue("s", str_repr);
}

static PyObject *py_fs_type2str(PyObject *self, PyObject *args)
{
    enum fs_type type;
    const char *str_repr;

    if (!PyArg_ParseTuple(args, "i", &type))
        return NULL;

    str_repr = fs_type2str(type);

    return Py_BuildValue("s", str_repr);
}

static PyObject *py_str2fs_type(PyObject *self, PyObject *args)
{
    const char *str_fstype;
    enum fs_type fs;

    if (!PyArg_ParseTuple(args, "s", &str_fstype))
        return NULL;

    fs = str2fs_type(str_fstype);

    return Py_BuildValue("i", fs);
}

static PyObject *py_str2dss_type(PyObject *self, PyObject *args)
{
    enum dss_type type;
    const char *str_repr;

    if (!PyArg_ParseTuple(args, "s", &str_repr)) {
        PyErr_SetString(ValueError, "Unrecognized type");
        return Py_BuildValue("i", DSS_INVAL);
    }

    type = str2dss_type(str_repr);

    return Py_BuildValue("i", type);
}

/**
 * Exposed methods, with little docstring descriptors.
 */
static PyMethodDef ConstMethods[] = {
    {"extent_state2str", py_extent_state2str, METH_VARARGS,
     "printable extent state name."},
    {"rsc_family2str", py_rsc_family2str, METH_VARARGS,
     "printable dev family name."},
    {"str2rsc_family", py_str2rsc_family, METH_VARARGS,
     "family enum value from name."},
    {"rsc_adm_status2str", py_rsc_adm_status2str, METH_VARARGS,
     "printable fs status."},
    {"fs_status2str", py_fs_status2str, METH_VARARGS,
     "printable fs status."},
    {"fs_type2str", py_fs_type2str, METH_VARARGS,
     "printable fs type."},
    {"str2fs_type", py_str2fs_type, METH_VARARGS,
     "fs type enum value from name."},
    {"str2dss_type", py_str2dss_type, METH_VARARGS,
     "dss type enum value from name."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef cModPyConst = {
    PyModuleDef_HEAD_INIT,
    "const",
    "",
    -1,
    ConstMethods
};

PyMODINIT_FUNC PyInit_const(void)
{
    PyObject *mod;

    mod = PyModule_Create(&cModPyConst);
    if (mod == NULL)
        return NULL;

    /* Misc. constants */
    PyModule_AddIntMacro(mod, PHO_URI_MAX);

    PyModule_AddIntMacro(mod, PHO_LABEL_MAX_LEN);
    PyModule_AddIntMacro(mod, PHO_LAYOUT_TAG_MAX);
    PyModule_AddIntMacro(mod, PHO_TIMEVAL_MAX_LEN);

    /* enum extent_state */
    PyModule_AddIntMacro(mod, PHO_EXT_ST_INVAL);
    PyModule_AddIntMacro(mod, PHO_EXT_ST_PENDING);
    PyModule_AddIntMacro(mod, PHO_EXT_ST_SYNC);
    PyModule_AddIntMacro(mod, PHO_EXT_ST_ORPHAN);
    PyModule_AddIntMacro(mod, PHO_EXT_ST_LAST);

    /* enum rsc_family */
    PyModule_AddIntMacro(mod, PHO_RSC_NONE);
    PyModule_AddIntMacro(mod, PHO_RSC_INVAL);
    PyModule_AddIntMacro(mod, PHO_RSC_DISK);
    PyModule_AddIntMacro(mod, PHO_RSC_TAPE);
    PyModule_AddIntMacro(mod, PHO_RSC_DIR);
    PyModule_AddIntMacro(mod, PHO_RSC_RADOS_POOL);
    PyModule_AddIntMacro(mod, PHO_RSC_LAST);

    /* enum rsc_adm_status */
    PyModule_AddIntMacro(mod, PHO_RSC_ADM_ST_INVAL);
    PyModule_AddIntMacro(mod, PHO_RSC_ADM_ST_LOCKED);
    PyModule_AddIntMacro(mod, PHO_RSC_ADM_ST_UNLOCKED);
    PyModule_AddIntMacro(mod, PHO_RSC_ADM_ST_FAILED);
    PyModule_AddIntMacro(mod, PHO_RSC_ADM_ST_LAST);

    /* enum lib_type */
    PyModule_AddIntMacro(mod, PHO_LIB_INVAL);
    PyModule_AddIntMacro(mod, PHO_LIB_DUMMY);
    PyModule_AddIntMacro(mod, PHO_LIB_SCSI);
    PyModule_AddIntMacro(mod, PHO_LIB_LAST);

    /* enum fs_type */
    PyModule_AddIntMacro(mod, PHO_FS_INVAL);
    PyModule_AddIntMacro(mod, PHO_FS_POSIX);
    PyModule_AddIntMacro(mod, PHO_FS_LTFS);
    PyModule_AddIntMacro(mod, PHO_FS_RADOS);
    PyModule_AddIntMacro(mod, PHO_FS_LAST);

    /* enum address_type */
    PyModule_AddIntMacro(mod, PHO_ADDR_INVAL);
    PyModule_AddIntMacro(mod, PHO_ADDR_PATH);
    PyModule_AddIntMacro(mod, PHO_ADDR_HASH1);
    PyModule_AddIntMacro(mod, PHO_ADDR_OPAQUE);
    PyModule_AddIntMacro(mod, PHO_ADDR_LAST);

    /* enum pho_log_level */
    PyModule_AddIntMacro(mod, PHO_LOG_DISABLED);
    PyModule_AddIntMacro(mod, PHO_LOG_ERROR);
    PyModule_AddIntMacro(mod, PHO_LOG_WARN);
    PyModule_AddIntMacro(mod, PHO_LOG_INFO);
    PyModule_AddIntMacro(mod, PHO_LOG_VERB);
    PyModule_AddIntMacro(mod, PHO_LOG_DEBUG);
    PyModule_AddIntMacro(mod, PHO_LOG_DEFAULT);

    /* enum dss_set_action */
    PyModule_AddIntMacro(mod, DSS_SET_INVAL);
    PyModule_AddIntMacro(mod, DSS_SET_INSERT);
    PyModule_AddIntMacro(mod, DSS_SET_UPDATE);
    PyModule_AddIntMacro(mod, DSS_SET_DELETE);
    PyModule_AddIntMacro(mod, DSS_SET_LAST);

    /* enum dss_type */
    PyModule_AddIntMacro(mod, DSS_NONE);
    PyModule_AddIntMacro(mod, DSS_INVAL);
    PyModule_AddIntMacro(mod, DSS_OBJECT);
    PyModule_AddIntMacro(mod, DSS_DEPREC);
    PyModule_AddIntMacro(mod, DSS_LAYOUT);
    PyModule_AddIntMacro(mod, DSS_DEVICE);
    PyModule_AddIntMacro(mod, DSS_MEDIA);
    PyModule_AddIntMacro(mod, DSS_MEDIA_UPDATE_LOCK);
    PyModule_AddIntMacro(mod, DSS_LAST);

    /* Media update bit fields */
    PyModule_AddIntMacro(mod, ADM_STATUS);
    PyModule_AddIntMacro(mod, TAGS);
    PyModule_AddIntMacro(mod, PUT_ACCESS);
    PyModule_AddIntMacro(mod, GET_ACCESS);
    PyModule_AddIntMacro(mod, DELETE_ACCESS);

    /* enum pho_xfer_flags */
    PyModule_AddIntMacro(mod, PHO_XFER_OBJ_REPLACE);
    PyModule_AddIntMacro(mod, PHO_XFER_OBJ_BEST_HOST);

    /* enum pho_xfer_op */
    PyModule_AddIntMacro(mod, PHO_XFER_OP_GET);
    PyModule_AddIntMacro(mod, PHO_XFER_OP_GETMD);
    PyModule_AddIntMacro(mod, PHO_XFER_OP_PUT);

    return mod;
}
