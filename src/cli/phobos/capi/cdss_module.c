/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos Distributed State Service (DSS) bindings for python
 */

/* keep this one first */
#include <Python.h>

/* Project internals */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pho_dss.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>


/* Length of internal exception buffers */
#define ERRBUFF_SIZE    1024


/* Generic error we can reuse within this module to explicitely indicate where
 * errors originate from.
 */
static PyObject *CDSSError;


/**
 * Set error message to our exception.
 */
static void set_error(const char *fmt, ...)
{
    char    errmsg[ERRBUFF_SIZE + 1];
    va_list args;
    int     rc;

    va_start(args, fmt);

    rc = vsnprintf(errmsg, ERRBUFF_SIZE, fmt, args);
    PyErr_SetString(CDSSError, rc < 0 ? "(unknown error)" : errmsg);

    va_end(args);
}

/**
 * wrapper over dss_init()
 * dss_handle = cdss.connection_open("dbname=phobos user=...")
 */
static PyObject *cdss_conn_open(PyObject *self, PyObject *args)
{
    const char          *conn_info;
    struct dss_handle   *dss;
    int                  rc;

    if (!PyArg_ParseTuple(args, "s", &conn_info))
        return NULL;

    rc = dss_init(conn_info, &dss);
    if (rc < 0) {
        set_error("Cannot open connection: %s", strerror(-rc));
        return NULL;
    }

    /* TODO migrate to the capsule interface for python 2.7+ */
    return PyCObject_FromVoidPtr(dss, NULL);
}

/**
 * Wrapper of dss_fini()
 * cdss.connection_close(dss_handle)
 */
static PyObject *cdss_conn_close(PyObject *self, PyObject *args)
{
    PyObject    *handle;
    void        *dss;

    if (!PyArg_ParseTuple(args, "O", &handle))
        return NULL;

    dss = PyCObject_AsVoidPtr(handle);
    if (dss == NULL)
        return NULL;

    dss_fini(dss);
    Py_RETURN_NONE;
}

/**
 * Insert criteria, which can be a long or a string, for us.
 */
static void add_criteria(struct dss_crit *crit, int *crit_idx, PyObject *py_key,
                         PyObject *py_cmp, PyObject *py_val)
{
    enum dss_fields  key = (enum dss_fields)PyInt_AS_LONG(py_key);
    enum dss_cmp     cmp = (enum dss_cmp)PyInt_AS_LONG(py_cmp);

    if (PyInt_Check(py_val))
        dss_crit_add(crit, crit_idx, key, cmp, val_bigint,
                     PyInt_AS_LONG(py_val));
    else
        dss_crit_add(crit, crit_idx, key, cmp, val_str,
                     PyString_AS_STRING(py_val));
}

/**
 * Compare the field against the list of known ones.
 */
static bool is_crit_field_sane(PyObject *obj)
{
    if (!PyInt_Check(obj))
        return false;

    return dss_fields2str((enum dss_fields)PyInt_AS_LONG(obj)) != NULL;
}

/**
 * Compare the opcode against the list of known ones.
 */
static bool is_crit_cmp_sane(PyObject *obj)
{
    if (!PyInt_Check(obj))
        return false;

    return dss_cmp2str((enum dss_cmp)PyInt_AS_LONG(obj)) != NULL;
}

/**
 * Type check only, we can hardly do much more here.
 */
static bool is_crit_value_sane(PyObject *obj)
{
    return PyInt_Check(obj) || PyString_Check(obj);
}

/**
 * A criteria (filter component) must be a tuple of the form:
 * (<FIELD>, <OPCODE>, <VALUE>)
 *
 * Make sure that the filter we're about to parse looks legit.
 */
static bool is_criteria_sane(PyObject *obj)
{
    if (!PyTuple_Check(obj))
        return false;

    if (PyTuple_GET_SIZE(obj) != 3)
        return false;

    if (!is_crit_field_sane(PyTuple_GET_ITEM(obj, 0)))
        return false;

    if (!is_crit_cmp_sane(PyTuple_GET_ITEM(obj, 1)))
        return false;

    if (!is_crit_value_sane(PyTuple_GET_ITEM(obj, 2)))
        return false;

    return true;
}

/**
 * Convert a filter, expressed as a python list of tuples, into an array of
 * dss_crit to pass down to the lower layers.
 */
static int build_crit_list(PyObject *crit_list, struct dss_crit **crit,
                           int *crit_cnt)
{
    struct dss_crit *p_crit;
    int              items_count;
    int              idx;
    int              i;
    int              rc = 0;

    if (crit == NULL || crit_cnt == NULL)
        return -EINVAL;

    *crit = NULL;
    *crit_cnt = 0;

    if (crit_list == Py_None || crit_list == NULL)
        return 0;

    if (!PyList_Check(crit_list))
        return -EINVAL;

    items_count = PyList_Size(crit_list);
    if (items_count <= 0)
        return items_count == 0 ? 0 : -EINVAL;

    p_crit = malloc(items_count * sizeof(struct dss_crit));
    if (p_crit == NULL)
        return -ENOMEM;

    for (i = 0, idx = 0; i < items_count; i++) {
        PyObject *expr = PyList_GET_ITEM(crit_list, i);

        if (!is_criteria_sane(expr)) {
            rc = -EINVAL;
            goto out_free;
        }

        add_criteria(p_crit, &idx,
                     PyTuple_GET_ITEM(expr, 0),
                     PyTuple_GET_ITEM(expr, 1),
                     PyTuple_GET_ITEM(expr, 2));
    }

    assert(idx == items_count);

    *crit = p_crit;
    *crit_cnt = idx;

out_free:
    if (rc)
        free(p_crit);
    return rc;
}

/**
 * Convert a dev_info structure into a python tuple.
 */
static PyObject *device2py_object(const struct dev_info *dev)
{
    return Py_BuildValue("(issssii)",
                         dev->family,
                         dev->model,
                         dev->path,
                         dev->host,
                         dev->serial,
                         dev->changer_idx,
                         dev->adm_status);
}

/**
 * Convert a list of dev_info structures into a python list of the
 * corresponding tuples.
 */
static int build_device_res_list(const struct dev_info *devices, int dev_cnt,
                                  PyObject **dev_list)
{
    int i;

    *dev_list = PyList_New(dev_cnt);
    if (*dev_list == NULL)
        return -ENOMEM;

    for (i = 0; i < dev_cnt; i++)
        PyList_SET_ITEM(*dev_list, i, device2py_object(&devices[i]));

    return 0;
}

/**
 * Convert a media_info structure into a python tuple.
 */
static PyObject *media2py_object(const struct media_info *media)
{
    return Py_BuildValue("(isisii(kkkkiik))",
                         media->id.type,
                         media_id_get(&media->id),
                         media->fs_type,
                         media->model,
                         media->adm_status,
                         media->fs_status,
                         media->stats.nb_obj,
                         media->stats.logc_spc_used,
                         media->stats.phys_spc_used,
                         media->stats.phys_spc_free,
                         media->stats.nb_load,
                         media->stats.nb_errors,
                         media->stats.last_load);
}

/**
 * Convert a list of media_info structures into a python list of the
 * corresponding tuples.
 */
static int build_media_res_list(const struct media_info *media, int mi_cnt,
                                  PyObject **media_list)
{
    int i;

    *media_list = PyList_New(mi_cnt);
    if (*media_list == NULL)
        return -ENOMEM;

    for (i = 0; i < mi_cnt; i++)
        PyList_SET_ITEM(*media_list, i, media2py_object(&media[i]));

    return 0;
}

/**
 * Convert a layout_info structure into a python tuple.
 */
static PyObject *extent2py_object(const struct layout_info *lyt)
{
    PyObject    *lyt_obj;
    PyObject    *ext_list;
    int          i;

    lyt_obj = Py_BuildValue("(sIii[])",
                            lyt->oid,
                            lyt->copy_num,
                            lyt->type,
                            lyt->state);
    if (!lyt_obj)
        return NULL;

    ext_list = PyTuple_GET_ITEM(lyt_obj, 4);
    assert(PyList_Check(ext_list));

    for (i = 0; i < lyt->ext_count; i++) {
        struct extent   *ext = &lyt->extents[i];

        PyList_Append(ext_list,
                      Py_BuildValue("(IKisiis#)",
                                    ext->layout_idx,
                                    ext->size,
                                    ext->media.type,
                                    media_id_get(&ext->media),
                                    ext->fs_type,
                                    ext->addr_type,
                                    ext->address.buff, ext->address.size));
    }

    return lyt_obj;
}

/**
 * Convert a list of layout_info structures into a python list of the
 * corresponding tuples.
 */
static int build_extent_res_list(const struct layout_info *layouts, int lyt_cnt,
                                 PyObject **lyt_list)
{
    int i;

    *lyt_list = PyList_New(lyt_cnt);
    if (*lyt_list == NULL)
        return -ENOMEM;

    for (i = 0; i < lyt_cnt; i++)
        PyList_SET_ITEM(*lyt_list, i, extent2py_object(&layouts[i]));

    return 0;
}

/**
 * Very simple wrapper over dss_device_get().
 *
 * This and the similar functions below take a DSS handle as first and
 * mandatory parameter.
 *
 * It accepts an optional second argument: A list of 3-uples expressing
 * the filter to apply.
 *
 * These functions all return the list of matched entries (possibly empty),
 * as tuples.
 *
 * cdss.device_get(dss_handle,
 *                 [(cdss.DSS_DEV_host, cdss.DSS_CMP_LIKE, '/dev/test'),
 *                  (...),
 *                  (...)])
 */
static PyObject *cdss_device_get(PyObject *self, PyObject *args)
{
    PyObject        *handle;
    PyObject        *crit_list = NULL;
    PyObject        *dev_list = NULL;
    void            *dss;
    struct dss_crit *crit = NULL;
    int              crit_cnt = 0;
    struct dev_info *dev = NULL;
    int              dev_cnt = 0;
    int              rc;

    if (!PyArg_ParseTuple(args, "O|O", &handle, &crit_list))
        return NULL;

    dss = PyCObject_AsVoidPtr(handle);
    if (dss == NULL)
        return NULL;

    rc = build_crit_list(crit_list, &crit, &crit_cnt);
    if (rc < 0) {
        set_error("Invalid criteria list");
        return NULL;
    }

    rc = dss_device_get(dss, crit, crit_cnt, &dev, &dev_cnt);
    if (rc < 0) {
        set_error("Cannot retrieve device(s): %s", strerror(-rc));
        goto out_free;
    }

    rc = build_device_res_list(dev, dev_cnt, &dev_list);
    if (rc < 0) {
        set_error("Cannot build result list: %s", strerror(-rc));
        goto out_free;
    }

out_free:
    free(crit);
    dss_res_free(dev, dev_cnt);
    return dev_list;
}

/**
 * Very simple wrapper over dss_media_get().
 * @see cdss_device_get()
 */
static PyObject *cdss_media_get(PyObject *self, PyObject *args)
{
    PyObject            *handle;
    PyObject            *crit_list = NULL;
    PyObject            *media_list = NULL;
    void                *dss;
    struct dss_crit     *crit = NULL;
    int                  crit_cnt = 0;
    struct media_info   *mi = NULL;
    int                  mi_cnt = 0;
    int                  rc;

    if (!PyArg_ParseTuple(args, "O|O", &handle, &crit_list))
        return NULL;

    dss = PyCObject_AsVoidPtr(handle);
    if (dss == NULL)
        return NULL;

    rc = build_crit_list(crit_list, &crit, &crit_cnt);
    if (rc < 0) {
        set_error("Invalid criteria list");
        return NULL;
    }

    rc = dss_media_get(dss, crit, crit_cnt, &mi, &mi_cnt);
    if (rc < 0) {
        set_error("Cannot retrieve media: %s", strerror(-rc));
        goto out_free;
    }

    rc = build_media_res_list(mi, mi_cnt, &media_list);
    if (rc < 0) {
        set_error("Cannot build result list: %s", strerror(-rc));
        goto out_free;
    }

out_free:
    free(crit);
    dss_res_free(mi, mi_cnt);
    return media_list;
}

/**
 * Very simple wrapper over dss_extent_get().
 * @see cdss_device_get()
 */
static PyObject *cdss_extent_get(PyObject *self, PyObject *args)
{
    PyObject            *handle;
    PyObject            *crit_list = NULL;
    PyObject            *extent_list = NULL;
    void                *dss;
    struct dss_crit     *crit = NULL;
    int                  crit_cnt = 0;
    struct layout_info  *li = NULL;
    int                  li_cnt = 0;
    int                  rc;

    if (!PyArg_ParseTuple(args, "O|O", &handle, &crit_list))
        return NULL;

    dss = PyCObject_AsVoidPtr(handle);
    if (dss == NULL)
        return NULL;

    rc = build_crit_list(crit_list, &crit, &crit_cnt);
    if (rc < 0) {
        set_error("Invalid criteria list");
        return NULL;
    }

    rc = dss_extent_get(dss, crit, crit_cnt, &li, &li_cnt);
    if (rc < 0) {
        set_error("Cannot retrieve extent(s): %s", strerror(-rc));
        goto out_free;
    }

    rc = build_extent_res_list(li, li_cnt, &extent_list);
    if (rc < 0) {
        set_error("Cannot build result list: %s", strerror(-rc));
        goto out_free;
    }

out_free:
    free(crit);
    dss_res_free(li, li_cnt);
    return extent_list;
}

/**
 * Convert raw (integer) dev family values into human readable strings. Handy
 */
static PyObject *cdss_dev_family2str(PyObject *self, PyObject *args)
{
    enum dev_family  family;

    if (!PyArg_ParseTuple(args, "i", &family))
        return NULL;

    return Py_BuildValue("s", dev_family2str(family));
}

/**
 * Convert raw (integer) status values into human readable strings. Handy
 */
static PyObject *cdss_dev_adm_status2str(PyObject *self, PyObject *args)
{
    enum dev_adm_status as;

    if (!PyArg_ParseTuple(args, "i", &as))
        return NULL;

    return Py_BuildValue("s", adm_status2str(as));
}

/**
 * Exposed methods, with little docstring descriptors.
 */
static PyMethodDef CDSSMethods[] = {
    {"connection_open", cdss_conn_open, METH_VARARGS, "Connect to DSS."},
    {"connection_close", cdss_conn_close, METH_VARARGS, "Close connection."},
    {"device_get", cdss_device_get, METH_VARARGS, "Get/list devices from DSS."},
    {"media_get", cdss_media_get, METH_VARARGS, "Get/list media from DSS."},
    {"extent_get", cdss_extent_get, METH_VARARGS, "Get/list extents from DSS."},
    {"device_family2str", cdss_dev_family2str, METH_VARARGS,
     "String representation for device family."},
    {"device_adm_status2str", cdss_dev_adm_status2str, METH_VARARGS,
     "String representation for device administrative status."},
    {NULL, NULL, 0, NULL}
};

/**
 * Custom exception to represent error cases originating from this module.
 */
static void module_exception_register(PyObject *mod)
{
    CDSSError = PyErr_NewException("cdss.GenericError", NULL, NULL);
    Py_INCREF(CDSSError);
    PyModule_AddObject(mod, "GenericError", CDSSError);
}

#define EXPOSE_INT_CONST(_mod, _c)    PyModule_AddIntConstant((_mod), #_c, _c)

/**
 * Expose the required DSS constants to the users.
 */
static void module_num_const_register(PyObject *mod)
{
    /* enum dss_cmp */
    EXPOSE_INT_CONST(mod, DSS_CMP_EQ);
    EXPOSE_INT_CONST(mod, DSS_CMP_NE);
    EXPOSE_INT_CONST(mod, DSS_CMP_GT);
    EXPOSE_INT_CONST(mod, DSS_CMP_GE);
    EXPOSE_INT_CONST(mod, DSS_CMP_LT);
    EXPOSE_INT_CONST(mod, DSS_CMP_LE);
    EXPOSE_INT_CONST(mod, DSS_CMP_LIKE);
    EXPOSE_INT_CONST(mod, DSS_CMP_JSON_CTN);
    EXPOSE_INT_CONST(mod, DSS_CMP_JSON_EXIST);


    /* enum dss_fields */
    EXPOSE_INT_CONST(mod, DSS_OBJ_oid);
    EXPOSE_INT_CONST(mod, DSS_OBJ_user_md);

    EXPOSE_INT_CONST(mod, DSS_EXT_oid);
    EXPOSE_INT_CONST(mod, DSS_EXT_copy_num);
    EXPOSE_INT_CONST(mod, DSS_EXT_state);
    EXPOSE_INT_CONST(mod, DSS_EXT_layout_type);
    EXPOSE_INT_CONST(mod, DSS_EXT_layout_info);
    EXPOSE_INT_CONST(mod, DSS_EXT_info);
    EXPOSE_INT_CONST(mod, DSS_EXT_media_idx);

    EXPOSE_INT_CONST(mod, DSS_MDA_family);
    EXPOSE_INT_CONST(mod, DSS_MDA_model);
    EXPOSE_INT_CONST(mod, DSS_MDA_id);
    EXPOSE_INT_CONST(mod, DSS_MDA_adm_status);
    EXPOSE_INT_CONST(mod, DSS_MDA_fs_status);
    EXPOSE_INT_CONST(mod, DSS_MDA_address_type);
    EXPOSE_INT_CONST(mod, DSS_MDA_fs_type);
    EXPOSE_INT_CONST(mod, DSS_MDA_stats);
    EXPOSE_INT_CONST(mod, DSS_MDA_nb_obj);
    EXPOSE_INT_CONST(mod, DSS_MDA_vol_used);
    EXPOSE_INT_CONST(mod, DSS_MDA_vol_free);

    EXPOSE_INT_CONST(mod, DSS_DEV_serial);
    EXPOSE_INT_CONST(mod, DSS_DEV_family);
    EXPOSE_INT_CONST(mod, DSS_DEV_host);
    EXPOSE_INT_CONST(mod, DSS_DEV_adm_status);
    EXPOSE_INT_CONST(mod, DSS_DEV_model);
    EXPOSE_INT_CONST(mod, DSS_DEV_path);
    EXPOSE_INT_CONST(mod, DSS_DEV_changer_idx);

    /* enum dev_family */
    EXPOSE_INT_CONST(mod, PHO_DEV_DISK);
    EXPOSE_INT_CONST(mod, PHO_DEV_TAPE);
    EXPOSE_INT_CONST(mod, PHO_DEV_DIR);

    /* enum dev_adm_status */
    EXPOSE_INT_CONST(mod, PHO_DEV_ADM_ST_UNLOCKED);
    EXPOSE_INT_CONST(mod, PHO_DEV_ADM_ST_LOCKED);
    EXPOSE_INT_CONST(mod, PHO_DEV_ADM_ST_FAILED);
}


PyMODINIT_FUNC initcdss(void)
{
    PyObject *mod;

    mod = Py_InitModule("cdss", CDSSMethods);
    if (mod == NULL)
        return;

    module_num_const_register(mod);
    module_exception_register(mod);
}
