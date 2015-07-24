#!/usr/bin/python

# Copyright CEA/DAM 2015
# Author: Henri Doreau <henri.doreau@cea.fr>
#
# This file is part of the Phobos project

from distutils.core import setup

setup(
    name = 'phobos',
    packages = ['phobos'],
    scripts = ['scripts/phobos'],
    version = '0.0.1',
    description = 'Phobos control scripts and libraries',
    author = 'Henri Doreau',
    author_email = 'henri.doreau@cea.fr')
