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

"""
High level interface over system-level configuration.
"""

import os
import phobos.capi.cfg as ccfg


def load_config_file(path):
    """Load phobos configuration file."""
    ret = ccfg.pho_cfg_init_local(path)
    if ret != 0:
        ret = abs(ret)
        raise IOError(ret, path, os.strerror(ret))

def get_config_value(scope, key):
    """Retrieve value for a given configuration item."""
    key_name = 'PHO_CFG_%s_%s'  %(scope.upper(), key.lower())
    if not hasattr(ccfg, key_name):
        raise KeyError('No such parameter: %s' % key_name)
    return ccfg.pho_cfg_get(getattr(ccfg, key_name))
