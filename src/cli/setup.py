#!/usr/bin/python

#
#  All rights reserved (c) 2014-2017 CEA/DAM.
#
#  This file is part of Phobos.
#
#  Phobos is free software: you can redistribute it and/or modify it under
#  the terms of the GNU Lesser General Public License as published by
#  the Free Software Foundation, either version 2.1 of the License, or
#  (at your option) any later version.
#
#  Phobos is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public License
#  along with Phobos. If not, see <http://www.gnu.org/licenses/>.
#

from distutils.core import setup, Extension

"""
Build and installation information for phobos CLI.
"""

# Macros to use for every C extension we build
GLOBAL_MACROS = [('HAVE_CONFIG_H', 1)]

GLOBAL_LIBS = ['phobos_store']
GLOBAL_LIBDIRS = ['../store/.libs']

dss_module = Extension('_dss',
                       sources=['phobos/capi/dss_wrap.c'],
                       include_dirs=['../include',
                                     '/usr/include/glib-2.0',
                                     '/usr/lib64/glib-2.0/include'],
                       libraries=GLOBAL_LIBS,
                       library_dirs=GLOBAL_LIBDIRS,
                       define_macros=GLOBAL_MACROS)

lrs_module = Extension('_lrs',
                       sources=['phobos/capi/lrs_wrap.c'],
                       include_dirs=['../include',
                                     '/usr/include/glib-2.0',
                                     '/usr/lib64/glib-2.0/include'],
                       libraries=GLOBAL_LIBS,
                       library_dirs=GLOBAL_LIBDIRS,
                       define_macros=GLOBAL_MACROS)

ldm_module = Extension('_ldm',
                       sources=['phobos/capi/ldm_wrap.c'],
                       include_dirs=['../include',
                                     '/usr/include/glib-2.0',
                                     '/usr/lib64/glib-2.0/include'],
                       libraries=GLOBAL_LIBS,
                       library_dirs=GLOBAL_LIBDIRS,
                       define_macros=GLOBAL_MACROS)

const_module = Extension('const',
                         sources=['phobos/capi/const_module.c'],
                         include_dirs=['../include',
                                       '/usr/include/glib-2.0',
                                       '/usr/lib64/glib-2.0/include'],
                         libraries=GLOBAL_LIBS,
                         library_dirs=GLOBAL_LIBDIRS,
                         define_macros=GLOBAL_MACROS)

log_module = Extension('log',
                       sources=['phobos/capi/log_module.c'],
                       include_dirs=['../include',
                                     '/usr/include/glib-2.0',
                                     '/usr/lib64/glib-2.0/include'],
                       libraries=GLOBAL_LIBS,
                       library_dirs=GLOBAL_LIBDIRS,
                       define_macros=GLOBAL_MACROS)

store_module = Extension('_store',
                         sources=['phobos/capi/store_wrap.c'],
                         include_dirs=['../include',
                                       '/usr/include/glib-2.0',
                                       '/usr/lib64/glib-2.0/include'],
                         libraries=GLOBAL_LIBS,
                         library_dirs=GLOBAL_LIBDIRS,
                         define_macros=GLOBAL_MACROS)

setup(
    name='phobos',
    packages=['phobos', 'phobos.capi'],
    ext_package='phobos.capi',
    ext_modules=[
        dss_module,
        lrs_module,
        ldm_module,
        const_module,
        log_module,
        store_module
    ],
    scripts=['scripts/phobos'],
    version='0.0.1',
    description='Phobos control scripts and libraries',
    author='Henri Doreau',
    author_email='henri.doreau@cea.fr')
