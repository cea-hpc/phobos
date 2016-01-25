#!/usr/bin/python

# Copyright CEA/DAM 2015
# This file is part of the Phobos project

from distutils.core import setup, Extension

"""
Build and installation information for phobos CLI.
"""

# Macros to use for every C extension we build
GLOBAL_MACROS = [('HAVE_CONFIG_H', 1)]

dss_module = Extension('_dss',
                       sources=['phobos/capi/dss_wrap.c'],
                       include_dirs=['../include',
                                     '/usr/include/glib-2.0',
                                     '/usr/lib64/glib-2.0/include'],
                       libraries=['phobos_store'],
                       library_dirs=['../store/.libs'],
                       define_macros=GLOBAL_MACROS)

lrs_module = Extension('_lrs',
                       sources=['phobos/capi/lrs_wrap.c'],
                       include_dirs=['../include',
                                     '/usr/include/glib-2.0',
                                     '/usr/lib64/glib-2.0/include'],
                       libraries=['phobos_store'],
                       library_dirs=['../store/.libs'],
                       define_macros=GLOBAL_MACROS)

ldm_module = Extension('_ldm',
                       sources=['phobos/capi/ldm_wrap.c'],
                       include_dirs=['../include',
                                     '/usr/include/glib-2.0',
                                     '/usr/lib64/glib-2.0/include'],
                       libraries=['phobos_store'],
                       library_dirs=['../store/.libs'],
                       define_macros=GLOBAL_MACROS)

cfg_module = Extension('_cfg',
                       sources=['phobos/capi/cfg_wrap.c'],
                       include_dirs=['../include',
                                     '/usr/include/glib-2.0',
                                     '/usr/lib64/glib-2.0/include'],
                       libraries=['phobos_store'],
                       library_dirs=['../store/.libs'],
                       define_macros=GLOBAL_MACROS)

log_module = Extension('log',
                       sources=['phobos/capi/log_module.c'],
                       include_dirs=['../include',
                                     '/usr/include/glib-2.0',
                                     '/usr/lib64/glib-2.0/include'],
                       libraries=['phobos_store'],
                       library_dirs=['../store/.libs'],
                       define_macros=GLOBAL_MACROS)

store_module = Extension('_store',
                         sources=['phobos/capi/store_wrap.c'],
                         include_dirs=['../include',
                                       '/usr/include/glib-2.0',
                                       '/usr/lib64/glib-2.0/include'],
                         libraries=['phobos_store'],
                         library_dirs=['../store/.libs'],
                         define_macros=GLOBAL_MACROS)

setup(
    name='phobos',
    packages=['phobos', 'phobos.capi'],
    ext_package='phobos.capi',
    ext_modules=[
        dss_module,
        lrs_module,
        ldm_module,
        cfg_module,
        log_module,
        store_module
    ],
    py_modules=['cfg'],
    scripts=['scripts/phobos'],
    version='0.0.1',
    description='Phobos control scripts and libraries',
    author='Henri Doreau',
    author_email='henri.doreau@cea.fr')
