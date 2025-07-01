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
Phobos CLI arguments utilities
"""

from phobos.cli.common.utils import str_to_timestamp
from phobos.core.const import rsc_family2str # pylint: disable=no-name-in-module
from phobos.core.ffi import ResourceFamily

def add_put_arguments(parser):
    """Default arguments for put-like actions"""
    parser.add_argument('-T', '--tags', type=lambda t: t.split(','),
                        help='only use media that contain all these tags '
                             '(comma-separated: foo,bar)')
    parser.add_argument('-f', '--family',
                        choices=list(map(rsc_family2str, ResourceFamily)),
                        help='targeted storage family')
    parser.add_argument('--library',
                        help='desired library (if not set, any available '
                             'library will be used)')
    parser.add_argument('-l', '--layout', '--lyt',
                        choices=['raid1', 'raid4'],
                        help='desired storage layout')
    parser.add_argument('-p', '--profile',
                        help='desired profile for family, tags and layout. '
                             'Specifically set family and layout supersede '
                             'the profile, tags are joined')
    parser.add_argument('-P', '--layout-params', '--lyt-params',
                        help='comma-separated list of key=value for layout '
                             'specific parameters')

def add_list_arguments(parser, attr, default_output, sort_option=False, # pylint: disable=too-many-arguments
                       lib_option=False, status_option=False):
    """Default arguments for list-like actions"""
    parser.add_argument('-o', '--output', type=lambda t: t.split(','),
                        default=default_output,
                        help=("attributes to output, comma-separated, "
                              "choose from {" + " ".join(attr) + "} "
                              "(default: %(default)s)"))
    if sort_option:
        parser.add_argument('--sort',
                            help=("attribute to sort the output with, "
                                  "choose from {" + " ".join(attr) + "} "))
        parser.add_argument('--rsort',
                            help=("attribute to sort the output in descending "
                                  "order, choose from {" + " ".join(attr) + "} "
                                  ))
    if lib_option:
        parser.add_argument('--library',
                            help=("attribute to filter the output by library "
                                  "name"))
    if status_option:
        parser.add_argument('--status',
                            help=("filter the output by status name, choose "
                                  "from {locked, unlocked, failed}"))

def add_log_arguments(parser, verb):
    """Default arguments for logs actions"""
    parser.add_argument('-D', '--drive',
                        help='drive ID of the logs to %s' % verb)
    parser.add_argument('-T', '--tape', help='tape ID of the logs to %s' % verb)
    parser.add_argument('--library',
                        help="Library containing the target drive and tape")
    parser.add_argument('-e', '--errno', type=int,
                        help='error number of the logs to %s' % verb)
    parser.add_argument('--errors', action='store_true',
                        help='%s all errors' % verb)
    parser.add_argument('-c', '--cause', help='cause of the logs to %s' % verb,
                        choices=["library_scan", "library_open",
                                 "device_lookup", "medium_lookup",
                                 "device_load", "device_unload",
                                 "ltfs_mount", "ltfs_umount", "ltfs_format",
                                 "ltfs_df", "ltfs_sync"])
    parser.add_argument('--start', type=str_to_timestamp, default=0,
                        help="timestamp of the most recent logs to %s,"
                             "in format YYYY-MM-DD [hh:mm:ss]" % verb)
    parser.add_argument('--end', type=str_to_timestamp, default=0,
                        help="timestamp of the oldest logs to %s,"
                             "in format 'YYYY-MM-DD [hh:mm:ss]'" % verb)

def add_object_arguments(parser):
    """Default arguments for get object-like actions"""
    parser.add_argument('--uuid', help='UUID of the object')
    parser.add_argument('--version', type=int, default=0,
                        help='Version of the object')
    group_deprec = parser.add_mutually_exclusive_group()
    group_deprec.add_argument('-d', '--deprecated', action='store_true',
                              help="target alive and deprecated objects")
    group_deprec.add_argument('-D', '--deprecated-only', action='store_true',
                              help="target only deprecated objects")
