/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos configuration management bindings for python.
 */

/* keep this one first */
#include <Python.h>

/* Project internals */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pho_cfg.h>

#include <errno.h>


static PyObject *options_map;


/**
 * Open and read local configuration file.
 */
static PyObject *ccfg_load_file(PyObject *self, PyObject *args)
{
    const char  *path;
    int          rc;

    if (!PyArg_ParseTuple(args, "s", &path))
        return NULL;

    rc = pho_cfg_init_local(path);
    if (rc < 0) {
        errno = -rc;
        return PyErr_SetFromErrno(PyExc_IOError);
    }

    Py_RETURN_NONE;
}

/**
 * Get value from configuration (or default).
 * Return None if no such value was found.
 */
static PyObject *ccfg_get_val(PyObject *self, PyObject *args)
{
    const char          *res = NULL;
    PyObject            *section_map;
    PyObject            *param_value;
    char                *section;
    char                *param;
    enum pho_cfg_params  num_desc;
    int                  rc;

    if (!PyArg_ParseTuple(args, "ss", &section, &param))
        return NULL;

    section_map = PyDict_GetItemString(options_map, section);
    if (!section_map)
        Py_RETURN_NONE;

    param_value = PyDict_GetItemString(section_map, param);
    if (!param_value)
        Py_RETURN_NONE;

    num_desc = (enum pho_cfg_params)PyInt_AS_LONG(param_value);
    rc = pho_cfg_get(num_desc, &res);
    if (rc < 0)
        Py_RETURN_NONE;

    return Py_BuildValue("s", res);
}

/**
 * Exposed methods, with little docstring descriptors.
 */
static PyMethodDef CConfigMethods[] = {
    {"cfg_load_file", ccfg_load_file, METH_VARARGS, "Load configuration file."},
    {"cfg_get_val", ccfg_get_val, METH_VARARGS, "Get configuration value."},
    {NULL, NULL, 0, NULL}
};


static void options_map_initialize(void)
{
    enum pho_cfg_params i;

    options_map = PyDict_New();

    /* Tricky: first is valid, last is not... */
    for (i = PHO_CFG_FIRST; i < PHO_CFG_LAST; i++) {
        const char  *section_name = pho_cfg_descr[i].section;
        const char  *param_name   = pho_cfg_descr[i].name;
        PyObject    *section;

        /* Skip holes */
        if (!section_name)
            continue;

        section = PyDict_GetItemString(options_map, section_name);
        if (!section) {
            section = PyDict_New();
            PyDict_SetItemString(options_map, section_name, section);
        }

        PyDict_SetItemString(section, param_name, PyInt_FromLong((long)i));
    }
}

PyMODINIT_FUNC initccfg(void)
{
    options_map_initialize();
    Py_InitModule("ccfg", CConfigMethods);
}
