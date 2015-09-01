#!/usr/bin/python

# Copyright CEA/DAM 2015
# Author: Henri Doreau <henri.doreau@cea.fr>
#
# This file is part of the Phobos project

"""
High level, object-oriented interface over DSS. This is the module to use
to interact with phobos DSS layer, as it provides a safe, expressive and
clean API to access it.
"""

import cdss

from collections import namedtuple

# Valid filter suffix and associated operators.
# No suffix means DSS_CMP_EQ.
FILTER_OPERATORS = (
    ('__not', cdss.DSS_CMP_NE),
    ('__gt', cdss.DSS_CMP_GT),
    ('__ge', cdss.DSS_CMP_GE),
    ('__lt', cdss.DSS_CMP_LT),
    ('__le', cdss.DSS_CMP_LE),
    ('__like', cdss.DSS_CMP_LIKE),
    ('__jcontain', cdss.DSS_CMP_JSON_CTN),
    ('__jexist', cdss.DSS_CMP_JSON_EXIST)
)

OBJECT_PREFIXES = {
    'device': 'DSS_DEV',
    'extent': 'DSS_EXT',
    'media':  'DSS_MDA',
}


def key_convert(obj_type, key):
    """Split key, return actual name and associated DSS_CMP_* operator."""
    kname, comp = key, cdss.DSS_CMP_EQ # default
    kname_prefx = OBJECT_PREFIXES[obj_type] # KeyError on unsupported obj_type
    for sufx, comp_enum in FILTER_OPERATORS:
        if key.endswith(sufx):
            kname, comp = key[:len(sufx):], comp_enum
            break
    return getattr(cdss, '%s_%s' % (kname_prefx, kname)), comp


def filter_convert(obj_type, **kwargs):
    """Convert a k/v filter into a CDSS-compatible list of criteria."""
    filt = []
    for k, v in kwargs.iteritems():
        k, comp = key_convert(obj_type, k)
        filt.append((k, comp, v))
    return filt


# Synonym to hide cdss to upper layers.
GenericError = cdss.GenericError


# DSS Device: see struct dev_info in pho_types.h
Device = namedtuple('Device', ['family', 'model', 'path', 'host', 'serial',
                               'changer_idx', 'adm_status'])

def mkdevice(val):
    """Return a new device object from raw data."""
    newvals = (
        cdss.device_family2str(val[0]),
        val[1], val[2], val[3], val[4], val[5],
        cdss.device_adm_status2str(val[6])
    )
    return Device._make(newvals)


class Client(object):
    """Main class: issue requests to the DSS and format replies."""
    def __init__(self, **kwargs):
        """Initialize a new DSS context."""
        super(Client, self).__init__(**kwargs)
        self.handle = None

    def connect(self, **kwargs):
        """
        Establish a fresh connection or renew a stalled one if needed.

        A special '_connect' keyword is supported, that overrides everything
        and whose value is used as is to initialize connection. If not present,
        a connection string is crafted: 'k0=v0 k1=v1...'
        """
        if self.handle is not None:
            self.disconnect()

        # Build a string of the type 'dbname=phobos user=blah password=foobar'
        conn_info = kwargs.get('_connect')
        if conn_info is None:
            conn_info = ' '.join(['%s=%s' % (k, v) for k, v in kwargs.items()])
        self.handle = cdss.connection_open(conn_info)

    def disconnect(self):
        """Disconnect from DSS and reset handle."""
        if self.handle is not None:
            cdss.connection_close(self.handle)
            self.handle = None

    def device_get(self, **kwargs):
        """Retrieve device objects from DSS."""
        # Remap fields that are expressed as strings externally and enums
        # internally.
        dev_filter_remap = {
            'family': {
                'disk': cdss.PHO_DEV_DISK,
                'tape': cdss.PHO_DEV_TAPE,
                'dir': cdss.PHO_DEV_DIR,
            },
            'adm_status': {
                'unlocked': cdss.PHO_DEV_ADM_ST_UNLOCKED,
                'locked': cdss.PHO_DEV_ADM_ST_LOCKED,
                'failed': cdss.PHO_DEV_ADM_ST_FAILED,
            }
        }
        for k, v in kwargs.iteritems():
            repl_map = dev_filter_remap.get(k)
            if repl_map is not None:
                try:
                    kwargs[k] = repl_map[v]
                except KeyError:
                    raise GenericError("Invalid filter value '%s'" % v)

        return [mkdevice(x) for x in self._get('device', **kwargs)]

    def extent_get(self, **kwargs):
        """Retrieve extent objects from DSS."""
        raise NotImplementedError("Not available in this version")

    def media_get(self, **kwargs):
        """Retrieve media objects from DSS."""
        raise NotImplementedError("Not available in this version")

    def _get(self, obj_type, **kwargs):
        """Generic code to retrieve objects from DSS."""
        if self.handle is None:
            raise DSSBaseException('Connection not established')

        getter = '%s_get' % obj_type
        if not hasattr(cdss, getter):
            raise DSSBaseException("Unknown item type: '%s'" % obj_type)

        filt = filter_convert(obj_type, **kwargs)
        return getattr(cdss, getter)(self.handle, filt)
