#!/usr/bin/python

# Copyright CEA/DAM 2015
# This file is part of the Phobos project

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
