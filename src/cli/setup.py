#!/usr/bin/python

# Copyright CEA/DAM 2015
# Author: Henri Doreau <henri.doreau@cea.fr>
#
# This file is part of the Phobos project

from distutils.core import setup, Extension


# Macros to use for every C extension we build
GLOBAL_MACROS = [('HAVE_CONFIG_H', 1)]

dss_module = Extension('_dss',
                       sources=['phobos/capi/dss_wrap.c'],
                       include_dirs = ['../include',
                                       '/usr/include/glib-2.0',
                                       '/usr/lib64/glib-2.0/include'],
                       libraries = ['phobos_store'],
                       library_dirs = ['../store/.libs'],
                       define_macros = GLOBAL_MACROS)

config_module = Extension('_config',
                          sources=['phobos/capi/config_wrap.c'],
                          include_dirs = ['../include',
                                          '/usr/include/glib-2.0',
                                          '/usr/lib64/glib-2.0/include'],
                          libraries = ['phobos_store'],
                          library_dirs = ['../store/.libs'],
                          define_macros = GLOBAL_MACROS)

clogging_module = Extension('clogging',
                            sources=['phobos/capi/clogging_module.c'],
                            include_dirs = ['../include',
                                            '/usr/include/glib-2.0',
                                            '/usr/lib64/glib-2.0/include'],
                            libraries = ['phobos_store'],
                            library_dirs = ['../store/.libs'],
                            define_macros = GLOBAL_MACROS)

setup(
    name = 'phobos',
    packages = ['phobos', 'phobos.capi'],
    ext_package = 'phobos.capi',
    ext_modules = [dss_module, config_module, clogging_module],
    py_modules = ['config'],
    scripts = ['scripts/phobos'],
    version = '0.0.1',
    description = 'Phobos control scripts and libraries',
    author = 'Henri Doreau',
    author_email = 'henri.doreau@cea.fr')
