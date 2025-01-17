#
#  All rights reserved (c) 2014-2025 CEA/DAM.
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
Phobos CLI utilities
"""

from argparse import ArgumentTypeError
from shlex import shlex
import os
import sys

import phobos.core.cfg as cfg
from phobos.core.const import (DSS_STATUS_FILTER_ALL, # pylint: disable=no-name-in-module
                               DSS_STATUS_FILTER_COMPLETE,
                               DSS_STATUS_FILTER_INCOMPLETE,
                               DSS_STATUS_FILTER_READABLE,
                               DSS_OBJ_ALIVE, DSS_OBJ_DEPRECATED,
                               DSS_OBJ_DEPRECATED_ONLY)
from phobos.core.ffi import LayoutInfo
from phobos.core.store import PutParams

def check_max_width_is_valid(value):
    """Check that the width 'value' is greater than the one of '...}'"""
    ivalue = int(value)
    if ivalue <= len("...}"):
        raise ArgumentTypeError("%s is an invalid positive int value" % value)
    return ivalue

def check_output_attributes(attrs, out_attrs, logger):
    """Check that out_attrs are valid"""
    attrs.extend(['*', 'all'])
    bad_attrs = set(out_attrs).difference(set(attrs))
    if bad_attrs:
        logger.error("bad output attributes: %s", " ".join(bad_attrs))
        sys.exit(os.EX_USAGE)

def get_params_status(status, logger):
    """Get the status filter"""
    status_number = 0
    if status:
        status_possibilities = {'i': DSS_STATUS_FILTER_INCOMPLETE,
                                'r': DSS_STATUS_FILTER_READABLE,
                                'c': DSS_STATUS_FILTER_COMPLETE}
        for letter in status:
            if letter not in status_possibilities:
                logger.error("status parameter '%s' must be composed "
                             "exclusively of i, r or c", status)
                sys.exit(os.EX_USAGE)
            status_number += status_possibilities[letter]
    else:
        status_number = DSS_STATUS_FILTER_ALL

    return status_number

def handle_sort_option(params, resource, logger, **kwargs):
    """Handle the sort/rsort option"""

    if params.get('sort') and params.get('rsort'):
        logger.error("The option --sort and --rsort cannot be used together")
        sys.exit(os.EX_USAGE)

    for key in ['sort', 'rsort']:
        if params.get(key):
            if isinstance(resource, LayoutInfo):
                attrs_sort = list(resource.get_sort_fields().keys())
            else:
                attrs_sort = list(resource.get_display_dict().keys())

            sort_attr = params.get(key)
            if sort_attr not in attrs_sort:
                if isinstance(resource, LayoutInfo):
                    attrs = list(resource.get_display_dict().keys())
                    if sort_attr in attrs:
                        logger.error("Sorting attributes not supported: %s",
                                     sort_attr)
                        sys.exit(os.EX_USAGE)
                logger.error("Bad sorting attributes: %s", sort_attr)
                sys.exit(os.EX_USAGE)
            kwargs[key] = sort_attr
    return kwargs

OP_NAME_FROM_LETTER = {'P': 'put', 'G': 'get', 'D': 'delete'}
def parse_set_access_flags(flags):
    """From [+|-]PGD flags, return a dict with media operations to set

    Unchanged operation are absent from the returned dict.
    Dict key are among {'put', 'get', 'delete'}.
    Dict values are among {True, False}.
    """
    res = {}
    if not flags:
        return {}

    if flags[0] == '+':
        target = True
        flags = flags[1:]
    elif flags[0] == '-':
        target = False
        flags = flags[1:]
    else:
        # present operations will be enabled
        target = True
        # absent operations will be disabled: preset all to False
        for op_name in OP_NAME_FROM_LETTER.values():
            res[op_name] = False

    for op_letter in flags:
        if op_letter not in OP_NAME_FROM_LETTER:
            raise ArgumentTypeError(f'{op_letter} is not a valid media '
                                    'operation flags')

        res[OP_NAME_FROM_LETTER[op_letter]] = target

    return res

def setaccess_epilog(family):
    """Generic epilog"""
    return """Examples:
    phobos %s set-access GD      # allow get and delete, forbid put
    phobos %s set-access +PG     # allow put, get (other flags are unchanged)
    phobos %s set-access -- -P   # forbid put (other flags are unchanged)
    (Warning: use the '--' separator to use the -PGD flags syntax)
    """ % (family, family, family)

def set_library(obj):
    """Set the library of obj first from its 'library' param, then its family"""
    obj.library = obj.params.get('library')
    if not obj.library:
        obj.library = cfg.get_default_library(obj.family)

def uncase_fstype(choices):
    """Check if an uncase FS type is a valid choice."""
    def find_choice(choice):
        """Return the uppercase choice if valid, the input choice if not."""
        for key, item in enumerate([elt.upper() for elt in choices]):
            if choice.upper() == item:
                return choices[key]
        return choice
    return find_choice

def attr_convert(usr_attr):
    """Convert k/v pairs as expressed by the user into a dictionnary."""
    tkn_iter = shlex(usr_attr, posix=True)
    tkn_iter.whitespace = '=,'
    tkn_iter.whitespace_split = True

    kv_pairs = list(tkn_iter) # [k0, v0, k1, v1...]

    if len(kv_pairs) % 2 != 0:
        print(kv_pairs)
        raise ValueError("Invalid attribute string")

    return dict(zip(kv_pairs[0::2], kv_pairs[1::2]))

def create_put_params(obj, copy):
    """Create the put params from the arguments"""
    lyt_attrs = obj.params.get('layout_params')
    if lyt_attrs is not None:
        lyt_attrs = attr_convert(lyt_attrs)
        obj.logger.debug("Loaded layout params set %r", lyt_attrs)

    put_params = PutParams(profile=obj.params.get('profile'),
                           copy_name=copy,
                           grouping=obj.params.get('grouping'),
                           family=obj.params.get('family'),
                           library=obj.params.get('library'),
                           layout=obj.params.get('layout'),
                           lyt_params=lyt_attrs,
                           no_split=obj.params.get('no_split'),
                           overwrite=obj.params.get('overwrite'),
                           tags=obj.params.get('tags', []))

    return put_params

def get_scope(deprec, deprec_only):
    """Set scope value."""
    if deprec:
        return DSS_OBJ_DEPRECATED
    elif deprec_only:
        return DSS_OBJ_DEPRECATED_ONLY
    else:
        return DSS_OBJ_ALIVE
