/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos Logging API bindings for python
 */

/* keep this one first */
#include <Python.h>

/* Project internals */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pho_common.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

/**
 * The goal of this module is to provide a unified stream of log for both the
 * python layer and the underlying C libraries.
 *
 * In order to leverage the powerful python standard module `logging' all
 * messages that match the current log level are gathered and delivered to a
 * python function.
 *
 * It should eventually look like:
 * $ phobos fnord doblah
 * 2015-09-23 12:28:41.123456 <INFO> (do_xy:cli.py:667) trying hard from python
 * 2015-09-23 12:28:41.124107 <ERROR> (blah:file.c:211) crap happened in lib
 */


/**
 * Remap standard logging::loglevels for comparison.
 * See logger_set_level() below.
 */
enum py_log_level {
    PY_LOG_FATAL    = 50,
    PY_LOG_CRITICAL = 50,
    PY_LOG_ERROR    = 40,
    PY_LOG_WARNING  = 30,
    PY_LOG_INFO     = 20,
    PY_LOG_DEBUG    = 10,
    PY_LOG_NOTSET   = 0,
    PY_LOG_DEFAULT  = PY_LOG_ERROR
};

/**
 * Callable python object (function...) registered externally that receives the
 * log records emitted from the lower layers.
 */
static PyObject *external_log_callback;


/**
 * Phobos comes with its own log levels. Map python codes to these ones.
 */
static inline enum pho_log_level level_py2pho(enum py_log_level lvl)
{
    switch (lvl) {
    case PY_LOG_CRITICAL:
    case PY_LOG_ERROR:
        return PHO_LOG_ERROR;
    case PY_LOG_WARNING:
        return PHO_LOG_WARN;
    case PY_LOG_INFO:
        return PHO_LOG_INFO;
    case PY_LOG_DEBUG:
        return PHO_LOG_DEBUG;
    case PY_LOG_NOTSET:
        return PHO_LOG_DISABLED;
    default:
        return PHO_LOG_DEFAULT;
    }
}

/**
 * Evil twin of the function above: map phobos log levels to python logging's
 * ones.
 */
static inline enum py_log_level level_pho2py(enum pho_log_level lvl)
{
    switch (lvl) {
    case PHO_LOG_DISABLED:
        return PY_LOG_NOTSET;
    case PHO_LOG_ERROR:
        return PY_LOG_ERROR;
    case PHO_LOG_WARN:
        return PY_LOG_WARNING;
    case PHO_LOG_INFO:
    case PHO_LOG_VERB:
        return PY_LOG_INFO;
    case PHO_LOG_DEBUG:
        return PY_LOG_DEBUG;
    default:
        return PY_LOG_DEFAULT;
    }
}

/**
 * This is the function actually registered to the phobos log layer.
 * Therefore, it receives all emitted log records that matches the current log
 * level. Remap them into a python tuple:
 *   (level, filename, func_name, line, errcode, time, message)
 * and pass it up to the python message handler.
 */
static void internal_log_forwarder(const struct pho_logrec *rec)
{
    PyObject *pyrec;
    PyObject *res;

    if (external_log_callback == NULL)
        return;

    /* Note the '[]' so that we can directly pass it as *args... */
    pyrec = Py_BuildValue("((issiiis))",
                          level_pho2py(rec->plr_level),
                          rec->plr_file,
                          rec->plr_func,
                          rec->plr_line,
                          rec->plr_err,
                          rec->plr_time.tv_sec,
                          rec->plr_msg);

    if (pyrec == NULL)
        return; /* Man, I'm so sorry... */

    res = PyObject_Call(external_log_callback, pyrec, NULL);

    Py_XDECREF(res);
    Py_DECREF(pyrec);
}

/**
 * Register a python callback to handle logs.
 *
 * The actually registered callback is a forwarder that remaps the log records
 * into a python tuple that we deliver to the registered python callable
 * specified here.
 */
static PyObject *logger_set_callback(PyObject *self, PyObject *args)
{
    PyObject *log_cb;

    if (!PyArg_ParseTuple(args, "O", &log_cb))
        return NULL;

    if (!PyCallable_Check(log_cb)) {
        PyErr_SetString(PyExc_TypeError, "argument must be callable");
        return NULL;
    }

    Py_XINCREF(log_cb);
    Py_XDECREF(external_log_callback);

    external_log_callback = log_cb;
    pho_log_callback_set(internal_log_forwarder);

    Py_RETURN_NONE;
}

/**
 * Adjust current log level for the underlying libraries.
 */
static PyObject *logger_set_level(PyObject *self, PyObject *args)
{
    enum py_log_level py_level;

    if (!PyArg_ParseTuple(args, "i", &py_level))
        return NULL;

    pho_log_level_set(level_py2pho(py_level));
    Py_RETURN_NONE;
}


/**
 * Exposed methods, with little docstring descriptors.
 */
static PyMethodDef CLoggingMethods[] = {
    {"set_level", logger_set_level, METH_VARARGS, "Set log level."},
    {"set_callback", logger_set_callback, METH_VARARGS, "Register log cb."},
    {NULL, NULL, 0, NULL}
};


PyMODINIT_FUNC initclogging(void)
{
    PyObject *mod;

    mod = Py_InitModule("clogging", CLoggingMethods);
    if (mod == NULL)
        return;
}
