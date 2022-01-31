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
 * \brief  Glue functions to transform C types into python types with proper
 * memory management
 */

/* keep this one first */
#include <Python.h>

/* External dependencies */
#include <jansson.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * Dumps a json_t to a python string and decref the json_t.
 * @param   json    pointer to the json_t to dump as a python int
 * @return          a python str
 */
static PyObject *py_jansson_dumps(PyObject *self, PyObject *args)
{
    PyObject            *py_json_str;
    unsigned long long   json_addr;
    json_t              *json;
    char                *json_str;

    if (!PyArg_ParseTuple(args, "K:jansson_dumps", &json_addr))
        return NULL;

    json = (json_t *)json_addr;
    json_str = json_dumps(json, JSON_COMPACT);
    json_decref(json);
    if (json_str == NULL)
        return NULL;

    py_json_str = Py_BuildValue("s", json_str);
    free(json_str);

    return py_json_str;
}

static PyMethodDef GlueMethods[] = {
    {"jansson_dumps", py_jansson_dumps, METH_VARARGS,
     "Dump a jansson json_t (pointer as python int) to a python string and "
     "then decref the json_t."},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef cModPyGlue = {
    PyModuleDef_HEAD_INIT,
    "glue",
    "",
    -1,
    GlueMethods
};

PyMODINIT_FUNC PyInit_glue(void)
{
    return PyModule_Create(&cModPyGlue);
}
